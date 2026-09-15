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
        # ---- the CIRCULAR slider: a point's ANGLE is the value ----------
        # 0 points up and the value turns clockwise, so a click due RIGHT of
        # the centre is a quarter of the range - and the dial is 0..1, so the
        # value it reports IS the fraction.
        dx, dy = pts["DIAL"]
        mon.click_at(*at(dx + 11, dy), settle=0.8)
        session.wait_for(r"ZOO-DIAL", 30)
        out_dial = session.output_since(mark)
        dials = re.findall(r"ZOO-DIAL ([\d.]+)", out_dial)
        self.check("dial-angle-is-the-value",
                   bool(dials) and abs(float(dials[-1]) - 0.25) <= 0.05,
                   "a click due right of the dial's centre gave %s; a quarter "
                   "of the range is 0.25"
                   % (dials[-1] if dials else "nothing"))

        # THE POINTER MUST NOT BE IN THE PICTURE. It sits where the last click
        # landed - over the dial - and the cursor is drawn into the
        # framebuffer, so a pixel check aimed at the knob sampled a black
        # cursor instead. park() first, as every other pixel check in this
        # project does.
        mon.park()

        shot = session.shot("u4")
        self.check("shot-taken", shot is not None, "a framebuffer dump")
        if shot is None:
            return

        # the dial: a CIRCLE, not a bar - its centre is the face and the
        # corner of its (square) frame is outside the ring
        dcx, dcy = pts["DIAL"]
        face = shot.px(int(dcx), int(dcy))
        corner = shot.px(int(dcx) - 14, int(dcy) - 14)
        self.check("dial-is-round",
                   face is not None and corner is not None
                   and face != corner,
                   "the dial's centre %s and the outside of its frame %s: a "
                   "round slider leaves the corners of its box alone"
                   % (face, corner))

        # and its knob is DRAWN where the cell says it is: the zoo logs the
        # cell's own knobPoint(), so this aims at the library's answer rather
        # than at a guess about the geometry. The knob is a blue disc with a
        # light centre, so its rim is the place to look.
        knob = re.search(r"ZOO-KNOB x=([\d.]+) y=([\d.]+)", out_dial)
        rim = None
        if knob:
            # +4, not +3: the knob is a blue disc of radius 5 with a LIGHT
            # centre of 2.5, so three points out lands on the centre and the
            # blue is the band between the two
            rim = shot.px(int(float(knob.group(1))) + 4,
                          int(float(knob.group(2))))
        self.check("dial-knob-is-where-the-cell-says",
                   rim is not None and rim[2] > rim[0] + 60,
                   "the knob's rim reads %s at the position the cell reported "
                   "(%s); the knob is blue, the face and ring are not"
                   % (rim, knob.groups() if knob else "no ZOO-KNOB line"))

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

        # WHOLE STEPS, SO IT READS AS STARS. The row is five cells and the value
        # is 3.6, so four stars are filled and the fifth is not. Sampling the
        # CENTRE of each is safe for the same reason the drawing can fan from
        # there: a star is star-shaped about its centre, so the centre is always
        # inside it.
        rx, ry = pts["RATING"]
        cell = 90.0 / 5.0
        # NOT mirrored: the monitor's INPUT y is mirrored, the SCREENSHOT is
        # not (the dial check samples the guest's own 808 and finds the knob).
        # Getting this wrong samples the other half of the screen, which is how
        # this first read a progress bar's track and called it a flat rating.
        yrow = int(ry)
        ink = shot.px(int(rx - 45.0 + cell * 3.5), yrow)   # the fourth star
        off = shot.px(int(rx - 45.0 + cell * 4.5), yrow)   # the fifth
        # WHICH SIDE IS INK: the filled star is a GREEN (the fill colour, whose
        # G dominates), the empty one the neutral track grey - so the filled
        # star is not "greener", it is a different HUE. Measured: (76,166,89)
        # against (199,199,209).
        self.check("rating-reads-as-whole-stars",
                   ink[1] > ink[0] + 40 and ink[1] > ink[2] + 40
                   and off[2] > off[1],
                   "a rating of 3.6 out of five: the FOURTH star's centre is "
                   "%s and the fifth's is %s - four whole stars should be "
                   "filled and the fifth left empty" % (ink, off))

        # A RELEVANCE BAR FILLS IN WHOLE SEGMENTS and colours them by level: ten
        # segments at 9.0 with critical at 8, so nine are RED and the tenth
        # keeps the track grey. Sampling segment CENTRES keeps the aim away from
        # the 1pt gaps between them - and the y is NOT mirrored, because the
        # screenshot is not (see the rating check, which learned it the hard way).
        vx, vy = pts["RELEVANCY"]
        seg = 120.0 / 10.0
        ink = shot.px(int(vx - 60.0 + seg * 8.5), int(vy))
        off = shot.px(int(vx - 60.0 + seg * 9.5), int(vy))
        self.check("relevancy-fills-in-whole-segments",
                   ink[0] > ink[1] + 60 and ink[0] > ink[2] + 60
                   and off[0] == off[1] == 230,
                   "past critical the ninth segment's centre is %s (a red) and "
                   "the tenth's is %s (the track) - whole segments, coloured by "
                   "level" % (ink, off))

        # THE SPINNER'S TWELVE SPOKES have a brightness gradient and the one
        # under the phase is brightest. phase_ only moves on advanceAnimation(),
        # which this board never calls, so it is 0: spoke 0 (due RIGHT of the
        # centre) is the bright one and spoke 11 (330deg) the dim one. A
        # spinner that never repaints still DRAWS, which is what this proves.
        sx, sy = pts["SPINNER"]
        r = 12.0 * 0.6
        bright = shot.px(int(sx + r), int(sy))
        dim = shot.px(int(sx + r * 0.866), int(sy + r * 0.5))
        # THE PHASE-INDEPENDENT PROPERTY, and all twelve spokes on purpose: a
        # spinner is a brightness gradient around the circle, so the brightest
        # and the dimmest spoke differ whatever phase it happens to be in. The
        # last two runs asserted on two points and could only say pass or fail;
        # twelve say WHERE, which is what they needed.
        sx, sy = pts["SPINNER"]
        SPOKE = [(1.0, 0.0), (0.866, 0.5), (0.5, 0.866), (0.0, 1.0),
                 (-0.5, 0.866), (-0.866, 0.5), (-1.0, 0.0), (-0.866, -0.5),
                 (-0.5, -0.866), (0.0, -1.0), (0.5, -0.866), (0.866, -0.5)]
        lit = []
        for dx, dy in SPOKE:
            lit.append(shot.px(int(sx + 7.2 * dx), int(sy + 7.2 * dy))[1])
        # also say what the frame's own middle reads, so a sample that has
        # missed the control entirely is obvious at a glance
        mid = shot.px(int(sx), int(sy))
        # AND THE BRIGHTEST SPOKE'S WHOLE COLOUR, picked by argmax so this stays
        # phase-independent: a component over 1 must SATURATE. Before the clamp
        # in Color::rgb this read (255, 255, 12) - a wrapped blue - where white
        # was meant, and it reads (255, 255, 255) after.
        bx, by = SPOKE[lit.index(max(lit))]
        top = shot.px(int(sx + 7.2 * bx), int(sy + 7.2 * by))
        self.check("spinner-spokes-vary-around-the-circle",
                   max(lit) > min(lit) + 60 and min(top) > 200,
                   "the spinner's twelve spokes read (green channel, from 0 "
                   "degrees round): %s, the centre reads %s, and the "
                   "brightest spoke's own colour is %s - a brightness gradient "
                   "around the circle, and a component over 1 saturating rather "
                   "than wrapping"
                   % (lit, mid, top))

        # THE SWATCH IS THE COLOUR THAT WAS SET, and activating it changes
        # exactly one other thing: the border. Both are read, because either
        # alone could pass by accident - a well drawing nothing but its border,
        # or one that never activates.
        wx, wy = pts["WELL"]
        swatch = shot.px(int(wx), int(wy))
        rim_before = shot.px(int(wx), int(wy) - 11)
        self.check("well-swatch-is-the-colour",
                   abs(swatch[0] - 230) < 8 and abs(swatch[1] - 89) < 8
                   and abs(swatch[2] - 64) < 8,
                   "the well was set to rgb(0.90, 0.35, 0.25) = (230, 89, 64) "
                   "and its swatch reads %s (the border above it reads %s)"
                   % (swatch, rim_before))

        mon.click_at(int(wx), int(screen_h - wy), settle=0.8)
        session.wait_for(r"ZOO-WELL", 15)
        out = session.output_since(mark)
        rim_after = shot.px(int(wx), int(wy) - 11)
        # THEN ACTIVATE IT: a click must report active=1 and darken the border.
        # The border is re-shot AFTER the click - the screenshot above is from
        # before it, and a stale one would read the same on both sides.
        mon.click_at(int(wx), int(screen_h - wy), settle=0.8)
        session.wait_for(r"ZOO-WELL", 15)
        out = session.output_since(mark)
        rim_after = session.shot("well-active").px(int(wx), int(wy) - 11)
        self.check("well-activates-and-says-so",
                   "active=1" in out and rim_after[0] < rim_before[0] - 50,
                   "clicking the well should report active=1 and darken its "
                   "border: the log tail is %s and the border went %s -> %s"
                   % ([l for l in out.splitlines() if l.startswith("ZOO-WELL")][-2:],
                      rim_before, rim_after))

        # AN INDETERMINATE BAR SAYS "WORKING", NOT "HOW FAR" - so it shows its
        # stripe even at value ZERO, where a determinate bar draws nothing but
        # track. That zero is what makes this checkable at all.
        ix, iy = pts["INDET"]
        stripe = shot.px(int(ix - 80 + 24), int(iy))    # inside the 30% stripe
        bare = shot.px(int(ix + 60), int(iy))           # past it: plain track
        self.check("indeterminate-shows-its-stripe-at-zero",
                   stripe[2] > stripe[0] + 60 and bare[2] < bare[0] + 30,
                   "at value 0 the indeterminate bar reads %s inside its stripe "
                   "and %s past it - a determinate bar would be track the whole "
                   "way" % (stripe, bare))
