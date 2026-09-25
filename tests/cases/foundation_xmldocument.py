# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSXMLDocument — XML slice XML-c's acceptance: the document and the parse bridge (foundation-plan.md §60).

Apple's abstract: "An XML document as internalized into a logical tree structure" — and that sentence IS the
slice: XML-a produced EVENTS, XML-b produced a TREE, and this class turns one into the other (`-initWithData:
options:error:` runs XML-a's parser over a delegate that BUILDS the tree) and writes it back out.

SHIPPED is the surface that needs no engine: the four initializers, the document attributes, the root element
and the two writing doors. REGISTERED, at the doors they belong to: `-dtd`/`-setDTD:` (the DTD class is
XML-d), `-validate`, the three XSLT doors, and FOUR OF THE FIVE OPTIONS — Validate, TidyHTML, TidyXML and
XInclude describe a pipeline this system does not have, and each is a NAMED REFUSAL (nil and an error that
says which one). The fifth, IncludeContentTypeDeclaration, describes the HTML/XHTML output, so the WRITING
door ignores it — a door with no error to refuse with — and the header says so.

TWO BOUNDARIES STATED RATHER THAN LEFT TO BE DISCOVERED: the XML DECLARATION is not read back (XML-a reports
a document's `<?xml …?>` as nothing), so a parsed document keeps the class's defaults and writing it out
emits a declaration from those; and a CDATA block arrives as a TEXT child whose marking is not recorded, so
writing it back escapes what was raw.

The probe is `/System/Shared/tests/foundation_xmldocument`, ONE unit, importing only
`<Foundation/Foundation.h>` plus the POSIX calls its file fixture makes.

  * `document-parses-into-a-tree` — the root element, its children, its attributes, and the XML written out;
  * `document-parses-from-a-string-and-a-file-url` — the other two doors, over a real file;
  * `document-namespace-declarations-become-children` — an `xmlns:p` ATTRIBUTE is a namespace CHILD (XML-b's
                                 stated reading) and comes back out in the start tag;
  * `document-a-malformed-document-answers-nil-and-its-error` — the parser's error through the door;
  * `document-the-declaration-is-written-from-the-attributes` — and the stated boundary behind it;
  * `document-the-attributes-default-and-round-trip` — version, encoding, MIME type, standalone, content kind;
  * `document-the-root-element-can-be-replaced`;
  * `document-a-refused-option-says-which-one` — four refusals, each NAMING itself;
  * `document-the-content-type-option-is-ignored-at-the-writing-door`;
  * `document-replacement-class-answers-its-argument` — Apple's subclassing hook;
  * `probe-tree-removed` — the tree is gone.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_xmldocument"
CHECKS = ("document-parses-into-a-tree", "document-parses-from-a-string-and-a-file-url",
          "document-namespace-declarations-become-children",
          "document-a-malformed-document-answers-nil-and-its-error",
          "document-the-declaration-is-written-from-the-attributes",
          "document-the-attributes-default-and-round-trip", "document-the-root-element-can-be-replaced",
          "document-a-refused-option-says-which-one",
          "document-the-content-type-option-is-ignored-at-the-writing-door",
          "document-replacement-class-answers-its-argument", "probe-tree-removed")


class Case(BaseCase):
    title = "NSXMLDocument: the parse bridge into the tree, and the document's own attributes"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_xmldocument")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-XMLDOCUMENT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-XMLDOCUMENT "):
                self.note(line)

        done = "FOUNDATION-XMLDOCUMENT DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-XMLDOCUMENT DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-XMLDOCUMENT %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-XMLDOCUMENT RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-XMLDOCUMENT-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
