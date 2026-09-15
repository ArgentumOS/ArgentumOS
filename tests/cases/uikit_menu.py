"""UIKit: the menu family.

Menu and MenuItem are the data, MenuView is the drawing, Menu::popUp runs one
modally, and PopUpButton is the control a person actually clicks. The gate is the
ROUND TRIP - button, menu, row, action - because that is the whole feature and it
is observable in the log without a single pixel: the toolkit reports where the
menu landed and how tall a row is, so the check aims at a row instead of guessing
the geometry.
"""
import re
import time

from harness import BaseCase

ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit: a pop-up button opens its menu and a picked row sends its action"
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

        session.run("export ARGENTUM_KEYLOG=1")
        mark = len(session.log_text())
        session.run("%s 90 &" % ZOO)
        if not session.wait_for(r"ZOO-READY", 120):
            self.check("board-up", False,
                       "the zoo never reported ZOO-READY; tail: %s"
                       % " | ".join(session.tail(4)))
            return

        sh = re.search(r"ZOO-SCREEN w=(\d+) h=(\d+)", session.output_since(mark))
        self.check("screen-known", sh is not None, "the board did not report ZOO-SCREEN")
        if not sh:
            return
        screen_h = int(sh.group(2))
        mon = session.monitor()

        # THE HARNESS'S POINTER IS A PRECONDITION, NOT A RESULT, and this case
        # learned it the same way uikit_u2c did: the test guest sometimes boots
        # with no pointer, and then every click below is a SILENT no-op. No
        # magic threshold - measure the noise floor on a static board, then
        # require the park to beat it.
        shot_a = session.shot("pointer-idle-a")
        noise = shot_a.diff(session.shot("pointer-idle-b"))
        mon.park()
        time.sleep(0.5)
        moved = shot_a.diff(session.shot("pointer-after-park"))
        if moved <= noise:
            self.check("pointer-input-is-up", False,
                       "parking the pointer changed %d pixels against a noise "
                       "floor of %d, so the guest has no pointer and no click "
                       "below can be believed" % (moved, noise))
            return
        self.check("pointer-input-is-up", True,
                   "parking the pointer moved the cursor (%d pixels, noise "
                   "floor %d)" % (moved, noise))

        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT (\S+) x=(\d+) y=(\d+)",
                                    session.output_since(mark))}
        self.check("the-pop-up-button-is-on-the-board", "POPUP" in pts,
                   "the board did not place a POPUP control; it knows %s"
                   % sorted(pts))
        if "POPUP" not in pts:
            return

        # point the pointer at the button, then click it: the menu opens
        bx, by = pts["POPUP"]
        mon.click_at(int(bx), int(screen_h - by), settle=0.8)
        session.wait_for(r"ARGENTUM-POPUP", 15)
        at = re.search(r"ARGENTUM-POPUP x=(-?[\d.]+) y=(-?[\d.]+) "
                       r"rowh=([\d.]+) n=(\d+)", session.log_text())
        self.check("the-menu-opened-where-the-toolkit-says", at is not None,
                   "clicking the button did not open a menu: no ARGENTUM-POPUP "
                   "line. tail: %s" % " | ".join(session.tail(3)))
        if not at:
            return

        x, y, rowh, n = (float(at.group(1)), float(at.group(2)),
                         float(at.group(3)), int(at.group(4)))
        self.check("the-button-has-the-board-items", n == 3,
                   "the pop-up button holds %d rows; the board gave it three "
                   "(Small, Medium, Large)" % n)

        # pick the SECOND row: button -> menu -> row -> action, in one step.
        # NO chrome offset: a menu is BORDERLESS, so its view IS the surface and
        # the rows start at the reported point. (Adding the board's 22pt here
        # over-reaches by exactly one row - which is how this check first read
        # "Large" when it was aiming at "Medium".)
        t0 = len(session.log_text())
        mon.click_at(int(x + 40), int(screen_h - (y + rowh * 1.5)), settle=0.8)
        session.wait_for(r"ZOO-POPUP", 15)
        got = [l for l in session.output_since(t0).splitlines()
               if l.startswith("ZOO-POPUP")]
        self.check("picking-a-row-sends-its-action", 'title="Medium"' in "".join(got),
                   "clicking the middle row should send that ROW's action; the "
                   "log says %s" % got)

        # THE MAGNIFIER OPENS THE RECENT SEARCHES: the same round trip, driven
        # by a different control. The field is 240 wide and its centre is what
        # ZOO-AT reports, so the magnifier sits at left + pad + half its glyph.
        sxf, syf = pts["SEARCH"]
        mag_x = int(sxf - 120 + 6 + 7)
        t2 = len(session.log_text())
        mon.click_at(mag_x, int(screen_h - syf), settle=0.8)
        session.wait_for(r"ARGENTUM-POPUP", 15)
        at2 = re.search(r"ARGENTUM-POPUP x=(-?[\d.]+) y=(-?[\d.]+) "
                        r"rowh=([\d.]+) n=(\d+)", session.output_since(t2))
        self.check("the-magnifier-opens-the-recent-searches", at2 is not None,
                   "clicking the search field's magnifier opened no menu: no "
                   "ARGENTUM-POPUP line in that click's output. tail: %s"
                   % " | ".join(session.tail(3)))
        if at2:
            rx, ry = float(at2.group(1)), float(at2.group(2))
            self.check("the-recents-are-the-ones-set", int(at2.group(4)) == 3,
                       "the recents menu holds %s rows; the board set three "
                       "(alpha, beta, gamma)" % at2.group(4))
            t3 = len(session.log_text())
            mon.click_at(int(rx + 40), int(screen_h - (ry + float(at2.group(3)) * 0.5)),
                         settle=0.8)
            session.wait_for(r"ZOO-RECENT", 15)
            picked = [l for l in session.output_since(t3).splitlines()
                      if l.startswith("ZOO-RECENT")]
            self.check("a-recent-sends-its-action", 'title="alpha"' in "".join(picked),
                       "picking the first recent should send that row's action; "
                       "the log says %s" % picked)

