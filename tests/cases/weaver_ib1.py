"""Weaver IB1 - instantiate a document (docs/design/weaver-plan.md 8).

The probe is display-free like IB0's: it builds a live tree, finds a control by
identifier, and lays it out from the frames and parent-relative masks the
document records. The evidence is the log and the resulting FRAMES, which are
exact numbers - see plan 8a on preferring a log where a log can carry the fact.

Layout in a document is frames plus masks (D15): sibling bindings were removed,
so nothing in a document refers to another node, there is no second pass, and
nothing depends on the order the document lists things in.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/interface_build"


class Case(BaseCase):
    title = "Weaver IB1: instantiate, identity, and the layout contract"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())

        mark = len(session.log_text())
        session.run("test -x %s && %s; echo IB1-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "IB1" in line:
                self.note(line)

        ran = "IB1:" in out
        self.check("probe-ran", ran,
                   "the probe ran" if ran
                   else "no probe output - is %s in the image? "
                        "(make rootagfs)" % PROBE)
        self.check("exit-code-zero", "IB1-EXIT=0" in out,
                   "the probe exited 0" if "IB1-EXIT=0" in out
                   else "the probe exited non-zero: %s" % out.strip()[-300:])
        self.check("summary-ok", "IB1-OK" in out, "IB1-OK reported")

        # the three nodes, in pre-order, with the identifiers the document gave
        order = re.findall(r'IB1: built (\w+) id="([^"]*)"', out)
        self.check("built-in-pre-order",
                   order == [("View", "panel"), ("Label", "greeting"),
                             ("Label", "status")],
                   "built, in pre-order: %s" % order)
        self.check("identity-resolves",
                   "viewWithIdentifier(\"status\") resolved to the built view"
                   in out,
                   "the document's id found the view it named")

        # the layout contract, as two exact numbers
        self.check("mask-pinned-view-held",
                   "a flexible MAX margin left the pinned view alone" in out,
                   "flexibleMaxX left the pinned child at 20 + 240")
        self.check("mask-flexible-view-grew",
                   "a flexible WIDTH took the whole delta (240 -> 540)" in out,
                   "flexibleWidth took the whole 300pt delta")

        # IB1b: the properties, through the class's table
        self.check("properties-applied-and-skipped",
                   "IB1: build report: built=3 applied=3 skipped=2" in out,
                   "3 properties applied, 2 reported and skipped (a wrong "
                   "kind and an unknown name)")
        self.check("properties-read-back",
                   'IB1: read back Label.text = "Hello" and "0 items" OK'
                   in out,
                   "the document's values read back through the table's "
                   "getters")
        self.check("inherited-property",
                   "IB1: read back INHERITED Label.hidden = true OK" in out,
                   "a property a class does not declare, reached through the "
                   "base")

        # IB1b: coverage - a registered class must be describable
        self.check("coverage-every-class-has-a-table",
                   "IB1: cover: every registered class has a property table"
                   in out,
                   "every registered class carries its own property table")

        # tolerant reads, and the fatal case
        self.check("unknown-root-is-fatal",
                   "an unknown ROOT class is fatal and named" in out,
                   "an unknown root class reported, not silently empty")
        self.check("unknown-descendant-skipped",
                   "an unknown DESCENDANT is skipped with a warning, the rest "
                   "builds" in out,
                   "an unknown descendant skipped while the rest built")
