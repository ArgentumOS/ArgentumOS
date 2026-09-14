"""Weaver IB5 - palette and hierarchy (docs/design/weaver-plan.md 8).

Scripted like IB2-IB4: the palette is the class registry (`--palette`), a drag
from it is `--add <class>` (v1 parent rule: a selected View/Box is the
container, otherwise the root; the new node is appended, so its sibling index
is the parent's child count). The outline (`--outline`) lists the tree
indented by depth, and selecting in the outline (`--select-outline name`) logs
both the outline pick and the canvas selection. The saved document is then
checked for the new node's class and its `childN` index.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_ib5.conf"


class Case(BaseCase):
    title = "Weaver IB5: the palette adds a control, the outline selects it"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        ctx.require_guest_file("Weaver")
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        # stage the fixture into the user's own documents (D11)
        mark = len(session.log_text())
        session.run("cp %s %s && echo DOC-COPIED" % (FIXTURE, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        # select the ROOT (empty corner 5,5) so --add targets it; then the
        # palette, a drag-add, the outline, an outline selection, and save.
        cmd = ('%s --open weaver_ib5.conf --click 5 5 --palette '
               '--add Button --outline --select-outline okButton --save; '
               'echo WEAVER-EXIT=$?' % WEAVER)
        mark = len(session.log_text())
        session.run(cmd)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("root-selected", "WEAVER: select panel" in out,
                   "clicking the empty corner selected the root container")
        self.check("palette-lists-catalog",
                   "WEAVER: palette" in out
                   and " View" in out and " Button" in out
                   and " Label" in out,
                   "the palette lists the registry (View/Button/Label ...)")
        self.check("add-logs-parent-and-index",
                   "WEAVER: add Button to panel at 2 (20,20 90x24)" in out,
                   "the drag-add appended a Button to the root at sibling "
                   "index 2")
        self.check("outline-lists-tree",
                   "WEAVER: outline View panel (0,0 400x300)" in out
                   and "WEAVER: outline   Label greeting (20,16 240x20)"
                   in out
                   and "WEAVER: outline   Button okButton (20,60 90x24)"
                   in out
                   and "WEAVER: outline   Button (20,20 90x24)" in out,
                   "the outline lists the tree, indented by depth")
        self.check("outline-select-logs-both",
                   "WEAVER: outline select okButton" in out
                   and "WEAVER: select okButton" in out,
                   "selecting in the outline selected in the canvas, both "
                   "logged")
        self.check("save-logged", "WEAVER: save %s (" % DOC in out,
                   "the extended document was saved")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the run exited 0")

        # the saved document: a third child (child2), a Button, same class
        # count as before + 1.
        mark = len(session.log_text())
        session.run("cat %s; echo CAT-EXIT=$?" % DOC)
        cat = session.output_since(mark)
        self.check("saved-doc-has-child2",
                   "child2 = {" in cat,
                   "the emitted document has a third child record")
        self.check("saved-doc-button-count",
                   cat.count('class = "Button"') == 2,
                   "the document now carries two Buttons (the fixture's and "
                   "the palette's)")
