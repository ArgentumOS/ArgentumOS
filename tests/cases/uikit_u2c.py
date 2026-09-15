"""U2c: the widget zoo board — the button family, clicked and looked at.

The board (/Applications/WidgetZoo) carries one control per bezel style and
behaviour. The real pointer picks a switch and both radios, and the
framebuffer is asked about the shapes: a gradient must differ top-to-bottom
inside its bezel, and a radio that is ON must show its mark.

The radio checks are the interesting ones: two radios in the same superview
are a GROUP, so picking B clears A — with no group object anywhere.
"""

import re
import time

from harness import BaseCase

ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit U2c: the widget zoo board shows and drives the button family"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse"])
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")
        if not ready:
            return

        mark = len(session.log_text())
        session.run("ARGENTUM_PAINT_MS=1 %s 60 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail())
            return
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("ZOO-"):
                self.note(line)
        self.check("board-up", True, "the zoo board opened its window")

        sh = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", out)
        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)", out)}
        # the controls THIS case drives must be located; the board may
        # gain more (it did: the text family), so do not count them
        need = ("SWITCH", "RADIO-A", "RADIO-B", "GRADIENT")
        missing = [n for n in need if n not in pts]
        self.check("controls-located", sh is not None and not missing,
                   "screen %s, controls at %s" % (sh.group(1), sorted(pts))
                   if sh and not missing else
                   "missing %s" % (missing or "the screen size"))
        if sh is None or missing:
            return
        screen_h = int(sh.group(1))

        def at(name):
            x, y = pts[name]
            return int(x), int(screen_h - y)	# the monitor's Y is mirrored

        mon = session.monitor()

        mon.park()

        # 1. the switch: a press sticks - and time it. The button's own
        # click carries a settle, which is the HARNESS waiting, not the
        # board answering, so this presses and releases with none and
        # measures from the press to the board's reply.
        mon.goto(*at("SWITCH"))
        t0 = time.time()
        mon.press(settle=0)
        mon.release(settle=0)
        session.wait_for(r"ZOO-CLICK Switch", 30)
        self.note("one click, press to ZOO-CLICK: %.0f ms"
                  % ((time.time() - t0) * 1000.0))
        out1 = session.output_since(mark)
        self.check("switch-sticks", "ZOO-CLICK Switch state=1" in out1,
                   "the switch reported state=1 after its click"
                   if "ZOO-CLICK Switch state=1" in out1
                   else "the switch did not stick: " + session.tail())

        # 2. the radio group: picking B clears A (no group object)
        mon.click_at(*at("RADIO-A"), settle=1.0)
        session.wait_for(r"ZOO-RADIOS a=1", 30)
        mon.click_at(*at("RADIO-B"), settle=1.0)
        session.wait_for(r"ZOO-RADIOS a=0 b=1", 30)
        out2 = session.output_since(mark)
        self.check("radio-a-selects", "ZOO-RADIOS a=1 b=0" in out2,
                   "picking A left A on and B off" if "ZOO-RADIOS a=1 b=0"
                   in out2 else "A did not select: " + session.tail())
        self.check("radio-group-is-exclusive", "ZOO-RADIOS a=0 b=1" in out2,
                   "picking B turned A OFF: the siblings clear each other"
                   if "ZOO-RADIOS a=0 b=1" in out2
                   else "the radios are not exclusive: " + session.tail())

        # 3. the shapes, in the framebuffer
        shot = session.shot("u2c")
        self.check("shot-taken", shot is not None, "a framebuffer dump")
        if shot is None:
            return
        wx, wy = 70, 50			# where the board's window opened
        ch = 22.0

        # the gradient button: its top must differ from its bottom
        gx, gy = pts["GRADIENT"]
        gx, gy = int(gx - 60), int(gy)	# inside the bezel, left of the title
        top = shot.px(gx, int(wy + ch + (gy - wy - ch) - 10))
        bot = shot.px(gx, int(wy + ch + (gy - wy - ch) + 10))
        self.check("gradient-bezel-gradates",
                   top is not None and bot is not None and top != bot,
                   "the Gradient bezel is %s at its top and %s at its bottom"
                   % (top, bot))

        # A BUTTON keeps its bezel, and a rounded one cuts its corners:
        # the corner pixel is the background, the middle is the bezel.
        gx2, gy2 = pts["GRADIENT"]
        corner = shot.px(int(gx2 - 118), int(gy2 - 12))
        middle = shot.px(int(gx2), int(gy2))
        self.check("bezel-is-round",
                   corner is not None and middle is not None
                   and corner != middle,
                   "corner %s vs middle %s: the bezel does not fill its "
                   "bounding box" % (corner, middle))

        # A RADIO HAS NO BEZEL (user-reported: radios were drawing one).
        # Its chrome is the small circle at its left and nothing else, so
        # the space to the right of the title is the window background.
        rx, ry = pts["RADIO-A"]
        right = shot.px(int(rx + 60), int(ry))
        luma_right = None if right is None else (right[0] * 299 + right[1] * 587
                                                + right[2] * 114) // 1000
        self.check("radio-has-no-bezel",
                   luma_right is not None and luma_right >= 236,
                   "the pixel right of a radio's title is %s (luma %s): the "
                   "background, not a bezel" % (right, luma_right))

        # ... and the circle IS drawn, at the left where a radio's mark
        # goes. It is a WHITE disc with a dark rim, so it is BRIGHTER than
        # the background - the check is that it is not the background, not
        # that it is dark (asking for dark here was my mistake).
        mark_px = shot.px(int(rx - 112), int(ry))
        bg = shot.px(int(rx + 60), int(ry))
        self.check("radio-mark-is-drawn",
                   mark_px is not None and bg is not None and mark_px != bg,
                   "a radio's circle is at its left: %s against the "
                   "background %s" % (mark_px, bg))

        # THE DAMAGE RECT MUST LAND WHERE THE CONTROL IS. The flush speaks
        # WINDOW coordinates; a view's rect is in CONTENT coordinates, one
        # chrome-height higher. Mixing them is a silent offset - the strip
        # below the region never repaints - and the pixel checks CANNOT see
        # it, because the board's first full push masked it. So: arithmetic.
        # ZOO-AT gives a row's centre in SCREEN coordinates, so the row in
        # window points is (centre - window origin - half the row).
        win = re.search(r"ZOO-WIN x=(-?[\d.]+) y=(-?[\d.]+) chrome=([\d.]+)",
                        out)
        sw = pts.get("SWITCH")
        pushes = re.findall(r"ARGENTUM-PUSH (\d+)x(\d+) at (\d+),(\d+)",
                            session.output_since(mark))
        row_push = next((p for p in pushes if int(p[0]) < 300), None)
        ok, msg = False, "no control-sized push seen"
        if win and sw and row_push:
            wx, wy = float(win.group(1)), float(win.group(2))
            px_, py_ = int(row_push[2]), int(row_push[3])
            wpx, hpx = int(row_push[0]), int(row_push[1])
            ex, ey = sw[0] - wx - 240 / 2.0, sw[1] - wy - 28 / 2.0
            ok = (abs(px_ - ex) <= 1 and abs(py_ - ey) <= 1
                  and wpx == 240 and hpx == 28)
            msg = ("the switch's damage rect is %dx%d at %d,%d; the row is at "
                   "%.0f,%.0f 240x28 in window points (window %g,%g)"
                   % (wpx, hpx, px_, py_, ex, ey, wx, wy))
        elif not win:
            msg = "the board did not report ZOO-WIN"
        self.check("damage-rect-is-in-window-space", ok, msg)

        # THE CIRCULAR ROW IS ONE CIRCLE AND NOTHING INSIDE IT. It was
        # drawing a radio's light disc inside the round bezel and pushing
        # its title over the rim, so the bezel's interior must now be
        # uniform: the same colour where that mark sat as at its centre.
        # (ZOO-AT logs a frame's ORIGIN, so the row's centre is y + 14.)
        cx, cy = pts["CIRCULAR"]
        at_old_mark = shot.px(int(cx - 112), int(cy))
        at_centre = shot.px(int(cx - 106), int(cy))
        self.check("circular-has-no-mark",
                   at_old_mark is not None and at_old_mark == at_centre,
                   "the round bezel's interior is uniform: %s where the mark "
                   "used to be, %s at its centre" % (at_old_mark, at_centre))

        # THE HELP ROW DRAWS ITS QUESTION MARK IN THE CIRCLE, NOT ITS TITLE
        # ACROSS THE FRAME. Ink inside the 28pt circle, none where a centred
        # label used to be painted.
        hx, hy = pts["HELP"]
        circle_ink = shot.ink((int(hx - 120), int(hy - 14),
                               int(hx - 92), int(hy + 14)))
        middle_ink = shot.ink((int(hx - 60), int(hy - 14),
                               int(hx + 100), int(hy + 14)))
        self.check("help-shows-question-mark",
                   circle_ink > 0 and middle_ink == 0,
                   "the glyph is in the circle (%d ink px) and the middle is "
                   "clean (%d ink px)" % (circle_ink, middle_ink))

        # THE BOARD'S OWN EXIT. Every check above leaves the board running
        # and the harness kills the guest, so the exit path - ZOO-TIMEOUT,
        # then the window's destructor - was never exercised, and that is
        # where the board was reported to fault. Run it to its own end and
        # ask the SHELL for its status: a fault arrives as SIGBUS/SIGSEGV
        # and a non-zero status, which a clean-looking console tail cannot
        # fake. The first instance must go, or two boards race for the
        # markers these checks read.
        session.run("killall WidgetZoo >/dev/null 2>&1")
        mark2 = len(session.log_text())
        session.run("%s 6; echo ZOO-EXIT=$?" % ZOO)
        session.wait_for(r"ZOO-EXIT=", 150)
        out3 = session.output_since(mark2)
        m3 = re.search(r"ZOO-EXIT=(\d+)", out3)
        status = int(m3.group(1)) if m3 else None
        self.check("board-exits-clean",
                   status is not None and status == 0,
                   "the board ran to its own end: status %s (expected 0); "
                   "tail: %s" % (status, session.tail()))

        self.check("board-still-alive",
                   "ZOO-TIMEOUT" in session.output_since(mark)
                   or "ZOO-CLOSED" in session.output_since(mark)
                   or session.alive(),
                   "the board survived the interactions")
