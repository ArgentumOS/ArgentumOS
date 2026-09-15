"""The QEMU monitor socket: pointer, keys, screendumps.

Three rules are baked in here because each of them cost a gate run where a
click simply vanished:

  * the guest drops ps/2 mouse chunks sent faster than it drains them, so a
    move is sent in <=127-count steps with a pause between them;
  * an unequal two-axis jump is lost the same way, so `goto` stages one axis
    at a time;
  * a button change with no motion in the same command is dropped, so every
    press is bracketed by a 1px nudge.

The emulated pointer starts at (0, 0) after a boot and the guest never reports
where it is, so the position is tracked here in `pos`.  Drive the whole
interaction from ONE monitor: a second socket adds its own timing skew.
"""

import os
import socket
import time


class Monitor:
    def __init__(self, path, timeout=45):
        self.pos = [0, 0]
        self.sock = None
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                s = socket.socket(socket.AF_UNIX)
                s.connect(path)
                self.sock = s
                break
            except OSError:
                time.sleep(0.5)
        if self.sock is None:
            raise RuntimeError("no monitor socket at %s" % path)

    def send(self, line):
        self.sock.send((line + "\n").encode())

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    # --- pointer ------------------------------------------------------
    def move(self, dx, dy, dt=0.03):
        """Relative move; dy is positive UP, as the monitor reports it."""
        while dx or dy:
            sx = max(-127, min(127, dx))
            sy = max(-127, min(127, dy))
            self.send("mouse_move %d %d" % (sx, sy))
            self.pos[0] += sx
            self.pos[1] -= sy
            dx -= sx
            dy -= sy
            time.sleep(dt)

    def goto(self, x, y, dt=0.06):
        """Absolute move, one axis at a time (see the module docstring)."""
        self.move(x - self.pos[0], 0, dt)
        self.move(0, self.pos[1] - y, dt)
        time.sleep(0.15)

    def park(self):
        """Drive into the top-left corner, where the guest clamps the pointer,
        and adopt (0, 0) as the known position."""
        self.move(-2000, 2000, 0.12)
        self.pos = [0, 0]
        time.sleep(0.4)

    def nudge(self):
        self.move(1, 0, 0.05)
        self.move(-1, 0, 0.05)

    def click(self, button=1, settle=0.6):
        """Click at the current position.  `settle` lets the guest react."""
        self.nudge()
        self.send("mouse_button %d" % button)
        time.sleep(0.2)
        self.nudge()
        self.send("mouse_button 0")
        time.sleep(settle)

    def click_at(self, x, y, button=1, settle=0.6):
        self.goto(x, y)
        self.click(button, settle)

    # --- press / drag / release ---------------------------------------
    def press(self, button=1, settle=0.3):
        """Hold a button down.  Pair with release() to script a drag."""
        self.nudge()
        self.send("mouse_button %d" % button)
        time.sleep(settle)

    def release(self, button=0, settle=0.4):
        """Let the button up (0 = none held)."""
        self.nudge()
        self.send("mouse_button %d" % button)
        time.sleep(settle)

    def drag(self, x0, y0, x1, y1, steps=8, settle=0.4):
        """Press at (x0,y0), move to (x1,y1) in steps, release.

        Steps matter: a drag is a sequence of motion events, and a control
        that only looks at the release would pass even if tracking were
        broken.
        """
        self.goto(x0, y0)
        self.press()
        for i in range(1, steps + 1):
            self.goto(int(x0 + (x1 - x0) * i / steps),
                      int(y0 + (y1 - y0) * i / steps), dt=0.05)
        self.release(settle=settle)

    # --- keyboard -----------------------------------------------------
    def key(self, name, settle=0.1):
        self.send("sendkey %s" % name)
        time.sleep(settle)

    def type_text(self, text, settle=0.05):
        for ch in text:
            self.key(ch, settle)

    # --- screen -------------------------------------------------------
    def screendump(self, path, timeout=25):
        """Ask for a PPM and wait until the file has stopped growing."""
        try:
            os.unlink(path)
        except OSError:
            pass
        self.send("screendump %s" % path)
        deadline = time.time() + timeout
        last, stable = -1, 0
        while time.time() < deadline:
            try:
                size = os.path.getsize(path)
            except OSError:
                time.sleep(0.2)
                continue
            if size > 0 and size == last:
                stable += 1
                if stable >= 2:
                    return path
            else:
                stable = 0
            last = size
            time.sleep(0.2)
        raise RuntimeError("screendump %s never settled" % path)
