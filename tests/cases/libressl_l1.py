# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""LibreSSL L1: a REAL TLS handshake on FNX — docs/design/libressl-plan.md §5/L1.

The pin is LibreSSL **4.3.2** (a sha256-pinned tarball, `tools/fetch-libressl.sh`; the git tree cannot
build — see that script) built CMake-only for the guest by `tools/libressl-build.sh`. L1 stages it into
the FSH and proves it WORKS ON THE OS.

The probe is a SHELL SCRIPT rather than a C program, deliberately: what L1 owes is not a library call but
**two processes talking** — `openssl s_server` on one side, `openssl s_client` on the other — which is how
the plan words it, and what makes this an acceptance for the OS rather than for a symbol. The handshake
exercises entropy (key and nonces), the socket layer, and the whole TLS state machine, with NO EXTERNAL
NETWORK: loopback only.

  * `openssl-runs`                    — `openssl version` answers LibreSSL 4.3.2;
  * `self-signed-cert-generated`      — `openssl req -x509 -newkey rsa:2048` on the GUEST (the entropy
                                        question, and a key generated in-guest);
  * `client-verified-the-certificate` — `s_client -verify_return_error` reports `Verify return code: 0`;
  * `tls-version-selected`            — the session negotiated TLSv1.3 or TLSv1.2;
  * `application-data-flowed`         — an HTTP GET came back through the tunnel (`s_server -www`);
  * `server-accepted-the-connection`  — the SERVER side saw it too. NAMED FOR WHAT IT PROVES: it greps
                                        `ACCEPT`, which s_server logs on the TCP ACCEPT, so it passed on a
                                        run where the client HUNG MID-HANDSHAKE. That is a check passing
                                        for the wrong reason, and renaming it is the fix.

WHAT THIS DOES NOT PROVE, STATED SO IT IS NOT MISTAKEN FOR COVERAGE: **the trust store.** The client
verifies against `-CAfile cert.pem` — the certificate this script just made — so it proves the HANDSHAKE,
not the verification POLICY. The store, and the compiled `OPENSSLDIR` that must point at it
(**measured: the build currently bakes in the Linux default `etc/ssl`, which on a system with no `/etc`
resolves to nothing — the finding L2 exists to close**), are L2's.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/libressl_l1.sh"
CHECKS = ("openssl-runs", "self-signed-cert-generated", "client-verified-the-certificate",
          "tls-version-selected", "application-data-flowed", "server-accepted-the-connection")


class Case(BaseCase):
    title = "LibreSSL L1: two processes, a real TLS handshake, on the guest"
    tier = "fast"
    # The handshake is loopback-only and self-contained, so a reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("libressl_l1.sh")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("sh %s; echo LIBRESSL-L1-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("LIBRESSL-L1 "):
                self.note(line)

        done = "LIBRESSL-L1 DONE" in out
        self.check("probe-ran", done,
                   "the script reached its end marker" if done
                   else "no LIBRESSL-L1 DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^LIBRESSL-L1 %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.startswith("LIBRESSL-L1 ") and l.endswith("FAIL")
                 or l.startswith("LIBRESSL-L1 ") and " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"LIBRESSL-L1 RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the script's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "LIBRESSL-L1-STATUS=0" in out,
                   "the script exited 0 (a non-zero status means a failed check)")
