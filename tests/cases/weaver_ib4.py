"""Weaver IB4 - the inspector (docs/design/weaver-plan.md 8).

The inspector is driven by the property table (D4): selecting a control
enumerates its properties through `interfacePropertyCount/At`, and setting one
goes through the table's setter on the LIVE canvas view while the document is
updated in the same step. All of it is state-only (the canvas is built
display-free, like the IB1/IB2 probes), so the gate asserts on the editor's
log: the enumeration, the live read-back after a set, the saved file, and the
reloaded document.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_ib4.conf"


class Case(BaseCase):
    title = "Weaver IB4: the inspector enumerates and sets through the table"
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

        # select the Label (its centre: 20+120, 16+10) -> the inspector
        # enumerates; set its title through the table; save; reload; select
        # again -> the enumeration reads the new title back.
        cmd = ('%s --open weaver_ib4.conf --click 140 26 '
               '--set text "Hello, FNX" --save --reload --click 140 26; '
               'echo WEAVER-EXIT=$?' % WEAVER)
        mark = len(session.log_text())
        session.run(cmd)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("select-logs-id", "WEAVER: select greeting" in out,
                   "clicking the Label selected it by identifier")
        self.check("inspect-enumerates",
                   "WEAVER: inspect Label greeting:" in out,
                   "the selection enumerated its properties")
        self.check("inspect-lists-own-and-inherited",
                   'text(string)="Hello"' in out
                   and "hidden(bool)=" in out,
                   "the enumeration lists the own property (text) and the "
                   "inherited one (hidden), with values")
        self.check("set-through-table",
                   'WEAVER: set greeting.text = "Hello, FNX" '
                   '(live reads back "Hello, FNX")' in out,
                   "the table's setter changed the live canvas view, and the "
                   "getter read it back")
        self.check("save-logged", "WEAVER: save %s (" % DOC in out,
                   "the changed document was saved")
        self.check("reload-enumerates-new-value",
                   'text(string)="Hello, FNX"' in out,
                   "after reload the enumeration reads the new title")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the run exited 0")

        # the saved file on disk carries the new title
        mark = len(session.log_text())
        session.run("cat %s; echo CAT-EXIT=$?" % DOC)
        cat = session.output_since(mark)
        self.check("saved-file-has-new-title",
                   'text = "Hello, FNX"' in cat,
                   "the emitted document carries the new title")
