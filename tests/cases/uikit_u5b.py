"""U5b: the ScrollView — and the board scrolled by a real press.

Two halves in one boot.

The first half is the display-free probe (/System/Shared/tests/scroll_view):
it puts a document of a known size in a scroll view of a known size and
asserts the numbers that come out. That is the whole contract checked exactly
— the offset clamped to content-less-hole, the DOCUMENT's frame untouched by
every scroll, the bars derived from the same numbers, the wheel, and the hole
clipping the hits.

The second half is the widget zoo, where the scroll view holds a stack of
fourteen 300x28 rows inside a 249x305 hole. The board reports the content,
the hole, the offset, the bars and WHERE ITS DOCUMENT SITS — and the press
below is the end-to-end half: an ordinary click on the vertical bar's
increment arrow, which must scroll exactly one line and must NOT move the
document.

WHY A CLICK AND NOT A WHEEL. The wheel path (X button 4/5 -> ScrollWheel) is
a property of the INPUT PATH, and the test guest's mouse cannot send one:
QEMU's HMP mouse_button is a three-bit mask, so there is no way to press a
wheel button from the harness. The wheel's own logic is asserted in the probe
with a synthetic event, where it belongs; the arrow is the same offset going
through a press the guest really has, so the gate is not weaker for it, only
honest about which end it tests. (Assert a property the LIBRARY owns, not one
the input path owns.)
"""

import re
import time

from harness import BaseCase

PROBE = "/System/Shared/tests/scroll_view"
ZOO = "/Applications/WidgetZoo"

SCROLL = re.compile(
    r"ZOO-SCROLL rows=(\d+) content=([\d.]+)x([\d.]+) visible=([\d.]+)x([\d.]+)"
    r" offset=([\d.-]+),([\d.-]+) doc=([\d.-]+),([\d.-]+) bars=(\d),(\d)")
AT = re.compile(r"ZOO-AT (\S+) x=([\d.-]+) y=([\d.-]+)")
OFFSET = re.compile(r"ZOO-SCROLL-OFFSET x=([\d.-]+) y=([\d.-]+)"
                    r" doc=([\d.-]+),([\d.-]+)")


