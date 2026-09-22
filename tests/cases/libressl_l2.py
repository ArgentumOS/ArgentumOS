# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""LibreSSL L2: THE FSH TRUST STORE DECIDES, AND IT DECIDES BOTH WAYS.

docs/design/libressl-plan.md §4 and §5/L2. L2's acceptance is *"a real TLS fetch (https) verifies a real
certificate against the FNX store and succeeds/fails on trust exactly as configured"*, and this case does
it LOCALLY and deterministically — no external network and no dependence on what any public CA has
signed:

  1. a CA and a server certificate that CA signed are generated ON THE GUEST (SAN: localhost, 127.0.0.1);
  2. the CA is INSTALLED at the FSH store, `/System/Configuration/SSL/cert.pem` — the file LibreSSL's
     compiled-in `OPENSSLDIR` points at and the file this build of curl was given as `CURL_CA_BUNDLE`;
  3. `openssl s_server` runs on loopback and `curl https://127.0.0.1:PORT/` must **SUCCEED**;
  4. the store's CA is REPLACED with a different one and the same fetch must be **REFUSED**.

  * `ca-generated`                        — a CA, on the guest;
  * `server-certificate-signed-by-the-ca` — with a subjectAltName, because modern verification matches the
                                            NAME against the SAN and a CN-only certificate would be
                                            refused for the right reason but the wrong test;
  * `openssldir-is-the-fsh-store`          — `openssl version -d` prints the COMPILED OPENSSLDIR and it
                                            is the store path: a measurement of the artifact, not of the
                                            script's intentions (and this is the check that would have
                                            caught the Linux `etc/ssl` default);
  * `ca-installed-in-the-fsh-store`        — the store now holds the CA this test made;
  * `https-verifies-against-the-store`     — curl, with **NO `--cacert`**, gets `code=200 verify=0`. The
                                            absence of `--cacert` is the point: the DEFAULT has to
                                            resolve to the store, which is what the build wired;
  * `an-untrusted-ca-is-refused`           — the same fetch, same server, a CA the store does not hold:
                                            curl must FAIL.

STEP 4 IS WHAT MAKES THIS A TEST OF POLICY RATHER THAN OF A HANDSHAKE: a store that was ignored would
pass step 3 only by accident and could not pass step 4 at all. And the case mutates a SYSTEM FILE on
purpose — the store is System content — putting back whatever it found, which in a freshly staged image
is nothing.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/libressl_l2.sh"
CHECKS = ("ca-generated", "server-certificate-signed-by-the-ca", "openssldir-is-the-fsh-store",
          "ca-installed-in-the-fsh-store", "https-verifies-against-the-store",
          "an-untrusted-ca-is-refused")


class Case(BaseCase):
    title = "libressl L2: the FSH trust store decides, and it decides both ways"
    tier = "fast"
    # Self-contained: its own CA, its own server, its own store writes (restored at the end). Every wait
    # in the script is bounded.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("libressl_l2.sh")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("sh %s; echo LIBRESSL-L2-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("LIBRESSL-L2"):
                self.note(line)

        done = "LIBRESSL-L2 DONE" in out
        self.check("probe-ran", done,
                   "the script reached its end marker" if done
                   else "no LIBRESSL-L2 DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^LIBRESSL-L2 %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "%d missing: %s" % (len(missing), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.startswith("LIBRESSL-L2 ") and " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"LIBRESSL-L2 RESULT ok=(\d+) fail=(\d+)", out)
        self.check("tally-accounts-for-every-check",
                   bool(tally) and int(tally.group(1)) + int(tally.group(2)) == len(CHECKS),
                   "the script's tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "LIBRESSL-L2-STATUS=0" in out,
                   "the script exited 0 (a non-zero status means a failed check)")
