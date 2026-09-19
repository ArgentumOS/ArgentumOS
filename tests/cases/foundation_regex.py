# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSRegularExpression / NSTextCheckingResult — F13.16's acceptance.

docs/design/foundation-plan.md §10, and the LAST item that track names. The engine is musl's, which
is the decision §10 recorded before the slice existed: the predicate family's `MATCHES` already binds
POSIX ERE through `<regex.h>`, the engine lives inside libc, and a second engine would be a second
set of behaviours to be wrong about. The probe is `/System/Shared/tests/foundation_regex`, ONE unit,
importing only `<foundation/Foundation.h>`.

  * `regex-compiles-and-refuses`         — a pattern with two groups, and a BROKEN one refused at
                                           construction with the engine's own message;
  * `regex-finds-all-matches`            — three matches in one string, each checked by the
                                           SUBSTRING its range points at;
  * `regex-capture-groups`               — group 0 is the whole match and the groups follow;
  * `regex-case-option`                  — the same pattern, folded and not;
  * `regex-utf16-ranges`                 — THE CONVERSION, on a NON-ASCII string: the engine counts
                                           bytes and Cocoa counts UTF-16 units, and an ASCII-only
                                           test would have passed with no conversion at all;
  * `regex-replace-with-template`        — `$2/$1` against `([0-9]+)-([0-9]+)`;
  * `regex-anchors-option`               — `AnchorsMatchLines`, which is `REG_NEWLINE`;
  * `regex-options-report-what-took-effect` — an option POSIX cannot express does not appear in
                                           `-options`: the caller is told rather than told nothing;
  * `regex-empty-match-advances`         — `a*` on `"bab"` is FOUR matches; a loop that did not
                                           advance would HANG here rather than fail, so the count is
                                           the measurement.

NAMED ABSENT, in the header rather than here: `AllowCommentsAndWhitespace`, `IgnoreMetacharacters`,
`UseUnixLineSeparators` and `UseUnicodeWordBoundaries` have no POSIX spelling, the block-based
enumeration doors are not implemented, and `-replacementStringForResult:...` is absent.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_regex"
CHECKS = ("regex-compiles-and-refuses", "regex-finds-all-matches", "regex-capture-groups",
          "regex-case-option", "regex-utf16-ranges", "regex-replace-with-template",
          "regex-anchors-option", "regex-options-report-what-took-effect",
          "regex-empty-match-advances")


class Case(BaseCase):
    title = "NSRegularExpression: regular expressions as objects, on musl's engine"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_regex")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-REGEX-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-REGEX "):
                self.note(line)

        done = "FOUNDATION-REGEX DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-REGEX DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-REGEX %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-REGEX RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-REGEX-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
