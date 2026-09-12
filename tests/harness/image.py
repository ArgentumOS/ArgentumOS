"""Screenshots and the pixel questions a case may ask of them.

QEMU's `screendump` writes a binary PPM (P6).  Two rules keep cases honest:

  * coordinates must come from the guest's own log, never from a constant -
    "x = 1248" is only right on a 1280-wide screen, and the shipped default
    has changed twice;
  * colours must come from what the theme/configuration actually produced
    (the WM logs the tones it draws), not from a magic triple.

Boxes are (x0, y0, x1, y1), exclusive of x1/y1.
"""


def luma_of(rgb):
    """Perceived brightness of an (r, g, b) triple, 0..255."""
    return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) // 1000


def hex_rgb(value):
    """0xRRGGBB (int or "0x..." string) -> (r, g, b)."""
    if isinstance(value, str):
        value = int(value, 16)
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def parse_ppm(data):
    """(w, h, pixels) for a binary PPM body."""
    parts = data.split(b"\n", 3)
    if len(parts) != 4 or parts[0].strip() != b"P6":
        raise ValueError("not a binary PPM (P6)")
    w, h = (int(x) for x in parts[1].split())
    return w, h, parts[3]


class Shot:
    """One screenshot, with the questions a case is likely to ask."""

    def __init__(self, w, h, data):
        self.w, self.h, self.data = w, h, data

    @classmethod
    def from_file(cls, path):
        with open(path, "rb") as fh:
            return cls(*parse_ppm(fh.read()))

    def px(self, x, y):
        i = (y * self.w + x) * 3
        return (self.data[i], self.data[i + 1], self.data[i + 2])

    def luma(self, x, y):
        return luma_of(self.px(x, y))

    def _clip(self, box):
        x0, y0, x1, y1 = box
        return (max(0, x0), max(0, y0), min(self.w, x1), min(self.h, y1))

    def points(self, box, step=1):
        x0, y0, x1, y1 = self._clip(box)
        for y in range(y0, y1, step):
            for x in range(x0, x1, step):
                yield x, y

    def light_frac(self, box, thresh=200, step=1):
        """Fraction of the box brighter than `thresh` (a fill, a tile...)."""
        pts = list(self.points(box, step))
        if not pts:
            return 0.0
        lit = sum(1 for x, y in pts if self.luma(x, y) > thresh)
        return lit / float(len(pts))

    def dark_frac(self, box, thresh=140, step=1):
        return 1.0 - self.light_frac(box, thresh, step)

    def ink(self, box, thresh=140, step=1):
        """How many pixels of the box are darker than `thresh` (drawn ink)."""
        return sum(1 for x, y in self.points(box, step) if self.luma(x, y) < thresh)

    def mean_luma(self, box, step=1):
        """Mean brightness of the box - for comparing two regions of chrome."""
        pts = list(self.points(box, step))
        if not pts:
            return 0.0
        return sum(self.luma(x, y) for x, y in pts) / float(len(pts))

    def diff(self, other):
        """Pixels that changed at all between two shots of the same screen."""
        n = min(len(self.data), len(other.data))
        return sum(1 for i in range(0, n - 2, 3)
                   if self.data[i:i + 3] != other.data[i:i + 3])

    def diff_box(self, other, box, tol=0):
        """Pixels that changed inside `box` (tol 0 = any difference).

        A box-limited diff is usually what a case wants: comparing two whole
        screens after a window appeared counts the window, not the thing under
        test.
        """
        changed = 0
        for x, y in self.points(box):
            a, b = self.px(x, y), other.px(x, y)
            if tol == 0:
                if a != b:
                    changed += 1
            elif any(abs(a[i] - b[i]) > tol for i in range(3)):
                changed += 1
        return changed

    def spans(self, y, box, thresh=200, min_len=1):
        """Runs of light pixels along row y, as (x0, x1) pairs.

        This is how to find text without guessing where it is: a menu bar is a
        sequence of light runs, and a case can count them or compare their
        order instead of asserting on absolute x.
        """
        x0, _, x1, _ = self._clip(box)
        runs, start = [], None
        for x in range(x0, x1):
            lit = self.luma(x, y) > thresh
            if lit and start is None:
                start = x
            elif not lit and start is not None:
                if x - start >= min_len:
                    runs.append((start, x))
                start = None
        if start is not None and x1 - start >= min_len:
            runs.append((start, x1))
        return runs

    def row_is_uniform(self, y, box, tol=6):
        """True when every sampled pixel of row y matches the first one."""
        x0, _, x1, _ = self._clip(box)
        try:
            ref = self.px(x0, y)
        except IndexError:
            return True
        for x in range(x0 + 1, x1):
            p = self.px(x, y)
            if any(abs(p[i] - ref[i]) > tol for i in range(3)):
                return False
        return True
