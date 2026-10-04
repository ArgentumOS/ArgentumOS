# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSXMLParser — W8 slice XML-a's acceptance: the event-driven parser (foundation-plan.md §60).

Apple's abstract is "an event driven parser of XML documents (including DTD declarations)". This slice ships
the parser and the fourteen "Handling XML" members of its delegate; the six DTD events and the tree classes
(NSXMLNode and its family) are their own slices, because a DTD is a grammar language and a tree is a second
parser over the same text. The parser is HAND-WRITTEN — this system has no libxml2 — and the values of the
error enum and of the policy enum are ours (§11.6.1 D2: Apple publishes the names and not the numbers, which
are libxml2's internal codes).

The probe's delegate keeps an EVENT LOG rather than booleans: what a parser promises is an ORDER, and an
order compared as a set is not the same claim.

The probe is `/System/Shared/tests/foundation_xmlparser`, ONE unit, importing only
`<Foundation/Foundation.h>` plus the POSIX calls its file fixture makes.

  * `xml-the-events-of-a-document-in-order` — document, element (with its attributes), characters, end,
                                 document — as an ORDER;
  * `xml-entities-are-decoded-in-text-and-attributes` — the five XML declares plus decimal and hexadecimal
                                 character references, in text AND in an attribute value;
  * `xml-comments-cdata-and-processing-instructions` — the three events, and the XML DECLARATION is not
                                 reported as a processing instruction;
  * `xml-a-self-closing-element-is-a-start-and-an-end` — what the document means;
  * `xml-namespaces-are-off-by-default` — the element name is then the qualified name and the URI is nil;
  * `xml-namespaces-on-split-and-expand` — with processing on: the local name, the declared URI, the
                                 qualified name as written;
  * `xml-prefix-mappings-are-reported-when-asked` — didStartMappingPrefix/didEndMappingPrefix;
  * `xml-a-malformed-document-fails-with-its-code-and-position` — NO, an error in Apple's domain, the
                                 parseErrorOccurred event, and a line and column that are not invented;
  * `xml-a-tag-mismatch-and-a-double-hyphen-comment-are-told-apart` — two malformations, two codes;
  * `xml-abort-stops-the-parse` — aborting from a callback fails the parse with the code Apple publishes for
                                 it, and no later event arrives;
  * `xml-it-parses-from-a-file-url-too` — the second initializer, over a real file;
  * `probe-tree-removed` — the tree is gone.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_xmlparser"
CHECKS = ("xml-the-events-of-a-document-in-order",
          "xml-entities-are-decoded-in-text-and-attributes",
          "xml-comments-cdata-and-processing-instructions",
          "xml-a-self-closing-element-is-a-start-and-an-end", "xml-namespaces-are-off-by-default",
          "xml-namespaces-on-split-and-expand", "xml-prefix-mappings-are-reported-when-asked",
          "xml-a-malformed-document-fails-with-its-code-and-position",
          "xml-a-tag-mismatch-and-a-double-hyphen-comment-are-told-apart", "xml-abort-stops-the-parse",
          "xml-it-parses-from-a-file-url-too", "probe-tree-removed")


class Case(BaseCase):
    title = "NSXMLParser: the event-driven parser, hand-written, with its delegate's events"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_xmlparser")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-XMLPARSER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-XMLPARSER "):
                self.note(line)

        done = "FOUNDATION-XMLPARSER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-XMLPARSER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-XMLPARSER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-XMLPARSER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-XMLPARSER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
