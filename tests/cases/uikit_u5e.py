"""U5e: a split view divides its space, and the divider between the panes drags.

The first half is the display-free probe (/System/Shared/tests/split_view): where
the panes and the divider are, what a point falls on, and what a position does
including its clamp. The DRAG is deliberately not in the probe — a synthetic
event tests the event, not the divider — so the second half drags the board's
real divider and reads where it came to rest.

The board logs ZOO-SPLIT at rest and again at the END of a drag (an interaction
in progress is not something to judge), so the drag has a line to wait for rather
than a line that repeats every frame.
"""

import re
import time

from harness import BaseCase

PROBE = "/System/Shared/tests/split_view"
ZOO = "/Applications/WidgetZoo"

SPLIT = re.compile(r"ZOO-SPLIT panes=(\d+) divider=([\d.]+),([\d.]+),"
                   r"([\d.]+)x([\d.]+) sizes=([\d.]+),([\d.]+)")
AT = re.compile(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)")


class Case(BaseCase):
    title = "UIKit U5e: a split view divides its space and the divider drags"
    tier = "slow"
    timeout = 300

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse",
                                  "-device", "usb-kbd"])
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")
        if not ready:
            return

        # ---- the probe: the split's arithmetic, on the guest -------------
        mark = len(session.log_text())
        session.run("test -x %s && %s; echo U5E-EXIT=$?" % (PROBE, PROBE))
        session.wait_for(r"U5E-EXIT=", 60)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U5E-FAIL"):
                self.note(line)
        self.check("the-split-probe-ran", "U5E-OK (" in out,
                   "the probe laid its panes out and reported"
                   if "U5E-OK (" in out
                   else "no verdict; tail: " + session.tail(3))
        self.check("no-split-case-failed", "U5E-FAIL" not in out,
                   "the panes, the divider's arithmetic and the clamp held")
        self.check("the-split-probe-exited-zero", "U5E-EXIT=0" in out,
                   "the probe exited 0"
                   if "U5E-EXIT=0" in out
                   else "the probe exited non-zero: " + session.tail(2))

        # ---- the board: two panes, and a real drag on the divider --------
        mark = len(session.log_text())
        # ARGENTUM_PAINT_MS: the toolkit's own instrument, on for this board so
        # the drag's cost is in the log rather than in an opinion (the u5b gate
        # exports it too).
        session.run("ARGENTUM_PAINT_MS=1 %s 30 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail(4))
            return
        board = session.output_since(mark)

        m = SPLIT.search(board)
        self.check("the-board-has-a-split-view", m is not None,
                   "the board reported its split"
                   if m else "no ZOO-SPLIT line; tail: " + session.tail(3))
        if not m:
            return
        panes = int(m.group(1))
        divider = (float(m.group(2)), float(m.group(3)),
                   float(m.group(4)), float(m.group(5)))
        sizes0 = (float(m.group(6)), float(m.group(7)))

        # Two panes of a 300pt split with 6pt of divider: 147 each, and the
        # divider sitting exactly halfway. The numbers are the SplitView's,
        # which is the point.
        self.check("the-board-has-two-panes", panes == 2, "panes=%d" % panes)
        self.check("the-panes-share-what-the-divider-leaves",
                   sizes0 == (147.0, 147.0), "sizes=%g,%g" % sizes0)
        self.check("the-divider-is-the-boundary",
                   divider[0] == 147.0 and divider[2] == 6.0
                   and divider[3] == 100.0,
                   "divider=%g,%g %gx%g (the halfway mark of a 300pt split)"
                   % divider)

        at = {n: (float(x), float(y)) for n, x, y in AT.findall(board)}
        screen = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", board)
        if "SPLIT" not in at or not screen:
            self.check("the-split-view-has-a-position", False,
                       "the board did not report the split view's position; "
                       "tail: " + session.tail(3))
            return
        screen_h = int(screen.group(1))
        sx, sy = at["SPLIT"]
        mon = session.monitor()

        # THE POINTER IS A PRECONDITION, NOT A RESULT (uikit_u2c's lesson): in
        # a run whose guest has no pointer every drag is a silent no-op and the
        # report reads like a broken widget. Park, and require the park to move
        # pixels before believing the drag.
        shot_a = session.shot("split-idle-a")
        noise = shot_a.diff(session.shot("split-idle-b"))
        mon.park()
        time.sleep(0.5)
        moved = shot_a.diff(session.shot("split-after-park"))
        if moved <= noise:
            self.check("pointer-input-is-up", False,
                       "parking the pointer changed %d pixels against a noise"
                       " floor of %d, so the guest has no pointer and the drag"
                       " below would be a no-op; tail: %s"
                       % (moved, noise, " | ".join(session.tail(4))))
            return
        self.check("pointer-input-is-up", True,
                   "parking the pointer moved the cursor (%d pixels, noise"
                   " floor %d)" % (moved, noise))

        # THE DRAG. The divider's centre IS the view's centre — 147 + 3 of 300
        # across, 50 of 100 down — which is exactly what logAt reports, so the
        # press lands on the divider with no arithmetic to get wrong. Pull it
        # 50pt right: the panes go 147/147 -> 197/97, and the board logs that at
        # the END of the drag. The monitor's Y is mirrored (the guest's origin
        # is the top-left).
        marker = len(session.log_text())
        mon.drag(int(sx), int(screen_h - sy), int(sx) + 50, int(screen_h - sy))
        session.wait_for(r"ZOO-SPLIT .* sizes=197,97", 20)
        after = session.output_since(marker)

        m2 = SPLIT.search(after)
        self.check("dragging-the-divider-resizes-both-panes",
                   m2 is not None
                   and (float(m2.group(6)), float(m2.group(7)))
                   == (197.0, 97.0),
                   "the board logged %s at the end of the drag"
                   % (m2.group(0) if m2 else "nothing new"))
