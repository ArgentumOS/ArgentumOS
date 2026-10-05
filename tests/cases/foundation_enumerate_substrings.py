# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`-enumerateSubstringsInRange:options:usingBlock:` — §62.48's acceptance.

The options and their eleven cases were ALREADY `shipped` in the ledger while the method that takes them was
declared nowhere: a vocabulary the surface file called complete and a door that did not exist. So this case's
subject is the door — the units come from the library's one breaker (`FNTextBreaking`, whose own header names this
method as a caller), and the boundaries it states are the boundaries it keeps.

WHAT IS ASSERTED: lines/words/characters come back whole and in order; a LINE or a SENTENCE is enclosed by its
PARAGRAPH, a WORD by its SENTENCE; `Reverse` is the same units the other way (a buffered walk, because an ICU
iterator is a forward cursor); `SubstringNotRequired` still reports ranges and hands back nothing; and
`Localized`, `ByCaretPositions`, `ByDeletionClusters`, ZERO units and TWO units all refuse.

The expectations are ABSOLUTE - the exact substrings and offsets of strings written into the probe - because a
check that compared two spellings of the same walk would pass for a wrong implementation as easily as a right one.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_enumerate_substrings"
CHECKS = (
    "lines-come-back-whole-and-in-order",
    "words-and-composed-characters-come-from-the-same-breaker",
    "a-lines-enclosing-range-is-its-paragraph",
    "paragraph-units-are-delimited-by-their-terminators",
    "stop-ends-the-walk-early",
    "reverse-is-the-same-units-in-the-other-order",
    "substring-not-required-still-reports-ranges-and-hands-back-nothing",
    "the-refused-options-refuse-and-so-do-zero-units-and-two",
)


class Case(BaseCase):
    title = "The door that consumes NSStringEnumerationOptions (§62.48)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_enumerate_substrings")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-ENUMERATE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-ENUMERATE "):
                self.note(line)

        done = "FOUNDATION-ENUMERATE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-ENUMERATE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-ENUMERATE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-ENUMERATE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-ENUMERATE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
