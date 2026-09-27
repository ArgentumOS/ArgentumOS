# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSLock / NSRecursiveLock / NSCondition / NSThread — F13.17's acceptance.

docs/design/foundation-plan.md §10, and the LAST row of §10's mechanism table: *"NSThread, NSLock,
NSRecursiveLock, NSCondition, NSRunLoop, NSTimer, NSOperationQueue, NSProgress — pthreads
(`third_party/musl/src/thread`) + the kernel's `clone`"*. What ships here is the LOCKS and `NSThread`;
`NSRunLoop`/`NSTimer` and `NSOperationQueue` are their own designs and are named absent.

The probe is `/System/Shared/tests/foundation_thread`, ONE unit, importing only
`<Foundation/Foundation.h>` plus `<sys/time.h>` for the elapsed-time measurements.

  * `thread-current-and-main`      — `+currentThread` is one object per thread, and the main thread
                                     knows it is the main thread;
  * `lock-serialises-two-threads`  — THE MEASUREMENT THAT EARNS ITS PLACE: two threads and the main
                                     thread each add 20000 times under an NSLock, and the total must be
                                     EXACTLY 60000. A lock that did nothing gives a smaller number and
                                     nothing else;
  * `lock-try-lock`                — the same thread asking twice gets NO rather than a deadlock,
                                     which is why this is asked with `-tryLock`;
  * `lock-recursive-reenters`      — the RECURSIVE lock may be taken twice by one thread;
  * `lock-before-date`             — a deadline that passes: NO, after about the time it was given;
  * `condition-signals`            — a waiter woken by a signaller;
  * `thread-detached-runs`         — a detached thread runs its target, and the ARGUMENT arrives;
  * `thread-start-runs-its-target` — the same, through `-initWithTarget:selector:object:` then `-start`
                                     (the door §14.5 left open with a measured `ran=0`, fixed by giving
                                     the thread ownership of its target and argument);
  * `thread-sleep-returns`        — WHAT THE LIBRARY OWNS: a positive interval and a deadline
                                     already past both RETURN, in bounded time. The elapsed time is
                                     PRINTED and not asserted, because this kernel returns from
                                     nanosleep(2) early — measured by CONTRAST with the lock's
                                     deadline loop, which uses clock_gettime and took its full
                                     40ms. Recorded in the plan as a kernel trait, F13.17;
  * `thread-cancel-is-a-flag`      — cancellation is a flag, and nothing here interrupts a thread.

AND §62.60'S SEVEN, for `NSConditionLock`, where the family's FIRST cross-thread handover is asserted:

  * `condition-lock-hands-over-across-threads` — one thread BLOCKS in `-lockWhenCondition:1` and another
                                     sets that value through `-unlockWithCondition:`; the flag proves the
                                     handover happened, which a lock-then-test-then-release would lose;
  * `condition-lock-exposes-its-condition` — the value it was built with;
  * `condition-lock-try-when-condition` — the matching value takes it, a wrong one answers NO and LEAVES
                                     THE LOCK FREE (the last clause takes it to prove that);
  * `condition-lock-unlock-sets-the-value` — `-unlockWithCondition:` sets the value and releases;
  * `condition-lock-before-date-times-out` — a deadline that passes: NO, after about the time given;
  * `condition-lock-when-condition-before-date-times-out` — the condition is never met, so only the deadline
                                     ends it, and the lock must NOT be left held when it gives up;
  * `the-name-setter-copies-for-the-whole-family` — a MUTABLE string is handed to all four classes and then
                                     mutated, so a setter that merely assigned would answer the mutated
                                     string. This is the regression guard for §62.60's second defect fix.

EVERY WAIT IN THE PROBE IS BOUNDED: a failure is a wrong number or a false flag, never a hang,
because a hang is the one outcome a probe cannot report.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_thread"
CHECKS = ("thread-current-and-main", "lock-serialises-two-threads", "lock-try-lock",
          "lock-recursive-reenters", "lock-before-date", "condition-signals",
          "thread-detached-runs", "thread-sleep-returns", "thread-cancel-is-a-flag",
          "thread-start-runs-its-target",
          "condition-lock-hands-over-across-threads", "condition-lock-exposes-its-condition",
          "condition-lock-try-when-condition", "condition-lock-unlock-sets-the-value",
          "condition-lock-before-date-times-out",
          "condition-lock-when-condition-before-date-times-out",
          "the-name-setter-copies-for-the-whole-family",
          )


class Case(BaseCase):
    title = "NSLock / NSThread: the locking classes and threads, over musl's pthreads"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_thread")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-THREAD-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-THREAD "):
                self.note(line)

        done = "FOUNDATION-THREAD DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-THREAD DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-THREAD %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-THREAD RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-THREAD-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
