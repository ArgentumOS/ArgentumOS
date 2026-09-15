"""Weaver's menubar now expresses the W-series: Classes, Test, Group.

The bar grows from [Weaver][File][Edit] to include [Classes] and [Test],
each item running the SAME Editor method its scripted twin calls. The
gate clicks real titles and rows: Classes > List Classes, Test > Run
Test (which builds the live test window), a real click on the button in
that window (dispatch), then Test > Quit Test.
"""

import re
import time

from harness import BaseCase


def wait_new(session, mark, pattern, secs=12):
    """Wait for `pattern` in the log AFTER `mark` (session.wait_for greps
    the WHOLE log, so it can match a popup from an earlier click)."""
    rx = re.compile(pattern)
    deadline = time.time() + secs
    while time.time() < deadline:
        if rx.search(session.log_text()[mark:]):
            return True
        time.sleep(0.25)
    return False

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_menubar2.conf"
ZONE = r"KESTREL: menubar zone x=(\d+) w=(\d+)"
MANAGE = (r"KESTREL: manage 0x[0-9a-f]+ 'Weaver Test' frame=0x[0-9a-f]+ "
          r"at (\d+),(\d+) (\d+)x(\d+)")
TESTWIN = r"WEAVER: test window 0x[0-9a-f]+ (\d+)x(\d+)"
FRAME_PX = 4
BAND_H = 20
ROW0 = 45
ROW1 = 71


class Case(BaseCase):
    title = "Weaver menubar: Classes and Test menus run the W-series"
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
        session.run("cp %s %s && rm -f %s.weaverundo && echo DOC-COPIED"
                    % (FIXTURE, DOC, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_menubar2.conf "
                    "--connect okButton action doThing owner --show "
                    "& echo WPID=$!" % WEAVER)
        managed = session.wait_for(r"WEAVER: window 0x[0-9a-f]+", 45)
        self.check("window-shown", managed,
                   "the editor window came up" if managed
                   else "no window line; guest tail: " + session.tail())
        zone = None
        if managed:
            session.wait_for(ZONE, 20)
            m = re.search(ZONE, session.log_text())
            zone = int(m.group(1)) if m else 0

        def bar_ink(shot, x0):
            right = 0
            for x in range(x0, 900):
                if any(shot.luma(x, y) < 140 for y in range(6, 25)):
                    right = x
            return right

        painted = None
        deadline = time.time() + 25
        while zone and time.time() < deadline:
            painted = session.shot("mb2-bar")
            if bar_ink(painted, zone + 4) > 0:
                break
            time.sleep(1.0)

        runs = []
        if painted is not None:
            cur = []
            for x in range(zone, 900):
                if any(painted.luma(x, y) < 140 for y in range(6, 25)):
                    cur.append(x)
                elif cur:
                    if cur[-1] - cur[0] > 4:
                        runs.append([cur[0], cur[-1]])
                    cur = []
            if cur and cur[-1] - cur[0] > 4:
                runs.append([cur[0], cur[-1]])
        merged = []
        for r in runs:
            if merged and r[0] - merged[-1][1] <= 14:
                merged[-1][1] = r[1]
            else:
                merged.append(r)
        self.check("bar-has-five-menus", len(merged) >= 5,
                   "the bar carries the application, File, Edit, Classes "
                   "and Test menus: %s" % (merged,))
        if len(merged) < 5:
            session.run("killall Weaver")
            return

        monitor = session.monitor()
        cx = (merged[3][0] + merged[3][1]) // 2		# Classes
        tx = (merged[4][0] + merged[4][1]) // 2		# Test

        # Classes > List Classes (the first row: no input needed)
        monitor.park()
        mark = len(session.log_text())
        monitor.goto(cx, 15)
        monitor.click()
        wait_new(session, mark, r"ARGENTUM-POPUP: open")
        monitor.goto(cx, ROW0)
        monitor.click()
        self.check("classes-menu-runs",
                   session.wait_for(r"WEAVER: classes 0", 20),
                   "Classes > List Classes ran the class listing")

        # Test > Run Test (first row): the live test window
        mark = len(session.log_text())
        monitor.goto(tx, 15)
        monitor.click()
        wait_new(session, mark, r"ARGENTUM-POPUP: open \"Test\"")
        monitor.goto(tx, ROW0)
        monitor.click()
        self.check("run-test-binds",
                   session.wait_for(r"WEAVER: test bind doThing owner", 30),
                   "Test > Run Test bound the connection")
        self.check("test-window",
                   session.wait_for(MANAGE, 30),
                   "the live test window is managed")

        # a REAL click on the button in the test window dispatches
        m = re.search(MANAGE, session.log_text())
        w = re.search(TESTWIN, session.log_text())
        if m and w:
            fx, fy = int(m.group(1)), int(m.group(2))
            ppt = float(re.search(
                r"WEAVER: window 0x[0-9a-f]+ \d+x\d+ ppt=([0-9.]+)",
                session.log_text()).group(1))
            monitor.click_at(fx + FRAME_PX + int(round(65 * ppt)),
                             fy + BAND_H + int(round(72 * ppt)))
            self.check("menu-test-dispatches",
                       session.wait_for(
                           r"WEAVER: test action doThing owner", 20),
                       "a click in the menu-made test window dispatched")
        else:
            self.check("menu-test-dispatches", False,
                       "no test window geometry to click")

        # Test > Quit Test (the SECOND row): derive the row from the
        # popup's own logged height (the first row sits just below the
        # bar's floor at y=30)
        # the bar belongs to the APPLICATION, so it is still there while
        # the TEST window (which publishes none) is the active one — that
        # is the point of the app-owned bar
        monitor.park()
        mark = len(session.log_text())
        monitor.goto(tx, 15)
        monitor.click()
        wait_new(session, mark, r"ARGENTUM-POPUP: open \"Test\"")
        popup = re.search(r"ARGENTUM-SHM: enabled (\d+)x(\d+)",
                          session.log_text()[mark:])
        h = int(popup.group(2)) if popup else 61
        row1 = 30 + int(1.5 * h / 2.0)		# row 1 of a 2-item menu
        monitor.goto(tx, row1)
        monitor.click()
        self.check("quit-test-runs",
                   session.wait_for(r"WEAVER: test quit", 20),
                   "Test > Quit Test unmapped the test window")
        session.run("killall Weaver")
