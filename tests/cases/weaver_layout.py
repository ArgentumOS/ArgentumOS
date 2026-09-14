"""Weaver visible layout - the editor window chrome (docs/design/weaver-plan.md
D13/6).

`--show` now lays out a real editor window: palette (TableView) and outline on
the left, the live document canvas in the centre, the inspector
(Box + Label + TextField) on the right. The gate derives all geometry from the
editor's own `WEAVER: layout ...` log and the WM's manage line, then checks
each region is actually drawn (non-background pixels).
"""

import re
import time

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_layout.conf"

FRAME_PX = 4
BAND_H = 20

LAYOUT = re.compile(
    r"WEAVER: layout window=([\d.]+)x([\d.]+) "
    r"canvas=([\d.]+),([\d.]+) ([\d.]+)x([\d.]+) "
    r"palette=([\d.]+),([\d.]+) ([\d.]+)x([\d.]+) "
    r"outline=([\d.]+),([\d.]+) ([\d.]+)x([\d.]+) "
    r"inspector=([\d.]+),([\d.]+) ([\d.]+)x([\d.]+)")
WINDOW = re.compile(r"WEAVER: window 0x[0-9a-f]+ \d+x\d+ ppt=([\d.]+) doc=\S+")
MANAGE = (r"KESTREL: manage 0x[0-9a-f]+ 'Weaver' frame=0x[0-9a-f]+ "
          r"at (\d+),(\d+) (\d+)x(\d+)")


class Case(BaseCase):
    title = "Weaver layout: palette, outline, canvas, inspector drawn"
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

        # select the Label (its centre: 20+120, 16+10) so the inspector
        # panel is populated, then show the window with the chrome.
        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_layout.conf "
                    "--click 140 26 --show &" % WEAVER)

        deadline = time.time() + 45
        log = session.log_text()
        while time.time() < deadline:
            if ("WEAVER: layout " in log
                    and re.search(MANAGE, log)):
                break
            time.sleep(0.5)
            log = session.log_text()

        layout = LAYOUT.search(log)
        manage = re.search(MANAGE, log)
        window = WINDOW.search(log)
        self.check("layout-logged", bool(layout),
                   "the editor logged its layout")
        self.check("window-managed", bool(manage),
                   "the WM managed the editor window")
        if not layout or not manage or not window:
            return

        ppt = float(window.group(1))
        fx, fy = int(manage.group(1)), int(manage.group(2))
        cx = fx + FRAME_PX
        cy = fy + BAND_H

        g = [float(x) for x in layout.groups()]
        (win_w, win_h, canvas_x, canvas_y, canvas_w, canvas_h,
         pal_x, pal_y, pal_w, pal_h,
         out_x, out_y, out_w, out_h,
         insp_x, insp_y, insp_w, insp_h) = g
        self.note("layout %s" % g)

        shot = session.shot("weaver-layout")

        def px(pt_x, pt_y):
            return (cx + int(round(pt_x * ppt)), cy + int(round(pt_y * ppt)))

        # window background sample: empty centre content below the canvas
        bg_x, bg_y = px(canvas_x + 10, canvas_y + canvas_h + 40)
        bg = shot.px(bg_x, bg_y)

        def ink(pt_x, pt_y, half=12):
            x0, y0 = px(pt_x, pt_y)
            n = 0
            for yy in range(y0 - half, y0 + half + 1):
                for xx in range(x0 - half, x0 + half + 1):
                    p = shot.px(xx, yy)
                    if any(abs(p[i] - bg[i]) > 30 for i in range(3)):
                        n += 1
            return n

        self.check("palette-drawn",
                   ink(pal_x + 40, pal_y + 120) > 0,
                   "the palette region draws its TableView rows")
        self.check("outline-drawn",
                   ink(out_x + 40, out_y + 42) > 0,
                   "the outline region draws its rows")
        self.check("inspector-drawn",
                   ink(insp_x + 40, insp_y + 120) > 0,
                   "the inspector region draws its Box + rows")
        self.check("canvas-drawn",
                   ink(canvas_x + 75, canvas_y + 92) > 0,
                   "the canvas region draws the live Button")

        session.run("killall Weaver")
