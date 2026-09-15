"""U2b-drag: the window's titlebar drag — KNOWN BROKEN, isolated.

The window moves in the model but the drag's delta stops after the first
motion event, and the probe STOPS ANSWERING afterwards (no U2B-CLOSED, no
U2B-TIMEOUT): a window MOVE (XMoveWindow) inside the motion path wedges
the client. This case exists to keep that visible and to keep it out of
the way of the interactions that DO work (uikit_u2b), which a wedged
probe would otherwise make unevaluatable.

Candidates to separate, in order:
  1. the XSync after every move against Xfb's asynchronous drain;
  2. Xfb's own handling of a moving window.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/control_click"
BROKEN = ("KNOWN BROKEN: the drag stops following after the first motion "
          "event and the probe then wedges. See this case's docstring for "
          "the two candidates to separate.")


class Case(BaseCase):
    title = "UIKit U2b: the titlebar drag (known broken, isolated)"
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
        session.run("%s &" % PROBE)
        if not session.wait_for(r"U2B-READY", 120):
            self.check("probe-ready", False, "no U2B-READY")
            return
        out = session.output_since(mark)
        win = re.search(r"U2B-WINDOW x=([\d.]+) y=([\d.]+)", out)
        bar = re.search(r"U2B-TITLEBAR screen=\(([\d.]+),([\d.]+)\)", out)
        sh = re.search(r"U2B-SCREEN w=\d+ h=(\d+)", out)
        if not (win and bar and sh):
            self.check("geometry-parsed", False, "no geometry in the log")
            return
        screen_h = int(sh.group(1))
        wx, wy = float(win.group(1)), float(win.group(2))
        gx, gy = float(bar.group(1)), float(bar.group(2))

        mon = session.monitor()

        mon.park()
        mon.drag(int(gx), int(screen_h - gy),
                 int(gx) + 90, int(screen_h - (gy + 60)))
        session.wait_for(r"U2B-MOVED|U2B-TIMEOUT", 40)
        out2 = session.output_since(mark)
        m = re.search(r"U2B-MOVED x=([\d.]+) y=([\d.]+)", out2)
        # NOT xfail: entering the move path works (the window reports a new
        # origin); what fails is the delta and the wedge after it, below.
        self.check("drag-enters-the-move-path", m is not None,
                   "the window reported a new origin" if m
                   else "no move at all")
        if m is not None:
            nx, ny = float(m.group(1)), float(m.group(2))
            self.check("drag-follows-the-pointer",
                       abs(nx - (wx + 90)) <= 12 and abs(ny - (wy + 60)) <= 12,
                       "moved (%g,%g) -> (%g,%g): a drag of (+90,+60)"
                       % (wx, wy, nx, ny), xfail=BROKEN)
        self.check("probe-still-answers", "U2B-TIMEOUT" in out2
                   or "U2B-CLOSED" in out2,
                   "the probe finished its loop", xfail=BROKEN)
