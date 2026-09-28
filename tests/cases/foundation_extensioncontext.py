# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`NSExtensionContext` + `NSExtensionRequestHandling` — §62.92's acceptance, closing `App Support / Extension
Support`.

THE HOST'S REQUEST, AS THE EXTENSION SEES IT. The extension's half is real and enforced: the host's items arrive
as a COPY, a request has ONE ending (a second one raises), and a cancellation must carry an error. The host's half
does not exist in this system — nothing starts an extension or receives its ending — so it lives in the internal
seam `FNExtensionContext.h`, exactly as §62.85's spell-server client half and §62.91's distant-object reply did:
the seam plays a host, which makes a context, hands it to a principal object, and reads the ending back. That is
what lets the WHOLE flow run in one process, and the probe's last check is the echo an Action extension performs.

FOUR CHECKS ARE ABOUT WHAT DID NOT HAPPEN and could not be inferred from a return value: the caller's array is
MUTATED after delivery (a context that kept a reference would show it), a nil item list yields an EMPTY array
rather than nil, a cancellation DOES NOT call the completion handler, and the class's method list is walked to
prove the public surface is Apple's three doors and nothing else — a context comes from the host, so the class
must not grow a second way to make one.

THE ONE DELIBERATE ABSENCE IS ALSO ASSERTED: `-init` refuses, because Apple is explicit that a context comes from
the host — which is why there is no public constructor to call in the first place.

The probe is `/System/Shared/tests/foundation_extensioncontext`, ONE unit, importing only
`<Foundation/Foundation.h>` (plus the internal seam).

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_extensioncontext"
CHECKS = (
    "the-class-the-protocol-and-the-key-are-declared",
    "the-host-makes-a-context-and-it-carries-what-it-sent",
    "a-context-with-no-items-holds-an-empty-array-not-nil",
    "the-host-hands-the-context-to-the-extension-s-principal-object",
    "completing-runs-the-handler-before-the-door-returns-with-expired-no",
    "the-ending-items-are-what-the-extension-returned",
    "a-nil-item-list-and-a-nil-handler-are-both-accepted",
    "cancelling-carries-the-error-and-does-not-call-the-handler",
    "a-request-has-one-ending-and-a-second-one-raises",
    "a-cancellation-without-an-error-raises",
    "the-class-has-no-public-constructor",
    "an-extension-echoes-its-input-back-to-the-host",
    "the-public-surface-is-apple-s-doors-and-nothing-else",
)


class Case(BaseCase):
    title = "NSExtensionContext: the host's request, one ending per request (§62.92)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_extensioncontext")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-EXTENSIONCONTEXT-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-EXTENSIONCONTEXT "):
                self.note(line)

        done = "FOUNDATION-EXTENSIONCONTEXT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-EXTENSIONCONTEXT DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-EXTENSIONCONTEXT %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-EXTENSIONCONTEXT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-EXTENSIONCONTEXT-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
