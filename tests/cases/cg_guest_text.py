# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Core Graphics AND text draw one picture on the guest's framebuffer.

The demonstration behind this case is `/System/Shared/tests/cgtext_demo`: a bitmap context of the
whole screen, filled, given a path with a curve, a linear gradient, a stroke, solid swatches and
three runs of TEXT drawn through `CGContextShowGlyphsAtPositions` — the one text door this tree has —
laid out with the advances `CGFontGetGlyphAdvances` reports. The surface is blitted to `/dev/fb0`
through its mmap, so a screendump is the evidence.

WHY A PICTURE AND NOT ONLY CHECKS: the host-side text probe passed every check while drawing every
glyph UPSIDE DOWN, because the check it had measured a property a mirror preserves. This case is the
guest's version of that lesson — it asserts pixel FACTS in the screenshot (a blue header band with
light glyphs in it, a red swatch that is red-dominant, a uniform backdrop) rather than asking the
demonstration to testify about itself.

The demo prints its marker BEFORE the blit and then sleeps, because the kernel's console owns the
same framebuffer: a marker printed after the blit would draw over the picture, and the sleep is the
window the screendump needs.
"""

import re

from harness import BaseCase

MODE = r"CGTEXT-DEMO: framebuffer (\d+)x(\d+) (\d+)bpp pitch (\d+)"
INK = r"CGTEXT-DEMO: (\d+) inked pixel\(s\) of (\d+)"

CARD_W, CARD_H = 900, 520


class Case(BaseCase):
    title = "Core Graphics and text paint the guest's screen"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("cgtext_demo")
        session = ctx.boot(name="cgtext")
        self.check("guest-up", session.wait_for(r"INIT: FNX userland alive", 150),
                   "the guest reached userland")

        session.run("/System/Shared/tests/cgtext_demo 25", marker=r"CGTEXT-DEMO-READY", secs=120)
        log = session.log_text()
        self.check("the demo reached its blit", "CGTEXT-DEMO-READY" in log)

        m = re.search(MODE, log)
        if m is None:
            self.check("the framebuffer reported its mode", False, "no mode line")
            session.stop()
            return
        w, h, bpp = int(m.group(1)), int(m.group(2)), int(m.group(3))
        self.note("framebuffer %dx%d %dbpp pitch %s" % (w, h, bpp, m.group(4)))
        self.check("the framebuffer is 32bpp, which is what this library draws", bpp == 32)

        ink = re.search(INK, log)
        self.check("the library painted a picture", ink is not None and int(ink.group(1)) > 20000,
                   "inked pixels: %s" % (ink.group(1) if ink else "none reported"))

        shot = session.shot("cgtext")
        self.note("screenshot: %dx%d" % (shot.w, shot.h))

        # THE CARD IS CENTRED, and the mapping is the one thing worth stating: the CG surface's y
        # runs UP and the screendump's rows run DOWN, so a CG y is row (h - 1 - y).
        x0 = (w - CARD_W) // 2
        y0 = (h - CARD_H) // 2

        def row(cg_y):
            return h - 1 - cg_y

        # 1. THE BACKDROP OUTSIDE THE CARD is the flat grey the demo filled the screen with.
        bg = shot.px(4, 4)
        self.check("the backdrop is the flat grey the surface was filled with",
                   bg[0] > 200 and abs(bg[0] - bg[1]) < 12 and abs(bg[1] - bg[2]) < 12,
                   "corner pixel %r" % (bg,))

        # 2. THE HEADER BAND is dark blue, and the WHITE TITLE is inside it — the text door's ink,
        #    measured as light pixels in a band that is otherwise dark.
        header = (x0 + 10, row(y0 + CARD_H - 20), x0 + CARD_W - 10, row(y0 + CARD_H - 66))
        light = shot.light_frac(header, thresh=200)
        mean = shot.mean_luma(header)
        self.check("the title band is dark blue with light glyphs in it",
                   light > 0.02 and mean < 120,
                   "light fraction %.3f, mean luma %.0f" % (light, mean))

        # 3. THE RED SWATCH IS RED-DOMINANT, which no greyscale accident can satisfy.
        red = shot.px(x0 + 380, row(y0 + 100))
        self.check("the red swatch is red-dominant", red[0] > red[1] + 40 and red[0] > red[2] + 40,
                   "pixel %r" % (red,))

        # 4. AND THE CARD ITSELF IS NOT THE BACKDROP: the panel is a different colour from the room.
        card = shot.px(x0 + 40, row(y0 + 200))
        self.check("the card is painted, not left as backdrop", card != bg, "pixel %r" % (card,))

        self.note("screenshot kept at %s" % self.artifact("cgtext-1.ppm"))
        session.stop()
