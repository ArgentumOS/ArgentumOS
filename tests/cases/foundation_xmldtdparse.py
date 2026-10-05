# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The six DTD events and the document's DTD — XML slice XML-e's acceptance (foundation-plan.md §60).

XML-a's parser skipped a document's internal subset as a balanced bracket region and reported nothing about
it, and the six DTD events of NSXMLParserDelegate were declared and never fired. This slice closes that half:
the subset is READ and its declarations become the six events — from the SAME reader the DTD objects use, so
a declaration is understood in exactly one place in this library — and NSXMLDocument keeps what they said
(`-dtd`) and writes it back out as a DOCTYPE that can be read again.

TWO BOUNDARIES ARE STATED WHERE THEY HAPPEN: an EXTERNAL subset is NAMED and never fetched (the DOCTYPE's
identifiers are not reported by this parser, so a document naming a file that does not exist still parses and
builds no DTD), and an UNCLOSED subset is a parse error rather than a silent end of document.

The probe is `/System/Shared/tests/foundation_xmldtdparse`, ONE unit, importing only
`<Foundation/Foundation.h>`. NO FIXTURE: a subset is a string, and the file that is never opened is named
rather than created — which is itself one of the checks.

  * `dtdparse-the-six-events-fire` — all six, from one subset declaring one of each;
  * `dtdparse-the-element-declaration-carries-its-model` — the model arrives as TEXT and the node built from
                                 it is classified by the same rule that classifies a model;
  * `dtdparse-the-attribute-declaration-carries-element-type-and-default`;
  * `dtdparse-the-entity-events-are-the-right-three` — internal, external and unparsed are three events;
  * `dtdparse-a-document-without-a-subset-fires-none`;
  * `dtdparse-the-document-keeps-its-dtd-and-writes-it-back` — including the round trip, which is what
                                 matters: the DOCTYPE it writes can be read again;
  * `dtdparse-an-external-subset-is-named-and-never-fetched`;
  * `dtdparse-an-unclosed-subset-is-a-parse-error`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_xmldtdparse"
CHECKS = ("dtdparse-the-six-events-fire", "dtdparse-the-element-declaration-carries-its-model",
          "dtdparse-the-attribute-declaration-carries-element-type-and-default",
          "dtdparse-the-entity-events-are-the-right-three",
          "dtdparse-a-document-without-a-subset-fires-none",
          "dtdparse-the-document-keeps-its-dtd-and-writes-it-back",
          "dtdparse-an-external-subset-is-named-and-never-fetched",
          "dtdparse-an-unclosed-subset-is-a-parse-error")


class Case(BaseCase):
    title = "XML-e: the six DTD events, and the document that keeps and re-writes its DTD"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_xmldtdparse")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-XMLDTDPARSE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-XMLDTDPARSE "):
                self.note(line)

        done = "FOUNDATION-XMLDTDPARSE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-XMLDTDPARSE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-XMLDTDPARSE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-XMLDTDPARSE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-XMLDTDPARSE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
