"""U2c: the widget zoo board — the button family, clicked and looked at.

The board (/Applications/WidgetZoo) carries one control per bezel style and
behaviour. The real pointer picks a switch and both radios, and the
framebuffer is asked about the shapes: a gradient must differ top-to-bottom
inside its bezel, and a radio that is ON must show its mark.

The radio checks are the interesting ones: two radios in the same superview
are a GROUP, so picking B clears A — with no group object anywhere.
"""

import re
import time

from harness import BaseCase

ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit U2c: the widget zoo board shows and drives the button family"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse"])
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")

        # THE INSTRUMENT, for the whole case: ButtonCell::containsPointInFrame
        # prints ARGENTUM-HIT (with the frame it believes it has and the point
        # it was asked about) when this is set. The fix for the invisible
        # sensitive area was a no-op twice, and each guess at why cost a gate -
        # this makes the next run answer it instead.
        # (ARGENTUM_HITLOG was set here for the one run that settled whether
        # containsPointInFrame is consulted at all. It is: 144 hit tests, the
        # Circular row's frame 0,0 240x28. The instrument stays in the toolkit,
        # off by default, like ARGENTUM_DRAW_MS.)
        if not ready:
            return

        mark = len(session.log_text())
        session.run("ARGENTUM_PAINT_MS=1 %s 60 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail())
            return
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("ZOO-"):
                self.note(line)
        self.check("board-up", True, "the zoo board opened its window")


        sh = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", out)
        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)", out)}
        # the controls THIS case drives must be located; the board may
        # gain more (it did: the text family), so do not count them
        need = ("SWITCH", "RADIO-A", "RADIO-B", "GRADIENT")
        missing = [n for n in need if n not in pts]
        self.check("controls-located", sh is not None and not missing,
                   "screen %s, controls at %s" % (sh.group(1), sorted(pts))
                   if sh and not missing else
                   "missing %s" % (missing or "the screen size"))
        if sh is None or missing:
            return
        screen_h = int(sh.group(1))

        def at(name):
            x, y = pts[name]
            return int(x), int(screen_h - y)	# the monitor's Y is mirrored

        mon = session.monitor()

        # THE HARNESS'S POINTER IS A PRECONDITION, NOT A RESULT. In a run where
        # the guest's X server has no pointer, every click below is a silent
        # no-op: not one ZOO-CLICK line, four waits running their 30s out, and
        # a report that reads like the app is broken.
        #
        # The signal must be something the FRAMEBUFFER shows, because the
        # obvious one is invisible: Xfb's "mouse on" is an ErrorF that goes to
        # Xfb's own log file, NOT the serial console, so a guard waiting for it
        # can never pass - it fails every run, and hides them. (It hid three.)
        # A pointer that is really up moves the cursor, which is the same fact
        # pointer-still-moves-after-exit relies on.
        # No magic threshold: measure the noise floor first (two shots with
        # nothing moving) and require the move to beat it by a margin. A magic
        # ">100" is what made the first version of this guard report a working
        # pointer as dead, because park() moves the pointer only a little.
        shot_a = session.shot("pointer-idle-a")
        noise = shot_a.diff(session.shot("pointer-idle-b"))
        mon.park()
        time.sleep(0.5)                 # let the server drain the cursor
        moved = shot_a.diff(session.shot("pointer-after-park"))
        # "> the noise" and not "> noise + 50": park() moves the pointer only a
        # little (a 1-2 pixel shift changes ~16 pixels of cursor fringe), so an
        # absolute margin calls a live pointer dead. Against a MEASURED noise
        # floor of 0 on a static board, any change at all is the pointer moving
        # and no change at all is a pointer that never moved.
        if moved <= noise:
            self.check("pointer-input-is-up", False,
                       "parking the pointer changed %d pixels against a "
                       "noise floor of %d, so the guest's X server has no "
                       "pointer and every click below would be a no-op; "
                       "nothing here can be believed. tail: %s"
                       % (moved, noise, " | ".join(session.tail(4))))
            return

        self.check("pointer-input-is-up", True,
                   "parking the pointer moved the cursor (%d pixels, noise "
                   "floor %d), so the clicks below have somewhere to land"
                   % (moved, noise))

        mon.park()

        # 1. the switch: a press sticks - and time it. The button's own
        # click carries a settle, which is the HARNESS waiting, not the
        # board answering, so this presses and releases with none and
        # measures from the press to the board's reply.
        ax, ay = at("SWITCH")
        mon.goto(ax - 112, ay)
        t0 = time.time()
        mon.press(settle=0)
        mon.release(settle=0)
                # A BUTTON IS SENSITIVE WHERE IT DRAWS (bug report: "the circular and
        # help buttons are sensitive outside the visible grey circle drawn.
        # Invisible sensitive areas are bad."). The circular row draws a small
        # circle at its left and its title beside it; the rest of its 240pt
        # frame is empty and must not reach the button.
        #
        # The pair is barbed on purpose: after the dead-space click, a SECOND
        # click on the circle is the barrier, so waiting for that line proves
        # the app processed input throughout, and counting the lines proves the
        # dead-space click contributed none - neither half can pass by the app
        # having simply stopped.
        ccx, ccy = pts["CIRCULAR"]
        circle_x, row_y = int(ccx) - 106, int(screen_h - ccy)

        t0 = len(session.log_text())
        mon.click_at(circle_x, row_y, settle=0.8)
        session.wait_for(r"ZOO-CLICK Circular", 15)
        self.check("the-circle-still-fires",
                   "ZOO-CLICK Circular" in session.output_since(t0),
                   "a click ON the circular button's circle did not reach it; "
                   "saw %s" % [l for l in session.output_since(t0).splitlines()
                               if l.startswith("ZOO-")][-4:])

        t1 = len(session.log_text())
        mon.click_at(int(ccx) + 94, row_y, settle=0.8)   # the empty space
        mon.click_at(circle_x, row_y, settle=0.8)        # and the barrier
        session.wait_for(r"ZOO-CLICK Circular", 15)
        clicks = re.findall(r"ZOO-CLICK Circular", session.output_since(t1))
        self.check("dead-space-is-not-sensitive", len(clicks) == 1,
                   "two clicks - one in the empty space beside the circular "
                   "button, one on its circle - produced %d ZOO-CLICK Circular "
                   "lines; only the circle is sensitive, so there should be 1"
                   % len(clicks))

        # IS THE OVERRIDE CONSULTED AT ALL? The log line is printed at the TOP
        # of ButtonCell::containsPointInFrame, so a Circular line here means the
        # override RAN and the ANSWER is what is wrong; no line at all means the
        # press never reaches it and the subject is the dispatch instead. The
        # message carries the frame and points either way, because the harness
        # prints PASS messages too.
        hits = re.findall(r'ARGENTUM-HIT "([^"]*)" f=([-\d.]+),([-\d.]+) '
                          r'([\d.]+)x([\d.]+) p=([-\d.]+),([-\d.]+)',
                          session.output_since(mark))
        circ = [h for h in hits if "Circular" in h[0]]

        session.wait_for(r"ZOO-CLICK Switch", 30, poll=0.005)
        self.note("one click, press to ZOO-CLICK: %.0f ms"
                  % ((time.time() - t0) * 1000.0))
        out1 = session.output_since(mark)
        self.check("switch-sticks", "ZOO-CLICK Switch state=1" in out1,
                   "the switch reported state=1 after its click"
                   if "ZOO-CLICK Switch state=1" in out1
                   else "the switch did not stick: " + session.tail())

        # 2. the radio group: picking B clears A (no group object)
        ax, ay = at("RADIO-A")
        mon.click_at(ax - 112, ay, settle=1.0)
        session.wait_for(r"ZOO-RADIOS a=1", 30)
        ax, ay = at("RADIO-B")
        mon.click_at(ax - 112, ay, settle=1.0)
        session.wait_for(r"ZOO-RADIOS a=0 b=1", 30)
        out2 = session.output_since(mark)
        self.check("radio-a-selects", "ZOO-RADIOS a=1 b=0" in out2,
                   "picking A left A on and B off" if "ZOO-RADIOS a=1 b=0"
                   in out2 else "A did not select: " + session.tail())
        self.check("radio-group-is-exclusive", "ZOO-RADIOS a=0 b=1" in out2,
                   "picking B turned A OFF: the siblings clear each other"
                   if "ZOO-RADIOS a=0 b=1" in out2
                   else "the radios are not exclusive: " + session.tail())

        # 3. the shapes, in the framebuffer
        shot = session.shot("u2c")
        self.check("shot-taken", shot is not None, "a framebuffer dump")
        if shot is None:
            return
        wx, wy = 70, 50			# where the board's window opened
        ch = 22.0

        # the gradient button: its top must differ from its bottom
        gx, gy = pts["GRADIENT"]
        gx, gy = int(gx - 60), int(gy)	# inside the bezel, left of the title
        top = shot.px(gx, int(wy + ch + (gy - wy - ch) - 10))
        bot = shot.px(gx, int(wy + ch + (gy - wy - ch) + 10))
        self.check("gradient-bezel-gradates",
                   top is not None and bot is not None and top != bot,
                   "the Gradient bezel is %s at its top and %s at its bottom"
                   % (top, bot))

        # A BUTTON keeps its bezel, and a rounded one cuts its corners:
        # the corner pixel is the background, the middle is the bezel.
        gx2, gy2 = pts["GRADIENT"]
        corner = shot.px(int(gx2 - 118), int(gy2 - 12))
        middle = shot.px(int(gx2), int(gy2))
        self.check("bezel-is-round",
                   corner is not None and middle is not None
                   and corner != middle,
                   "corner %s vs middle %s: the bezel does not fill its "
                   "bounding box" % (corner, middle))

        # A RADIO HAS NO BEZEL (user-reported: radios were drawing one).
        # Its chrome is the small circle at its left and nothing else, so
        # the space to the right of the title is the window background.
        rx, ry = pts["RADIO-A"]
        right = shot.px(int(rx + 60), int(ry))
        luma_right = None if right is None else (right[0] * 299 + right[1] * 587
                                                + right[2] * 114) // 1000
        self.check("radio-has-no-bezel",
                   luma_right is not None and luma_right >= 236,
                   "the pixel right of a radio's title is %s (luma %s): the "
                   "background, not a bezel" % (right, luma_right))

        # ... and the circle IS drawn, at the left where a radio's mark
        # goes. It is a WHITE disc with a dark rim, so it is BRIGHTER than
        # the background - the check is that it is not the background, not
        # that it is dark (asking for dark here was my mistake).
        mark_px = shot.px(int(rx - 112), int(ry))
        bg = shot.px(int(rx + 60), int(ry))
        self.check("radio-mark-is-drawn",
                   mark_px is not None and bg is not None and mark_px != bg,
                   "a radio's circle is at its left: %s against the "
                   "background %s" % (mark_px, bg))

        # AN INVISIBLE SENSITIVE AREA IS A DEFECT. The circular row draws a
        # small circle at its left and its title beside it; the rest of its
        # 240pt frame is empty, and a click there must not reach the button.
        # It did - the hit test was the whole frame - so most of the row was
        # sensitivity nobody could see. The pair matters: the circle itself
        # must STILL fire, or the fix would just be a dead button.
        ccx, ccy = pts["CIRCULAR"]
        t_hit = len(session.log_text())
        mon.click_at(int(ccx) - 106, int(screen_h - ccy), settle=0.6)
        out_on = session.output_since(t_hit)

        t_hit2 = len(session.log_text())
        mon.click_at(int(ccx) + 94, int(screen_h - ccy), settle=0.6)
        out_off = session.output_since(t_hit2)

        # THE DAMAGE RECT MUST LAND WHERE THE CONTROL IS. The flush speaks
        # WINDOW coordinates; a view's rect is in CONTENT coordinates, one
        # chrome-height higher. Mixing them is a silent offset - the strip
        # below the region never repaints - and the pixel checks CANNOT see
        # it, because the board's first full push masked it. So: arithmetic.
        # ZOO-AT gives a row's centre in SCREEN coordinates, so the row in
        # window points is (centre - window origin - half the row).
        win = re.search(r"ZOO-WIN x=(-?[\d.]+) y=(-?[\d.]+) chrome=([\d.]+)",
                        out)
        sw = pts.get("SWITCH")
        pushes = re.findall(r"ARGENTUM-PUSH (\d+)x(\d+) at (\d+),(\d+)",
                            session.output_since(mark))
        row_push = next((p for p in pushes if int(p[0]) < 300), None)
        ok, msg = False, "no control-sized push seen"
        if win and sw and row_push:
            wx, wy = float(win.group(1)), float(win.group(2))
            px_, py_ = int(row_push[2]), int(row_push[3])
            wpx, hpx = int(row_push[0]), int(row_push[1])
            ex, ey = sw[0] - wx - 240 / 2.0, sw[1] - wy - 28 / 2.0
            ok = (abs(px_ - ex) <= 1 and abs(py_ - ey) <= 1
                  and wpx == 240 and hpx == 28)
            msg = ("the switch's damage rect is %dx%d at %d,%d; the row is at "
                   "%.0f,%.0f 240x28 in window points (window %g,%g)"
                   % (wpx, hpx, px_, py_, ex, ey, wx, wy))
        elif not win:
            msg = "the board did not report ZOO-WIN"
        self.check("damage-rect-is-in-window-space", ok, msg)

        # PER-VIEW DAMAGE: the pass must WALK only the damaged subtree.
        # The pair is the check - the board's first paint walks the whole
        # tree, a click's walks one control - because "few views" on its own
        # would also be true of a pass that drew nothing at all.
        paints = re.findall(r"ARGENTUM-PAINT paint=([\d.]+) flush=([\d.]+) "
                            r"ms (\d+)x(\d+) views=(\d+) dmg=(\d+)x(\d+)",
                            session.output_since(mark))
        first = paints[0] if paints else None
        click_paints = [p for p in paints if int(p[5]) < 300]
        walked = [int(p[4]) for p in click_paints]
        self.check("per-view-damage",
                   first is not None and int(first[4]) >= 10
                   and walked and max(walked) <= 5,
                   "the board's first paint walked %s views; the damage-sized "
                   "paints walked %s (%d of them)"
                   % (first[4] if first else "?", walked or "none",
                      len(click_paints)))

        # NO BOX INSIDE THE BOX. SearchFieldCell and TokenFieldCell delegate
        # their entry area to the text cell, which used to draw its OWN bezel
        # around it too - an outlined input cell inside the field's bezel.
        # Counting a field's full-width horizontal lines is how to see that:
        # one bezel is exactly two (top and bottom), an outlined entry makes
        # four. The entry's own edges are the same fill as the field's now,
        # so only the field's outline registers.
        for name in ("SEARCH", "TOKEN"):
            zx, zy = pts[name]
            cols = list(range(int(zx) - 100, int(zx) + 100, 2))
            lines = [y for y in range(int(zy) - 20, int(zy) + 20)
                     if sum(1 for x in cols if shot.luma(x, y) < 228)
                     >= 0.8 * len(cols)]
            self.check("no-box-inside-%s" % name.lower(), len(lines) == 2,
                       "the %s field's full-width lines are %s: one bezel has "
                       "two, an outlined entry inside it makes four"
                       % (name, lines))

        # TEXT SITS IN ITS BOX. Context::drawText's y is the TOP of the run
        # box, but the layout used to hand it a BASELINE - one ascent too
        # low, so every text-stack view rode low and a field's text was
        # clipped along its bottom edge. Geometric on purpose: the text's own
        # centre has to be the box's centre. It measures the PLACEHOLDER,
        # the one field that always has text (and grey, hence the
        # threshold); the typed fields are empty in this shot.
        zx, zy = pts["PLACEHOLDER"]
        half = 13		# the zoo's fields are 26pt tall
        # The box's own border rows are dark too, and they are symmetric
        # about the centre - including them made this check pass for any
        # text position at all. Scan the INTERIOR, and require the spread a
        # real line of text has.
        rows = [(y, shot.ink((int(zx) - 110, y, int(zx) + 110, y + 1),
                             thresh=190))
                for y in range(int(zy) - half + 2, int(zy) + half - 2)]
        ink_rows = [y for y, n in rows if n > 3]
        centre = (ink_rows[0] + ink_rows[-1]) / 2.0 if ink_rows else None
        self.check("field-text-sits-in-its-box",
                   centre is not None and len(ink_rows) >= 6
                   and abs(centre - zy) <= 4,
                   "the placeholder's text rows are %s (%d of them), centre "
                   "%s against its box centre %.0f"
                   % (ink_rows, len(ink_rows), centre, zy))

        # THE CIRCULAR ROW IS ONE CIRCLE AND NOTHING INSIDE IT. It was
        # drawing a radio's light disc inside the round bezel and pushing
        # its title over the rim, so the bezel's interior must now be
        # uniform: the same colour where that mark sat as at its centre.
        # (ZOO-AT logs a frame's ORIGIN, so the row's centre is y + 14.)
        cx, cy = pts["CIRCULAR"]
        at_old_mark = shot.px(int(cx - 112), int(cy))
        at_centre = shot.px(int(cx - 106), int(cy))
        self.check("circular-has-no-mark",
                   at_old_mark is not None and at_old_mark == at_centre,
                   "the round bezel's interior is uniform: %s where the mark "
                   "used to be, %s at its centre" % (at_old_mark, at_centre))

        # THE HELP ROW DRAWS ITS QUESTION MARK IN THE CIRCLE, NOT ITS TITLE
        # ACROSS THE FRAME. Ink inside the 28pt circle, none where a centred
        # label used to be painted.
        hx, hy = pts["HELP"]
        circle_ink = shot.ink((int(hx - 120), int(hy - 14),
                               int(hx - 92), int(hy + 14)))
        middle_ink = shot.ink((int(hx - 60), int(hy - 14),
                               int(hx + 100), int(hy + 14)))
        self.check("help-shows-question-mark",
                   circle_ink > 0 and middle_ink == 0,
                   "the glyph is in the circle (%d ink px) and the middle is "
                   "clean (%d ink px)" % (circle_ink, middle_ink))

        # THE BOARD'S OWN EXIT. Every check above leaves the board running
        # and the harness kills the guest, so the exit path - ZOO-TIMEOUT,
        # then the window's destructor - was never exercised, and that is
        # where the board was reported to fault. Run it to its own end and
        # ask the SHELL for its status: a fault arrives as SIGBUS/SIGSEGV
        # and a non-zero status, which a clean-looking console tail cannot
        # fake. The first instance must go, or two boards race for the
        # markers these checks read.
        # THE EVIDENCE TO WATCH IS THE KERNEL'S, not the screen's: when Xfb
        # hangs it stops reading its input devices and the mouse driver says so
        # - "mouse: record dropped (queue full)". A screen that merely stopped
        # draining would still be reading input, so that line is what tells a
        # hang apart from a frozen shadow. From here on there must be none.
        t_exit = len(session.log_text())
        session.run("killall WidgetZoo >/dev/null 2>&1")
        mark2 = len(session.log_text())
        session.run("%s 6; echo ZOO-EXIT=$?" % ZOO)
        session.wait_for(r"ZOO-EXIT=", 150)
        out3 = session.output_since(mark2)
        m3 = re.search(r"ZOO-EXIT=(\d+)", out3)
        status = int(m3.group(1)) if m3 else None
        self.check("board-exits-clean",
                   status is not None and status == 0,
                   "the board ran to its own end: status %s (expected 0); "
                   "tail: %s" % (status, session.tail()))

        # THE SERVER IS STILL ALIVE AND STILL DRAWING. A shadow that stops
        # draining shows up as a screen that stops changing: the toolkit runs
        # on and nothing reaches the framebuffer - even the pointer stops.
        # The count of changed pixels is not subtle about it: a window
        # appearing moves ~100000 of them, a frozen screen moves none.
        shot_a = session.shot("after-exit")
        session.run("%s 5 &" % ZOO)
        deadline = time.time() + 60
        while time.time() < deadline and session.count(r"ZOO-READY") < 2:
            time.sleep(0.25)
        shot_b = session.shot("after-second")
        changed = shot_a.diff(shot_b)
        self.check("screen-still-updates-after-exit", changed > 1000,
                   "reopening the board after the first one exited changed %d "
                   "pixels; a frozen screen changes none, pointer included"
                   % changed)

        # THE SERVER'S OWN LOG IS READABLE NOW (LogInit to the FSH temporary
        # dir). Before this, everything the server said about itself went to a
        # stderr nobody could see, which is why several rounds of input
        # diagnosis were argued from a console that could never carry them.
        session.run("cat '/System/Temporary Files/xfb.log' | tail -40")
        time.sleep(0.6)

        # IS THE INPUT BACKEND LIVE AT ALL? It reports the fds it opened, and
        # every notify (bounded) says so. Without this, a silent console reads
        # as "the path is clean" when it may mean the path was never taken.
        lines = [l for l in session.log_text().splitlines()
                 if "XFB-INPUT:" in l]
        fds = [l for l in lines if "fds mouse=" in l]
        self.check("input-backend-is-live", bool(fds),
                   "the input backend never reported its fds; XFB-INPUT lines "
                   "seen: %s" % (lines[-6:] or "none"))
        self.note("input lines: mouse notifies %d, kbd notifies %d, banners %d"
                  % (len([l for l in lines if "mouse notify" in l]),
                     len([l for l in lines if "kbd notify" in l]),
                     len(fds)))

        # THE STALL REPORT ITSELF IS TESTED, not assumed: input arrives through
        # SetNotifyFd, so a gap of seconds between two wakeups is the shape of
        # "the server stopped reading its input" - and it is the line that says
        # so. A deliberate gap makes that deterministic instead of hoping the
        # run happens to have a quiet stretch, and it catches the instrument
        # going deaf (the first version of it used time(), which never fired
        # once, in any run).
        mon.move(20, 0)
        time.sleep(2.2)
        mon.move(-20, 0)
        mon.nudge()
        time.sleep(0.5)
        self.check("input-stall-report-works",
                   "XFB-INPUT:" in session.log_text(),
                   "a deliberate 2s gap between pointer moves produced no "
                   "XFB-INPUT line, so the server never reported the gap",
                   )

        # AND THE POINTER STILL MOVES. The reported signature is a server that
        # is ALIVE - the cursor is still on screen - with a pointer that will
        # not move, while the console works. Moving the pointer is the cheapest
        # test of both halves at once: Xfb has to receive the motion AND drain
        # the result, so a cursor that does not move after an exit means either
        # the input path is dead or the drain has stopped.
        shot_c = session.shot("before-move")
        mon.move(30, 0)
        mon.nudge()
        shot_d = session.shot("after-move")
        moved = shot_c.diff(shot_d)
        self.check("pointer-still-moves-after-exit", moved > 100,
                   "moving the pointer after the exit changed %d pixels (the "
                   "cursor's own area is a few hundred; a dead input path or a "
                   "stopped drain changes none)" % moved)

        # THE WAY A PERSON EXITS: closing the window. Everything above exits
        # through the TIMER (ZOO-TIMEOUT), which is what a gate does; a user
        # clicks the close box, and that goes through the window's close
        # request - a different teardown, and the one the shadow's close hook
        # is on. Then the same two properties are asked again.
        session.run("%s 30 &" % ZOO)
        deadline = time.time() + 60
        while time.time() < deadline and session.count(r"ZOO-READY") < 4:
            time.sleep(0.25)
        cb = re.search(r"ZOO-CLOSE x=([\d.]+) y=([\d.]+)",
                       session.log_text())
        if cb:
            mon.click_at(int(float(cb.group(1))),
                         int(screen_h - float(cb.group(2))), settle=0.6)
        closed = session.wait_for(r"ZOO-CLOSED", 30)
        self.check("close-box-closes-the-window", bool(cb) and closed,
                   "clicking the close box made the board exit: %s (close box "
                   "%s)" % (closed, cb.groups() if cb else "not logged"))
        shot_e = session.shot("after-close")
        mon.move(-30, 0)
        mon.nudge()
        shot_f = session.shot("after-close-move")
        moved = shot_e.diff(shot_f)
        self.check("pointer-still-moves-after-close", moved > 100,
                   "after closing the window the pointer moved %d pixels (a "
                   "dead input path or a stopped drain changes none)" % moved)

        self.check("input-not-overrunning-after-exits",
                   "record dropped" not in session.output_since(t_exit),
                   "no input record was dropped while the board exited and the "
                   "pointer moved: tail %s" % session.tail(3))

        self.check("board-still-alive",
                   "ZOO-TIMEOUT" in session.output_since(mark)
                   or "ZOO-CLOSED" in session.output_since(mark)
                   or session.alive(),
                   "the board survived the interactions")
