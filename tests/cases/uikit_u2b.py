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
        # A REAL pointer device, the way mk/00-base.mk gives the dev flow
        # one: a USB mouse on xHCI with the PS/2 controller absent.  The
        # i8042=off is load-bearing - with a PS/2 mouse present QEMU routes
        # the monitor's mouse_move to THAT device, and the kernel's PS/2
        # path is retired (psaux -> the native /System/Devices/mouse), so
        # the injection would reach nothing.
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse"])
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        # What the kernel did with the USB mouse is verifiable directly:
        # the guest log shows the xHCI enumeration and "usb-mouse: mouse on
        # slot 1".  What is NOT yet verified is Xfb's side of the path -
        # whether the device Xfb opens (/System/Devices/mouse, the by-role
        # alias) exists and whether pointer records arrive through it.
        mark0 = len(session.log_text())
        session.run("ls -l /System/Devices/mouse; "
                    "ls -l /System/Devices/USB/Mouse 2>&1 | head -3")
        devs = session.output_since(mark0)
        for line in devs.strip().splitlines():
            if line.startswith(("l", "-", "c", "ls:")):
                self.note("device: " + line.strip())
        # the alias is a relative symlink (mouse -> USB/Mouse), so ask the
        # TARGET, not the alias' own line
        if "USB/Mouse" not in devs:
            raise Skip("the role alias /System/Devices/mouse does not point "
                       "at a mouse device, so Xfb (hw/xfb/fnxinput.c reads "
                       "it; XFB_MOUSE overrides) has nothing to poll - "
                       "docs/design/cocoa-parity-plan.md (U2b)")
        session.run("tail -12 '/System/Variable Data/log/Xfb.log' 2>&1")
        xlog = session.output_since(mark0)
        for line in xlog.strip().splitlines()[-12:]:
            self.note("xfb: " + line.strip()[:120])
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

        # The seam is RELATIVE: park in the corner so the guest clamps the
        # pointer and (0,0) is a known starting point.
        mon.park()

        # And its Y is MIRRORED against the guest's: measured, not assumed -
        # asking for y=120 put the X pointer at y=959 on a 1080-tall screen.
        # Every screen coordinate below is converted with that.
        hm = re.search(r"U2B-SCREEN w=(\d+) h=(\d+)",
                       session.output_since(mark))
        if hm is None:
            self.check("screen-size-known", False,
                       "the probe did not report its screen size")
            return
        screen_h = int(hm.group(2))
        self.check("screen-size-known", screen_h > 0,
                   "the X screen is %sx%s" % (hm.group(1), hm.group(2)))

        def at(x, y):
            """(x, y) in X screen coordinates -> monitor coordinates."""
            return int(x), int(screen_h - y)
        session.wait_for(r"U2B-POINTER", 20)
        out0 = session.output_since(mark)
        self.check("server-sees-the-pointer",
                   "U2B-POINTER" in out0,
                   "the X server reported a pointer position (input reaches "
                   "the guest)" if "U2B-POINTER" in out0 else
                   "the server never moved its pointer: the input path from "
                   "the USB mouse to Xfb is the failure, not the toolkit")

        # 1. a click on the first button fires its action
        px, py = point("PRESS")
        mon.click_at(*at(px, py), settle=1.0)
        session.wait_for(r"U2B-ACTION pressed n=1", 30)
        out2 = session.output_since(mark)
        ptr = re.findall(r"U2B-POINTER x=(-?\d+) y=(-?\d+)", out2)
        self.check("pointer-went-to-the-button",
                   any(abs(int(x) - int(px)) <= 6 and abs(int(y) - int(py)) <= 6
                       for x, y in ptr),
                   "the pointer reached (%d,%d) on screen; positions seen: %s"
                   % (int(px), int(py), ptr[-3:]))
        self.check("click-fires-action", "U2B-ACTION pressed n=1" in out2,
                   "one click on the button sent the action exactly once"
                   if "U2B-ACTION pressed n=1" in out2
                   else "no action; guest tail: " + session.tail())

        # 2. a click on the toggle flips its state (and it stays)
        tx, ty = point("TOGGLE")
        mon.click_at(*at(tx, ty), settle=1.0)
        session.wait_for(r"U2B-ACTION toggled n=1", 30)
        out3 = session.output_since(mark)
        self.check("toggle-flips", "U2B-ACTION toggled n=1 state=1" in out3,
                   "the toggle reported state=1 after its click"
                   if "U2B-ACTION toggled n=1 state=1" in out3
                   else "toggle did not reach state 1; tail: "
                   + session.tail())

        # 3. the close box closes the window (the window's own chrome).
        # The titlebar DRAG is deliberately NOT here: it is unverified and
        # its wedge would hide this check - see uikit_u2b_drag.py.
        cx, cy = point("CLOSE")
        mon.click_at(*at(cx, cy), settle=1.0)
        session.wait_for(r"U2B-OK", 60)
        out5 = session.output_since(mark)
        # U2B-CLOSED, not U2B-OK: the probe also prints OK when its loop
        # times out, and that is exactly the vacuous pass to avoid
        self.check("close-box-closes", "U2B-CLOSED" in out5,
                   "clicking the window's own close box closed it"
                   if "U2B-CLOSED" in out5
                   else "the close box did nothing (probe said: "
                   + ("TIMEOUT" if "U2B-TIMEOUT" in out5 else "nothing")
                   + ")")
        m2 = re.search(r"U2B-CLOSED presses=(\d+) toggles=(\d+) state=(\d+)",
                       out5)
        if m2 is not None:
            self.check("one-press-one-action", m2.group(1) == "1",
                       "the button fired once for one click (presses=%s)"
                       % m2.group(1))
            self.check("no-double-fire", m2.group(2) == "1",
                       "the toggle fired once (toggles=%s)" % m2.group(2))
