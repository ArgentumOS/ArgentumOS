"""U0: Auto Layout — the constraint model and the solver.

The display-free probe builds a small constraint system (edges, sizes,
centres, a multiplier, an inequality and a priority conflict), solves it
and asserts the resulting frames.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/layout_solve"


class Case(BaseCase):
    title = "UIKit U0: Auto Layout constraints solve to the expected frames"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        # no desktop any more (the class layer is being rebuilt): the
        # probe is display-free, so a shell is all it needs
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("test -x %s && %s; echo U0-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if line.startswith("U0"):
                self.note(line)

        self.check("probe-ran", "U0-OK" in out,
                   "the constraint probe solved its system")
        self.check("probe-exit-zero", "U0-EXIT=0" in out,
                   "the probe exited 0")
        self.check("no-failures", "U0-FAIL" not in out,
                   "no frame assertion failed")
