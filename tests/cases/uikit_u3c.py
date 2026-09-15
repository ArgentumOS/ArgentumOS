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

        # and then keys edit it
        for ch in "hi":
            mon.key(ch, settle=0.35)
        session.wait_for(r"ZOO-EDIT hi", 30)
        out2 = session.output_since(mark)
        edits = re.findall(r"ZOO-EDIT (.*)", out2)
        self.check("keys-edit-the-field", "hi" in edits,
                   "the field's edit stream was %s" % (edits[-4:] or "empty"))

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
