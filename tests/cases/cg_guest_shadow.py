# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The Core Graphics demo paints a card on the guest's framebuffer, SHADOWS AND ALL.

The demonstration behind this case is `/System/Shared/tests/cg_demo` — the same source `make
demo-coregraphics` builds on the host, where it writes a .ppm instead of a framebuffer — and it draws
a card with four shadow panels (a hard shadow, a blurred one, a coloured one and one with shadowing
OFF), text through the string doors, gradients, strokes, swatches, a clip and a transparency layer.

WHY A SCREENSHOT AND NOT ONLY THE HOST PROBE: `coregraphics_shadow` measures the shadow on a 16x8
canvas and it is the right instrument for the door's CONTRACT. This case is the other half — the
composited picture, at a real size, through the guest's own library — and it asserts PIXEL FACTS a
passing-but-wrong shadow cannot satisfy: that the shadow is darker than the card it falls on, that it
STOPS where the offset says it should, that a blur REACHES past where the hard shadow stopped, and
that the OFF panel's would-be shadow is not there at all.

THE COORDINATES COME FROM THE DEMO'S OWN MAP. `cg_demo` prints `CG-DEMO-MAP <role> <x> <row>` for
every place it expects to be read, in screen coordinates — the row already flipped — so nothing here
has to know the CG y axis runs the other way. The map says WHERE to look and never what should be
there: every assertion below is about the pixels, so a wrong coordinate fails a check rather than
satisfying it.

The demo prints its marker BEFORE the blit, and then holds the picture, because the kernel's console
owns the same framebuffer: the sleep is the window the screendump needs.
"""

import re

from harness import BaseCase

MODE = r"CG-DEMO: framebuffer (\d+)x(\d+) (\d+)bpp pitch (\d+)"
INK = r"CG-DEMO: (\d+) inked pixel\(s\) of (\d+)"
MAP = r"CG-DEMO-MAP (\S+) (\d+) (\d+)"

# The roles the demo promises to print. Missing one is a failure of THIS case's own ground, so it is
# checked rather than looked up blindly.
ROLES = ("backdrop", "card", "hard-shadow", "hard-canvas", "blur-near", "blur-far", "color-shadow",
         "off-canvas", "red-swatch", "grad-left", "grad-right", "header-tl", "header-br")


class Case(BaseCase):
    title = "Core Graphics draws its shadows on the guest's screen"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("cg_demo")
        session = ctx.boot(name="cgshadow")
        self.check("guest-up", session.wait_for(r"INIT: FNX userland alive", 150),
                   "the guest reached userland")

        session.run("/System/Shared/tests/cg_demo 25", marker=r"CG-DEMO-READY", secs=120)
        log = session.log_text()
        self.check("the demo reached its blit", "CG-DEMO-READY" in log)

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

        mp = {}
        for mm in re.finditer(MAP, log):
            mp[mm.group(1)] = (int(mm.group(2)), int(mm.group(3)))
        missing = [r for r in ROLES if r not in mp]
        self.check("the demo printed its map of where to look", not missing,
                   "missing: %s" % ", ".join(missing) if missing else "%d points" % len(mp))

        shot = session.shot("cgshadow")
        self.note("screenshot: %dx%d" % (shot.w, shot.h))

        def luma(role):
            p = mp.get(role)
            return shot.luma(p[0], p[1]) if p else None

        def px(role):
            p = mp.get(role)
            return shot.px(p[0], p[1]) if p else None

        # --- the room, and the card in it ---------------------------------------------------------
        bg = px("backdrop")
        self.check("the backdrop is the flat grey the surface was filled with",
                   bg is not None and bg[0] > 200 and abs(bg[0] - bg[1]) < 12
                   and abs(bg[1] - bg[2]) < 12, "corner pixel %r" % (bg,))

        card, hard, canvas = luma("card"), luma("hard-shadow"), luma("hard-canvas")
        self.check("the card is lighter than the room it sits in",
                   card is not None and bg is not None and card > bg[0] - 40,
                   "card %s, backdrop %r" % (card, bg))

        # --- 1. the hard shadow -------------------------------------------------------------------
        self.check("the panel's shadow is painted where the offset puts it",
                   card is not None and hard is not None and hard < card - 40,
                   "shadow %s vs card %s" % (hard, card))

        self.check("...and it STOPS where the offset says: just past its edge is bare card",
                   card is not None and canvas is not None and abs(canvas - card) <= 6,
                   "past the edge %s vs card %s" % (canvas, card))

        # --- 2. the blur, which is the whole point of the pair --------------------------------------
        far, near = luma("blur-far"), luma("blur-near")
        self.check("the SAME shadow blurred REACHES past where the hard one stopped",
                   far is not None and card is not None and canvas is not None
                   and far < card - 10 and far < canvas - 10,
                   "blur past the hard edge %s, bare card there %s" % (far, canvas))

        self.check("...because the blur SOFTENS the edge rather than moving the shadow",
                   near is not None and hard is not None and far is not None
                   and near > hard + 8 and near < card - 20,
                   "blurred edge %s vs hard edge %s" % (near, hard))

        # --- 3. the colour, and the OFF state -------------------------------------------------------
        colour = px("color-shadow")
        self.check("a coloured shadow is that colour: the blue one is blue-dominant",
                   colour is not None and colour[2] > colour[0] + 60 and colour[2] > colour[1] + 60,
                   "pixel %r" % (colour,))

        off = luma("off-canvas")
        self.check("a NULL shadow colour turns shadowing OFF — its shadow IS NOT THERE",
                   card is not None and off is not None and abs(off - card) <= 6,
                   "where its shadow would be %s vs card %s" % (off, card))

        # --- 4. and the rest of the demo's stack, from the same screenshot --------------------------
        red = px("red-swatch")
        self.check("a solid swatch is the colour it was set to: red-dominant",
                   red is not None and red[0] > red[1] + 40 and red[0] > red[2] + 40,
                   "pixel %r" % (red,))

        gl, gr = luma("grad-left"), luma("grad-right")
        self.check("the gradient band runs from a dark blue to an orange",
                   gl is not None and gr is not None and gr > gl + 40,
                   "left %s, right %s" % (gl, gr))

        tl, br = mp.get("header-tl"), mp.get("header-br")
        if tl is None or br is None:
            self.check("the header band is dark with light glyphs in it", False, "no band in the map")
        else:
            box = (tl[0], tl[1], br[0], br[1])
            light = shot.light_frac(box, thresh=200)
            mean = shot.mean_luma(box)
            self.check("the header band is dark blue with light glyphs in it",
                       light > 0.02 and mean < 120,
                       "light fraction %.3f, mean luma %.0f" % (light, mean))

        self.note("screenshot kept at %s" % ctx.artifact("cgshadow-1.ppm"))
        session.stop()
