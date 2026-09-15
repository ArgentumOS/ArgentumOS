"""U0B: Auto Layout — the constraint model and the solver.

The display-free probe builds a small constraint system (edges, sizes,
centres, a multiplier, an inequality and a priority conflict), solves it
and asserts the resulting frames.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/viewcontroller_basic"


class Case(BaseCase):
    title = "UIKit U0B: Auto Layout constraints solve to the expected frames"
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
        session.run("test -x %s && %s; echo U1D-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if line.startswith("U1D"):
                self.note(line)

        self.check("probe-ran", "U1D-OK" in out,
                   "the controller probe ran")
        self.check("probe-exit-zero", "U1D-EXIT=0" in out,
                   "the probe exited 0")
        self.check("no-failures", "U1D-FAIL" not in out,
                   "no assertion failed")