class Case(BaseCase):
    title = "UIKit U5b: a scroll view clips its content, and a bar scrolls it"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse",
                                  "-device", "usb-kbd"])
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")
        if not ready:
            return

        # ---- the probe: the contract, exactly ---------------------------
        mark = len(session.log_text())
        session.run("test -x %s && %s; echo U5B-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U5B-FAIL"):
                self.note(line)
        self.check("the-scroll-probe-ran", "U5B-OK (" in out,
                   "the probe scrolled its content and reported"
                   if "U5B-OK (" in out
                   else "no verdict; tail: " + session.tail(3))
        self.check("no-scroll-case-failed", "U5B-FAIL" not in out,
                   "every scroll case held")
        self.check("the-scroll-probe-exited-zero", "U5B-EXIT=0" in out,
                   "the probe exited 0"
                   if "U5B-EXIT=0" in out
                   else "the probe exited non-zero: " + session.tail(2))

        # ---- the board: the scroll view is real, and sized from its content
        mark = len(session.log_text())
        # the paint cost of a SCROLL step, which is the number a user feels:
        # every paint reports the views it walked, the damage it was given and
        # the time it took (see the damage section of tests/cases/uikit_u4.py)
        session.run("export ARGENTUM_PAINT_MS=1")
        session.run("%s 30 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail(4))
            return
        board = session.output_since(mark)

        m = SCROLL.search(board)
        self.check("the-board-has-a-scroll-view", m is not None,
                   "the board reported a scroll view"
                   if m else "no ZOO-SCROLL line; tail: " + session.tail(3))
        if not m:
            return
        (rows, cw, ch, vw, vh, ox, oy, dx, dy, barsV, barsH) = (
            int(m.group(1)), float(m.group(2)), float(m.group(3)),
            float(m.group(4)), float(m.group(5)), float(m.group(6)),
            float(m.group(7)), float(m.group(8)), float(m.group(9)),
            int(m.group(10)), int(m.group(11)))

        # 14 titled buttons of 28 on an 8pt gap is 496 tall; the 264x320 scroll
        # view less a 15pt bar each way is a 249x305 hole. THE CONTENT IS THE
        # DOCUMENT'S SIZE and the HOLE IS THE FRAME LESS THE BARS — the two
        # numbers the whole scroll range is the difference of.
        self.check("the-content-and-the-hole-are-the-real-ones",
                   rows == 14 and (cw, ch) == (300.0, 496.0)
                   and (vw, vh) == (249.0, 305.0),
                   "rows=%d content=%gx%g visible=%gx%g (14 rows of 300x28 on"
                   " an 8pt gap is 300x496, inside 264x320 less two 15pt bars)"
                   % (rows, cw, ch, vw, vh))
        self.check("both-bars-are-up", barsV == 1 and barsH == 1,
                   "the board asked for both bars and got them")
        self.check("nothing-is-scrolled-yet", (ox, oy) == (0.0, 0.0),
                   "the list starts at the top, offset %g,%g" % (ox, oy))
        # AND THE DOCUMENT SITS AT ITS ORIGIN. This is the invariant the
        # whole design turns on: the content view is NOT what scrolling
        # moves (the clip view's offset is), so it starts and stays at 0,0.
        self.check("the-document-is-at-its-origin", (dx, dy) == (0.0, 0.0),
                   "the document view is at %g,%g — where the layout system"
                   " put it, not where a scroll would have dragged it"
                   % (dx, dy))

        # ---- the press: the bar's own arrow, clicked ---------------------
        at = {m.group(1): (float(m.group(2)), float(m.group(3)))
              for m in re.finditer(AT, board)}
        screen = re.search(r"ZOO-SCREEN w=(\d+) h=(\d+)", board)

        if "VSCROLLER" not in at or not screen:
            self.check("the-bar-has-a-position", False,
                       "the board did not report the bar's position; tail: "
                       + session.tail(3))
            return
        screen_h = int(screen.group(2))
        vx, vy = at["VSCROLLER"]
        # the increment arrow is the bar's BOTTOM 15pt, and the bar is as
        # tall as the hole: half a bar down from its centre, less half an
        # arrow, is the middle of that button
        arrow_y = vy + vh / 2.0 - 7.5

        mon = session.monitor()

        # THE HARNESS'S POINTER IS A PRECONDITION, NOT A RESULT: in a run
        # where the guest's X server has no pointer every click is a silent
        # no-op, and the report reads like the app is broken (uikit_u2c
        # learned this the expensive way). Measure the noise floor, then
        # require parking to beat it.
        shot_a = session.shot("scroll-idle-a")
        noise = shot_a.diff(session.shot("scroll-idle-b"))

        mon.park()
        time.sleep(0.5)
        moved = shot_a.diff(session.shot("scroll-after-park"))
        if moved <= noise:
            self.check("pointer-input-is-up", False,
                       "parking the pointer changed %d pixels against a noise"
                       " floor of %d, so the guest has no pointer and the"
                       " click below would be a no-op; tail: %s"
                       % (moved, noise, " | ".join(session.tail(4))))
            return
        self.check("pointer-input-is-up", True,
                   "parking the pointer moved the cursor (%d pixels, noise"
                   " floor %d), so the click has somewhere to land"
                   % (moved, noise))

        mon.park()
        marker = len(session.log_text())
        # the monitor's Y is mirrored (the guest's origin is the top-left)
        mon.goto(int(vx), int(screen_h - arrow_y))
        mon.click(settle=0.8)
        session.wait_for(r"ZOO-SCROLL-OFFSET", 15)
        scrolled = session.output_since(marker)

        sm = OFFSET.search(scrolled)
        self.check("the-arrow-scrolled-the-list", sm is not None,
                   "the board reported a scroll"
                   if sm else "no ZOO-SCROLL-OFFSET after the click; the"
                   " board said: " + session.tail(3))
        if not sm:
            return
        ox2 = float(sm.group(1))
        oy2 = float(sm.group(2))
        dx2 = float(sm.group(3))
        dy2 = float(sm.group(4))

        # ONE LINE, and only down: the increment arrow is the VIEW's step
        # (lineScroll, 16), and a vertical arrow must not touch x.
        self.check("one-arrow-click-is-one-line",
                   oy2 == 16.0 and ox2 == 0.0,
                   "the arrow moved the offset to %g,%g — one 16pt line down"
                   " the vertical axis and nothing on the other" % (ox2, oy2))
        # THE INVARIANT, ON THE GUEST: the list scrolled and its content view
        # did not move. This is what makes a constrained document view
        # possible, and it is the one thing a frame-based scroll would fail.
        self.check("the-document-still-has-not-moved",
                   (dx2, dy2) == (0.0, 0.0),
                   "after scrolling, the document view is still at %g,%g"
                   % (dx2, dy2))
