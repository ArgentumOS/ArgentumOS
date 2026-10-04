# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSTextCheckingResult as the general class, and the vocabulary that says what a result IS.

The probe is `/System/Shared/tests/foundation_textchecking`, ONE unit importing only
`<Foundation/Foundation.h>`.

THE FIRST HALF IS THE CLASS Apple documents: the thirteen checking types and the two typedefs beside them, the
eleven keys of the component dictionaries, every factory and the payload each one carries, the ranges, the
adjustment (Apple publishes no rule for a shift that would invalidate one, so the refusal is this library's and is
asserted as such), and the identity rules - `-copy` on an immutable result, and an `-isEqual:` that compares WHAT
WAS FOUND rather than merely where.

  * `checking-types-are-distinct-single-bits` — the thirteen are distinct powers of two;
  * `the-three-masks-bracket-the-thirteen` — the system/custom split is disjoint and their union is AllTypes;
  * `the-eleven-keys-are-distinct-strings` — two keys sharing a value would make a component unaddressable;
  * `every-factory-sets-its-own-type`     — the kind is readable from the object, not from the door it came through;
  * `a-result-carries-only-its-own-payload` — the wrong door answers nil rather than a stray pointer;
  * `ranges-answer-and-a-past-the-end-index-is-not-found`;
  * `adjusting-ranges-shifts-and-refuses-a-negative-shift`;
  * `copy-is-the-receiver-and-keeps-the-payload` — which is what the F13.16 version of the class got wrong;
  * `equality-compares-the-payload`, `components-is-the-general-door`.

THE SECOND HALF IS WHERE THE CLASS IS USED as Apple uses it: a regex match now carries its expression and its
type, and a NAMED group answers by name - which cost the `(?<name>...)` translation the regex class performs for a
POSIX engine that cannot spell one, with its own two checks (the numbering is unchanged, and escapes and character
classes are not touched on the way).

THE VALUES ARE OURS AND THE PROBE CLAIMS ONLY WHAT IS TRUE OF THEM: Apple publishes the thirteen case names and no
number for any of them, so the checks assert that the bits are distinct and that the masks bracket them, rather
than comparing with a value this tree would have had to invent.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_textchecking"
CHECKS = ("checking-types-are-distinct-single-bits",
          "the-three-masks-bracket-the-thirteen",
          "the-eleven-keys-are-distinct-strings",
          "every-factory-sets-its-own-type",
          "a-result-carries-only-its-own-payload",
          "ranges-answer-and-a-past-the-end-index-is-not-found",
          "adjusting-ranges-shifts-and-refuses-a-negative-shift",
          "copy-is-the-receiver-and-keeps-the-payload",
          "equality-compares-the-payload",
          "components-is-the-general-door",
          "a-matches-result-is-typed-and-carries-its-expression",
          "named-groups-answer-by-name",
          "the-translation-keeps-index-addressing-and-the-callers-pattern",
          "the-translation-leaves-escapes-and-classes-alone",
          "the-block-enumerator-walks-matches-and-honours-stop")


class Case(BaseCase):
    title = "NSTextCheckingResult: the general result, and what makes one of each kind"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_textchecking")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-TEXTCHECKING-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-TEXTCHECKING "):
                self.note(line)

        done = "FOUNDATION-TEXTCHECKING DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-TEXTCHECKING DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-TEXTCHECKING %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-TEXTCHECKING RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-TEXTCHECKING-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
