"""Weaver functional inspector (docs/design/weaver-plan.md D13/6, post-IB7).

The visible inspector's TextFields are now LIVE: an end-edit (Return or focus
loss) applies the field text through the property table to the canvas AND the
document. The gate drives the SAME handler via the scripted `--field` command
(plan 8a fallback — it asserts on the editor's state, not on typing into a
window), then checks the saved document and the reloaded enumeration.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_inspector.conf"


class Case(BaseCase):
    title = "Weaver inspector: a field edit applies to canvas, doc, and save"
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

        # select the Label (its centre: 20+120, 16+10), edit its title
        # through the field path, save, reload, and read the new value back.
        cmd = ('%s --open weaver_inspector.conf --click 140 26 '
               '--field text "Hello, Field" --save --reload --click 140 26; '
               'echo WEAVER-EXIT=$?' % WEAVER)
        mark = len(session.log_text())
        session.run(cmd)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("select-logs-id", "WEAVER: select greeting" in out,
                   "clicking the Label selected it by identifier")
        self.check("field-applies-through-table",
                   'WEAVER: inspector set greeting.text = "Hello, Field" '
                   '(live reads back "Hello, Field")' in out,
                   "the field edit applied through the table and read back "
                   "from the live canvas")
        self.check("save-logged", "WEAVER: save %s (" % DOC in out,
                   "the edited document was saved")
        self.check("reload-reads-new-value",
                   'text(string)="Hello, Field"' in out,
                   "after reload the enumeration reads the edited title")
        self.check("exit-zero", "WEAVER-EXIT=0" in out,
                   "the run exited 0")

        # the saved file on disk carries the edited title
        mark = len(session.log_text())
        session.run("cat %s; echo CAT-EXIT=$?" % DOC)
        cat = session.output_since(mark)
        self.check("saved-file-has-edited-title",
                   'text = "Hello, Field"' in cat,
                   "the emitted document carries the edited title")
