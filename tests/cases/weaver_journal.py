"""Weaver undo journal: committed-but-unsaved gestures survive a kill -9.

The journal is written on every gesture commit (<doc>.weaverundo, atomic
tmp+rename), recovered by --open (dirty, undoable), and cleared by save,
reload, or a clean exit. The gate crashes the editor with kill -9 after a
commit and checks that the next open replays the move and can undo it.
"""

import re

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_journal.conf"
JOURNAL = DOC + ".weaverundo"


class Case(BaseCase):
    title = "Weaver undo journal: a kill -9 recovers the unsaved gesture"
    tier = "fast"
    timeout = 480

    def run(self, ctx):
        ctx.require_guest_file("Weaver")
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("cp %s %s && rm -f %s && echo DOC-COPIED"
                    % (FIXTURE, DOC, JOURNAL))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s (and any stale journal is gone)"
                   % DOC)

        # --- crash run: commit a move, then --show keeps the process alive;
        #     kill -9 leaves the journal behind ---
        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_journal.conf "
                    "--click 65 72 --drag 65 72 105 112 --show "
                    "& echo WPID=$!" % WEAVER)
        committed = session.wait_for(
            r"WEAVER: commit move okButton 20,60 90x24 -> 60,100 90x24", 30)
        self.check("crash-run-committed", committed,
                   "the move committed before the crash" if committed
                   else "no commit line; guest tail: " + session.tail())
        out = session.output_since(mark)
        m = re.search(r"WPID=(\d+)", out)
        self.check("crash-run-pid", bool(m),
                   "the background pid was captured")
        if committed and m:
            session.run("kill -9 %d" % int(m.group(1)))
            session.run("sleep 1")

        mark = len(session.log_text())
        session.run("test -f %s && echo JOURNAL-PRESENT" % JOURNAL)
        out = session.output_since(mark)
        self.check("journal-present", "JOURNAL-PRESENT" in out,
                   "the journal survived the kill -9")

        # --- recovery run: open replays the move, undo restores it ---
        mark = len(session.log_text())
        session.run("%s --open weaver_journal.conf --rect okButton "
                    "--undo --rect okButton; echo WEAVER-EXIT=$?" % WEAVER)
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "WEAVER" in line:
                self.note(line)

        self.check("journal-recovered",
                   "WEAVER: journal recover 1 command(s) from %s "
                   "(dirty)" % JOURNAL in out,
                   "the open replayed the journaled move and went dirty")
        self.check("recovered-rect",
                   "WEAVER: rect okButton = 60,100 90x24" in out,
                   "the rect reads back the recovered move")
        self.check("recovered-undo",
                   "WEAVER: undo move okButton -> 20,60 90x24" in out,
                   "one undo restored the pre-crash frame")
        self.check("undo-rect",
                   "WEAVER: rect okButton = 20,60 90x24" in out,
                   "the rect is pre-gesture after the undo")
        self.check("recovery-exit-zero", "WEAVER-EXIT=0" in out,
                   "the recovery run exited 0")

        # --- the recovery run exited cleanly, so the journal is cleared ---
        mark = len(session.log_text())
        session.run("test ! -f %s && echo JOURNAL-CLEAR" % JOURNAL)
        out = session.output_since(mark)
        self.check("journal-cleared", "JOURNAL-CLEAR" in out,
                   "a clean exit cleared the journal")
