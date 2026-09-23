# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The WebSocket task itself: the layers meeting the substrate over a real connection — W7, §59 slice 3b.

docs/design/foundation-plan.md §59. The last rung: `NSURLSessionWebSocketTask` holds a stream task (§58), opens it
with `FNWebSocketHandshake` (§59 3a), and speaks `FNWebSocketFraming` + `FNWebSocketAssembler` (§59 2, 2b) over
it. Every layer below it is already green; this case is about the ORDER they are wired in.

The peer is the probe's OWN and it is raw: one plain-HTTP exchange (the upgrade), then RFC 6455 **through the same
codec the task uses** — a client frame arrives masked, a server frame goes out unmasked, which is the direction
rule the codec already enforces. No TLS, no proxy, no library in between.

What each check is for:

  * `the-upgrade-is-accepted` — the peer answers only when the key digests, so the delegate door firing means
    §4.2.2 agreed on BOTH sides;
  * `and-the-subprotocol-is-the-one-the-server-chose` — the client offers a list, the server picks, and the
    delegate is the only place that answer can be reported;
  * `a-message-reaches-the-far-end-and-comes-back` — the peer ECHOES what it read, so the message the client
    receives is the peer's own account rather than a copy of what the client thinks it sent;
  * `a-ping-is-answered-with-a-pong` — the pong handler is the only place that answer can arrive;
  * `the-close-carries-the-peers-code-and-reason` / `and-the-task-reports-them-too` — the peer answers with ITS
    code and reason (1001 + "peer says bye"), and both the delegate door and `closeCode`/`closeReason` must carry
    them;
  * `and-the-task-ends-exactly-once` — a task that reports twice is worse than one that never does;
  * `only-ws-and-wss-urls-make-a-task` — Apple's rule for the factories, answered with nil rather than with a task
    that would fail later in a worse way;
  * `and-a-reserved-close-code-is-refused-rather-than-sent` — §7.4.1: 1005 may be reported and never sent;
  * `the-peer-walked-the-whole-script` — the peer's EXIT CODE, whose distinct values name which step it failed at,
    so 0 means every exchange above happened in the order it expected.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_websockettask"
CHECKS = (
    "the-upgrade-is-accepted",
    "and-the-subprotocol-is-the-one-the-server-chose",
    "a-message-reaches-the-far-end-and-comes-back",
    "a-ping-is-answered-with-a-pong",
    "the-close-carries-the-peers-code-and-reason",
    "and-the-task-reports-them-too",
    "and-the-task-ends-exactly-once",
    "only-ws-and-wss-urls-make-a-task",
    "and-a-reserved-close-code-is-refused-rather-than-sent",
    "the-peer-walked-the-whole-script",
)


class Case(BaseCase):
    title = "The WebSocket task: the layers meeting the substrate (W7, §59 slice 3b)"
    tier = "fast"
    # Its own listener, its own peer, its own port: self-contained, and every wait is bounded.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_websockettask")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-WSTASK-CASE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-WSTASK"):
                self.note(line)

        missing = [c for c in CHECKS
                   if not any(line == "FOUNDATION-WSTASK %s ok" % c for line in out.splitlines())]
        self.check("every-check-reported", not missing,
                   "all %d checks reported ok" % len(CHECKS) if not missing
                   else "missing or failed: %s" % ", ".join(missing))
        ran = "FOUNDATION-WSTASK DONE" in out
        self.check("probe-ran", ran,
                   "the probe reached its end marker" if ran
                   else "did not reach its end marker; tail: " + out.strip()[-400:])
