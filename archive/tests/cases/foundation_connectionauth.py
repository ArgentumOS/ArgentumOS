# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The composition, alone in one probe — §62.41's acceptance.

docs/design/foundation-plan.md §62.40 proved that the transport's own 401 re-issue can ask for a fresh body, and
§62.35 proved that a connection's `-connection:needNewBodyStream:` translates that question faithfully. THIS file
joins them: a CONNECTION whose transfer is challenged has to hear the challenge, answer it through the challenge's
SENDER, be asked for a NEW body when the transport re-issues, and end with its delegate told the transfer finished.

IT IS ITS OWN PROBE BECAUSE THE FIRST ATTEMPT WAS UNATTRIBUTABLE. It began as a fifth leg of
`foundation_authloop`, whose four legs share ONE listener with per-leg `accept()` calls — so a fifth leg's
connections interleave with the earlier ones', and the diagnostic that would have separated them could not: two of
those legs use the SAME DELEGATE CLASS, and a class name cannot tell two instances apart. That attempt was
reverted. Here there is one path, one listener, and nothing to interleave.

The probe is `/System/Shared/tests/foundation_connectionauth`, ONE unit, importing only
`<Foundation/Foundation.h>`, AND IT IS ITS OWN SERVER: a hand-written socket that answers the first request 401
with a Basic challenge and the second 200 — the second being the one that must carry the credential AND the body
again.

  * `the-probe-binds-its-own-listener`                    — the probe is the server;
  * `the-connection-was-made`                             — a connection whose delegate is the one under test;
  * `the-first-attempt-sent-the-streamed-body`            — the body reached the wire before any challenge;
  * `the-connection-delegate-heard-the-challenge`         — 401 -> the session's delegate (the connection) ->
                                                            the connection's delegate;
  * `the-connection-delegate-was-asked-for-a-new-body-stream` — the link §62.40 named as untested;
  * `the-re-issued-request-carried-the-credential-and-the-body` — both, on the wire;
  * `the-connection-finished-without-failing`             — the 200 reached the delegate's own ending.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_connectionauth"
CHECKS = ("the-probe-binds-its-own-listener",
          "the-connection-was-made",
          "the-first-attempt-sent-the-streamed-body",
          "the-connection-delegate-heard-the-challenge",
          "the-connection-delegate-was-asked-for-a-new-body-stream",
          "the-re-issued-request-carried-the-credential-and-the-body",
          "the-connection-finished-without-failing")


class Case(BaseCase):
    title = "A connection across a 401 with a stream body: the composition (§62.41)"
    tier = "fast"
    # It listens on its own loopback port and reads nothing from the image; the shared guest is enough.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_connectionauth")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CONNECTIONAUTH-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CONNECTIONAUTH "):
                self.note(line)

        done = "FOUNDATION-CONNECTIONAUTH DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CONNECTIONAUTH DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CONNECTIONAUTH %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CONNECTIONAUTH RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CONNECTIONAUTH-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
