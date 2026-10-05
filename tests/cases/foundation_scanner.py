# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSScanner — a cursor over a string: read one thing at a time and leave the position where it stopped.

The probe is `/System/Shared/tests/foundation_scanner`, ONE unit importing only `<Foundation/Foundation.h>`.

THE POSITION IS THE CONTRACT, AND MOST OF THESE CHECKS ARE ABOUT WHEN IT MOVES: a scan skips the characters it was
told to skip BEFORE it looks, a scan that finds nothing moves nothing, a question about the end moves nothing
either, and setting the position is how a caller rescans — which is why the one raise on this class is asserted
rather than avoided.

  * `a-scanner-reads-the-string-it-was-given` — the two constructors, the three defaults (position 0, case
    sensitive, whitespace and newlines skipped) and `isAtEnd` on an empty string;
  * `whitespace-is-skipped-before-the-element-and-not-during-it`, `an-empty-skip-set-stops-at-the-space`;
  * `the-position-is-an-input-and-setting-it-past-the-end-raises` — the rewind, then `NSRangeException` by name,
    and the scanner still usable after the catch;
  * `the-end-ignores-skipped-characters-and-asking-does-not-move-the-position` — the save-and-restore;
  * `a-string-scan-advances-only-when-it-matches`, `case-sensitivity-is-opt-in-and-the-answer-is-the-receivers-text`
    (the answer is the RECEIVER's text, not the caller's object);
  * `a-character-run-is-taken-to-its-end-and-then-there-is-nothing`, and
    `scanning-for-the-skipped-characters-scans-nothing` — Apple's own named case, where the position ends up PAST
    the skipped run because the skip ran and the scan then found nothing;
  * the four `scan-up-to…` behaviours: stopping on a set and leaving it, taking the rest when the stop is absent,
    leaving the position at the START of the matched string, and refusing (with the result untouched) when that
    string is the first thing there;
  * the numeric grammar the header states, since Apple publishes none: signs on the signed doors and the refusal
    of the unsigned one, an overflowing run being VALID with the position past all of it, an exponent with no
    digits not being consumed, the optional `0x` on hex integers against the REQUIRED one on hex floats, and
    `-scanDecimal:`'s two rules (the canonical compaction, and 38 significant digits truncated toward zero);
  * `a-locale-changes-the-decimal-separator-and-agrees-with-the-formatter` — RELATIONAL rather than a hard-coded
    comma: the probe asks `NSNumberFormatter`, the class that already reads ICU for this fact, what a locale's
    separator IS, and requires the scanner to agree. That is the property the header claims, and it holds whether
    or not the machine's ICU data has a German locale;
  * `a-copy-is-independent-of-the-scanner-it-came-from` — this class is MUTABLE, so its `-copy` is a real copy
    rather than the receiver;
  * `every-scan-takes-a-null-result-to-mean-skip-past-it`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_scanner"
CHECKS = ("a-scanner-reads-the-string-it-was-given",
          "whitespace-is-skipped-before-the-element-and-not-during-it",
          "an-empty-skip-set-stops-at-the-space",
          "the-position-is-an-input-and-setting-it-past-the-end-raises",
          "the-end-ignores-skipped-characters-and-asking-does-not-move-the-position",
          "a-string-scan-advances-only-when-it-matches",
          "case-sensitivity-is-opt-in-and-the-answer-is-the-receivers-text",
          "a-character-run-is-taken-to-its-end-and-then-there-is-nothing",
          "scanning-for-the-skipped-characters-scans-nothing",
          "scan-up-to-characters-stops-on-the-set-and-leaves-it",
          "scan-up-to-characters-with-no-stop-takes-the-rest-of-the-string",
          "scan-up-to-a-string-leaves-the-position-at-it-and-refuses-when-it-is-first",
          "scan-up-to-a-string-that-is-nowhere-takes-the-rest",
          "decimal-integers-take-a-sign-and-refuse-a-letter",
          "an-overflowing-integer-is-valid-and-the-position-is-past-all-of-it",
          "the-unsigned-door-refuses-a-minus-and-takes-a-plus",
          "doubles-carry-an-exponent-and-an-exponent-without-digits-is-not-taken",
          "hex-integers-take-an-optional-prefix-but-need-digits",
          "a-hex-float-requires-its-prefix",
          "scan-decimal-answers-the-value-in-this-librarys-canonical-form",
          "a-decimal-longer-than-the-mantissa-truncates-toward-zero",
          "a-locale-changes-the-decimal-separator-and-agrees-with-the-formatter",
          "a-copy-is-independent-of-the-scanner-it-came-from",
          "every-scan-takes-a-null-result-to-mean-skip-past-it")


class Case(BaseCase):
    title = "NSScanner: the position, the four numeric grammars, and the locale's decimal separator"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_scanner")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-SCANNER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-SCANNER "):
                self.note(line)

        done = "FOUNDATION-SCANNER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-SCANNER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-SCANNER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-SCANNER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-SCANNER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
