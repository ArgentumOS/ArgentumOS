"""Weaver's app-owned menubar (S4.2d): the app draws its own bar window.

The editor publishes a Menu model through Application::setMenuBar(); the
toolkit opens the bar as its OWN window and Kestrel places it over the app
zone (logging the zone). The gate clicks real titles and rows and asserts
the actions WEAVER logs: File > Save runs ed.save(), Edit > Undo runs
ed.undo() (a fresh document has nothing to undo).
"""

import re
import time

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_menubar.conf"
ZONE = r"KESTREL: menubar zone x=(\d+) w=(\d+)"


class Case(BaseCase):
    title = "Weaver menubar: the app-owned bar runs File and Edit"
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
                   "the fixture is in %s (and any stale journal is gone)"
                   % DOC)

        # launch the editor in display mode; its bar window is app-owned
        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_menubar.conf --show "
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
        self.check("menubar-zone-logged", bool(zone),
                   "the WM placed the app's own bar and logged the zone")

        # the app's bar paints a moment after the WM maps it: wait for ink
        def bar_ink_right(shot, x0):
            right = 0
            for x in range(x0, 900):
                if any(shot.luma(x, y) < 140 for y in range(6, 25)):
                    right = x
            return right

        painted = None
        deadline = time.time() + 25
        while zone and time.time() < deadline:
            painted = session.shot("weaver-bar-wait")
            if bar_ink_right(painted, zone + 4) > 0:
                break
            time.sleep(1.0)
        ink = bar_ink_right(painted, zone + 4) if painted else 0
        self.check("app-menus-in-bar", ink > zone + 20,
                   "the app's bar window carries its menus (ink x=%d, "
                   "right of the WM zone at x=%d)" % (ink, zone))

        # find the title runs: [Weaver] [File] [Edit]
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
        self.check("bar-titles-found", len(merged) >= 3,
                   "the bar carries Weaver, File and Edit: %s" % (merged,))

        if len(merged) >= 3:
            monitor = session.monitor()
            monitor.park()
            rowy = 45

            # File > Save. The File menu is [New, Save, Reload, --,
            # Test Interface], so Save is the SECOND row: derive its
            # position from the popup's own logged height.
            fx = (merged[1][0] + merged[1][1]) // 2
            saves = session.count(r"WEAVER: save ")
            monitor.goto(fx, 15)
            monitor.click()
            time.sleep(1.0)
            popup = re.findall(r"ARGENTUM-SHM: enabled \d+x(\d+)",
                               session.log_text())
            h = int(popup[-1]) if popup else 130
            items = len(re.findall(r"ARGENTUM-POPUP: open", session.log_text()))
            rowy2 = 30 + int(1.5 * h / max(items and 5 or 5, 1))
            monitor.goto(fx, rowy2)
            monitor.click()
            self.check("file-save-ran",
                       session.count(r"WEAVER: save ") > saves,
                       "clicking File > Save ran the editor's save "
                       "(%d -> %d save line(s))"
                       % (saves, session.count(r"WEAVER: save ")))

            # Edit > Undo (a fresh doc has nothing to undo, but the
            # editor's undo path must run)
            ex = (merged[2][0] + merged[2][1]) // 2
            undos = session.count(r"WEAVER: undo")
            monitor.goto(ex, 15)
            monitor.click()
            monitor.goto(ex, rowy)
            monitor.click()
            self.check("edit-undo-ran",
                       session.count(r"WEAVER: undo") > undos,
                       "clicking Edit > Undo ran the editor's undo "
                       "(%d -> %d undo line(s))"
                       % (undos, session.count(r"WEAVER: undo")))

        session.run("killall Weaver")
