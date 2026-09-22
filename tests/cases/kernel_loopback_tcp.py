# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Can this kernel's loopback carry a payload? — a Foundation-free reproducer.

Raised by docs/design/libressl-plan.md §5/L1. The TLS handshake STALLED with `openssl s_client`
printing `CONNECTED(fd)` and `openssl s_server` logging `ACCEPT` — connect and accept work — but with
`-state` on, the client's handshake trace was EMPTY. No bytes moved, and that is a statement about the
network layer rather than about TLS. This case settles it with no SSL, no Foundation and no
third-party library anywhere in the program.

The probe is `/System/Shared/tests/kernel_loopback_tcp`, plain C:

  * `loopback-socket-created`          — an AF_INET SOCK_STREAM socket;
  * `bind-to-127-0-0-1`                — a FIXED port, because `bind(2)` to port 0 SUCCEEDS here and
                                         leaves the port unresolved (measured in W6b);
  * `listen`                           — and the listen that port 0 would have broken;
  * `fork-the-peer`                    — the peer is a CHILD PROCESS, so the test is not one process
                                         agreeing with itself;
  * `client-connect`                   — the client's connect() to the listener;
  * `payload-arrived-at-the-peer`      — the peer's own account, read out of its EXIT CODE;
  * `response-arrived-at-the-client`   — and the client read the reply bytes;
  * `peer-exited-cleanly`              — it got as far as running;
  * `poll-reports-readiness-but-never-writability`
                                       — THE FINDING, asserted as the limit it currently is: a payload
                                         crosses with BLOCKING I/O (the checks above prove it), AND the
                                         same `poll(2)` call answers asymmetrically about the very same
                                         socket — POLLIN is reported, POLLOUT NEVER is, although a
                                         blocking write succeeds. That is what stalls anything that
                                         multiplexes and waits for writability before sending, LibreSSL's
                                         s_client/s_server included. It flips when poll reports
                                         writability, the way §45-Y's select check flipped.

EVERY READ AND WRITE IS BOUNDED by a `poll(2)` deadline, because a probe whose purpose is to test a
path that may be wedged must not be able to hang. A stall therefore reports as a `-DIAG` line and a
failed check rather than as a killed case.

WHY IT MATTERS BEYOND TLS: libcurl on this guest has only ever reached `connect()` (its
`curl-http-reaches-connect` check asserts the ECONNREFUSED that proves the attempt was made). If a
payload cannot cross loopback, that fact is upstream of both the TLS handshake and any loopback HTTP
test — and it belongs to the kernel's network layer, not to either library.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/kernel_loopback_tcp"
CHECKS = ("loopback-socket-created", "bind-to-127-0-0-1", "listen", "fork-the-peer",
          "client-connect", "payload-arrived-at-the-peer", "response-arrived-at-the-client",
          "peer-exited-cleanly", "poll-reports-readiness-but-never-writability")


class Case(BaseCase):
    title = "kernel_loopback_tcp: can loopback carry a payload at all?"
    tier = "fast"
    # Self-contained (its own listener, its own peer) and bounded, so a reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("kernel_loopback_tcp")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo KERNEL-LOOPBACK-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("KERNEL-LOOPBACK"):
                self.note(line)

        done = "KERNEL-LOOPBACK DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no KERNEL-LOOPBACK DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^KERNEL-LOOPBACK %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.startswith("KERNEL-LOOPBACK ") and " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"KERNEL-LOOPBACK RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "KERNEL-LOOPBACK-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
