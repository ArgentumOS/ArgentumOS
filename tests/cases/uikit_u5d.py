"""U5d: a tab view shows one pane and switches on a press.

The first half is the display-free probe (/System/Shared/tests/tab_view): where
each tab is, what a point falls on, where the panes go, and what a selection
does to them. The PRESS is deliberately not in the probe — a synthetic event
tests the event, not the strip — so the second half of this case is a real click
on the board's third tab.

The board logs ZOO-TABS when the selection CHANGES (the same shape as the scroll
region's ZOO-SCROLL-OFFSET), so the click has an event to wait for rather than a
line that repeats every frame.
"""

import re

from harness import BaseCase

import time

PROBE = "/System/Shared/tests/tab_view"
ZOO = "/Applications/WidgetZoo"

TABS = re.compile(r"ZOO-TABS tabs=(\d+) selected=(-?\d+) strip=([\d.]+)x([\d.]+)"
                  r" pane=([\d.]+)x([\d.]+)")
AT = re.compile(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)")


class Case(BaseCase):
    title = "UIKit U5d: a tab view shows one pane and switches on a press"
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

        # ---- the probe: the strip's arithmetic, on the guest -------------
        mark = len(session.log_text())
        session.run("test -x %s && %s; echo U5D-EXIT=$?" % (PROBE, PROBE))
        session.wait_for(r"U5D-EXIT=", 60)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U5D-FAIL"):
                self.note(line)
        self.check("the-tab-probe-ran", "U5D-OK (" in out,
                   "the probe laid its strip out and reported"
                   if "U5D-OK (" in out
                   else "no verdict; tail: " + session.tail(3))
        self.check("no-tab-case-failed", "U5D-FAIL" not in out,
                   "the strip's rects, the hit test and the selection held")
        self.check("the-tab-probe-exited-zero", "U5D-EXIT=0" in out,
                   "the probe exited 0"
                   if "U5D-EXIT=0" in out
                   else "the probe exited non-zero: " + session.tail(2))

        # ---- the board: three tabs, and a real press on the third --------
        mark = len(session.log_text())
        session.run("%s 30 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail(4))
            return
        board = session.output_since(mark)

        m = TABS.search(board)
        self.check("the-board-has-a-tab-view", m is not None,
                   "the board reported its tabs"
                   if m else "no ZOO-TABS line; tail: " + session.tail(3))
        if not m:
            return
        tabs = int(m.group(1))
        sel0 = int(m.group(2))
        strip = (float(m.group(3)), float(m.group(4)))
        pane = (float(m.group(5)), float(m.group(6)))

        # three tabs of 84x24, and the panes get everything below the strip:
        # the numbers are the TabView's, which is the point.
        self.check("the-board-opens-on-the-first-tab",
                   tabs == 3 and sel0 == 0, "tabs=%d selected=%d"
                   % (tabs, sel0))
        self.check("the-strip-is-the-size-the-tab-view-was-given",
                   strip == (84.0, 24.0), "strip=%gx%g" % strip)
        self.check("the-pane-fills-what-is-left",
                   pane[0] == 224.0 and pane[1] == 96.0,
                   "pane=%gx%g (224 wide, 120 less the 24pt strip)" % pane)

        at = {n: (float(x), float(y))
              for n, x, y in AT.findall(board)}
        screen = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", board)
        if "TABS" not in at or not screen:
            self.check("the-tab-view-has-a-position", False,
                       "the board did not report the tab view's position; "
                       "tail: " + session.tail(3))
            return
        screen_h = int(screen.group(1))
        tx, ty = at["TABS"]
        mon = session.monitor()

        # THE POINTER IS A PRECONDITION, NOT A RESULT (uikit_u2c's lesson): in
        # a run whose guest has no pointer every click is a silent no-op and
        # the report reads like a broken widget. Park, and require the park to
        # move pixels before believing the click.
        shot_a = session.shot("tabs-idle-a")
        noise = shot_a.diff(session.shot("tabs-idle-b"))
        mon.park()
        time.sleep(0.5)
        moved = shot_a.diff(session.shot("tabs-after-park"))
        if moved <= noise:
            self.check("pointer-input-is-up", False,
                       "parking the pointer changed %d pixels against a noise"
                       " floor of %d, so the guest has no pointer and the click"
                       " below would be a no-op; tail: %s"
                       % (moved, noise, " | ".join(session.tail(4))))
            return
        self.check("pointer-input-is-up", True,
                   "parking the pointer moved the cursor (%d pixels, noise"
                   " floor %d), so the click has somewhere to land"
                   % (moved, noise))

        # THE LOGGED POINT IS THE VIEW'S CENTRE (logAt logs a view's centre),
        # so from a 224x120 tab view the third 84-wide tab's centre is 98 to
        # the right (+112 - 42 - 112 + 168) and 48 above (+60 - 12). The
        # monitor's Y is mirrored (the guest's origin is the top-left).
        marker = len(session.log_text())
        mon.goto(int(tx + 98), int(screen_h - (ty - 48)))
        mon.click(settle=0.8)
        session.wait_for(r"ZOO-TABS .* selected=2", 20)
        after = session.output_since(marker)

        m2 = TABS.search(after)
        self.check("a-press-on-the-third-tab-selects-it",
                   m2 is not None and int(m2.group(2)) == 2,
                   "the board logged %s after the click"
                   % (m2.group(0) if m2 else "nothing new"))
        # THE PANES DO NOT MOVE with the selection: the pane area is the
        # content rect either way, so only WHO is showing changes.
        if m2:
            self.check("the-pane-area-is-the-same-after-the-switch",
                       (float(m2.group(5)), float(m2.group(6))) == pane,
                       "pane %sx%s then %sx%s"
                       % (pane[0], pane[1], m2.group(5), m2.group(6)))
