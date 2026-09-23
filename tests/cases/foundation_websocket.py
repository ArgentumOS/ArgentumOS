# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The WebSocket values: the message and the two enums — W7, §59 slice 1.

docs/design/foundation-plan.md §59. The first of the WebSocket family's four slices, and the only one with no
transport in it: `NSURLSessionWebSocketMessage` (two initialisers, three read-only properties) and the two enums
(`NSURLSessionWebSocketMessageType`, two cases; `NSURLSessionWebSocketCloseCode`, thirteen).

What the probe pins, and why each check is separate:

  * `a-data-message-holds-data-and-no-string` / `a-string-message-holds-a-string-and-no-data` — Apple's page
    states the rule in words ("if initialized with data, the string property will be nil and vice versa"), and
    BOTH DIRECTIONS are asserted because a plausible wrong implementation — a text message handing back its UTF-8
    bytes as `data` — passes the first and fails the second;
  * `the-message-copied-its-payload-rather-than-borrowing-it` — OURS rather than Apple's (they do not say what
    happens to the object handed in); asserted because a choice nobody asserts is a choice nobody has;
  * `and-a-nil-payload-makes-no-message` — a message must hold exactly one payload, and the page gives two
    initialisers and no third;
  * `the-two-message-types-are-the-values-we-chose` — ours under D2, because Apple publishes the case names and
    not their values;
  * `the-twelve-closed-codes-are-the-rfcs` / `and-the-code-that-is-not-in-the-rfc-is-ours` — RFC 6455 §7.4.1
    DEFINES twelve of the thirteen, and `...Invalid` is not in the RFC at all, so its value is our choice.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_websocket"
CHECKS = (
    "a-data-message-holds-data-and-no-string",
    "a-string-message-holds-a-string-and-no-data",
    "the-message-copied-its-payload-rather-than-borrowing-it",
    "and-a-nil-payload-makes-no-message",
    "the-two-message-types-are-the-values-we-chose",
    "the-twelve-closed-codes-are-the-rfcs",
    "and-the-code-that-is-not-in-the-rfc-is-ours",
)


class Case(BaseCase):
    title = "The WebSocket values: the message and the two enums (W7, §59 slice 1)"
    tier = "fast"
    # No server and no socket: this slice is values, so a reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_websocket")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-WEBSOCKET-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-WEBSOCKET"):
                self.note(line)

        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-WEBSOCKET %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-WEBSOCKET DONE" in out
        self.check("probe-ran", ran,
                   "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
