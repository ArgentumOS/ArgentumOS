# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`NSDistributedLock` — a lock whose claim IS a file (`open(2)` with `O_CREAT|O_EXCL`), so the atomicity is the kernel's.\n\nEVERY CHECK WORKS THROUGH TWO OR THREE OBJECTS ON ONE PATH, because a lock is only a lock if somebody ELSE is refused: the first claims, the second cannot, a third unlocks something it does not hold and changes nothing, and `-breakLock` takes it from anybody — the door a lock left by a dead holder needs. The lock date is read from the FILE, so the check that matters is that a NON-HOLDER can read when the lock was taken.\n\nThe probe is `/System/Shared/tests/foundation_distributedlock`, ONE unit, importing only `<Foundation/Foundation.h>`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_distributedlock"
CHECKS = (
    "a-lock-is-made-and-does-not-start-held",
    "try-lock-claims-the-name-and-a-second-claimant-cannot-take-it",
    "calling-try-lock-twice-on-the-holder-is-not-a-failure",
    "unlocking-lets-the-other-claimant-in",
    "unlock-releases-only-what-the-caller-holds",
    "break-lock-takes-it-from-anybody",
    "a-lock-with-an-empty-path-is-refused",
)


class Case(BaseCase):
    title = "A lock that is a file, measured through a second claimant (§62.55)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_distributedlock")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DLOCK-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DLOCK "):
                self.note(line)

        done = "FOUNDATION-DLOCK DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DLOCK DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DLOCK %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DLOCK RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DLOCK-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
