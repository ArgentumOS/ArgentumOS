"""U2b-drag: the window's titlebar drag, driven by the real pointer.

This case exists because the drag was reported as broken and was not. The
first version waited for the FIRST `U2B-MOVED` line and compared it with
the FINAL expected position, so it judged a drag that was still in
progress - and called the still-arriving motion a "wedge". The evidence
that settled it is this case's own log: the window steps with the pointer
(150, 157, 163, 170, 171) and lands exactly on target, because the drag
delta comes from the event's ROOT coordinates.

So: send the whole drag, wait for the probe to finish its loop, THEN ask
where the window ended up.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/control_click"

class Case(BaseCase):
    title = "UIKit U2b: the titlebar drag, driven by the real pointer"
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
        session.run("%s 30 &" % PROBE)
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
        # wait for the WHOLE interaction: the probe's own loop ends and it
        # says so. Judging the first motion event is what made this case
        # lie before.
        session.wait_for(r"U2B-TIMEOUT|U2B-CLOSED", 60)
        out2 = session.output_since(mark)
        moves = re.findall(r"U2B-MOVED x=([\d.]+) y=([\d.]+)", out2)
        for x, y in moves:
            self.note("moved to (%s,%s)" % (x, y))
        self.check("drag-enters-the-move-path", len(moves) > 0,
                   "the window reported %d move(s) while the pointer moved"
                   % len(moves))
        self.check("probe-still-answers", "U2B-TIMEOUT" in out2
                   or "U2B-CLOSED" in out2,
                   "the probe finished its loop: the drag never wedged it")
        if moves:
            # the LAST move is where the drag ended
            nx, ny = float(moves[-1][0]), float(moves[-1][1])
            self.check("drag-follows-the-pointer",
                       abs(nx - (wx + 90)) <= 12 and abs(ny - (wy + 60)) <= 12,
                       "moved (%g,%g) -> (%g,%g): a drag of (+90,+60)"
                       % (wx, wy, nx, ny))
            self.check("drag-tracks-in-steps", len(moves) >= 3,
                       "the window followed in %d steps, not one jump"
                       % len(moves))
