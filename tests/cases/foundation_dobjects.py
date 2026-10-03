# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Distributed objects — §62.56's acceptance.

`NSConnection`, `NSDistantObject` and `NSPortCoder`, on this library's own transport with this library's
own name server.

NEARLY EVERY CHECK IS A ROUND TRIP WHOSE ANSWER ONLY THE SERVICE CAN COMPUTE: the service builds a string
from its argument and the client gets THAT STRING. A proxy that returned a plausible value without
travelling would pass a weaker check; it cannot pass this one.

THE BOUNDARY IS MEASURED FROM BOTH SIDES: a method whose RESULT is not an object is refused by the SERVICE
(the client raises with the service's message), and a method whose ARGUMENT is not an object is refused by
the PROXY before anything is sent - which the service's own counter confirms, because a refusal that still
called the method would be no boundary at all.

WHAT CROSSES IS ANY `NSCoding` OBJECT, which is the half §62.57 had to re-measure: the coder carries a
keyed archive, so an object with its OWN STATE travels (its class crosses as a name the far side rebuilds),
and an object that conforms to nothing is refused by the archiver on the SENDING side. Both of those checks
count the service's calls, so a ticket that came back is a ticket that was built there, and a refused object
never reached it.

The probe is `/System/Shared/tests/foundation_dobjects`, ONE unit, importing only `<Foundation/Foundation.h>`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_dobjects"
CHECKS = (
    "connection-request-modes-answer-a-copy-not-the-store",
    "connection-remove-request-mode-inverts-add",
    "connection-request-modes-are-a-set-that-keeps-order",
    "connection-multiple-threads-is-a-one-way-latch",
    "connection-conversation-queueing-round-trips",
    "connection-request-timeout-round-trips",
    "a-service-publishes-a-name-and-answers-for-its-root-object",
    "a-client-gets-a-proxy-for-the-published-name",
    "a-call-crosses-and-the-service-s-computed-answer-comes-back",
    "two-arguments-and-a-nil-result-cross",
    "an-object-with-its-own-state-crosses-both-ways",
    "an-object-that-is-not-nscoding-is-refused-by-the-coder",
    "a-scalar-result-is-refused-by-the-service",
    "a-scalar-argument-is-refused-by-the-proxy-and-nothing-is-called",
    "all-connections-answers-the-live-registry-and-holds-the-serving-connection",
    "a-registered-name-answers-a-self-contained-connection-and-an-absent-one-answers-nil",
    "a-connections-reply-timeout-defaults-to-60-and-round-trips",
    "an-unregistered-name-answers-no-proxy",
    "invalidating-a-connection-posts-its-notification-and-withdraws-its-name",
    "a-proxy-declines-to-be-archived-and-answers-its-own-doors",
)


class Case(BaseCase):
    title = "Distributed objects: a call that travels and an answer that comes back (§62.56)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_dobjects")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DOBJECTS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DOBJECTS "):
                self.note(line)

        done = "FOUNDATION-DOBJECTS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DOBJECTS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DOBJECTS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DOBJECTS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DOBJECTS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
