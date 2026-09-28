# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSData's deprecated option spellings and the compression error codes — §62.97's acceptance.

THE SPELLINGS ARE COMPARED, AND THEN USED. A deprecated name resolving to a different bit would compile and
read a file a different way, which a check that merely asked "did the read succeed" could not see — so the first
check compares each name to the canonical value it must equal, and the second WRITES a file with `NSAtomicWrite`
and reads it back byte for byte through all three deprecated reading spellings.

THE COMPRESSION CODES ARE APPLE'S, AND THEIR RANGE IS THE CONTENT: 5376 for a failed compression, 5377 for a
failed decompression, bracketed by Minimum and Maximum — the window Apple reserves so a codec failure can never
be mistaken for another Cocoa error. Both halves are asserted.

AND THE CODES ARE WIRED TO BEHAVIOUR RATHER THAN MERELY DECLARED: asking for an algorithm this library has no
codec for answers `NSCompressionFailedError`, and garbage handed to the zlib decoder answers
`NSDecompressionFailedError`. Before this unit both answered `code:1`, a number nothing declared — invisible to
the caller that switches on an error, which is the documented way to use these doors. The last check keeps the
diagnostic that was already there: the error still NAMES the algorithm it refused.

The probe is `/System/Shared/tests/foundation_dataoptions`, ONE unit, importing only `<Foundation/Foundation.h>`.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_dataoptions"
CHECKS = (
    "the-deprecated-spellings-are-the-canonical-values",
    "the-deprecated-spellings-really-open-the-doors-they-name",
    "the-compression-error-codes-are-apples-and-the-range-brackets-them",
    "a-refused-compression-and-a-failed-decompression-carry-their-own-codes",
    "the-error-still-names-the-algorithm",
)


class Case(BaseCase):
    title = "NSData: the deprecated spellings and the compression error codes (§62.97)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_dataoptions")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DATAOPTIONS-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DATAOPTIONS "):
                self.note(line)

        done = "FOUNDATION-DATAOPTIONS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DATAOPTIONS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DATAOPTIONS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DATAOPTIONS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DATAOPTIONS-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
