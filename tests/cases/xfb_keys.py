"""Xfb: does a typed key reach the guest's X server at all?

A raw-Xlib probe (no toolkit) maps a window, takes the input focus and
logs every keysym it receives. This case types a known word with the QEMU
monitor and asks whether it arrived — because "the toolkit's key handling
does not work" and "no key reaches the guest" look identical from inside
the toolkit.

The guest boots with a USB keyboard and NO PS/2 controller
(`pc,i8042=off`), which is the configuration the kernel's keyboard device
node and Xfb's default path both have to survive.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/x_keys"
WORD = "fnx"


class Case(BaseCase):
    title = "Xfb: a typed key reaches the X server"
    tier = "slow"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-kbd"])
        ready = session.shell_ready(150)
        self.check("shell-ready", ready, "the serial console has a shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s 25 &" % PROBE)
        if not session.wait_for(r"XKEYS-READY", 120):
            self.check("probe-ready", False,
                       "no XKEYS-READY; guest tail: " + session.tail())
            return
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("XKEYS-"):
                self.note(line)
        self.check("probe-ready", True,
                   "the raw key reader took the focus and is listening")

        mon = session.monitor()

        # type the word, key by key: sendkey takes X key names
        for ch in WORD:
            mon.key(ch, settle=0.25)
        session.wait_for(r"XKEYS-DONE", 40)
        out2 = session.output_since(mark)
        keys = [l for l in out2.splitlines() if l.startswith("XKEYS-KEY ")]
        for line in keys:
            self.note(line)
        self.check("keys-arrived", len(keys) >= len(WORD),
                   "%d key(s) of the %d typed reached the X server"
                   % (len(keys), len(WORD)))
        text = "".join(l.split("text=")[-1] for l in keys)
        self.check("the-word-arrived", WORD in text,
                   "the server read \"%s\"" % text)
