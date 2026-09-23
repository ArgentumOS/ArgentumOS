# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""RFC 6455's byte layer: the frame codec, as pure functions — W7, §59 slice 2.

docs/design/foundation-plan.md §59. The framing is a LAYER (`FNWebSocketFraming`, internal — not public API and
not in Foundation.h, FNPointerTable being the precedent) rather than a paragraph inside the task, and this case is
the reason: a codec that only existed as "what the task does with a socket" could only be tested through a
connection and a peer, and a failure would name a connection rather than a frame.

What the probe pins, and the trap each check exists for:

  * the round trip and both directions of the mask rule — a client's frames MUST be masked (§5.3) and a server's
    MUST NOT be (§5.1), and both directions are asserted because an implementation that ignored the rule would
    pass whichever one it happened to satisfy;
  * THE THREE LENGTH ENCODINGS (§5.2) — 126 and 127 are MARKERS, not lengths, which is the single most common way
    a hand-written codec is wrong and is wrong only for payloads nobody tries;
  * the control frame rules (§5.5) — never fragmented, never longer than 125 bytes;
  * THE INCREMENTAL CONTRACT — a frame that has not fully arrived answers 0 ("ask again"), which is neither an
    error nor a shorter frame, and a reader that confused them would lose the rest of a message;
  * the no-copy property — the frame's payload is a window into the caller's buffer;
  * and the close payload's one non-byte rule — §7.4.1 reserves 1005, 1006 and 1015 for a READER to report what
    happened to it, so they may be reported and never sent.

No socket, no handshake and no server anywhere in it.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_wsframe"
CHECKS = (
    "a-text-frame-parses-back-to-its-payload",
    "and-a-client-frame-carries-the-mask-and-a-key",
    "and-the-mask-is-its-own-inverse",
    "a-126-byte-payload-uses-the-16-bit-encoding",
    "a-70000-byte-payload-uses-the-64-bit-encoding",
    "and-a-small-payload-uses-the-7-bit-one",
    "a-server-frame-may-not-be-masked",
    "and-a-client-frame-must-be",
    "a-reserved-opcode-is-refused",
    "a-control-frame-may-not-be-fragmented",
    "and-may-not-be-longer-than-125-bytes",
    "a-frame-that-has-not-arrived-is-not-yet-a-frame",
    "and-the-frame-points-into-the-buffer-it-was-given",
    "a-close-code-may-be-sent",
    "and-the-three-reserved-codes-may-not",
    "and-an-empty-close-payload-means-no-code-at-all",
    "and-a-length-that-promises-a-code-needs-a-payload-to-read-it",
)


class Case(BaseCase):
    title = "RFC 6455's byte layer: the frame codec (W7, §59 slice 2)"
    tier = "fast"
    # Pure functions over bytes: no server, no socket, no state between runs.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_wsframe")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-WSFRAME-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-WSFRAME"):
                self.note(line)

        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-WSFRAME %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-WSFRAME DONE" in out
        self.check("probe-ran", ran,
                   "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
