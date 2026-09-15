"""The visible palette adds a control when a class row is clicked.

A TableView row click fires the delegate, which runs the SAME
Editor::addNode the scripted --add command uses; refreshDisplay() then
rebuilds the live canvas and outline. The gate also samples the canvas's
empty area to prove the host now paints the WINDOW-grey page tone behind
the document, not the session-blue surface.
"""

import re

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_palette.conf"
MANAGE = (r"KESTREL: manage 0x[0-9a-f]+ 'Weaver' frame=0x[0-9a-f]+ "
          r"at (\d+),(\d+)")
WINDOW = r"WEAVER: window 0x[0-9a-f]+ (\d+)x(\d+) ppt=([0-9.]+)"
PALETTE = r"WEAVER: palette rows=(\d+) rowH=([0-9.]+) header=([0-9.]+)"
BOXROW = (r"BOX-A: box='Palette' child\[0\] x=([0-9.]+) y=([0-9.]+) "
          r"w=([0-9.]+) h=([0-9.]+)")
FRAME_PX = 4
BAND_H = 20


class Case(BaseCase):
    title = "Weaver palette: clicking a class row adds it to the document"
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
                   "the fixture is in %s (and any stale journal is gone)"
                   % DOC)

        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_palette.conf --show "
                    "& echo WPID=$!" % WEAVER)
        session.wait_for(r"WEAVER: palette rows=", 45)
        managed = session.wait_for(MANAGE, 45)
        self.check("window-managed", managed,
                   "the editor window is managed" if managed
                   else "no manage line; guest tail: " + session.tail())
        boxed = session.wait_for(r"BOX-A: box='Palette' child\[0\]", 45)
        m = re.search(MANAGE, session.log_text())
        w = re.search(WINDOW, session.log_text())
        pr = re.search(PALETTE, session.log_text())
        br = re.search(BOXROW, session.log_text())
        self.check("palette-geometry-logged", bool(boxed) and bool(pr)
                   and bool(br),
                   "the palette logged its rows, row height and box child")

        if not (m and w and pr and br):
            session.run("killall Weaver")
            return
        fx, fy = int(m.group(1)), int(m.group(2))
        ppt = float(w.group(3))
        row_h = float(pr.group(2))
        header = float(pr.group(3))
        tx = float(br.group(1)) + float(br.group(3)) / 2.0
        # rows: [Containers] View Box ScrollView SplitView [Controls]
        #       Label Button ... -> 7
        ty = float(br.group(2)) + header + 7.5 * row_h	# row 7 = Button

        # the canvas host should paint the WINDOW-grey page tone: sample
        # the empty area inside the panel, right of the two controls
        cx = fx + FRAME_PX + int(round((200 + 350) * ppt))
        cy = fy + BAND_H + int(round((20 + 250) * ppt))

        monitor = session.monitor()
        monitor.park()
        shot = session.shot("palette-before")
        bg = shot.px(cx, cy)
        self.check("canvas-window-grey",
                   bg[0] > 200 and bg[1] > 200 and bg[2] > 200,
                   "the canvas host paints the window-grey page tone "
                   "(sampled rgb=%s at doc 350,250)" % (bg,))

        # click the Button row and assert the SAME log --add produces
        sx = fx + FRAME_PX + int(round(tx * ppt))
        sy = fy + BAND_H + int(round(ty * ppt))
        monitor.goto(sx, sy)
        monitor.click()
        self.check("palette-click-adds",
                   session.wait_for(
                       r"WEAVER: add Button to panel at 2 \(20,20 90x24\)",
                       30),
                   "clicking the Button row added it through Editor::addNode")

        session.run("killall Weaver")
