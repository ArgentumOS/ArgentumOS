# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSXMLNode + NSXMLElement — XML slice XML-b's acceptance: the tree (foundation-plan.md §60).

Apple's abstracts: NSXMLNode is "the nodes in the abstract, logical tree structure that represents an XML
document", NSXMLElement is "the element nodes in an XML tree structure". Between them the pages publish
about seventy members; this slice ships the part a caller can BUILD AND READ — the kinds and their factories,
the accessors, the tree navigation, the children and attributes, the namespace helpers and the serialization
— and registers the part that needs an engine this system does not have: XPATH (an XPath engine), XSLT (an
XSLT processor), `-validate` (DTD validation, which XML-a already recorded as absent), the
`NSXMLDocumentTidy*` options (libxml2's HTML parser) and `-canonicalXMLStringPreservingComments:` (C14N, a
specification of its own). NSXMLDocument itself is slice XML-c: it is the class that PARSES into this tree.

TWO STORAGE READINGS ARE OURS AND ARE ASSERTED: ATTRIBUTES LIVE IN THE ELEMENT'S OWN STORE (so `-children`
is the content, and `-attributes` is in insertion order), and A NAMESPACE IS A CHILD of the element that
declares it (so it appears in that element's start tag when written out).

The probe is `/System/Shared/tests/foundation_xmltree`, ONE unit, importing only
`<Foundation/Foundation.h>`. NO FIXTURE: a tree is built by hand.

  * `tree-the-factories-make-the-kinds` — each factory answers the kind it names; the DTD factory is
                                 REFUSED (nil) because the DTD kinds are XML-c's;
  * `tree-an-element-serializes-with-its-attributes` — escaping by the rules of each CONTEXT (a quote is
                                 fatal in an attribute and ordinary in text);
  * `tree-an-empty-element-follows-the-option` — `<a></a>` by default, `<a/>` when asked;
  * `tree-the-quote-option-decides-the-attribute-quotes` — double by default, single when asked;
  * `tree-pretty-print-indents-element-children`;
  * `tree-the-children-are-the-tree-and-a-node-has-one-parent` — adding ADOPTS (taking the node from
                                 wherever it was) and `-detach` gives it back;
  * `tree-navigation-is-document-order` — nextNode/previousNode are TREE order, with siblings, index and
                                 level agreeing;
  * `tree-attributes-live-in-the-elements-own-store`;
  * `tree-namespaces-are-children-and-resolve-upward`;
  * `tree-elements-for-name-and-for-local-name`;
  * `tree-the-name-helpers-split-and-bind-prefixes` — including the two prefixes XML itself binds.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_xmltree"
CHECKS = ("tree-the-factories-make-the-kinds", "tree-an-element-serializes-with-its-attributes",
          "tree-an-empty-element-follows-the-option",
          "tree-the-quote-option-decides-the-attribute-quotes",
          "tree-pretty-print-indents-element-children",
          "tree-the-children-are-the-tree-and-a-node-has-one-parent",
          "tree-navigation-is-document-order", "tree-attributes-live-in-the-elements-own-store",
          "tree-namespaces-are-children-and-resolve-upward",
          "tree-elements-for-name-and-for-local-name",
          "tree-the-name-helpers-split-and-bind-prefixes")


class Case(BaseCase):
    title = "NSXMLNode/NSXMLElement: the tree, built by hand, walked and written back out"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_xmltree")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-XMLTREE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-XMLTREE "):
                self.note(line)

        done = "FOUNDATION-XMLTREE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-XMLTREE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-XMLTREE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-XMLTREE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-XMLTREE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
