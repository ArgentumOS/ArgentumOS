"""Weaver IB6 - outlets and the app-resource path (docs/design/weaver-plan.md 8).

Wren is a sample bundle whose interface ships as a document in its own
Resources/ (D10). It loads it with the same interfaceLoadFile/interfaceBuild
the editor uses and resolves its named controls by identifier (D1 outlets),
logged. The gate then proves the SHIPPED document is still editable: it copies
it into the user's Documents (the same bytes - the md5s must match) and edits
the copy with Weaver, so the shipped bundle resource stays pristine and the
gate stays repeatable across boots.
"""

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
WREN = "/Applications/Wren.app/bin/Wren"
SHIPPED = "/Applications/Wren.app/Resources/Interface.conf"
EDIT = "/Users/Admin/Documents/weaver_ib6.conf"


class Case(BaseCase):
    title = "Weaver IB6: a bundle interface resolves outlets and stays editable"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        ctx.require_guest_file("Wren")
        ctx.require_guest_file("Weaver")
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        # --- the app boots its interface from its bundle Resources ---
        mark = len(session.log_text())
        session.run("%s; echo WREN-EXIT=$?" % WREN)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WREN" in line:
                self.note(line)

        self.check("loads-bundle-resource",
                   "WREN: load %s (3 nodes)" % SHIPPED in out,
                   "the bundle's Resources document was loaded")
        self.check("outlet-greeting",
                   "WREN: outlet greeting resolved (20,16 240x20)" in out,
                   "the greeting outlet resolved by identifier")
        self.check("outlet-button",
                   "WREN: outlet okButton resolved (20,60 90x24)" in out,
                   "the button outlet resolved by identifier")
        self.check("wren-exit-zero",
                   "WREN-OK (2 outlet(s) resolved)" in out
                   and "WREN-EXIT=0" in out,
                   "the sample app exited 0 with both outlets")

        # --- the SAME document, copied byte-for-byte into the user's
        # Documents, is still editable by Weaver ---
        mark = len(session.log_text())
        session.run("cp %s %s && md5sum %s %s" % (SHIPPED, EDIT, SHIPPED,
                                                  EDIT))
        out = session.output_since(mark)
        hashes = [line.split()[0] for line in out.strip().splitlines()
                  if len(line.split()) == 2 and line.split()[0].isalnum()]
        self.check("copy-identical",
                   len(hashes) == 2 and hashes[0] == hashes[1],
                   "the editable copy is byte-identical to the shipped "
                   "document")

        mark = len(session.log_text())
        session.run("%s --open %s --move okButton 5 5 --save --reload "
                    "--rect okButton; echo WEAVER-EXIT=$?"
                    % (WEAVER, EDIT))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("weaver-opens-copy",
                   "WEAVER: open %s (3 nodes)" % EDIT in out,
                   "Weaver opened the copy of the shipped document")
        self.check("weaver-moves-it",
                   "WEAVER: move okButton 20,60 -> 25,65" in out,
                   "the edit (a move) applied to the document")
        self.check("weaver-saves-it",
                   "WEAVER: save %s (" % EDIT in out,
                   "the document was saved")
        self.check("weaver-reloads-edited",
                   "WEAVER: rect okButton = 25,65 90x24" in out,
                   "after reload the edited rect is the committed one")
        self.check("weaver-exit-zero", "WEAVER-EXIT=0" in out,
                   "the edit run exited 0")
