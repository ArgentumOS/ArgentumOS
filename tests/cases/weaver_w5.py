"""Weaver W5: Test Interface — the document runs live.

`--test` builds the document into a REAL window with no editor chrome,
binds each action connection to a logging stub (Weaver plays the owner),
and runs. The gate clicks the button in the test window (proving the
wiring crosses a real click), then closes the window and expects the
test to end and the process to return.
"""

import re
import time

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_w5.conf"
MANAGE = (r"KESTREL: manage 0x[0-9a-f]+ 'Weaver Test' frame=0x[0-9a-f]+ "
          r"at (\d+),(\d+) (\d+)x(\d+)")
WINDOW = r"WEAVER: test window 0x[0-9a-f]+ (\d+)x(\d+) ppt=([0-9.]+)"
FRAME_PX = 4
BAND_H = 20


class Case(BaseCase):
    title = "Weaver W5: the document runs live and dispatches a real click"
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

        mark = len(session.log_text())
        session.run("cp %s %s && rm -f %s.weaverundo && echo DOC-COPIED"
                    % (FIXTURE, DOC, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_w5.conf "
                    "--connect okButton action doThing owner --test "
                    "& echo WPID=$!" % WEAVER)
        bound = session.wait_for(r"WEAVER: test bind doThing owner", 45)
        self.check("bind-logged", bound,
                   "the test bound the action connection to a stub")
        shown = session.wait_for(MANAGE, 45)
        self.check("test-window", shown,
                   "the test window came up" if shown
                   else "no test window; guest tail: " + session.tail())
        w = re.search(WINDOW, session.log_text())

        self.check("no-editor-chrome",
                   bool(w) and int(w.group(1)) < 600 and int(w.group(2)) < 450,
                   "the test window is the INTERFACE's size (not the "
                   "editor's 1147x693)")
        m = re.search(MANAGE, session.log_text())
        if not (m and w):
            session.run("killall Weaver")
            return
        fx, fy = int(m.group(1)), int(m.group(2))
        ppt = float(w.group(3))

        # the button's centre: doc 20,60 90x24 -> 65,72
        bx = fx + FRAME_PX + int(round(65 * ppt))
        by = fy + BAND_H + int(round(72 * ppt))
        monitor = session.monitor()
        monitor.park()
        monitor.click_at(bx, by)
        self.check("click-dispatches",
                   session.wait_for(
                       r"WEAVER: test action doThing owner", 20),
                   "clicking the button in the test window ran the "
                   "connection's stub")

        # the close box ends the test and the editor process returns
        monitor.click_at(fx + 12, fy + 10)
        self.check("test-ends-on-close",
                   session.wait_for(r"WEAVER: test done", 30),
                   "closing the test window ended the test")
        session.run("killall Weaver")
