"""Discover tests/cases/*.py, run them by tier, report, and clean up.

Cases boot QEMU, so this is sequential by design: one case owns the machine at
a time.  Each case is guarded by its own wall-clock timeout, and its sessions
are killed whatever happens - including on timeout - because a leaked guest
holds the image lock.

Exit status: 0 all selected cases passed, 1 something failed, 2 the harness
cannot run here (the message says which make target fixes that).
"""

import argparse
import fnmatch
import importlib.util
import os
import signal
import sys
import time
import traceback

from . import paths
from .case import BaseCase, Context, Skip
from .qemu import launch

CASES_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "cases")


class _Timeout(Exception):
    pass


def discover():
    """{name: case} for every BaseCase subclass in tests/cases/*.py.

    A case's name is its file name (`audio.py` -> "audio"), so `make test
    T=audio` is predictable.
    """
    cases = {}
    if not os.path.isdir(CASES_DIR):
        return cases
    for filename in sorted(os.listdir(CASES_DIR)):
        if not filename.endswith(".py") or filename.startswith("_"):
            continue
        path = os.path.join(CASES_DIR, filename)
        module_name = "fnx_case_" + filename[:-3]
        spec = importlib.util.spec_from_file_location(module_name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        try:
            spec.loader.exec_module(module)
        except Exception:
            print("FAIL %s: the case file could not be imported:" % filename[:-3])
            traceback.print_exc()
            raise SystemExit(1)
        for attr in list(vars(module).values()):
            if (isinstance(attr, type) and issubclass(attr, BaseCase)
                    and attr is not BaseCase and attr.__module__ == module_name):
                case = attr(filename[:-3])
                if case.name in cases:
                    raise SystemExit("duplicate case name %r" % case.name)
                cases[case.name] = case
    return cases


def select(cases, tier, only):
    selected = []
    for name in sorted(cases):
        case = cases[name]
        if only and not any(fnmatch.fnmatch(name, pat) for pat in only):
            continue
        if tier == "fast" and case.tier != "fast":
            continue
        if tier == "slow" and case.tier != "slow":
            continue
        selected.append(case)
    return selected


def summarize(cases):
    """`make test-list` output: name, tier, timeout, title."""
    width = max([len(n) for n in cases] or [4])
    print("%-*s  %-4s  %5s  %s" % (width, "case", "tier", "time", "what it proves"))
    print("%s  %-4s  %5s  %s" % ("-" * width, "----", "-----", "-" * 40))
    for name in sorted(cases):
        case = cases[name]
        print("%-*s  %-4s  %4ds  %s"
              % (width, name, case.tier, case.timeout, case.title or "(no title)"))
    slow = sum(1 for c in cases.values() if c.tier == "slow")
    print("\n%d case(s): %d fast (make test), %d slow (make test-all)"
          % (len(cases), len(cases) - slow, slow))


class GuestPool:
    """One guest, reused by consecutive cases that say they can share it.

    WHY THIS EXISTS: a case costs a full TCG boot, and most cases only run a probe and read its
    output - the case contract already slices the log by an offset taken before it runs, so a
    reused guest needs NO change in any case file.

    WHY IT IS OPT-IN: the reasons to boot fresh are real - a different boot config, an assertion
    about BOOT itself, a case that mutates the system - and a wrong answer from a shared guest is
    worse than a slow one. A case sets `shared_session = True` only once it has been shown to
    answer the same this way.

    SAFETY: the session is dropped whenever a case does not pass, so a poisoned guest cannot reach
    the next case, and whenever the boot arguments differ from the ones the guest was started with.
    """

    def __init__(self):
        self.session = None
        self.argv = None

    def acquire(self, work_dir, **kw):
        if self.session is not None and not self.session.alive():
            self.drop()                     # it died on its own; start again
        if self.session is None:
            self.session = launch(work_dir, **kw)
            self.argv = kw
        elif kw != self.argv:
            # A DIFFERENT BOOT CONFIG NEEDS A DIFFERENT GUEST. This is the rule that keeps a case
            # asking for another RAM size or another machine from silently sharing this one.
            self.drop()
            self.session = launch(work_dir, **kw)
            self.argv = kw
        return self.session

    def verify(self):
        """Is the shared guest still ANSWERING? A case can leave the kernel damaged, and the next
        case would then sit in its shell-wait for the whole timeout instead of failing fast - which
        is exactly what foundation_expression did (151s) before this existed."""
        if self.session is None:
            return
        mark = len(self.session.log_text())
        try:
            self.session.run("echo FNGUEST-ALIVE", secs=20)
        except Exception:
            self.drop()
            return
        if "FNGUEST-ALIVE" not in self.session.output_since(mark):
            self.drop()

    def drop(self):
        if self.session is not None:
            try:
                self.session.stop()
            except Exception:
                pass
            self.session = None
            self.argv = None


def run_case(case, verbose, host=False, pool=None):
    """Run one case with a wall-clock guard.  Returns (outcome, seconds)."""
    ctx = Context(case.name, host=host, pool=pool,
                  share=getattr(case, "shared_session", False))
    started = time.time()

    def _alarm(signum, frame):
        raise _Timeout("exceeded its %ds budget" % case.timeout)

    previous = signal.signal(signal.SIGALRM, _alarm)
    signal.setitimer(signal.ITIMER_REAL, case.timeout)
    outcome = "pass"
    try:
        case.run(ctx)
    except Skip as exc:
        outcome = "skip"
        print("SKIP %s: %s" % (case.name, exc))
    except _Timeout as exc:
        outcome = "fail"
        case.check("timeout", False, str(exc))
    except Exception:
        outcome = "fail"
        # INTO THE MESSAGE, not just stderr: a traceback that only reaches the
        # terminal is lost the moment the run is captured with `> file`, which
        # is how every gate here is recorded - and a case that failed with an
        # unreadable reason wastes a whole boot.
        case.check("harness-error", False,
                   "the case raised an exception:\n%s"
                   % traceback.format_exc())
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)
        ctx.cleanup()

    if outcome == "pass":
        if not case.checks:
            outcome = "fail"
            case.check("checks-ran", False, "no check was made")
        elif not case.passed():
            outcome = "fail"
    return outcome, time.time() - started


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="make test",
        description="Boot the assembled OS and assert on what it does.")
    parser.add_argument("--tier", default=os.environ.get("FNX_TEST_TIER", "fast"),
                        choices=("fast", "slow", "all"),
                        help="which cases to run (default: fast)")
    parser.add_argument("--only", default="",
                        help="comma-separated case names or globs")
    parser.add_argument("--list", action="store_true",
                        help="show the cases and exit")
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("--host", action="store_true",
                        help="run the probes on THIS machine instead of booting "
                             "a guest (mk/60-host.mk). Faster by orders of "
                             "magnitude, and NOT a substitute: it touches no "
                             "kernel, Xfb, /dev or FSH, and links glibc. Only "
                             "host-clean cases run; the rest are skipped.")
    args = parser.parse_args(argv)

    cases = discover()
    if args.list:
        summarize(cases)
        return 0
    if not cases:
        print("no cases found in %s" % paths.rel(CASES_DIR))
        return 2

    only = [p for p in args.only.split(",") if p.strip()]
    selected = select(cases, args.tier, only)
    if not selected:
        # Naming a slow case on the fast tier is an easy mistake; say which
        # tier it is in and what to type instead of "no cases selected".
        matched = [case for case in cases.values()
                   if only and any(fnmatch.fnmatch(case.name, pat)
                                   for pat in only)]
        if matched:
            tiers = sorted({case.tier for case in matched})
            print("no case selected: %s is in the %s tier, and --tier is %s\n"
                  "  use `make test-all TESTS=%s` (or FNX_TEST_TIER=%s)"
                  % (", ".join(sorted(case.name for case in matched)),
                     "/".join(tiers), args.tier, matched[0].name, tiers[0]))
        else:
            print("no cases selected (tier=%s, only=%s) - `make test-list` shows them"
                  % (args.tier, args.only or "-"))
        return 2

    needs_boot = (not args.host) and any(case.needs_boot for case in selected)
    try:
        version = paths.check_prereqs(need_image=needs_boot, need_qemu=needs_boot)
    except paths.PrereqError as exc:
        print("cannot run: %s" % exc)
        return 2

    if needs_boot:
        fresh, detail = paths.image_freshness()
        print("harness: %s" % version)
        print("guest:   %s at %s RAM, audio %s"
              % (paths.rel(paths.root_image()), paths.default_mem(),
                 paths.default_audiodev()))
        if not fresh:
            print("warning: %s is older than %s - the image may be stale\n"
                  "         (make rootagfs to rebuild it)"
                  % (paths.rel(paths.root_image()), detail))
        print("")

    results, elapsed = {}, {}
    checks_ok = checks_total = checks_xfail = 0
    pool = None if args.host else GuestPool()
    for case in selected:
        # DROP BEFORE, NOT ONLY AFTER: a case that needs its OWN guest cannot boot while the pool
        # still holds one - it would hit the image lock and raise (foundation_collection and
        # foundation_string did, in 0.0s, before this line existed).
        if pool is not None and not getattr(case, "shared_session", False):
            pool.drop()
        print("== %s [%s]%s" % (case.name, case.tier,
                                (" - " + case.title) if case.title else ""))
        outcome, secs = run_case(case, args.verbose, args.host, pool)
        results[case.name], elapsed[case.name] = outcome, secs
        checks_total += len(case.checks)
        checks_ok += sum(1 for c in case.checks if c.ok)
        checks_xfail += sum(1 for c in case.checks if c.expected_fail)
        # ALWAYS, not only on failure: the point of a shared guest is that the LATER cases should
        # cost almost nothing, and that is only checkable if every case reports its own seconds.
        print("   -> %s in %.1fs" % (outcome.upper(), secs))
        # A CASE THAT DID NOT PASS LEAVES AN UNKNOWN SYSTEM BEHIND: drop the shared guest so the
        # next case starts clean, and drop it as well when this case never wanted to share.
        if pool is not None:
            if outcome != "pass":
                pool.drop()
            elif getattr(case, "shared_session", False):
                pool.verify()       # a guest that no longer answers must not reach the next case
        print("")

    passed = [n for n, r in results.items() if r == "pass"]
    failed = [n for n, r in results.items() if r == "fail"]
    skipped = [n for n, r in results.items() if r == "skip"]

    print("-" * 60)
    if skipped:
        print("skipped: %s" % ", ".join(sorted(skipped)))
    if failed:
        print("failed:  %s" % ", ".join(sorted(failed)))
    known = ("  (%d known-broken check(s), marked XFAIL)" % checks_xfail
             if checks_xfail else "")
    total_time = sum(elapsed.values())
    if failed:
        print("TESTS-FAIL %d/%d case(s), %d/%d check(s) in %.0fs"
              % (len(passed), len(results), checks_ok, checks_total, total_time))
        print("artifacts (logs, screenshots): %s" % paths.rel(paths.ARTIFACTS))
        return 1
    print("TESTS-OK %d/%d case(s), %d/%d check(s) in %.0fs%s"
          % (len(passed), len(results), checks_ok, checks_total, total_time, known))
    return 0


if __name__ == "__main__":
    sys.exit(main())
