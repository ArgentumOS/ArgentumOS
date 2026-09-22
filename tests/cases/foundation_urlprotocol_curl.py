# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""FNCURLURLProtocol — W7 slice 2c's first half: THE BRIDGE (libcurl behind the NSURLProtocol seam).

docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c. The plan splits
2c as "FNCURLURLProtocol … then NSURLSession", and this case is the first half. It needs NO session: the
bridge is an ordinary NSURLProtocol subclass, so the probe starts one by hand with a client and watches
it — which is the whole reason the bridge can be proven before the session that will normally drive it.

The probe is `/System/Shared/tests/foundation_urlprotocol_curl`, ONE unit, importing only
`<Foundation/Foundation.h>`. Its fetches are `file://` URLs: deterministic, no server, no network — and
still REAL transfers through the same code path (streaming write callback, header callback, completion).
It writes its own fixture, so the expected bytes live in the probe rather than in a file somebody else
may edit.

  * `bridge-claims-its-schemes`                     — file/http/https, which IS curl's scheme list here,
                                                      and NOT anything else (ftp is left unclaimed);
  * `bridge-registers-as-a-protocol`                — the registry finds it for the schemes it claims;
  * `bridge-fetches-a-file-url`                     — the body arrives, byte for byte;
  * `bridge-reports-response-then-data-then-finish` — THE ORDER, which is the seam's contract;
  * `bridge-reports-the-response-length`            — the response's derived length matches the file;
  * `bridge-reports-a-missing-file-as-a-failure`    — and a failure ENDS the transfer without a finish.

`bridge-reports-response-then-data-then-finish` IS THE ONE THAT EARNS ITS PLACE: a probe that only checked
"the bytes arrived" would pass for a bridge that reported them in the wrong order, or that reported both
an ending and a failure.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlprotocol_curl"
CHECKS = ("bridge-claims-its-schemes", "bridge-registers-as-a-protocol",
          "bridge-fetches-a-file-url", "bridge-reports-response-then-data-then-finish",
          "bridge-reports-the-response-length", "bridge-reports-a-missing-file-as-a-failure")


class Case(BaseCase):
    title = "FNCURLURLProtocol: libcurl behind the NSURLProtocol seam (W7 slice 2c, the bridge)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it writes one fixture in the temporary directory,
    # fetches it, and reads the probe's output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlprotocol_curl")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLPROTOCOL-CURL-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLPROTOCOL-CURL "):
                self.note(line)

        done = "FOUNDATION-URLPROTOCOL-CURL DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLPROTOCOL-CURL DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLPROTOCOL-CURL %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLPROTOCOL-CURL RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLPROTOCOL-CURL-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
