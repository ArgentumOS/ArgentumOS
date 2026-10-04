# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""RFC 6455's opening handshake as a layer — W7, §59 slice 3a.

docs/design/foundation-plan.md §59. `FNWebSocketHandshake` is internal (not public API, not in Foundation.h) and
it exists as a layer for the same reason the codec does: **the part of a WebSocket client most dangerous to get
wrong is also the part that needs no socket.**

A client that accepts a WRONG `Sec-WebSocket-Accept` has silently handed its connection to whatever answered — a
cache, a proxy, or a page that happened to return `101`. So the digest (§4.2.2) is tested against **RFC 6455's
own worked example** rather than against a server that agrees with whatever we wrote:

    key dGhlIHNhbXBsZSBub25jZQ==  ->  accept s3pPLMBiTxaQ9kYGzzhZRbK+xOo=

What the probe pins:

  * that known answer — if that line is wrong, nothing else in this case means anything;
  * the nonce (§4.1): sixteen random bytes, base64-encoded, and two calls differ;
  * the request: the request line (with a path even when the URL has none), `Host`, `Upgrade`, `Connection`, the
    version, the key, and the protocol list when there is one — and **no `Sec-WebSocket-Extensions` ever**;
  * the reply: `101` plus `Upgrade` plus the right accept, with the headers matched **without regard to case**
    (HTTP's rule — a server spelling it `UPGRADE:` is a correct server, and a bytes-comparing client would refuse
    it); a bare `HTTP/1.1 101` with no reason phrase passes; a wrong accept, a non-101 status and a 101 without
    the upgrade header are each refused *with a reason the caller can report*;
  * and the negotiated subprotocol coming back, so a caller can check it against what it asked for.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_wshandshake"
CHECKS = (
    "the-accept-matches-rfc-6455s-own-example",
    "a-key-is-sixteen-random-bytes-in-base64",
    "and-two-keys-are-not-the-same",
    "the-request-asks-for-the-upgrade",
    "and-the-protocol-list-travels-comma-separated",
    "and-no-extensions-are-ever-offered",
    "and-a-url-with-no-path-still-has-one",
    "a-good-reply-is-an-upgrade",
    "and-the-headers-do-not-care-about-case",
    "and-a-bare-101-with-no-reason-phrase-is-refused-for-the-right-reason",
    "a-wrong-accept-is-refused",
    "and-a-non-101-reply-is-refused-with-the-status-named",
    "and-a-101-without-the-upgrade-header-is-refused",
    "and-the-negotiated-protocol-comes-back",
)


class Case(BaseCase):
    title = "RFC 6455's opening handshake as a layer (W7, §59 slice 3a)"
    tier = "fast"
    # No socket and no server: the handshake is a function of a key and a reply.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_wshandshake")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-WSHANDSHAKE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-WSHANDSHAKE"):
                self.note(line)

        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-WSHANDSHAKE %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-WSHANDSHAKE DONE" in out
        self.check("probe-ran", ran,
                   "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
