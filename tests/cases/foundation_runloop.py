# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSRunLoop / NSTimer — F13.18's acceptance.

docs/design/foundation-plan.md §10, and part of the last row of its mechanism table. A timer is a
REQUEST, not a thread: it names when and what, and nothing happens until a RUN LOOP takes it and
waits. What ships here is the TIMER half of the run loop; run-loop sources, observers and performers
are their own designs and are named absent in the header.

The probe is `/System/Shared/tests/foundation_runloop`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `timer-fires-once`              — a non-repeating timer fires once and is then invalid;
  * `timer-repeats-until-invalidated` — THE FIRST CHECK THAT EARNS ITS PLACE: a repeating timer's
                                     count RISES and then STOPS the moment it is invalidated — two
                                     numbers that must differ;
  * `timer-order-follows-dates`     — the second: two timers scheduled OUT OF ORDER fire in DATE
                                     order, so the recorded order is the measurement;
  * `timer-userinfo-and-interval`   — the user info is the same object, and the interval survives;
  * `timer-unscheduled-is-inert`    — a timer never added to a loop is not fired by one, and `-fire`
                                     fires it once;
  * `runloop-is-per-thread`         — `+currentRunLoop` answers one object per thread (checked on the
                                     main thread here) and `+mainRunLoop` is the first loop made;
  * `runmode-one-pass`              — `-runMode:beforeDate:` fires what is due in ONE pass;
  * `source-fires-when-ready`       — W6a: a byte waiting on a watched descriptor fires the source;
  * `source-idle-does-not-fire`     — and an EMPTY pipe says nothing, which is what makes the check
                                     above a measurement rather than a tautology;
  * `source-keeps-loop-alive`       — a live source is live WORK: the pass answers YES with it
                                     registered and NO once it is removed (a PAIR, both required);
  * `source-waits-for-readiness`    — a byte written at 0.06s wakes a loop whose deadline is 2s, and the
                                     elapsed time is BETWEEN the two — it waited, and it did not spin;
  * `source-dead-target-is-skipped` — a source whose target has been deallocated is skipped, not called
                                     (the seam holds its target as a zeroing weak reference);
  * `runloop-addport-schedules`     — W6b: APPLE'S OWN DOOR. `-addPort:forMode:` makes a port a watched
                                     source (it is a forward — the port schedules itself — and the check
                                     is that the forward reaches the seam), and
  * `runloop-removeport-unschedules`  `-removePort:forMode:` takes it back. Both are CURRENT API, not
                                     deprecated, and both were unimplementable until `NSPort` landed.

EVERY RUN IN THE PROBE IS BOUNDED BY A DEADLINE: `-run` never returns while a repeating timer is
live, so a probe that called it would hang rather than fail.

NAMED ABSENT, AND EACH ONE IS CURRENT APPLE API RATHER THAN A DEPRECATED SURFACE: `-limitDateForMode:`
and `-acceptInputForMode:beforeDate:` (the other two one-pass doors), the
`-performSelector:target:argument:order:modes:` family with its two cancel forms (a scheduled-perform
queue, which no caller here needs yet), `-getCFRunLoop` (there is no CF in this tree), and
`NSRunLoopCommonModes` as a real mode SET rather than the single name it is treated as. THE TWO THAT
CAME OFF THIS LIST ARE RUN-LOOP SOURCES (W6a) and APPLE'S PORT DOOR (W6b).
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_runloop"
CHECKS = ("timer-fires-once", "timer-repeats-until-invalidated", "timer-order-follows-dates",
          "timer-userinfo-and-interval", "timer-unscheduled-is-inert", "runloop-is-per-thread",
          "runmode-one-pass",
          "source-fires-when-ready", "source-idle-does-not-fire", "source-keeps-loop-alive",
          "source-waits-for-readiness", "source-dead-target-is-skipped",
          "runloop-addport-schedules", "runloop-removeport-unschedules")


class Case(BaseCase):
    title = "NSRunLoop / NSTimer: a timer is a request, and the loop is what waits"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_runloop")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-RUNLOOP-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-RUNLOOP "):
                self.note(line)

        done = "FOUNDATION-RUNLOOP DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-RUNLOOP DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-RUNLOOP %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-RUNLOOP RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-RUNLOOP-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
