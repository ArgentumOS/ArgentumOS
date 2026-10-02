# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The run store and its file-format doors — W10 slice 1's acceptance, extended by §62.58.

THE SUBJECT IS `NSAttributedString` AS A RUN STORE. Apple's promise for `-attributesAtIndex:effectiveRange:`
is the LONGEST range over which the attributes are the same, so the checks build equal attribute sets in
several different ways — one dictionary over a whole string, two appends, an edit group — and require each to
read back as ONE run. Beside that: the primitives the store assumes (`-copy` on a dictionary, a mutable
dictionary's snapshot), the coding paths, and the inventory BOTH WAYS (the shipped selectors must exist and
the AppKit/UIKit/TextKit half must be absent).

§62.58 MADE THE RTF DOOR A REAL WRITER, so the two checks that used to assert the doors answer nil are now
seven: RTF produces a document with its own shell, escapes the format's reserved characters, carries
non-ASCII as the signed `\\uN?` escape, turns an inline-intent bit into the control word that means it, and
writes a line feed as a paragraph break — while the doors that still cannot do their work (RTFD, the doc
format) are still required to REFUSE BY NAME, because a silent nil is indistinguishable from an empty
document.

(2026-09-30) THE "Calculating linguistic units" GROUP SHIPPED, so six checks were added and three selectors
left the absent list: `-doubleClickAtIndex:`, `-nextWordFromIndex:forward:` and `-lineBreakBeforeIndex:` are
answered over `FNTextBreaking` (the word/line-break substrate, §62.42) and are no longer "the excluded AppKit
half"; the deprecated `-URLAtIndex:effectiveRange:` now has this library's own documented tokenizer; the three
`+loadFromHTMLWith{Data,FileURL,String}:options:completionHandler:` siblings refuse through their handler like
`+loadFromHTMLWithRequest:`; and `-initWithContentsOfMarkdownFileAtURL:options:baseURL:error:` adds Apple's
four-argument spelling beside the shorter markdown door. `-lineBreakByHyphenatingBeforeIndex:withinRange:`
stays ABSENT: it needs a hyphenation resource this system does not carry.

The probe is `/System/Shared/tests/foundation_attributedstring`, ONE unit, importing only
`<Foundation/Foundation.h>`. NO FIXTURE: an attributed string is built from a string, and the RTF checks read
the produced BYTES rather than a decoded string.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_attributedstring"
CHECKS = (
    "attributedstring-mutablestring-length-agrees-with-the-store",
    "attributedstring-mutablestring-is-live",
    "attributedstring-inflecting-refuses-by-name",
    "attributedstring-format-context-keeps-the-format-attributes",
    "attributedstring-format-context",
    "attributedstring-localized-format-options-context",
    "attributedstring-localized-format-context",
    "attributedstring-localized-format-options",
    "attributedstring-localized-format",
    "attributedstring-option-argument-attributes-unmerged",
    "attributedstring-option-replacement-index",
    "attributedstring-format-attributes-reach-the-substitution",
    "attributedstring-format-substitutes",
    "attributedstring-description-is-the-string",
    "attributedstring-mutation-concatenates-exactly",
    "attributedstring-mutation-never-writes-null","morphology-vocabulary-is-distinct-and-carried",
          "coding-round-trips-through-the-archiver",
          "coding-refuses-what-a-property-list-cannot-carry",
          "coding-supports-secure-coding-answers-yes",
          "file-format-doors-refuse-by-name",
          "rtf-writes-a-document-with-its-own-shell",
          "rtf-escapes-the-formats-reserved-characters",
          "rtf-carries-non-ascii-as-the-signed-u-escape",
          "rtf-maps-an-inline-intent-bit-to-its-control-word",
          "rtf-writes-a-line-feed-as-a-paragraph-break",
          "the-other-format-doors-still-refuse-by-name",
          "primitives-a-store-owns-what-it-was-given",
          "primitives-dictionaries-behave-as-the-store-assumes",
          "primitives-constructors-do-not-alias-their-source",
          "inventory-the-shipped-selectors-exist", "inventory-the-boundaries-are-absent",
          # 2026-10-01: the mutable-only current-locale format door (NSMutableAttributedString/appendLocalizedFormat:).
          "append-localized-format-appends-the-formatted-string",
          "string-and-length-are-the-initialisers", "the-coalescing-contract-holds-whatever-built-it",
          "the-attribute-range-can-be-longer-than-the-dictionary-run",
          "the-longest-effective-range-form-clips-to-its-range",
          "a-replacement-takes-the-attributes-in-force-at-its-start",
          "appending-an-attributed-string-carries-its-own-runs",
          "a-substring-keeps-the-attributes-it-covered",
          "equality-is-by-value-not-by-identity",
          "delete-and-set-keep-text-and-runs-together",
          "a-nil-value-removes-and-removal-shows-in-the-runs",
          "enumeration-tiles-the-range-and-reverses", "an-out-of-range-index-raises",
          "a-copy-is-independent-and-an-immutable-copy-is-a-snapshot",
          # 2026-09-30: the "Calculating linguistic units" group (over FNTextBreaking), the deprecated URL
          # door, the three +loadFromHTMLWith* siblings, and the markdown baseURL: file door.
          "double-click-answers-the-word-at-an-index",
          "next-word-walks-to-word-starts",
          "line-break-before-index-answers-the-enclosing-lines-start",
          "url-at-index-answers-the-url-that-covers-it",
          "the-html-siblings-refuse-through-their-handler",
          "the-markdown-file-door-takes-a-base-url",
          # 2026-10-01: the "Getting the supported text-file formats" group's six implementable members
          # (the four deprecated class methods and the two modern class properties); -prefersRTFDInRange:
          # stays absent (it asks about attachments, which have no class here).
          "the-supported-text-format-doors-answer-their-vocabularies",
          "the-format-doors-list-the-published-file-types")


class Case(BaseCase):
    title = "Attributed strings: the run store and its contract"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_attributedstring")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-ATTRIBUTEDSTRING-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            # the MEASUREMENTS are the point of this run: surface them, because the assertions are written
            # from these numbers rather than from a guess.
            if line.startswith("FOUNDATION-ATTRIBUTEDSTRING ") or line.startswith("SUBSTRATE "):
                self.note(line)

        done = "FOUNDATION-ATTRIBUTEDSTRING DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-ATTRIBUTEDSTRING DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-ATTRIBUTEDSTRING %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-ATTRIBUTEDSTRING RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-ATTRIBUTEDSTRING-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
