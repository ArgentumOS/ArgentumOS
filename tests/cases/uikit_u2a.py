"""U2a: the display path — a real window on Xfb, drawn and pixel-checked.

The probe opens a titled window, draws a content view (background + tile
+ text) through the drawing context, and stays up. This case reads the
geometry and colours OUT OF THE PROBE'S LOG — never from constants — and
then asks the framebuffer whether those colours landed where the probe
said they would. That is the difference between "the code ran" and "the
display path works".
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/window_draw"
CONTENT_RGB = (0x2E, 0x7D, 0x32)


def close(got, want, tol=12):
    if got is None or want is None:
        return False
    return all(abs(g - w) <= tol for g, w in zip(got, want))


class Case(BaseCase):
    title = "UIKit U2a: the display path draws a real window"
    tier = "slow"		# it needs a booted session with Xfb on /dev/fb0
    timeout = 420

    def run(self, ctx):
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        # in the BACKGROUND with its output on the console: it must still
        # be on screen when the screenshot is taken
        mark = len(session.log_text())
        session.run("%s &" % PROBE)
        session.wait_for(r"U2A-OK", 90)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U2A-"):
                self.note(line)

        self.check("probe-ran", "U2A-OK" in out,
                   "the probe opened a window" if "U2A-OK" in out
                   else "no U2A-OK; guest tail: " + session.tail())
        if "U2A-OK" not in out:
            return
        self.check("no-display-failure",
                   "U2A-NO-DISPLAY" not in out and "U2A-NO-WINDOW" not in out,
                   "the X connection and the window both came up")

        m = re.search(r"U2A-WINDOW wPx=(\d+) hPx=(\d+) pxPerPt=([\d.]+) "
                      r"chromeH=([\d.]+) contentX=([\d.]+) contentY=([\d.]+)",
                      out)
        if m is None:
            self.check("geometry-parsed", False, "no U2A-WINDOW line")
            return
        wpx, hpx = int(m.group(1)), int(m.group(2))
        chromeh = float(m.group(4))
        self.check("geometry-parsed", wpx > 0 and hpx > 0,
                   "surface %dx%d px, chrome %g pt" % (wpx, hpx, chromeh))

        t = re.search(r"U2A-TILE x=([\d.]+) y=([\d.]+) w=([\d.]+) h=([\d.]+) "
                      r"tile=([0-9A-Fa-f]{6})", out)
        if t is None:
            self.check("tile-parsed", False, "no U2A-TILE line")
            return
        tx, ty, tw, th = (float(t.group(i)) for i in range(1, 5))
        trgb = int(t.group(5), 16)
        want = ((trgb >> 16) & 0xFF, (trgb >> 8) & 0xFF, trgb & 0xFF)

        shot = session.shot("u2a")
        self.check("shot-taken", shot is not None, "a framebuffer dump")
        if shot is None:
            return

        # the probe put the window at (60,40); content starts below the
        # chrome, and at pxPerPt 1 points are pixels
        wx, wy = 60, 40
        gy = int(wy + chromeh)
        px, py = int(wx + tx + tw * 0.75), int(gy + ty + th * 0.5)
        got = shot.px(px, py)
        self.check("tile-on-screen", close(got, want),
                   "tile colour at (%d,%d): got %s want %s"
                   % (px, py, got, want))

        px2, py2 = int(wx + wpx * 0.8), int(gy + hpx * 0.75)
        bg = shot.px(px2, py2)
        self.check("content-on-screen", close(bg, CONTENT_RGB),
                   "content colour at (%d,%d): got %s want %s"
                   % (px2, py2, bg, CONTENT_RGB))

        # the chrome above the content is the WINDOW's own: no window
        # manager drew it, and it is not the content's colour
        chrome_px = shot.px(int(wx + wpx * 0.5), int(wy + chromeh * 0.5))
        self.check("chrome-is-the-windows-own",
                   not close(chrome_px, CONTENT_RGB),
                   "the titlebar pixel is chrome, not content: got %s"
                   % (chrome_px,))

        # BRIGHT pixels, not ink(): the harness's ink() counts DARK pixels,
        # and the tile itself is darker than the threshold - so asking for
        # ink here counted the whole tile and passed with no text at all
        # (it did, until the text engine's null-family bug was fixed).
        tbox = (int(wx + tx), int(gy + ty), int(wx + tx + tw),
                int(gy + ty + th))
        bright = shot.light_frac(tbox, thresh=230)
        self.check("text-drawn", bright > 0.01,
                   "%.2f%% of the tile is near-white text (the tile's own "
                   "luma is below 200, so this can only be the glyphs)"
                   % (bright * 100.0))
