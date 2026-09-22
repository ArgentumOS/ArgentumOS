# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""libcurl on the guest — W7 slice 2b's acceptance.

docs/design/foundation-transport-plan.md §4. The Foundation's HTTP transport is **libcurl**, vendored
at pin `curl-8_22_0` and built CMake-only with **no TLS backend** (libressl-plan.md makes LibreSSL/
libtls the one system SSL library, so a second one must not enter the tree).

This case judges the LIBRARY, not the Foundation: the probe has no Foundation in it at all — the same
reasoning `kernel_pipe_dup2` follows for the kernel. It is `/System/Shared/tests/curl_smoke`.

  * `curl-global-init`        — the library initialises;
  * `curl-version-is-8-22`    — the PIN is the version that actually loaded;
  * `curl-has-no-tls-backend` — TLS-less, asserted from the version feature bits and the version
                                STRING (a silently-enabled backend is the failure mode);
  * `curl-file-fetch`         — bytes MOVE, over `file://` with a PERCENT-ENCODED space, compared
                                against what the probe itself wrote;
  * `curl-http-reaches-connect` — HTTP is SUPPORTED, proven by the error it gives when the connection
                                cannot be made (`CURLE_COULDNT_CONNECT`) rather than by
                                `CURLE_UNSUPPORTED_PROTOCOL`;
  * `curl-https-refused`      — `https://` fails CLEARLY with `CURLE_UNSUPPORTED_PROTOCOL`: it does
                                not silently degrade.

THE LAST TWO ARE THE POINT, and they are a PAIR: one asserts that plain HTTP gets as far as the
network layer, the other that HTTPS is refused by name. A build that answered the same error to both
would pass neither honestly.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/curl_smoke"
CHECKS = ("curl-global-init", "curl-version-is-8-22", "curl-has-no-tls-backend",
          "curl-file-fetch", "curl-http-reaches-connect", "curl-https-refused")


class Case(BaseCase):
    title = "libcurl on the guest: it loads, it moves bytes, and it has no TLS (W7 slice 2b)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("curl_smoke")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo CURL-SMOKE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("CURL-SMOKE "):
                self.note(line)

        done = "CURL-SMOKE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no CURL-SMOKE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^CURL-SMOKE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"CURL-SMOKE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "CURL-SMOKE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
