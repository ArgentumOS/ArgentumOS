# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The Foundation's strings — F1's acceptance.

docs/design/foundation-plan.md §5 (F1) and §9. The probe is
`/System/Shared/tests/foundation_string`, built from TWO translation units, and
its checks are chosen to cover the two representations clang produces for a
literal:

  * `tiny`        — a literal of FEWER THAN 9 ASCII characters is a TAGGED
                    POINTER (clang packs the characters into the pointer); it is
                    only readable because the Foundation registers a class at
                    the tag the runtime dispatches through. That registration is
                    the whole reason F1 was blocked, so it is asserted directly;
  * `owned`       — a long literal is a real object, and equals an owned string;
  * `mixed`       — a tagged string and an owned one are the SAME value (equal
                    and equal-hashing), which is what makes them interchangeable;
  * `utf8`        — `-length` counts BYTES and `-characterCount` counts
                    characters (the user's decision), with `-characterAtIndex:`
                    indexing characters;
  * `mutable`     — `NSMutableString` mutation, and `-copy` returning a snapshot;
  * `description` — the root class's names its class, an override wins, and a
                    string describes itself;
  * `cross-tu`    — a constant string defined in the other translation unit;
  * `class-format-arguments` — the class-side `+stringWithFormat:arguments:` must
                    consume a COPY of the caller's `va_list` (C99 7.15.1.4), so
                    TWO renders from one caller list have to agree. That is the
                    contract the F4 crash was an instance of, and the check the
                    probe was missing when the crash was recorded as unexplained.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_string"
CHECKS = ("tiny", "owned", "mixed", "utf8", "mutable", "description", "cross-tu",
          "string-api-complete", "string-format", "string-format-tagged-object",
          "class-format-arguments",
          "string-compare",
          "string-transform", "string-convert", "string-path",
          "string-encoding", "string-mutable", "characterset-api-complete",
          "locale-basics", "locale-turkic-upper", "locale-turkic-lower",
          "locale-turkic-neutral", "locale-literal-high-byte",
          "locale-turkic-compare", "locale-boundaries",
          "locale-current", "locale-api-complete")


class Case(BaseCase):
    title = "the Foundation's strings: tagged literals, owned strings, UTF-8"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_string")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-STRING-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-STRING "):
                self.note(line)

        done = "FOUNDATION-STRING DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-STRING DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-STRING %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-STRING RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-STRING-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
