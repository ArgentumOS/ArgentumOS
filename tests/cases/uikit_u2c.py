"""U2c: the widget zoo board — the button family, clicked and looked at.

The board (/Applications/WidgetZoo) carries one control per bezel style and
behaviour. The real pointer picks a switch and both radios, and the
framebuffer is asked about the shapes: a gradient must differ top-to-bottom
inside its bezel, and a radio that is ON must show its mark.

The radio checks are the interesting ones: two radios in the same superview
are a GROUP, so picking B clears A — with no group object anywhere.
"""

import re

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
        session.run("%s 60 &" % ZOO)
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

        # 1. the switch: a press sticks
        mon.click_at(*at("SWITCH"), settle=1.0)
        session.wait_for(r"ZOO-CLICK Switch", 30)
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
        mark = shot.px(int(rx - 112), int(ry))
        bg = shot.px(int(rx + 60), int(ry))
        self.check("radio-mark-is-drawn",
                   mark is not None and bg is not None and mark != bg,
                   "a radio's circle is at its left: %s against the "
                   "background %s" % (mark, bg))

        self.check("board-still-alive",
                   "ZOO-TIMEOUT" in session.output_since(mark)
                   or "ZOO-CLOSED" in session.output_since(mark)
                   or session.alive(),
                   "the board survived the interactions")
