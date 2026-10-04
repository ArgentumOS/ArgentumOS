# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The naming half of the port family — §62.54's acceptance.

`NSPortNameServer`, `NSMessagePort`, and the three subclasses that are three names for one mechanism.

WHAT A NAME SERVER IS FOR IS A NAME THAT FINDS SOMETHING, so almost every check here is a DELIVERY rather than a
lookup: a port is registered under a name, the name is resolved by a DIFFERENT door, and a message sent to what the
name answered arrives at the port that registered it. A registry that stored and returned objects while nothing
could reach them would pass every lookup check there is.

THE RULES THE HEADERS STATE ARE THE RULES THIS PROBE MEASURES, because they are choices rather than facts: a second
registration REPLACES the first (the second port's delegate receives and the first's does not); a host is
answerable only when it is this machine; an invalidated port takes its own name out; and a SOCKET port cannot be
published here at all — its registration answers NO, because publishing an address needs an accept path that
`NSConnection` owns and this library does not have yet.

The probe is `/System/Shared/tests/foundation_portnames`, ONE unit, importing only `<Foundation/Foundation.h>`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_portnames"
CHECKS = (
    "a-named-message-port-reports-its-name",
    "the-default-name-server-is-the-message-port-one",
    "a-registered-name-finds-a-port-to-send-to",
    "a-message-sent-to-a-name-arrives-at-the-port-that-registered-it",
    "removing-a-name-stops-the-lookup",
    "a-second-registration-replaces-the-first",
    "a-host-lookup-answers-only-for-this-machine",
    "the-three-servers-answer-from-one-registry",
    "a-socket-port-registration-is-refused",
    "an-invalidated-port-forgets-its-own-name",
)


class Case(BaseCase):
    title = "The naming half of the port family: a message that goes to a name (§62.54)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_portnames")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-PORTNAMES-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-PORTNAMES "):
                self.note(line)

        done = "FOUNDATION-PORTNAMES DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-PORTNAMES DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-PORTNAMES %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-PORTNAMES RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-PORTNAMES-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
