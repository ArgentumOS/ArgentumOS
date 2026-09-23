# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The state half of the framing: frames in, messages out — W7, §59 slice 2b.

docs/design/foundation-plan.md §59. `FNWebSocketAssembler` is the companion of the codec, internal like it, and
the split is the point: **the codec remembers nothing and this remembers everything that matters.** A parse is a
function of bytes; a *message* is a function of history — which fragments belong together, what type the message
is, how much of it there is, and whether a control frame has arrived in the middle of it.

What the probe pins:

  * whole messages (both kinds), fragmented messages, and the order of their fragments;
  * **THE INTERLEAVED CONTROL FRAME OF §5.4 — answered WHEN IT ARRIVES, inside a fragmented message, with the
    message it interrupted going on untouched.** §59 deliberately left this as a *measurement* rather than a
    preference, because refusing it would be wrong for a compliant peer; this is where it is settled. A reader
    that queued the control frame behind the message would stall a peer's keepalive for as long as the message
    takes — and a stall is exactly what a ping exists to detect;
  * the three ways a peer can be wrong (§5.4): a continuation with nothing to continue, a new message started
    before the last one finished, and a message past the limit;
  * **the limit is the WHOLE MESSAGE, not one frame** — the fidelity point in Apple's own words ("includes the sum
    of all bytes from continuation frames"), so the same total split into pieces must fail too;
  * and the default limit, which is OURS (D2): Apple documents the property and not its default, and the two
    published accounts of it contradict each other.

The frames are REAL: each is built with `FNWebSocketCreateFrame` and parsed with `FNWebSocketParseFrame` before
it reaches the assembler, so what is under test is the same journey a frame makes off a socket.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_wsassemble"
CHECKS = (
    "a-single-frame-message-arrives-whole",
    "and-a-binary-message-assembles-the-same-way",
    "a-fragmented-message-assembles-in-order",
    "a-control-frame-in-the-middle-is-answered-when-it-arrives",
    "and-the-message-it-interrupted-is-untouched",
    "a-continuation-with-nothing-to-continue-is-an-error",
    "a-new-message-before-the-last-one-finished-is-an-error",
    "a-message-past-the-limit-fails",
    "and-the-limit-counts-the-fragments-together",
    "and-a-control-frame-is-not-part-of-any-message",
    "the-default-limit-is-the-megabyte-we-chose",
)


class Case(BaseCase):
    title = "The framing's state half: frames in, messages out (W7, §59 slice 2b)"
    tier = "fast"
    # No server and no socket: the assembler is fed parsed frames, so a reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_wsassemble")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-WSASSEMBLE-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-WSASSEMBLE"):
                self.note(line)

        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-WSASSEMBLE %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-WSASSEMBLE DONE" in out
        self.check("probe-ran", ran,
                   "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
