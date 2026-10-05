# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSNotificationQueue — §62.61's acceptance, and the run-loop seam it needed.

docs/design/foundation-plan.md §62.61. The class is a CENTER's buffer: it adds WHEN a notification is delivered
(the three posting styles) and COALESCING (a newer request standing for older matching ones). It could not be
written until the run loop exposed a phase seam — NSNotification.h recorded that as the reason it was absent —
so this case covers both halves at once: the seam is only observable through the queue, and the queue's two
queued styles are only observable through the seam.

The probe is `/System/Shared/tests/foundation_notificationqueue`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-default-queue-is-one-per-thread` — `+defaultQueue` is one object per thread;
  * `another-thread-gets-its-own-default-queue` — and a DIFFERENT one from another thread's;
  * `post-now-delivers-before-the-call-returns` — `NSPostNow` is synchronous;
  * `post-now-coalesces-the-queued-one-away` — and it still coalesces what is already waiting, which is what
                                           separates it from a bare `-postNotification:`;
  * `post-asap-waits-for-the-pass-and-then-arrives` — `NSPostASAP` is NOT delivered at the enqueue and IS one
                                           run-loop pass later (the negative half is the check);
  * `post-when-idle-arrives-when-the-loop-is-about-to-wait` — `NSPostWhenIdle` arrives when the loop reaches
                                           its idle phase, which it only reaches because a queued notification
                                           counts as live work;
  * `coalescing-leaves-one-and-no-coalescing-leaves-both` — the mask's two answers, both asserted;
  * `the-mask-decides-what-counts-as-a-match` — same name, different senders: `OnSender` coalesces neither,
                                           `OnName` coalesces one;
  * `dequeue-removes-only-what-matches-and-it-is-never-posted` — a dequeued notification is gone, and its
                                           neighbour is not;
  * `a-mode-the-loop-is-not-running-withholds-the-post` — `forModes:` gates delivery on the loop's mode.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_notificationqueue"
CHECKS = ("the-default-queue-is-one-per-thread",
          "another-thread-gets-its-own-default-queue",
          "post-now-delivers-before-the-call-returns",
          "post-now-coalesces-the-queued-one-away",
          "post-asap-waits-for-the-pass-and-then-arrives",
          "post-when-idle-arrives-when-the-loop-is-about-to-wait",
          "coalescing-leaves-one-and-no-coalescing-leaves-both",
          "the-mask-decides-what-counts-as-a-match",
          "dequeue-removes-only-what-matches-and-it-is-never-posted",
          "a-mode-the-loop-is-not-running-withholds-the-post")


class Case(BaseCase):
    title = "NSNotificationQueue: the three posting styles, coalescing, and the run-loop seam"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_notificationqueue")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-NOTIFICATIONQUEUE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-NOTIFICATIONQUEUE "):
                self.note(line)

        done = "FOUNDATION-NOTIFICATIONQUEUE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-NOTIFICATIONQUEUE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-NOTIFICATIONQUEUE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-NOTIFICATIONQUEUE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-NOTIFICATIONQUEUE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
