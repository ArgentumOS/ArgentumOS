# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`NSMachPort` and the message transport — §62.53's acceptance.

The port family was struck BY NAME by §11.5 ("because Mach is what it exists for") and §62.24's policy put it back.
What came back is not Mach: this system has no Mach port rights or namespace, so a port is an AF_UNIX SOCKET PAIR
with this library's message transport carrying what a message is. The substitution is stated in `NSMachPort.h`,
along with the three consequences it refuses at the door.

THE PROBE WRITES THE FRAME ITSELF, IN NETWORK BYTE ORDER, INSTEAD OF ASKING THE LIBRARY TO SEND AND RECEIVE ITS
OWN BYTES. That is the difference between a test and a tautology: a round trip through one implementation passes
for *any* frame at all, including one no other implementation could read.

THE THREE CHECKS THAT ARE NOT A ROUND TRIP are the stated boundaries: the from-number doors refuse (a bare number
names no port here), a component that is not data is refused at the send door rather than dropped, and a frame
length too large to believe INVALIDATES the port rather than allocating for it.

A pair is used per check, because one check deliberately leaves a port invalid and a shared pair would make the
other checks depend on the order they run in.

The probe is `/System/Shared/tests/foundation_machport`, ONE unit, importing only `<Foundation/Foundation.h>`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_machport"
CHECKS = (
    "a-port-is-valid-and-answers-a-handle",
    "the-from-number-doors-refuse",
    "a-peer-is-handed-over-once-and-is-a-port",
    "a-message-crosses-the-pair-with-its-id-and-components",
    "a-component-that-is-not-data-is-refused",
    "nothing-is-delivered-until-the-frame-is-whole",
    "a-frame-too-large-to-believe-invalidates-the-port",
)


class Case(BaseCase):
    title = "NSMachPort: a port over a socket pair, and a message that crosses it (§62.53)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_machport")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-MACHPORT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-MACHPORT "):
                self.note(line)

        done = "FOUNDATION-MACHPORT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-MACHPORT DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        for name in CHECKS:
            if not re.search(r"^FOUNDATION-MACHPORT %s ok$" % re.escape(name), out, re.M):
                self.note("MISSING: " + name)
        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-MACHPORT %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-MACHPORT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-MACHPORT-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
