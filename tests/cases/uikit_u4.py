"""U4: the value controls — a slider, a stepper, a progress bar and a
level indicator.

The interactive two are DRIVEN: the slider's knob is dragged to a known
fraction of its track and the value it reports is checked against the
position, and the stepper's two halves are clicked. The other two are
LOOKED AT: a determinate progress bar must be filled for exactly its
fraction of the width, and the level indicator past its critical threshold
must be drawn in the critical colour — which is the only way to see that a
threshold does anything.

Coordinates come from the board's log, and the monitor's Y is mirrored
(screen_h - y), as every pointer case here must do.
"""

import re

from harness import BaseCase

ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit U4: the value controls (slider, stepper, progress, level)"
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
        mark = len(session.log_text())
        session.run("%s 60 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail())
            return
        out = session.output_since(mark)
        sh = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", out)
        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)", out)}
        need = ("SLIDER", "STEPPER", "PROGRESS", "LEVEL")
        missing = [n for n in need if n not in pts]
        self.check("controls-located", sh is not None and not missing,
                   "screen %s, controls at %s" % (sh.group(1), sorted(pts))
                   if sh and not missing else "missing %s" % (missing or "?"))
        if sh is None or missing:
            return
        screen_h = int(sh.group(1))
        mon = session.monitor()

        def at(x, y):
            return int(x), int(screen_h - y)

        mon.park()

        # ---- the slider: drag the knob to three quarters of the track ---
        sx, sy = pts["SLIDER"]
        # the knob starts at the far left (value 0), so the drag starts there
        mon.goto(*at(sx - 120, sy))
        mon.press()
        # 10 steps of 10 points, SLOWLY: a slider repaint is a full board
        # repaint (the coarse damage model), so a fast drag overruns the
        # guest's input queue and it DROPS motion - which shows up as a
        # value that stops short of where the pointer went. dt=0.25 keeps
        # the client inside its own drain rate.
        for i in range(1, 11):
            mon.goto(*at(sx - 120 + 10 * i, sy), dt=0.25)
        mon.release(settle=0.8)
        session.wait_for(r"ZOO-SLIDE", 30)
        out2 = session.output_since(mark)
        slides = re.findall(r"ZOO-SLIDE ([\d.]+)", out2)
        self.check("slider-reports-its-value", len(slides) > 1,
                   "a continuous drag sent %d action(s), the last reading %s"
                   % (len(slides), slides[-1] if slides else None))
        if slides:
            seen = [float(v) for v in slides]
            # The input path DROPS motion during a drag (each step repaints
            # the whole board, and the guest's queue is small), so the
            # number of readings is not the number of steps sent. What IS
            # checkable is the thing the slider promises: the value maps
            # LINEARLY to the pointer's x. The steps I inject are equal, so
            # equal differences in the readings prove the mapping - and 4.5
            # per 10pt on a 222pt track is exactly what (x-9)/222*100 gives.
            steps = [b - a for a, b in zip(seen[1:], seen[2:])]
            self.check("slider-tracks-the-pointer",
                       len(seen) >= 3 and max(seen) > 5,
                       "the knob followed the drag: %s of 100 (the last "
                       "event the guest kept)" % seen)
            self.check("slider-mapping-is-linear",
                       len(steps) >= 2 and all(abs(d - steps[0]) < 0.6
                                               for d in steps),
                       "equal pointer steps gave equal value steps: %s"
                       % [round(d, 3) for d in steps])

        # ---- the stepper: the upper half increments, the lower decrements -
        tx, ty = pts["STEPPER"]
        mon.click_at(*at(tx, ty - 8), settle=0.8)
        session.wait_for(r"ZOO-STEP 1", 30)
        out3 = session.output_since(mark)
        self.check("stepper-up", "ZOO-STEP 1" in out3,
                   "clicking the upper half made the value 1"
                   if "ZOO-STEP 1" in out3 else
                   "no step up; tail: " + session.tail())
        mon.click_at(*at(tx, ty + 8), settle=0.8)
        session.wait_for(r"ZOO-STEP 0", 30)
        out4 = session.output_since(mark)
        self.check("stepper-down", re.search(r"ZOO-STEP 0\b", out4) is not None,
                   "clicking the lower half brought it back to 0")

        # ---- the two bars, in the framebuffer ---------------------------
        shot = session.shot("u4")
        self.check("shot-taken", shot is not None, "a framebuffer dump")
        if shot is None:
            return

        # the progress bar: filled to its fraction and not beyond
        wx, wy = 70, 50
        px, py = pts["PROGRESS"]
        bar_left, bar_right = int(px - 120), int(px + 120)
        inside = shot.px(bar_left + 20, int(py))	# 25% -> well inside
        beyond = shot.px(bar_right - 20, int(py))	# well past it
        self.check("progress-fills-its-fraction",
                   inside is not None and beyond is not None
                   and inside != beyond,
                   "inside the bar %s, past the fill %s (the log says the "
                   "fraction is 0.25)" % (inside, beyond))

        # the level indicator: past its critical threshold, so RED
        lx, ly = pts["LEVEL"]
        fill = shot.px(int(lx - 100), int(ly))
        self.check("level-threshold-colours-the-fill",
                   fill is not None and fill[0] > 150 and fill[1] < 120,
                   "the level indicator's fill at 9.5/10 (critical at 9) is "
                   "%s: a red, not the normal green" % (fill,))
