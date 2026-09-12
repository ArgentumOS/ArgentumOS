"""Clicking a dock tile launches its app; a second click raises it instead, and
a window drag ends when the button comes up - even mid fast motion.

What it proves: the dock sits at the screen edge, a click on its first tile
starts the pinned app, the app's frame lands inside the work area (clear of the
dock, which owns a column of the desktop), the tile then shows a running dot,
clicking again raises the running app rather than starting a second copy, and a
flick drag leaves the frame where the release happened (a deferred drag end
kept the window on the pointer after the button was up).

This is also the case that exercises the pointer machinery in
tests/harness/monitor.py, so it is the one that says whether input driving
works at all: the guest drops ps/2 chunks sent too fast, and it loses a press
that comes with no motion.
"""

import re
import time

from harness import BaseCase

APP = "Argentum widget zoo"
DOCK_LINE = (r"KESTREL: dock (\w+) (\d+)x(\d+) at (\d+),(\d+) "
             r"tiles=(\d+) icon=(\d+)")
MANAGE_LINE = (r"KESTREL: manage 0x[0-9a-f]+ '%s' frame=0x[0-9a-f]+ "
               r"at (\d+),(\d+) (\d+)x(\d+)")
XERR = r"X Error|BadWindow|BadMatch|BadValue|BadDrawable"

# The dock's internal layout, mirroring kestrel.cpp (DOCK_PAD / TILE_GAP).
# The log gives the dock's box, its tile count and the icon size; the padding
# above the first tile is the WM's layout contract with this case.
DOCK_PAD = 8


class Case(BaseCase):
    title = "the dock: a tile launches or raises, and a drag ends at the release"
    tier = "slow"
    timeout = 600

    def run(self, ctx):
        ctx.require_guest_file("widget_zoo")
        session = ctx.boot()
        if not session.wait_for(r"KESTREL-READY", 150):
            self.check("session-up", False, "the desktop never came up")
            return
        self.check("session-up", True, "the desktop is running")

        dock = re.search(DOCK_LINE, session.log_text())
        self.check("dock-logged", bool(dock), "the WM reported its dock")
        if not dock:
            return

        side = dock.group(1)
        dw, dh, dx, dy = (int(dock.group(i)) for i in (2, 3, 4, 5))
        icon = int(dock.group(7))

        monitor = session.monitor()
        monitor.park()
        before = session.shot("before")
        self.check("dock-at-edge", dx + dw == before.w,
                   "the dock reaches the %s edge of a %d-wide screen (x %d..%d)"
                   % (side, before.w, dx, dx + dw))
        # Edge chrome: the dock spans the whole height left below the menubar
        # (it starts at the bar's bottom edge and reaches the screen's).  It is
        # inset only on the inside, where the work area keeps windows off it -
        # a vertical inset of its own left a gap above and below it.
        self.check("dock-full-height", dy + dh == before.h and dy > 0,
                   "the dock spans y %d..%d of %d (below the menubar)"
                   % (dy, dy + dh, before.h))

        # The first tile's centre, from the dock the WM logged - not from a
        # screen coordinate (a 1280-wide screen puts it elsewhere than 1920).
        tile_x = dx + dw // 2
        tile_y = dy + DOCK_PAD + icon // 2
        monitor.goto(tile_x, tile_y)
        monitor.click()

        launched = session.wait_for(r"KESTREL: dock launch '[^']*'", 45)
        self.check("tile-launches", launched,
                   "a click on the tile started the pinned app")
        managed = session.wait_for(r"KESTREL: manage 0x[0-9a-f]+ '%s'"
                                   % re.escape(APP), 45)
        self.check("app-managed", managed,
                   "the desktop manages the app's window")

        after = session.shot("launched")
        frame = re.search(MANAGE_LINE % re.escape(APP), session.log_text())
        if frame:
            fx, fy, fw, fh = (int(frame.group(i)) for i in (1, 2, 3, 4))
            inset = (fx + fw <= dx) if side == "right" else (fx >= dx + dw)
            self.check("frame-inset-from-dock", inset,
                       "frame %d,%d %dx%d stays clear of the dock at x=%d"
                       % (fx, fy, fw, fh, dx))
        else:
            self.check("frame-inset-from-dock", False,
                       "the desktop did not log the frame geometry")

        # The running dot: the tile itself changed once its app had a window.
        tile = (dx, tile_y - icon // 2, dx + dw, tile_y + icon // 2)
        changed = before.diff_box(after, tile)
        self.check("running-dot", changed > 0,
                   "%d px of the tile changed after its app was mapped" % changed)

        monitor.click()
        raised = session.wait_for(r"KESTREL: dock raise '[^']*'", 45)
        self.check("second-click-raises", raised,
                   "the second click raised the running app")
        launches = session.count(r"KESTREL: dock launch '[^']*'")
        self.check("no-second-launch", launches == 1,
                   "%d launch(es) for two clicks" % launches)

        # --- the drag must END at the release ---------------------------
        # A drag used to be ended by a TIMER: the release counted only once
        # the pointer had been quiet for a while (a full second when the
        # release landed mid fast motion), and motion with the button up
        # moved the window AND re-armed that clock.  So a flick followed by
        # any hand movement kept the window on the pointer for as long as
        # the hand kept moving - "it sticks after I let go".  The end is
        # the release itself now.  Drag by a known delta, release, then
        # move the pointer FAR without the button: the frame must stay
        # where the release left it.
        if frame:
            BAND_H = 20			# kestrel.cpp: the title bar
            flick = [(40, 0), (40, 0), (40, 0), (30, 0), (0, -30)]
            net = [0, 0]

            monitor.goto(fx + fw // 2, fy + BAND_H // 2)
            monitor.nudge()		# a bare button change is dropped
            monitor.send("mouse_button 1")
            for fdx, fdy in flick:
                monitor.move(fdx, fdy)
                net[0] += fdx
                net[1] -= fdy		# monitor dy is positive UP
            monitor.send("mouse_button 0")	# the release, mid-motion
            monitor.move(200, 0)		# the hand keeps going: 200px
            time.sleep(1.5)			# a deferred end would land here

            end = None
            for line in session.log_text().splitlines():
                hit = re.search(r"KESTREL: move 0x[0-9a-f]+ '%s' to (\d+),(\d+)"
                                % re.escape(APP), line)
                if hit:
                    end = (int(hit.group(1)), int(hit.group(2)))
            want = (fx + net[0], fy + net[1])
            self.check(
                "drag-ends-at-release",
                end is not None and abs(end[0] - want[0]) < 40
                and abs(end[1] - want[1]) < 40,
                "the frame ended at %s; the release was at %d,%d (a window "
                "still following the pointer would land near %d,%d)"
                % (("%d,%d" % end) if end else "never",
                   want[0], want[1], want[0] + 200, want[1]))

        self.check("no-x-errors", session.count(XERR) == 0,
                   "no X protocol error")
