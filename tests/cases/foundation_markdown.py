# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The markdown family's two value objects — §62.64's acceptance.

docs/design/foundation-plan.md §62.64. These are the nouns of "Strings with Metadata" that need NO parser:
`NSAttributedStringMarkdownParsingOptions` (what an importer would be configured with) and
`NSAttributedStringMarkdownSourcePosition` (where in the markdown a piece of text came from). The importer itself
is the dependency §12.6 still lists, and the plan's vocabulary rule has shipped a family's nouns before its
engine before (W10 shipped the attributes and the two enums).

The probe is `/System/Shared/tests/foundation_markdown`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-options-start-from-their-two-published-and-three-our-defaults` — two of the five starting values are
                                     Apple's own words ("The default is NO", "The default is nil") and three are
                                     OURS and say so (the full syntax, the forgiving failure policy, no
                                     source-position attributes);
  * `the-options-properties-round-trip` — all five are readwrite;
  * `a-copy-is-a-snapshot-and-the-language-code-is-copied` — a MUTABLE source is mutated after being handed over,
                                     so an assigning setter would answer the mutated string;
  * `the-two-enums-are-distinct-within-themselves` — the three syntax values and the two failure policies are
                                     pairwise distinct;
  * `a-source-position-keeps-its-four-numbers` and `a-source-position-copy-is-a-snapshot` — the position is four
                                     numbers and a copy is its own object;
  * `a-range-covers-the-line-between-the-two-columns` — one line, END-EXCLUSIVE (ours: Apple publishes the
                                     method's purpose and not its inclusivity);
  * `a-range-can-span-two-lines` — the conversion is real rather than an offset;
  * `a-column-is-a-utf8-byte-and-not-a-character` — THE CHECK WITH TEETH: in "héllo" the é is two bytes, so
                                     columns 1..6 cover FOUR UTF-16 units, and a character-counting implementation
                                     would answer five;
  * `a-position-past-the-end-is-clamped` — no refusal, no crash, and the range stays inside the string.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_markdown"
CHECKS = ("the-options-start-from-their-two-published-and-three-our-defaults",
          "the-options-properties-round-trip",
          "a-copy-is-a-snapshot-and-the-language-code-is-copied",
          "the-two-enums-are-distinct-within-themselves",
          "a-source-position-keeps-its-four-numbers",
          "a-source-position-copy-is-a-snapshot",
          "a-range-covers-the-line-between-the-two-columns",
          "a-range-can-span-two-lines",
          "a-column-is-a-utf8-byte-and-not-a-character",
          # §62.107: the importer's half.
          "header-intent-and-level",
          "inline-intent-bits",
          "link-attributes-and-base-url",
          "code-block-language-and-literal-content",
          "list-intent-nesting-ordinal-and-delimiter",
          "table-refused-by-name",
          "image-alt-and-url-attributes",
          "relative-destination-without-a-base-has-no-url",
          "entities-and-escapes",
          "hard-break-and-soft-break-bits",
          "failure-policy-difference",
          "inline-only-syntaxes",
          "source-position-attributes",
          "extended-attributes",
          "language-code-attribute",
          # §62.108: the string-table doors and both macro families (the classic door was missing).
          "localized-string-door-fallbacks",
          "localized-attributed-door-parses-markdown",
          "localized-attributed-macros",
          "localized-string-macros",
          "a-position-past-the-end-is-clamped")


class Case(BaseCase):
    title = "The markdown family's value objects: options and source positions"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_markdown")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-MARKDOWN-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-MARKDOWN "):
                self.note(line)

        done = "FOUNDATION-MARKDOWN DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-MARKDOWN DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-MARKDOWN %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-MARKDOWN RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-MARKDOWN-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
