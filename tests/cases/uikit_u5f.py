"""U5f: a grid view lays its cells out, and measures itself from them.

The first half is the display-free probe (/System/Shared/tests/grid_view): where a
cell goes, how wide a column and tall a row are, what the grid's fitting size is,
and — the check this widget most needs — that LAYING OUT TWICE CHANGES NOTHING. A
grid that measured its cells from the frames it placed them in would grow a little
every pass, and only the second pass would show it.

A grid is a layout and not a control, so this case has no interaction half: the
board reports what the grid measured (ZOO-GRID) and the case asserts those numbers
are the ones the cells asked for.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/grid_view"
ZOO = "/Applications/WidgetZoo"

GRID = re.compile(r"ZOO-GRID cols=(\d+) rows=(\d+) size=([\d.]+)x([\d.]+) "
                  r"colw=([\d.]+),([\d.]+),([\d.]+) rowh=([\d.]+),([\d.]+)")


class Case(BaseCase):
    title = "UIKit U5f: a grid view lays its cells out and measures itself"
    tier = "slow"
    timeout = 300

    def run(self, ctx):
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")
        if not ready:
            return

        # ---- the probe: the grid's arithmetic, on the guest ---------------
        mark = len(session.log_text())
        session.run("test -x %s && %s; echo U5F-EXIT=$?" % (PROBE, PROBE))
        session.wait_for(r"U5F-EXIT=", 60)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U5F-FAIL"):
                self.note(line)
        self.check("the-grid-probe-ran", "U5F-OK (" in out,
                   "the probe laid its cells out and reported"
                   if "U5F-OK (" in out
                   else "no verdict; tail: " + session.tail(3))
        self.check("no-grid-case-failed", "U5F-FAIL" not in out,
                   "the columns, the rows, the placement and the fitting size"
                   " held, and laying out twice changed nothing")
        self.check("the-grid-probe-exited-zero", "U5F-EXIT=0" in out,
                   "the probe exited 0"
                   if "U5F-EXIT=0" in out
                   else "the probe exited non-zero: " + session.tail(2))

        # ---- the board: a three-by-two grid of buttons --------------------
        mark = len(session.log_text())
        session.run("%s 30 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail(4))
            return
        board = session.output_since(mark)

        m = GRID.search(board)
        self.check("the-board-has-a-grid-view", m is not None,
                   "the board reported its grid"
                   if m else "no ZOO-GRID line; tail: " + session.tail(3))
        if not m:
            return
        cols, rows = int(m.group(1)), int(m.group(2))
        size = (float(m.group(3)), float(m.group(4)))
        colw = (float(m.group(5)), float(m.group(6)), float(m.group(7)))
        rowh = (float(m.group(8)), float(m.group(9)))

        # The board's cells are built with widths 56/72/88 and heights 24/32,
        # spacing 8 across and 6 down, padding 4: every number below is the
        # grid's own arithmetic reading those cells, which is the point.
        self.check("the-board-has-three-columns-of-two", cols == 3 and rows == 2,
                   "cols=%d rows=%d" % (cols, rows))
        self.check("each-column-is-as-wide-as-its-widest-cell",
                   colw == (56.0, 72.0, 88.0), "colw=%g,%g,%g" % colw)
        self.check("each-row-is-as-tall-as-its-tallest-cell",
                   rowh == (24.0, 32.0), "rowh=%g,%g" % rowh)
        self.check("the-grid-measures-itself-from-its-cells",
                   size == (240.0, 70.0),
                   "size=%gx%g (4 + 56 + 8 + 72 + 8 + 88 + 4 across,"
                   " 4 + 24 + 6 + 32 + 4 down)" % size)
