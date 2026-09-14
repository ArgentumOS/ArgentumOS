"""Weaver IB3 - manipulation: move, resize, handles, undo
(docs/design/weaver-plan.md 8).

Driven by scripted commands like IB2 (plan 8a fallback): the SAME gesture
engine a real pointer reaches through EditorSurface is driven from argv, and
the gate asserts on the editor's `WEAVER: ...` log. The acceptance is three
observables: one log line per committed gesture (old rect -> new rect), undo
restores the exact previous rect (logged), and an edit-then-revert leaves the
file on disk unchanged until save.

No pixel check: the log carries the facts (plan 8a prefers a log where a log
can), and the checksum is the on-disk truth.
"""

import re

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_ib3.conf"

MD5 = re.compile(r"([0-9a-f]{32})")


class Case(BaseCase):
    title = "Weaver IB3: gestures commit once and undo restores the rect"
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

        def md5():
            mark = len(session.log_text())
            session.run("md5sum %s" % DOC)
            out = session.output_since(mark)
            m = MD5.search(out)
            return m.group(1) if m else ""

        def weaver(*cmds):
            mark = len(session.log_text())
            session.run("%s %s; echo WEAVER-EXIT=$?"
                        % (WEAVER, " ".join(cmds)))
            out = session.output_since(mark)
            for line in out.strip().splitlines():
                if "WEAVER" in line:
                    self.note(line)
            return out

        before = md5()
        self.check("checksum-taken", bool(before),
                   "the pristine document has a checksum")

        # --- a move gesture commits, then undo restores the exact rect ---
        out = weaver("--open weaver_ib3.conf",
                     "--click 65 72",
                     "--drag 65 72 105 112",
                     "--undo",
                     "--rect okButton")
        self.check("select-before-drag", "WEAVER: select okButton" in out,
                   "the press target is selected by identifier")
        self.check("move-commits-old-to-new",
                   "WEAVER: commit move okButton 20,60 90x24 -> "
                   "60,100 90x24" in out,
                   "one committed move logged the old and new rect")
        self.check("undo-restores-move",
                   "WEAVER: undo move okButton -> 20,60 90x24" in out,
                   "undo logged the restored rect")
        self.check("rect-after-undo",
                   "WEAVER: rect okButton = 20,60 90x24" in out,
                   "the document is back to the pre-gesture rect")
        self.check("move-run-exit-zero", "WEAVER-EXIT=0" in out,
                   "the move+undo run exited 0")

        # --- a resize gesture commits, then undo restores the exact rect ---
        out = weaver("--open weaver_ib3.conf",
                     "--click 65 72",
                     "--resize 110 84 140 114",
                     "--undo",
                     "--rect okButton")
        self.check("resize-commits-old-to-new",
                   "WEAVER: commit resize okButton 20,60 90x24 -> "
                   "20,60 120x54" in out,
                   "one committed resize (the bottom-right handle) logged "
                   "old and new")
        self.check("undo-restores-resize",
                   "WEAVER: undo resize okButton -> 20,60 90x24" in out,
                   "undo restored the pre-resize rect")
        self.check("rect-after-resize-undo",
                   "WEAVER: rect okButton = 20,60 90x24" in out,
                   "the document is back to the pre-gesture rect")
        self.check("resize-run-exit-zero", "WEAVER-EXIT=0" in out,
                   "the resize+undo run exited 0")

        # --- edit-then-revert: the file on disk is unchanged until save ---
        after_revert = md5()
        self.check("file-unchanged-until-save",
                   bool(after_revert) and after_revert == before,
                   "two gestures, both undone, left the file byte-identical")

        # --- a committed gesture + save DOES change the file ---
        out = weaver("--open weaver_ib3.conf",
                     "--click 65 72",
                     "--drag 65 72 105 112",
                     "--save",
                     "--reload",
                     "--rect okButton")
        self.check("save-after-commit",
                   "WEAVER: save %s (" % DOC in out,
                   "the committed move was saved")
        self.check("reload-shows-moved-rect",
                   "WEAVER: rect okButton = 60,100 90x24" in out,
                   "after reload the moved rect is the committed one")
        after_save = md5()
        self.check("file-changed-after-save",
                   bool(after_save) and after_save != before,
                   "the saved file differs from the pristine one")
