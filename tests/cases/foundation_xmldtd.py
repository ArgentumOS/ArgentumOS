# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSXMLDTD + NSXMLDTDNode — XML slice XML-d's acceptance: the DTD as a data model (foundation-plan.md §60).

WHAT A DTD IS HERE, SAID FIRST: XML-a's parser skips a document's internal subset and reports nothing about
it, and the six DTD events of NSXMLParserDelegate are declared and never fired. So this slice builds the
DTD's OBJECTS — the nineteen node kinds Apple publishes, the declaration nodes parsed out of their own XML
text, and the container with the four lookups — and the PARSER-SIDE half (firing the six events while
reading a document, and handing the document its DTD) is slice XML-e, stated there rather than half-built
here.

The nineteen kinds are generated from the ledger so they cannot drift: the ten attribute TYPES of an
`<!ATTLIST>`, the five element-declaration forms, and the four entity kinds. And one thing an external
subset does here is be REMEMBERED (a SYSTEM or PUBLIC identifier is kept, and nothing fetches it) — the same
boundary XML-a states for external entities, asserted as a fact about a declaration rather than a silence
about a fetch.

The probe is `/System/Shared/tests/foundation_xmldtd`, ONE unit, importing only `<Foundation/Foundation.h>`
plus the POSIX calls its file fixture makes.

  * `dtd-a-declaration-parses-into-a-node` — and the declaration it writes back is the one it read;
  * `dtd-an-attlist-declaration-parses` — the ATTRIBUTE TYPE decides the node's DTD kind (ID, not "an
                                 attribute declaration"), and the element is what the lookup is by;
  * `dtd-entity-declarations-internal-external-and-parameter` — the three shapes, including NDATA making it
                                 an UNPARSED entity with a notation;
  * `dtd-notation-declarations` — SYSTEM and PUBLIC;
  * `dtd-a-container-parses-an-internal-subset` — every `<!…>` becomes a child; the subset and both
                                 identifiers are remembered;
  * `dtd-the-four-lookups-find-their-declarations` — element, attribute (by element+name), entity and
                                 notation, each answering nil when it is not there;
  * `dtd-the-predefined-entities` — the five XML itself defines;
  * `dtd-a-string-that-is-not-a-declaration-is-refused` — nil rather than an empty node whose kind says
                                 nothing;
  * `dtd-a-dtd-parses-from-a-file-url` — the second door, over a real `.dtd` file;
  * `probe-tree-removed` — the tree is gone.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_xmldtd"
CHECKS = ("dtd-a-declaration-parses-into-a-node", "dtd-an-attlist-declaration-parses",
          "dtd-entity-declarations-internal-external-and-parameter", "dtd-notation-declarations",
          "dtd-a-container-parses-an-internal-subset", "dtd-the-four-lookups-find-their-declarations",
          "dtd-the-predefined-entities", "dtd-a-string-that-is-not-a-declaration-is-refused",
          "dtd-a-dtd-parses-from-a-file-url", "probe-tree-removed")


class Case(BaseCase):
    title = "NSXMLDTD/NSXMLDTDNode: the declarations, the container and the five predefined entities"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_xmldtd")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-XMLDTD-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-XMLDTD "):
                self.note(line)

        done = "FOUNDATION-XMLDTD DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-XMLDTD DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-XMLDTD %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-XMLDTD RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-XMLDTD-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
