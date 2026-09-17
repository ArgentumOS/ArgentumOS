"""U5c: a collection view lays its items out in a flow.

The first half is the display-free probe (/System/Shared/tests/collection_view):
it asserts the FLOW'S ARITHMETIC for known sizes — how many items fit a line, the
frame of the first item, the frame of the one that wraps, the height lines plus
gaps plus insets come to — and that a collection view places its items there and
SIZES ITSELF to the flow.

The second half is the board, which holds a collection view inside a scroll
view. That is the composition the container layer exists for, and the numbers
below are the flow's, not the board's: twelve 56x28 tiles, 6 apart, 4 in from
the edges, in a 249-wide hole, is three to a line and four lines, so 138 tall
inside an 81-tall visible area — which is why the vertical bar is there.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/collection_view"
ZOO = "/Applications/WidgetZoo"

COLL = re.compile(
    r"ZOO-COLLECTION items=(\d+) size=([\d.]+)x([\d.]+) visible=([\d.]+)x([\d.]+)"
    r" offset=([\d.-]+),([\d.-]+) bars=(\d)")


class Case(BaseCase):
    title = "UIKit U5c: a collection view lays its items out in a flow"
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

        # ---- the probe: the flow's arithmetic, on the guest -------------
        mark = len(session.log_text())
        session.run("test -x %s && %s; echo U5C-EXIT=$?" % (PROBE, PROBE))
        session.wait_for(r"U5C-EXIT=", 60)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U5C-FAIL"):
                self.note(line)
        self.check("the-flow-probe-ran", "U5C-OK (" in out,
                   "the probe laid its items out and reported"
                   if "U5C-OK (" in out
                   else "no verdict; tail: " + session.tail(3))
        self.check("no-flow-case-failed", "U5C-FAIL" not in out,
                   "the wrap, the frames, the height and the self-sizing held")
        self.check("the-flow-probe-exited-zero", "U5C-EXIT=0" in out,
                   "the probe exited 0"
                   if "U5C-EXIT=0" in out
                   else "the probe exited non-zero: " + session.tail(2))

        # ---- the board: a collection view inside a scroll view ----------
        mark = len(session.log_text())
        session.run("%s 20 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail(4))
            return
        board = session.output_since(mark)

        m = COLL.search(board)
        self.check("the-board-has-a-collection-view", m is not None,
                   "the board reported a collection view"
                   if m else "no ZOO-COLLECTION line; tail: "
                   + session.tail(3))
        if not m:
            return
        items = int(m.group(1))
        cw, ch = float(m.group(2)), float(m.group(3))
        vw, vh = float(m.group(4)), float(m.group(5))
        ox, oy = float(m.group(6)), float(m.group(7))
        bars = int(m.group(8))

        # 12 tiles of 56x28 on a 6pt gap, 4 in from every edge, in a hole 249
        # wide (264 less the 15pt bar): three to a line, four lines, and
        # 4 + 4*28 + 3*6 + 4 = 138 tall. The numbers are the flow's, which is
        # the point — the board computes none of them.
        self.check("the-flow-wrapped-the-tiles",
                   items == 12 and (cw, ch) == (249.0, 138.0),
                   "items=%d content=%gx%g (12 tiles of 56x28, 6 apart, 4 in"
                   " from the edges, 3 to a line is 249x138)"
                   % (items, cw, ch))
        self.check("the-collection-is-taller-than-its-hole", ch > vh,
                   "content %g tall in a %g-tall hole" % (ch, vh))
        self.check("and-that-is-what-the-bar-is-for", bars == 1,
                   "the collection asked for a vertical bar: bars=%d" % bars)
        self.check("the-collection-starts-at-the-top", (ox, oy) == (0.0, 0.0),
                   "offset %g,%g" % (ox, oy))
