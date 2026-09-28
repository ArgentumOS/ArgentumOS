# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSDistantObjectRequest and NSConnectionDelegate — §62.91's acceptance.

The last pieces the port family owed: a request a connection's delegate may answer ITSELF, and the delegate
protocol that offers it.

THE HEADLINE IS THAT AN EXCEPTION CROSSES THE WIRE AND IS RAISED AT THE CALLER — which is why this unit began by
giving `NSException` the `NSCoding` conformance Apple declares and this library was missing (the DO wire refuses an
unarchivable object on the sending side). The probe asserts that round trip SEPARATELY as well, because a failure
there would make the interception check inexplicable.

The probe is `/System/Shared/tests/foundation_distantobjectrequest`, ONE unit, importing only
`<Foundation/Foundation.h>` — and it makes a REAL service: a published name, a proxy told its protocol, and a
round trip through the name server, the recipe `foundation_dobjects` established.

TWO CHECKS ARE ABOUT WHAT DID NOT HAPPEN, which is the only way to tell the paths apart: a delegate that
intercepts must leave the SERVICE'S OWN METHOD UNCALLED (its counter is the witness), and a connection whose
delegate is nil must serve exactly as it always did.

AND THREE OF APPLE'S PROTOCOL DOORS ARE ABSENT ON PURPOSE — `shouldMakeNewConnection:` and the two authentication
doors — with their grounds in `NSConnection.h` (no parent/child connections exist in an in-process design, and
nothing here authenticates); the probe asserts their absence rather than assuming it.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_distantobjectrequest"
CHECKS = (
    "the-class-and-the-protocol-are-declared",
    "a-connection-starts-with-no-delegate-and-keeps-the-one-it-is-given",
    "the-three-doors-this-library-does-not-consult-are-absent",
    "an-exception-now-round-trips-the-wire",
    "a-delegate-that-intercepts-owns-the-reply",
    "the-request-carries-its-connection-its-invocation-and-its-conversation",
    "a-request-has-one-reply-and-a-second-one-raises",
    "a-delegate-that-declines-leaves-the-ordinary-path-alone",
    "an-exception-crosses-the-wire-and-is-raised-at-the-client",
    "a-connection-without-a-delegate-serves-as-it-always-did",
)


class Case(BaseCase):
    title = "NSDistantObjectRequest: a request a delegate answers itself (§62.91)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_distantobjectrequest")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DISTANTOBJECTREQUEST-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DISTANTOBJECTREQUEST "):
                self.note(line)

        done = "FOUNDATION-DISTANTOBJECTREQUEST DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DISTANTOBJECTREQUEST DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DISTANTOBJECTREQUEST %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DISTANTOBJECTREQUEST RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DISTANTOBJECTREQUEST-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
