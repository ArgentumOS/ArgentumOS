# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The compression codecs — F12's acceptance.

docs/design/foundation-plan.md §5 (F12). The LAST queue row, and the first family that is a
BINDING rather than a rule: DEFLATE is a table of Huffman codes, so it comes from the `libz` this
system already ships (and already stages for the guest) instead of being written out. The probe is
`/System/Shared/tests/foundation_codecs`, built from two translation units; the support unit builds
the BYTES, so the codec is exercised on data it did not create.

  * `codec-round-trip`  — compress then decompress gives the bytes back;
  * `codec-large`       — 100 KB of repetition, whose compressed form is tiny, so decompression
                          has to grow its buffer many times (the doubling loop);
  * `codec-zlib-stream` — the output IS a zlib stream: the 0x78 header byte is MEASURED;
  * `codec-empty`       — an empty buffer round-trips;
  * `codec-odd`         — a buffer with no pattern round-trips (compression need not shrink it);
  * `codec-refusals`    — LZFSE/LZ4/LZMA answer nil AND an error that NAMES the algorithm;
  * `codec-bad-input`   — bytes that are not a stream are refused with an error, not a crash;
  * `codec-truncated`   — a PREFIX of a valid stream is refused;
  * `codec-mutable`     — the in-place forms, and that a refusal leaves the receiver ALONE;
  * `codec-null-error`  — a NULL error out-parameter still answers nil;
  * `cross-tu`          — bytes built in the other unit compress here.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_codecs"
CHECKS = ("codec-round-trip", "codec-large", "codec-zlib-stream", "codec-empty",
          "codec-odd", "codec-refusals", "codec-bad-input", "codec-truncated",
          "codec-mutable", "codec-null-error", "cross-tu")


class Case(BaseCase):
    title = "The compression codecs: one binding, one codec, three refusals"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_codecs")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CODECS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CODECS "):
                self.note(line)

        done = "FOUNDATION-CODECS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CODECS DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CODECS %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-CODECS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CODECS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
