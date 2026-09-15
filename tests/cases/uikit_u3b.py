"""U3b: the text views on the zoo board — the stack, on screen.

The board carries a label (a text field with no bezel), a field showing its
placeholder, a label too long for its frame, and a wrapped text view. This
case reads what the LAYOUT produced out of the board's own log, and then
asks the framebuffer whether glyphs are really there.

Two lessons are baked in: an interaction's log is read after the layout
answered (a field's cell sizes its container as it draws), and a text check
counts the DARK pixels inside a box whose background is light — asking for
ink() inside a mid-tone box counts the box.
"""

import re

from harness import BaseCase

ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit U3b: the text views draw through the stack"
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
        session.run("%s 45 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "no ZOO-READY; guest tail: " + session.tail())
            return
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith(("ZOO-TEXT", "ZOO-SHOWN", "ZOO-AT")):
                self.note(line)
        self.check("board-up", True, "the board with the text controls is up")

        # the LAYOUT's own answers: a label is one line, the wrapped view is
        # more than one
        label = re.search(r"ZOO-TEXT LABEL lines=(\d+)", out)
        view = re.search(r"ZOO-TEXT VIEW lines=(\d+)", out)
        self.check("label-is-one-line",
                   label is not None and label.group(1) == "1",
                   "the label laid out to %s line(s)"
                   % (label.group(1) if label else "?"))
        self.check("text-view-wraps",
                   view is not None and int(view.group(1)) >= 2,
                   "the text view wrapped the prose to %s lines"
                   % (view.group(1) if view else "?"))

        # the truncation the STACK computed is what the field shows
        shown = re.search(r"ZOO-SHOWN (.+)", out)
        self.check("long-label-truncates",
                   shown is not None and "\u2026" in shown.group(1),
                   "the too-long label shows \"%s\""
                   % (shown.group(1) if shown else "?"))

        # and the glyphs are really on the screen
        sh = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", out)
        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)", out)}
        if sh is None or "TEXTVIEW" not in pts:
            self.check("geometry-parsed", False, "no screen/coordinate log")
            return
        self.check("geometry-parsed", True, "screen %s, text view located"
                   % sh.group(1))

        shot = session.shot("u3b")
        self.check("shot-taken", shot is not None, "a framebuffer dump")
        if shot is None:
            return
        wx, wy = 70, 50
        vx, vy = pts["TEXTVIEW"]
        # a box well inside the text view's white background
        box = (int(vx - 100), int(vy - 20), int(vx + 100), int(vy + 20))
        # the text is DARK on that white background: ink() is the right
        # question here (the tile lesson was the other way round)
        ink = shot.ink(box, thresh=120)
        area = (box[2] - box[0]) * (box[3] - box[1])
        self.check("glyphs-on-screen", ink > 0,
                   "%d dark pixel(s) inside the text view: the stack's text "
                   "reached the framebuffer" % ink)
        self.check("background-not-filled-with-ink", ink < area * 0.5,
                   "%d of %d pixels are glyphs (%.1f%%), so the view is not "
                   "a dark rectangle" % (ink, area, 100.0 * ink / area))

        # the placeholder field: grey text on white, drawn although the
        # field's string is empty
        ph = pts.get("PLACEHOLDER")
        if ph is not None:
            pbox = (int(ph[0] - 100), int(ph[1] - 8), int(ph[0] - 10),
                    int(ph[1] + 8))
            pink = shot.ink(pbox, thresh=200)
            self.check("placeholder-drawn", pink > 0,
                       "%d pixel(s) of placeholder text in an empty field"
                       % pink)
