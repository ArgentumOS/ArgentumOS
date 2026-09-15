"""Weaver's dock tile: clicking the THIRD tile launches a fresh window.

The dock launches bin/Weaver with no arguments; Weaver's no-arg path now
opens a new untitled template document in a window (title 'Weaver -
Untitled (edited)') instead of printing usage and exiting. The gate
clicks the real third tile and asserts the bundle resolved, the dock
launched it, and the WM manages the untitled window.
"""

import re

from harness import BaseCase

DOCK_LINE = (r"KESTREL: dock (\w+) (\d+)x(\d+) at (\d+),(\d+) "
             r"tiles=(\d+) icon=(\d+)")
DOCK_PAD = 8
TILE_GAP = 8


class Case(BaseCase):
    title = "Weaver dock tile: the third tile launches an untitled window"
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

        dock = re.search(DOCK_LINE, session.log_text())
        self.check("dock-logged", bool(dock),
                   "the WM reported its dock" if dock
                   else "no dock line")
        if not dock:
            return
        dy = int(dock.group(5))
        icon = int(dock.group(7))
        tile_x = 1916			# the dock's tile column centre
        # the THIRD tile (Widget Zoo, Calculator, then Weaver)
        tile_y = dy + DOCK_PAD + 2 * (icon + TILE_GAP) + icon // 2

        monitor = session.monitor()
        monitor.park()
        monitor.goto(tile_x, tile_y)
        monitor.click()

        self.check("weaver-bundle-resolved",
                   session.wait_for(r"KESTREL: bundle 'Weaver.app' ok", 45),
                   "the third tile's bundle manifest validated")
        self.check("weaver-launched",
                   session.wait_for(r"KESTREL: dock launch 'Weaver'", 45),
                   "the dock launched the bundle's payload")
        self.check("untitled-window-managed",
                   session.wait_for(
                       r"KESTREL: manage 0x[0-9a-f]+ "
                       r"'Weaver - Untitled \(edited\)'", 45),
                   "the WM manages the new untitled document window")

        session.run("killall Weaver")
