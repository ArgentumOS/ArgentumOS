"""Weaver W1: palette categories, and a component DRAGS into the window.

The palette is the registry grouped by category (Containers, Controls)
with non-selectable header rows; a press on a class row records it and
the release decides: over the palette it is a plain click (add at the
default position), over the canvas the class is instantiated at the DROP
point (GORM's drag-a-component-in).
"""

import re
import time

from harness import BaseCase

WEAVER = "/Applications/Weaver.app/bin/Weaver"
FIXTURE = "/System/Shared/tests/weaver_ib2.conf"
DOC = "/Users/Admin/Documents/weaver_w1.conf"
MANAGE = (r"KESTREL: manage 0x[0-9a-f]+ 'Weaver' frame=0x[0-9a-f]+ "
          r"at (\d+),(\d+)")
WINDOW = r"WEAVER: window 0x[0-9a-f]+ (\d+)x(\d+) ppt=([0-9.]+)"
PALETTE = r"WEAVER: palette rows=(\d+) rowH=([0-9.]+) header=([0-9.]+)"
CATS = r"WEAVER: palette categories Containers=(\d+) Controls=(\d+)"
BOXROW = (r"BOX-A: box='Palette' child\[0\] x=([0-9.]+) y=([0-9.]+) "
          r"w=([0-9.]+) h=([0-9.]+)")
FRAME_PX = 4
BAND_H = 20
BUTTON_ROW = 7		# [Containers] View Box ScrollView SplitView [Controls] Label Button


class Case(BaseCase):
    title = "Weaver W1: palette categories + drag a control into the canvas"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        ctx.require_guest_file("Weaver")
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("cp %s %s && rm -f %s.weaverundo && echo DOC-COPIED"
                    % (FIXTURE, DOC, DOC))
        out = session.output_since(mark)
        self.check("fixture-staged", "DOC-COPIED" in out,
                   "the fixture is in %s" % DOC)

        mark = len(session.log_text())
        session.run("DISPLAY=:0 %s --open weaver_w1.conf --show "
                    "& echo WPID=$!" % WEAVER)
        managed = session.wait_for(MANAGE, 45)
        self.check("window-managed", managed,
                   "the editor window is managed" if managed
                   else "no manage line; guest tail: " + session.tail())
        session.wait_for(r"BOX-A: box='Palette' child\[0\]", 45)
        m = re.search(MANAGE, session.log_text())
        w = re.search(WINDOW, session.log_text())
        pr = re.search(PALETTE, session.log_text())
        cats = re.search(CATS, session.log_text())
        br = re.search(BOXROW, session.log_text())

        self.check("palette-categories",
                   bool(cats) and int(cats.group(1)) >= 2
                   and int(cats.group(2)) >= 5,
                   "the palette is grouped by category (Containers, "
                   "Controls)")
        if not (m and w and pr and br):
            session.run("killall Weaver")
            return

        fx, fy = int(m.group(1)), int(m.group(2))
        ppt = float(w.group(3))
        row_h = float(pr.group(2))
        header = float(pr.group(3))
        tx = float(br.group(1)) + 4.0
        ty = float(br.group(2)) + header + (BUTTON_ROW + 0.5) * row_h

        # the drop point, deep inside the canvas (doc 300,250)
        drop_doc = (300.0, 250.0)
        sx = fx + FRAME_PX + int(round(tx * ppt))
        sy = fy + BAND_H + int(round(ty * ppt))
        dx = fx + FRAME_PX + int(round((200 + drop_doc[0]) * ppt))
        dy = fy + BAND_H + int(round((20 + drop_doc[1]) * ppt))

        mark = len(session.log_text())
        monitor = session.monitor()
        monitor.park()
        before = session.shot("w1-before")
        monitor.goto(sx, sy)
        monitor.send("mouse_button 1")
        time.sleep(0.3)
        monitor.goto(dx, dy)
        time.sleep(0.3)
        monitor.send("mouse_button 0")
        time.sleep(0.8)

        # the pointer quantizes to guest pixels, so the drop lands within
        # a pixel of the requested point (300,250) — assert the neighbourhood
        self.check("drag-adds-at-drop",
                   session.wait_for(
                       r"WEAVER: add Button to panel at 2 "
                       r"\(29[89](\.\d+)?,24[89](\.\d+)? 90x24\)", 20),
                   "the drop instantiated the class AT the drop point")

        # the dropped control must be ON SCREEN: the drop box changed
        # (a button's chrome tone is close to the page tone, so a colour
        # threshold is the wrong instrument — a diff is decisive)
        after = session.shot("w1-after")
        box = (dx - 4, dy - 4, dx + 124, dy + 40)
        changed = before.diff_box(after, box)
        self.check("drop-is-drawn", changed > 0,
                   "%d px changed in the drop box after the drag" % changed)
        session.run("killall Weaver")
