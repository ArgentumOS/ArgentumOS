"""U4: the value controls — a slider, a stepper, a progress bar and a
level indicator.

The interactive two are DRIVEN: the slider's knob is dragged to a known
fraction of its track and the value it reports is checked against the
position, and the stepper's two halves are clicked — and then HELD DOWN,
because a stepper that repeats while pressed is the difference between one
step and nine. The other two are
LOOKED AT: a determinate progress bar must be filled for exactly its
fraction of the width, and the level indicator past its critical threshold
must be drawn in the critical colour — which is the only way to see that a
threshold does anything.

Coordinates come from the board's log, and the monitor's Y is mirrored
(screen_h - y), as every pointer case here must do.
"""

import re
import time

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
        session.run("export ARGENTUM_HITLOG=1")   # the hit-test instrument
        session.run("export ARGENTUM_KEYLOG=1")   # the pop-up and panel ones
        session.run("export ARGENTUM_HITLOG=1")
        # the damage model's cost, which is what the next U5 item (narrowing
        # the paint) has to move: every paint prints the views it walked and
        # the size of the damage it was given
        session.run("export ARGENTUM_PAINT_MS=1")
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
        paint = re.findall(r"ARGENTUM-PAINT paint=\S+ flush=\S+ ms \S+ "
                           r"views=(\d+) dmg=(\d+)x(\d+)", out)
        win = re.search(r"ARGENTUM-PAINT .* (\d+)x(\d+) views=", out)
        self.note("paint cost on this board: %d paints so far, views=%s, "
                  "dmg=%s, window=%s"
                  % (len(paint), sorted({int(v) for v, _, _ in paint}),
                     sorted({(int(w), int(h)) for _, w, h in paint}),
                     win.groups() if win else "?"))
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
        # 10 steps of 10 points, SLOWLY. This used to say "a slider repaint is
        # a full board repaint (the coarse damage model), so a fast drag
        # overruns the guest's input queue and it DROPS motion": the repaint is
        # NOT the reason. Measured with ARGENTUM_PAINT_MS=1, a step walks 3
        # views, damages 240x24 and costs 10ms - the paint is already
        # damage-limited, and the check below keeps it that way. Motion does
        # still drop at this rate; why is not this case's question. dt=0.25
        # keeps the client inside its own drain rate.
        for i in range(1, 11):
            mon.goto(*at(sx - 120 + 10 * i, sy), dt=0.25)
        mon.release(settle=0.8)
        session.wait_for(r"ZOO-SLIDE", 30)
        out2 = session.output_since(mark)
        slides = re.findall(r"ZOO-SLIDE ([\d.]+)", out2)
        self.check("slider-reports-its-value", len(slides) > 1,
                   "a continuous drag sent %d action(s), the last reading %s"
                   % (len(slides), slides[-1] if slides else None))

        # ---- THE DAMAGE PRUNING, as a property of the LIBRARY -------------
        # A control that says WHAT changed must not cost a tree walk: the paint
        # returns before descending into any subtree that cannot touch the
        # damage. Stated over the paints this drag produced, so it does not
        # depend on how many steps arrived - for every paint whose damage is
        # small, the walk must be small too. A whole-tree repaint for one
        # control's step fails this (measured: 52 views for the full frame
        # against 3 for a step).
        #
        # The line grew a "rects=" field when the damage became a REGION (a
        # list of rects painted one at a time, rather than one union that
        # covers everything between them), and this regex has to carry it:
        # the day it was added, this check failed with "0 of 0 paints" - which
        # is the check doing its job, on the telemetry instead of the paint.
        p2 = re.findall(r"ARGENTUM-PAINT paint=\S+ flush=\S+ ms \S+ "
                        r"views=(\d+).*?dmg=(\d+)x(\d+)", out2)
        sizes = [(int(a), int(b))
                 for a, b in re.findall(r" ms (\d+)x(\d+) views=", out2)]
        board = max(sizes, key=lambda s: s[0] * s[1]) if sizes else (0, 0)
        area = board[0] * board[1]
        small = [int(v) for v, w, h in p2 if int(w) * int(h) * 4 < area]
        self.check("a-small-damage-walks-a-small-part-of-the-tree",
                   len(small) >= 8 and max(small) <= 12,
                   "on a %dx%d board, %d of %d paints have damage under a "
                   "quarter of it and their walks are %s (the full frame walks "
                   "the tree; a control must not)"
                   % (board[0], board[1], len(small), len(p2),
                      sorted(set(small))))
        if slides:
            seen = [float(v) for v in slides]
            # The input path DROPS motion during a drag (the guest's queue is
            # small; the repaint is NOT the cost - see the check above), so the
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

        # ---- the stepper, HELD DOWN -------------------------------------
        # NSStepper.autorepeat defaults to true: "the first mouse down does one
        # increment ... and, after a delay of 0.5 seconds, increments at a rate
        # of ten times per second". So the PRESS steps (which is why the clicks
        # above still step exactly once), a hold under half a second is still
        # one step, and a hold past it keeps stepping.
        def steps_since(t):
            return [float(v) for v in
                    re.findall(r"ZOO-STEP ([\d.]+)", session.output_since(t))]

        t5 = len(session.log_text())
        mon.goto(*at(tx, ty - 8))
        mon.press(settle=0.3)		# held 0.3s: under the 0.5s delay
        short = steps_since(t5)
        mon.release()
        self.check("a-short-hold-is-one-step", short == [1.0],
                   "holding the upper arrow for 0.3s produced %s; the repeat "
                   "waits half a second, so it must be exactly one step"
                   % (short or "nothing"))

        t6 = len(session.log_text())
        mon.goto(*at(tx, ty - 8))
        mon.press(settle=0.3)
        time.sleep(1.0)			# ~1.3s held in total
        mon.release()
        long_hold = steps_since(t6)
        # one on the press + about (1.3 - 0.5) / 0.1 = 8 more. The floor is what
        # separates "repeats" from "one step"; the ceiling catches a runaway.
        self.check("holding-the-arrow-repeats", 4 <= len(long_hold) <= 15,
                   "a ~1.3s hold stepped %d times (%s); one on the press and "
                   "then ten a second after 0.5s is about 9"
                   % (len(long_hold), long_hold))

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

        # THE ARROWS STEP THE DATE, verified arithmetically rather than by
        # reading the field: two clicks on the upper arrow must differ by
        # exactly one day, and UTC is what makes that exact - no DST, no
        # timezone data, a day is 86400 seconds.
        px_, py_ = pts["DATEPICKER"]
        arrow_x = int(px_ + 75)
        up_y = int(screen_h - (py_ - 6))
        t0 = len(session.log_text())
        mon.click_at(arrow_x, up_y, settle=0.6)
        session.wait_for(r"ZOO-DATE", 15)
        mon.click_at(arrow_x, up_y, settle=0.6)
        session.wait_for(r"ZOO-DATE", 15)
        epochs = [int(m.group(1))
                  for m in re.finditer(r"ZOO-DATE epoch=(-?\d+)",
                                       session.output_since(t0))]
        self.check("date-up-arrow-steps-a-day",
                   len(epochs) >= 2 and epochs[-1] - epochs[-2] == 86400,
                   "two clicks on the upper arrow gave epochs %s; a day in UTC "
                   "is exactly 86400 seconds" % epochs)

        # AND THE FIELD IS NOT A BUTTON: clicking the date text must send
        # nothing. That is the invisible-sensitive-area rule again, in a
        # control that legitimately has two live regions and one dead one - the
        # same check the circular button needed.
        time.sleep(1.0)		# let any trailing line land before the mark
        t1 = len(session.log_text())
        mon.click_at(int(px_ - 40), int(screen_h - py_), settle=0.8)
        # BARBED, like every other negative check here: click the ARROW after
        # the field, so waiting for its line proves the app was processing
        # input throughout and the COUNT proves the field click added nothing.
        # Without the barrier this check cannot tell a dead field from a log
        # slice that is still catching up.
        mon.click_at(arrow_x, up_y, settle=0.8)
        session.wait_for(r"ZOO-DATE", 15)
        tail = session.output_since(t1).splitlines()
        sent = [l for l in tail if l.startswith("ZOO-DATE")]
        # THE WHOLE RUN, not the slice: this check makes 2 clicks and the step
        # check before it made 2, so the control should have been asked about 4
        # points. Printing all of them says WHICH clicks it saw.
        asked = [l for l in session.log_text().splitlines()
                 if l.startswith("ARGENTUM-DATE")]
        # THE COUNT is the assertion - the barrier proves the app is live and
        # the count proves the field click added nothing. The ARGENTUM-DATE
        # lines go in the MESSAGE as diagnostics, not in the condition: the
        # slice contains the barrier's own line (p=160, an ARROW), so demanding
        # a FIELD verdict here would fail a correct control.
        # THE COUNT IS THE ASSERTION, and the instrument is what shows it is a
        # real one: the whole run asked mouseDown about FOUR points - the two
        # arrow clicks of the step check, this check's field click, and its
        # barrier - and the field click came back FIELD, at p=45.0,11.0 against
        # bounds 0,0 170x24. So the control refuses the field and the barrier
        # proves the app was live while it did.
        self.check("date-field-is-not-a-button", len(sent) == 1,
                   "%d ZOO-DATE lines in the slice (expected 1); the whole run "
                   "asked mouseDown about %d points: %s"
                   % (len(sent), len(asked), asked))

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

        # THE WELL ACTIVATES THE SHARED COLOUR PANEL, and a swatch picked there
        # reaches the same place a click on the well does: the same round trip
        # the menu family proved, with a panel instead of a menu. The panel is
        # TITLED (Cocoa's NSColorPanel has a title bar), so its swatches sit
        # below the chrome.
        t4 = len(session.log_text())
        mon.click_at(int(wx), int(screen_h - wy), settle=0.8)
        session.wait_for(r"ARGENTUM-PANEL", 15)
        # THE WHOLE LOG, not the slice: the well was already clicked by its own
        # earlier checks, so the panel may already be up and its line may
        # predate this click. orderFront is idempotent, which is the point.
        pn = re.search(r"ARGENTUM-PANEL x=(-?[\d.]+) y=(-?[\d.]+) "
                       r"([\d.]+)x([\d.]+)", session.log_text())
        self.check("the-well-opens-the-colour-panel", pn is not None,
                   "clicking the well opened no panel: no ARGENTUM-PANEL line "
                   "in that click's output. tail: %s" % " | ".join(session.tail(3)))
        if pn:
            px_, py_ = float(pn.group(1)), float(pn.group(2))
            t5 = len(session.log_text())
            mon.click_at(int(px_ + 13), int(screen_h - (py_ + 22 + 13)),
                         settle=0.8)
            # WAIT FOR A NEW LINE, not for the pattern: ZOO-WELL is already in
            # the log from the well's own clicks, so wait_for() would return at
            # once and the case would end before the pick was even flushed.
            picked5 = []
            for _ in range(30):
                picked5 = [l for l in session.output_since(t5).splitlines()
                           if l.startswith("ZOO-WELL")]
                if picked5:
                    break
                time.sleep(0.5)
            self.check("a-swatch-pick-reaches-the-well", bool(picked5),
                       "picking a swatch in the panel sent nothing: %s" % picked5)

            # AND THE PALETTE CAN BE CLOSED. Its close box is at the TOP-RIGHT
            # of the chrome (closeBoxRect: wPt - bs - 6). The box only SETS a
            # flag; the loop performs it and tells the panel, which takes
            # itself down. Both halves are asserted: the panel HANDLED it, and
            # its pixels left the screen.
            # THE PANEL KEEPS ITS FRAME. Cocoa's orderFront: takes no position,
            # so a panel is not pinned under the control that opened it - and
            # once a person drags it, that is where it belongs. Dragging it
            # here is what makes the reopen below DISCRIMINATING: the old
            # behaviour would put it back under the well, ~60x48 away.
            pwd = float(pn.group(3))
            # LEFT and down: the panel is first placed at the screen's top
            # right, so dragging it RIGHT pushes its close box off the edge and
            # the close below has nothing to hit.
            dx, dy = -60, 48
            mon.drag(int(px_ + pwd / 2), int(screen_h - (py_ + 11)),
                     int(px_ + pwd / 2) + dx, int(screen_h - (py_ + 11 + dy)),
                     steps=10, settle=0.6)
            mx, my = px_ + dx, py_ + dy

            before_close = session.shot("panel-before-close")
            t6 = len(session.log_text())
            mon.click_at(int(mx + pwd - 12), int(screen_h - (my + 11)),
                         settle=0.8)
            closed = []
            for _ in range(30):
                closed = [l for l in session.output_since(t6).splitlines()
                          if l.startswith("ARGENTUM-PANELCLOSE")]
                if closed:
                    break
                time.sleep(0.5)
            self.check("the-palette-handles-its-close-box", bool(closed),
                       "clicking the palette's close box should take it down; "
                       "the log says %s" % closed)
            after_close = session.shot("panel-after-close")
            gone = before_close.diff_box(after_close,
                                         (int(px_), int(py_),
                                          int(px_ + float(pn.group(3))),
                                          int(py_ + float(pn.group(4)))))
            self.check("the-palette-is-gone-from-the-screen", gone > 100,
                       "the palette's area should return to the board behind "
                       "it; only %d pixels changed" % gone)

            # AND IT COMES BACK. The panel RELEASES its window on close, so a
            # fresh orderFront() is not a no-op against a stale one - which is
            # what "hidden but still held" would look like.
            t7 = len(session.log_text())
            mon.click_at(int(wx), int(screen_h - wy), settle=0.8)
            reopened = []
            for _ in range(30):
                reopened = [l for l in session.output_since(t7).splitlines()
                            if l.startswith("ARGENTUM-PANEL ")]
                if reopened:
                    break
                time.sleep(0.5)
            again = re.search(r"ARGENTUM-PANEL x=(\d+) y=(\d+)", " ".join(reopened))
            self.check("the-palette-reopens-where-it-was-left",
                       again is not None
                       and abs(int(again.group(1)) - mx) <= 3
                       and abs(int(again.group(2)) - my) <= 3,
                       "a panel keeps the frame it was dragged to: it was moved "
                       "to (%d,%d) and should reopen there (at most 3pt out), "
                       "not under the well at (%d,%d); the log says %s"
                       % (mx, my, int(px_), int(py_), reopened))
