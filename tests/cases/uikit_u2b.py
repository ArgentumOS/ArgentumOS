"""U2b: input, Control and Button — driven by real pointer events.

The probe opens a window with two buttons and logs (in SCREEN space) where
they are. This case then moves the REAL pointer at those coordinates — the
QEMU mouse, through Xfb, into the guest's X server — and asks what
happened. Nothing is synthesized inside the guest: if the hit test, the
press capture, the action dispatch or the chrome were wrong, the log would
not say so and the checks would fail.
"""

import re

from harness import BaseCase, Skip

PROBE = "/System/Shared/tests/control_click"


class Case(BaseCase):
    title = "UIKit U2b: a real click fires a control, the chrome works"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        # BLOCKED, and deliberately not skipped quietly: the test guest has
        # NO POINTER INPUT PATH.  The harness boots "pc,usb=off" with no
        # pointer device, and the kernel's PS/2 code path was retired
        # (psaux -> the native /System/Devices/mouse device), so the
        # monitor's relative mouse_move reaches nothing.  The probe logs
        # that no event ever arrived (presses=0) and the window never even
        # opened its drag.  Xfb already carries the seam for this
        # (XFB_MOUSE / XFB_KBD, "so test harnesses can feed PS/2 packets
        # over a raw serial line"); what is missing is the harness side:
        # either an extra serial wired to XFB_MOUSE, or a real pointer
        # device attached at boot (usb=on + usb-tablet, which also needs
        # the kernel's USB HID mouse and Xfb's poller to see it).
        raise Skip("no pointer input reaches the guest: harness boots "
                   "pc,usb=off and psaux is retired; wire XFB_MOUSE to an "
                   "extra serial (or attach usb-tablet) before this can be "
                   "verified - see docs/design/cocoa-parity-plan.md (U2b)")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s &" % PROBE)
        if not session.wait_for(r"U2B-READY", 120):
            self.check("probe-ready", False,
                       "no U2B-READY; guest tail: " + session.tail())
            return
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("U2B-"):
                self.note(line)
        self.check("probe-ready", True, "the window with its buttons is up")

        def point(name):
            m = re.search(r"U2B-%s screen=\(([\d.]+),([\d.]+)\)" % name, out)
            return (float(m.group(1)), float(m.group(2))) if m else None

        win = re.search(r"U2B-WINDOW x=([\d.]+) y=([\d.]+)", out)
        if win is None or point("PRESS") is None or point("TOGGLE") is None:
            self.check("geometry-parsed", False, "the probe logged no geometry")
            return
        self.check("geometry-parsed", True,
                   "window at (%s,%s), buttons and chrome located"
                   % (win.group(1), win.group(2)))

        mon = session.monitor()

        # 1. a click on the first button fires its action
        px, py = point("PRESS")
        mon.click_at(int(px), int(py), settle=1.0)
        session.wait_for(r"U2B-ACTION pressed n=1", 30)
        out2 = session.output_since(mark)
        self.check("click-fires-action", "U2B-ACTION pressed n=1" in out2,
                   "one click on the button sent the action exactly once"
                   if "U2B-ACTION pressed n=1" in out2
                   else "no action; guest tail: " + session.tail())

        # 2. a click on the toggle flips its state (and it stays)
        tx, ty = point("TOGGLE")
        mon.click_at(int(tx), int(ty), settle=1.0)
        session.wait_for(r"U2B-ACTION toggled n=1", 30)
        out3 = session.output_since(mark)
        self.check("toggle-flips", "U2B-ACTION toggled n=1 state=1" in out3,
                   "the toggle reported state=1 after its click"
                   if "U2B-ACTION toggled n=1 state=1" in out3
                   else "toggle did not reach state 1; tail: "
                   + session.tail())

        # 3. dragging the titlebar moves the window (root-coordinate delta)
        wx, wy = float(win.group(1)), float(win.group(2))
        gx, gy = point("TITLEBAR")
        mon.drag(int(gx), int(gy), int(gx) + 90, int(gy) + 60)
        moved = session.wait_for(r"U2B-MOVED x=([\d.]+)", 30)
        out4 = session.output_since(mark)
        m = re.search(r"U2B-MOVED x=([\d.]+) y=([\d.]+)", out4)
        self.check("titlebar-drag-moves-window", moved and m is not None,
                   "the window reported a new origin (%s,%s) after the drag"
                   % (m.group(1), m.group(2)) if m else "the window never moved")
        if m is not None:
            nx, ny = float(m.group(1)), float(m.group(2))
            self.check("drag-follows-the-pointer",
                       abs(nx - (wx + 90)) <= 12 and abs(ny - (wy + 60)) <= 12,
                       "moved (%g,%g) -> (%g,%g): a drag of (+90,+60)"
                       % (wx, wy, nx, ny))

        # 4. the close box closes the window (the window's own chrome)
        cx, cy = point("CLOSE")
        mon.click_at(int(cx), int(cy), settle=1.0)
        closed = session.wait_for(r"U2B-OK", 30)
        out5 = session.output_since(mark)
        self.check("close-box-closes", closed and "U2B-CLOSED" in out5,
                   "clicking the window's own close box closed it"
                   if closed else "the close box did nothing")
        m2 = re.search(r"U2B-CLOSED presses=(\d+) toggles=(\d+) state=(\d+)",
                       out5)
        if m2 is not None:
            self.check("one-press-one-action", m2.group(1) == "1",
                       "the button fired once for one click (presses=%s)"
                       % m2.group(1))
            self.check("no-double-fire", m2.group(2) == "1",
                       "the toggle fired once (toggles=%s)" % m2.group(2))
