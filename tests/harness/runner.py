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


def run_case(case, verbose):
    """Run one case with a wall-clock guard.  Returns (outcome, seconds)."""
    ctx = Context(case.name)
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
        case.check("harness-error", False, "the case raised an exception:")
        traceback.print_exc()
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

    needs_boot = any(case.needs_boot for case in selected)
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
    for case in selected:
        print("== %s [%s]%s" % (case.name, case.tier,
                                (" - " + case.title) if case.title else ""))
        outcome, secs = run_case(case, args.verbose)
        results[case.name], elapsed[case.name] = outcome, secs
        checks_total += len(case.checks)
        checks_ok += sum(1 for c in case.checks if c.ok)
        checks_xfail += sum(1 for c in case.checks if c.expected_fail)
        if outcome != "pass":
            print("   -> %s in %.1fs" % (outcome.upper(), secs))
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
