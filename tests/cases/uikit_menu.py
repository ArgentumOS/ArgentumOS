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
        session.run("export ARGENTUM_MOTIONLOG=1")
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

        # THE POP-UP AFFORDANCE. A pop-up button must SAY it opens something:
        # Cocoa carves a segment off its right and puts an up/down chevron
        # there. The board reports the button's box, so this is MEASURED - ink
        # in BOTH halves of the right-hand strip, which a plain button would
        # have only if its title happened to reach there.
        shot = session.shot("board-before-popup")
        pf = re.search(r"ZOO-FRAME POPUP x=(-?[\d.]+) y=(-?[\d.]+) "
                       r"w=([\d.]+) h=([\d.]+)", session.log_text())
        self.check("the-board-reports-the-buttons-box", pf is not None,
                   "no ZOO-FRAME line for POPUP; tail: %s"
                   % " | ".join(session.tail(3)))
        fx = fy = fw = fh = 0.0
        if pf:
            fx, fy, fw, fh = (float(pf.group(i)) for i in (1, 2, 3, 4))
            u = shot.ink((int(fx + fw - 16), int(fy) + 2, int(fx + fw - 2),
                          int(fy + fh / 2)))
            d = shot.ink((int(fx + fw - 16), int(fy + fh / 2),
                          int(fx + fw - 2), int(fy + fh) - 2))
            self.check("the-pop-up-button-shows-its-chevron", u >= 6 and d >= 6,
                       "a pop-up button carries an up/down chevron in its right "
                       "segment; the ink there was up=%d down=%d" % (u, d))

        # THE POP-UP IS A ROUNDED RECT, NOT A CAPSULE. BezelStyle::Rounded is
        # radius = height/2 - a pill at 24pt - and that is what the pop-up had.
        # The board already draws BOTH styles as reference rows, so this check
        # is DIFFERENTIAL: the pop-up's corner must measure like the RoundRect
        # button's, not like the Rounded one's.
        ref = {}
        for m in re.finditer(r"ZOO-FRAME (\S+) x=(-?[\d.]+) y=(-?[\d.]+) "
                             r"w=([\d.]+) h=([\d.]+)", session.log_text()):
            ref[m.group(1)] = tuple(float(m.group(i)) for i in (2, 3, 4, 5))
        self.check("the-board-draws-both-bezels-to-compare",
                   "BEZEL-ROUNDED" in ref and "BEZEL-ROUNDRECT" in ref,
                   "the button column should carry both bezel styles; the board "
                   "reported %s" % sorted(ref))
        if "BEZEL-ROUNDED" in ref and "BEZEL-ROUNDRECT" in ref and pf:
            # HOW DEEP the bezel's left edge is below the button's top, at the
            # leftmost column. A capsule (radius = height/2) does not REACH the
            # corner at all - the shape only begins ~9pt down - while a rounded
            # rect (radius 4) begins ~1pt down. Counting ink was useless: every
            # bezel colour is lighter than the ink threshold, so all three
            # measured 0 and the check passed on nothing at all.
            def edge_depth(box):
                x0, y0, _w, hh = box
                back = shot.luma(int(x0) - 8, int(y0 + hh / 2))
                for dy in range(int(hh)):
                    if abs(shot.luma(int(x0) + 1, int(y0) + dy) - back) > 6:
                        return dy
                return int(hh)

            pill = edge_depth(ref["BEZEL-ROUNDED"])
            rect = edge_depth(ref["BEZEL-ROUNDRECT"])
            mine = edge_depth((fx, fy, fw, fh))
            self.check("the-pop-up-is-a-rounded-rect-not-a-capsule",
                       abs(mine - rect) <= 2 and mine < pill,
                       "the bezel's left edge should start near the top like "
                       "the RoundRect button (%d), not far down like the "
                       "Rounded capsule (%d); the pop-up measured %d"
                       % (rect, pill, mine))


        # THE POP-UP'S TITLE IS LEFT-ALIGNED. ButtonCell centres every button's
        # title - right for a push button, wrong for a pop-up, which reads as a
        # field. The reference button is the SAME control centred, so this is
        # differential: where does the title's ink sit, as a fraction of the
        # width it is drawn in (skipping the pop-up's chevron segment)?
        def ink_centroid(box, seg):
            x0, y0, ww, hh = box
            tot = n = 0
            for x, y in shot.points((int(x0), int(y0) + 5,
                                     int(x0 + ww - seg), int(y0 + hh) - 5)):
                if shot.luma(x, y) < 150:
                    tot += x - x0
                    n += 1
            return (tot / n / ww) if n else -1.0

        if "BEZEL-ROUNDRECT" in ref and pf:
            centred = ink_centroid(ref["BEZEL-ROUNDRECT"], 0)
            self.check("the-reference-button-is-centred",
                       0.40 <= centred <= 0.60,
                       "a plain push button centres its title, so the reference "
                       "should measure near the middle; it measured %.2f"
                       % centred)
            mine_t = ink_centroid((fx, fy, fw, fh), 20)
            self.check("the-pop-up-title-is-left-aligned", 0.0 < mine_t < 0.38,
                       "a pop-up left-aligns its title, so the ink should sit "
                       "left of centre in the box it is drawn in; it measured "
                       "%.2f (centred reads about 0.50)" % mine_t)
            # AND INSET FROM THE BORDER. With no padding the first character
            # began against the bezel. Measured directly as the GAP, which is
            # what "too little space" means: the centroid cannot see it, since
            # a title jammed left and one nicely inset both sit left.
            first = -1
            for gx in range(int(fx) + 2, int(fx + fw) - 20):
                col = [shot.luma(gx, gy) for gy in range(int(fy) + 5,
                                                         int(fy + fh) - 5)]
                if min(col) < 140:
                    first = gx - int(fx)
                    break
            self.check("the-pop-up-title-is-inset-from-the-border",
                       3 <= first <= 14,
                       "there should be a margin between the bezel and the "
                       "first character; the title's ink starts %d pt in "
                       "(Cocoa's content inset is about 6)" % first)
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

        # THE MENU FOLLOWS THE POINTER: the row under it is the row a click
        # would take, so it is DRAWN chosen. Move there with no press. Row i
        # spans kMenuPad + i*rowh from the menu's top, and the menu is
        # borderless so its view IS the surface.
        mon.goto(int(x + 40), int(screen_h - (y + 4 + rowh * 1.5)))
        time.sleep(0.6)
        hs = session.shot("menu-hover-row-1")
        # the GUTTER, left of where the title starts: with no ink there the
        # pixel is pure row fill, blue when chosen and near-white when not
        hot = hs.px(int(x + 10), int(y + 4 + rowh * 1.5))
        cold = hs.px(int(x + 10), int(y + 4 + rowh * 0.5))
        self.check("the-menu-draws-the-hovered-row-chosen",
                   hot[2] > hot[0] + 60 and hot[2] > hot[1],
                   "the row under the pointer should be drawn with the "
                   "selection colour; the pixel there is %s" % (hot,))
        self.check("a-row-not-under-the-pointer-is-not-chosen",
                   cold[2] < cold[0] + 60,
                   "only the row under the pointer should be drawn chosen; row "
                   "0's pixel is %s" % (cold,))

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

        # AND THE BUTTON ITSELF FOLLOWS. The clicked row's title is NOT the
        # same thing as the button's - the check above passed for a whole
        # session while the button still read "Small", which is exactly the
        # defect. So: the button's own title, AND its face in pixels.
        # The board reports the button's OWN title from its loop, so this is
        # the state AFTER the selection settled - not the row's action, which
        # is sent before the button has selected anything. Poll for a NEW line:
        # the board also reports its initial selection at start-up.
        sel = []
        for _ in range(30):
            sel = [l for l in session.output_since(t0).splitlines()
                   if l.startswith("ZOO-POPUP-SELECT")]
            if sel:
                break
            time.sleep(0.5)
        self.check("the-button-reports-the-picked-row",
                   'title="Medium"' in "".join(sel),
                   "the button's OWN title must follow the pick - the clicked "
                   "row's title is not the same thing; the board says %s" % sel)
        if pf:
            after = session.shot("board-after-pick")
            changed = shot.diff_box(after, (int(fx), int(fy),
                                            int(fx + fw), int(fy + fh)))
            self.check("the-button-shows-the-picked-row", changed > 12,
                       "the button's face must change when its title does "
                       "(Small -> Medium): %d pixels changed in its box"
                       % changed)

        # THE MAGNIFIER OPENS THE RECENT SEARCHES: the same round trip, driven
        # by a different control. The field is 240 wide and its centre is what
        # ZOO-AT reports, so the magnifier sits at left + pad + half its glyph.
        #
        # The menu is Cocoa's DEFAULT search menu, whose shape is the claim
        # here: a heading, the recents newest first, a separator, a Clear item
        # - six rows for the three the board remembers. Rows 0 and 4 are the
        # heading and the separator, so the first RECENT is row 1.
        sxf, syf = pts["SEARCH"]
        mag_x = int(sxf - 120 + 6 + 7)

        def open_recents(mark_at):
            mon.click_at(mag_x, int(screen_h - syf), settle=0.8)
            session.wait_for(r"ARGENTUM-POPUP", 15)
            return re.search(r"ARGENTUM-POPUP x=(-?[\d.]+) y=(-?[\d.]+) "
                             r"rowh=([\d.]+) n=(\d+)",
                             session.output_since(mark_at))

        t2 = len(session.log_text())
        at2 = open_recents(t2)
        self.check("the-magnifier-opens-the-recent-searches", at2 is not None,
                   "clicking the search field's magnifier opened no menu: no "
                   "ARGENTUM-POPUP line in that click's output. tail: %s"
                   % " | ".join(session.tail(3)))
        if at2:
            rx, ry = float(at2.group(1)), float(at2.group(2))
            rowh = float(at2.group(3))
            self.check("the-menu-is-cocoas-default-search-menu",
                       int(at2.group(4)) == 6,
                       "the default search menu is a heading, the recents, a "
                       "separator and a Clear item (6 rows for the board's "
                       "three); this one holds %s" % at2.group(4))

            # PICKING A RECENT: it goes back in the FIELD and the search RUNS.
            # The board hears it through the field's own action, which is the
            # line a TYPED search produces - so this reads the whole round
            # trip, not just "a row's action arrived".
            t3 = len(session.log_text())
            mon.click_at(int(rx + 40), int(screen_h - (ry + rowh * 1.5)),
                         settle=0.8)
            session.wait_for(r"ZOO-SEARCH", 15)
            ran = [l for l in session.output_since(t3).splitlines()
                   if l.startswith("ZOO-SEARCH")]
            self.check("picking-a-recent-puts-it-in-the-field-and-runs-it",
                       "ZOO-SEARCH [alpha]" in "".join(ran),
                       "picking the first recent must put it in the field and "
                       "run the search; the log says %s" % ran)

            # THE CLEAR ITEM empties the recents, so the next time the menu
            # opens it is the single disabled row Cocoa shows instead.
            t4 = len(session.log_text())
            at4 = open_recents(t4)
            if at4:
                mon.click_at(int(float(at4.group(1)) + 40),
                             int(screen_h - (float(at4.group(2))
                                             + float(at4.group(3)) * 5.5)),
                             settle=0.8)
            t5 = len(session.log_text())
            at5 = open_recents(t5)
            self.check("clear-empties-the-recents",
                       at5 is not None and int(at5.group(4)) == 1,
                       "after Clear the menu is the one row Cocoa shows with "
                       "nothing remembered; it holds %s rows"
                       % (at5.group(4) if at5 else "no menu at all"))

