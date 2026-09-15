"""U3c: editing — real keys into an editable field.

The board carries an editable text field. This case clicks it (a click
focuses a responder), TYPES with the QEMU keyboard, presses Return to
commit, and presses BackSpace — then reads the board's own edit stream to
see each key land and the field's value after the deletion.

The guest boots with a USB keyboard and no PS/2 controller, so this also
exercises the two fixes underneath: the kernel's keyboard device node and
Xfb's by-role default path.
"""

import re

from harness import BaseCase

ZOO = "/Applications/WidgetZoo"


class Case(BaseCase):
    title = "UIKit U3c: a typed key edits a field"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse",
                                  "-device", "usb-kbd"])
        ready = session.shell_ready(150)
        session.run("export ARGENTUM_KEYLOG=1")
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
        sh = re.search(r"ZOO-SCREEN w=\d+ h=(\d+)", out)
        pts = {m.group(1): (float(m.group(2)), float(m.group(3)))
               for m in re.finditer(r"ZOO-AT (\S+) x=([\d.]+) y=([\d.]+)", out)}
        self.check("field-located", sh is not None and "EDITTEXT" in pts,
                   "screen %s, the editable field at %s"
                   % (sh.group(1) if sh else "?", pts.get("EDITTEXT")))
        if sh is None or "EDITTEXT" not in pts:
            return
        screen_h = int(sh.group(1))
        mon = session.monitor()

        # a CLICK focuses the field (Control::mouseDown asks the window).
        # The monitor's Y is MIRRORED against the guest's - measured, and
        # the reason the first run of this case clicked the SWITCH instead
        # (asked for y=673, landed on 407): screen_h - y, every time.
        mon.park()
        fx, fy = pts["EDITTEXT"]
        mon.click_at(int(fx), int(screen_h - fy), settle=1.0)

        # A CLICK GIVES THE FIELD THE CURSOR, BEFORE any keystroke. The
        # caret check further down runs after typing, which is exactly the
        # gap this was reported in: a click into a field showed nothing at
        # all, and only typing made the caret appear. The caret is the only
        # thing on screen that says the click took, so it is checked on the
        # click alone - and an empty field's caret sits at the entry's left
        # edge, which the scan covers.
        shot0 = session.shot("u3c-click")
        ex0, ey0 = pts["EDITTEXT"]
        tallest0 = 0
        for x in range(int(ex0) - 116, int(ex0) + 104):
            run = best = 0
            for y in range(int(ey0) - 11, int(ey0) + 11):
                if shot0.luma(x, y) < 140:
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
            tallest0 = max(tallest0, best)
        self.check("click-shows-the-caret", tallest0 >= 16,
                   "after the click and before any key, the tallest dark run "
                   "in the field is %d rows (a caret is the field's interior)"
                   % tallest0,
                   )

        # SHIFT, THROUGH THE REAL KEYBOARD PATH. mon.key() injects at the X
        # level, so no gate has ever touched the kernel's scancode
        # translation; QEMU's sendkey drives the EMULATED USB KEYBOARD, so the
        # event goes usb-kbd -> HID report -> kernel (which folds Shift into
        # the key value) -> the device node -> X. That path never read the
        # report's modifier byte, so Shift did nothing on a real keyboard.
        t5 = len(session.log_text())
        mon.sendkey("shift-a")
        session.wait_for(r"ZOO-EDIT", 20)
        edits5 = re.findall(r"ZOO-EDIT (.*)", session.output_since(t5))
        self.check("shift-reaches-the-kernel",
                   any("A" in e for e in edits5[:3]),
                   "after shift-a on the emulated keyboard the field's edit "
                   "stream was %s: an unfolded key arrives as 'a', and no key "
                   "at all means the emulated keyboard path is broken"
                   % (edits5[:3] or "empty"))
        # whatever the shifted key inserted is cleared before the typing
        # checks below, which assert on the field's edit stream
        mon.key("backspace", settle=0.4)

        # and then keys edit it
        for ch in "hi":
            mon.key(ch, settle=0.35)
        session.wait_for(r"ZOO-EDIT hi", 30)
        out2 = session.output_since(mark)
        edits = re.findall(r"ZOO-EDIT (.*)", out2)
        self.check("keys-edit-the-field", "hi" in edits,
                   "the field's edit stream was %s" % (edits[-4:] or "empty"))

        # THE CARET AND THE INK COLOUR, in pixels - both were reported from
        # the screen: a field that takes keystrokes with no cursor, and text
        # typed into a field that had shown a placeholder coming out grey.
        shot = session.shot("u3c")
        ex, ey = pts["EDITTEXT"]
        # the caret is as tall as the field's interior (18pt) where the
        # tallest glyph stem is its ascender (~13): so the longest dark run
        # in any one column is the caret, and nothing else gets close.
        tallest = 0
        for x in range(int(ex) - 116, int(ex) + 104):
            run = best = 0
            for y in range(int(ey) - 11, int(ey) + 11):
                if shot.luma(x, y) < 140:
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
            tallest = max(tallest, best)
        self.check("caret-is-visible", tallest >= 16,
                   "the tallest dark run in the field is %d rows; a caret is "
                   "the field's interior height" % tallest)

        # THE FIRST TYPED CHARACTER TOO. Measuring "the darkest pixel in the
        # field" was satisfied by the caret, which sits after the text; the
        # LEFTMOST ink column is the first character, where the placeholder's
        # grey would still be showing.
        cols = [(x, min(shot.luma(x, y)
                        for y in range(int(ey) - 11, int(ey) + 11)))
                for x in range(int(ex) - 110, int(ex) + 110)]
        first = next((c for c in cols if c[1] < 200), None)
        self.check("first-typed-character-is-ink",
                   first is not None and first[1] < 100,
                   "the leftmost ink column is %s at x=%s: the placeholder "
                   "grey is ~140, so a greyed first character cannot pass"
                   % (first[1] if first else None,
                      first[0] if first else None))

        # Return commits (sends the action, the field as the sender)
        mon.key("ret", settle=0.5)
        session.wait_for(r"ZOO-COMMIT", 30)
        out3 = session.output_since(mark)
        self.check("return-commits", "ZOO-COMMIT hi" in out3,
                   "Return sent the action with the field's text"
                   if "ZOO-COMMIT hi" in out3
                   else "no commit; tail: " + session.tail())

        # BackSpace deletes (the caret is at the end after the inserts)
        mon.key("backspace", settle=0.4)
        session.wait_for(r"ZOO-EDIT h\b", 30)
        out4 = session.output_since(mark)
        self.check("backspace-deletes",
                   re.search(r"ZOO-EDIT h\n", out4) is not None,
                   "the field went back to \"h\" after one backspace"
                   if re.search(r"ZOO-EDIT h\n", out4)
                   else "the deletion did not land; tail: " + session.tail())

        # NOTHING LEFT BEHIND. Moving the focus away has to take the caret
        # with it: the outgoing view used to keep drawing one, so a field that
        # had been typed in and cleared kept a stray vertical line - the
        # caret - until something else happened to repaint it. The check reads
        # the field after the focus has moved, and 16 rows separates a caret
        # (18) from the text it was sitting after (~10).
        mon.click_at(int(pts["SEARCH"][0]), int(screen_h - pts["SEARCH"][1]),
                     settle=0.6)
        shot5 = session.shot("u3c-after")
        tallest5 = 0
        for x in range(int(fx) - 116, int(fx) + 104):
            run = best = 0
            for y in range(int(fy) - 11, int(fy) + 11):
                if shot5.luma(x, y) < 140:
                    run += 1
                    best = max(best, run)
                else:
                    run = 0
            tallest5 = max(tallest5, best)
        self.check("no-caret-left-behind", tallest5 < 16,
                   "with the focus moved to another field, the tallest dark "
                   "run left in the first is %d rows (a caret is 18, its text "
                   "is ~10)" % tallest5)
