"""Weaver IB0 - the interface document round-trips (docs/design/weaver-plan.md 8).

A document that cannot survive its own emitter is not a format, so this is the
first thing Weaver's work proves. The probe is display-free: the model, the
emitter and the libconfig reader need no X connection, no input and no staged
control - which is exactly why IB0 is the slice that can land first.

Idempotence alone would pass if every field were dropped, so the probe also
asserts, per fixture, on the content that must survive: the class, the
identifiers, each scalar kind, the strut binding, and the escapes.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/interface_roundtrip"


class Case(BaseCase):
    title = "Weaver IB0: the interface document round-trips"
    tier = "fast"
    timeout = 420

    def run(self, ctx):
        session = ctx.boot_to_desktop(secs=150)
        ready = session.shell_ready(90)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())

        mark = len(session.log_text())
        # `test -x` first, so a missing probe reads as a missing probe rather
        # than as a probe that mysteriously printed nothing.
        session.run("test -x %s && %s; echo IB0-EXIT=$?" % (PROBE, PROBE))
        out = session.output_since(mark)
        for line in out.strip().splitlines():
            if "IB0" in line:
                self.note(line)

        ran = "IB0:" in out
        self.check("probe-ran", ran,
                   "the probe ran" if ran
                   else "no probe output - is %s in the image? "
                        "(make rootagfs)" % PROBE)
        self.check("exit-code-zero", "IB0-EXIT=0" in out,
                   "the probe exited 0: every fixture round-tripped"
                   if "IB0-EXIT=0" in out
                   else "the probe exited non-zero: %s" % out.strip()[-200:])
        self.check("summary-ok", "IB0-OK" in out, "IB0-OK reported")

        fixtures = re.findall(r"IB0: fixture (\d+) \(([^)]*)\) OK", out)
        self.check("every-fixture-round-tripped", len(fixtures) >= 4,
                   "%d fixture(s) idempotent: %s"
                   % (len(fixtures), ", ".join(n for _, n in fixtures)))
