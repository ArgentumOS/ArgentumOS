# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSStream, the head — W6's streams half. docs/design/foundation-plan.md §45-Y.

WHAT IS PROVED HERE IS THE HEAD, and a head can get two things wrong:

  * THE VALUES IT INVENTS. Apple publishes the CASE NAMES of `NSStreamEvent`, `NSStreamStatus`, the property
    keys and the typealiases, and never the numbers or strings behind them (§2's clean-room wall, and the
    plan's D2 ruling). So the values here are OUR CHOICE and the probe pins each one - a contract, not a
    convention nobody checked.
  * THE SEAM IT OFFERS A SUBSTREAM. The probe builds one (`FnTestStream`) over a real `pipe(2)`: the run
    loop's own file-descriptor source must fire the delegate when the pipe has a byte in it, and must NOT
    fire when it has not. That is the entire point of scheduling a stream, and it is asserted end to end
    rather than described.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_stream"
CHECKS = (
    "event-mask-values",
    "status-values",
    "property-key-strings",
    "network-bag-values",
    "bare-stream-starts-not-open",
    "bare-stream-open-is-inert",
    "property-bag-round-trip",
    "property-bag-forgets",
    "property-bag-refuses-a-nil-key",
    "substream-moves-the-status",
    "no-event-without-readiness",
    "source-fires-delegate",
    "input-stream-refuses-a-read-before-open",
    "input-stream-from-data-opens",
    "input-stream-from-data-reads",
    "input-stream-getbuffer",
    "input-stream-reads-the-rest",
    "input-stream-ends-at-eof",
    "input-stream-offset-key-reads",
    "input-stream-offset-key-seeks",
    "input-stream-from-file-constructs",
    "input-stream-from-file-opens",
    "input-stream-from-file-reads",
    "input-stream-getbuffer-refuses-a-file",
    "input-stream-closes",
    "select-does-not-report-a-regular-file",
    "input-stream-over-a-file-answers-the-read-would-not-block",
)


class Case(BaseCase):
    title = "the Foundation's stream head: the values, the property bag and the run-loop seam"
    tier = "fast"
    # Its only resources are a pipe it makes itself, so a reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_stream")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-STREAM-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-STREAM "):
                self.note(line)

        done = "FOUNDATION-STREAM DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-STREAM DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-STREAM %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS),
                           ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-STREAM RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-STREAM-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
