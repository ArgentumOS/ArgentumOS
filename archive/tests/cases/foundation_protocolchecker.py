# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`NSProtocolChecker` — a proxy that answers only for its protocol.\n\nTHE PROPERTY THAT MATTERS IS NOT "A BAD CALL RAISES" BUT "A BAD CALL NEVER REACHES THE TARGET": a checker that forwarded everything and let the runtime complain would pass a check that only looked for an exception, so every refusal is measured TWICE — the call is refused AND the target's own counter shows the method never ran. The two optional-door checks separate "asked the protocol" from "asked the target".\n\nThe probe is `/System/Shared/tests/foundation_protocolchecker`, ONE unit, importing only `<Foundation/Foundation.h>`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_protocolchecker"
CHECKS = (
    "a-checker-is-made-for-a-target-and-a-protocol",
    "a-selector-the-protocol-declares-reaches-the-target",
    "an-optional-selector-the-target-implements-reaches-it",
    "a-selector-outside-the-protocol-is-refused-and-never-invoked",
    "responds-to-selector-follows-the-protocol-not-the-target",
    "an-optional-door-the-target-does-not-implement-is-refused",
)


class Case(BaseCase):
    title = "The proxy that answers only for its protocol (§62.55)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_protocolchecker")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-PROTOCOLCHECKER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-PROTOCOLCHECKER "):
                self.note(line)

        done = "FOUNDATION-PROTOCOLCHECKER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-PROTOCOLCHECKER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-PROTOCOLCHECKER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-PROTOCOLCHECKER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-PROTOCOLCHECKER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
