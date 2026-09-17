# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Objective-C on the guest: the runtime, across two translation units.

`libobjc2` is the runtime this OS adopted (docs/design/objc-toolchain-plan.md),
and this case is its acceptance. It runs `/System/Shared/tests/objc_smoke`.

What makes the probe worth running is that it is built from TWO translation
units: the class and its root class live in `objc_smoke_support.m`, while the
CATEGORY on that class lives in `objc_smoke.m`. Cross-translation-unit class
registration is exactly the case that was misdiagnosed once during P1 (see the
plan's record), so it is asserted here rather than assumed.

The probe prints one `OBJC-SMOKE <name> ok|FAIL <detail>` line per check and a
final `OBJC-SMOKE RESULT ok=N fail=M`; the case asserts every check BY NAME, the
probe's own tally, its exit status, and that no FAIL line exists anywhere.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/objc_smoke"
CHECKS = ("class", "greet", "origin", "category.twice", "protocol", "property",
          "block", "arc.block", "catch", "synchronized", "pool")


class Case(BaseCase):
    title = "the Objective-C runtime: a class, a cross-TU category, ARC and blocks"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("objc_smoke")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo OBJC-SMOKE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("OBJC-SMOKE "):
                self.note(line)

        done = "OBJC-SMOKE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no OBJC-SMOKE DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        # Every check, by name - a probe that stops early cannot pass by
        # printing a good-looking tally.
        missing = [c for c in CHECKS
                   if not re.search(r"^OBJC-SMOKE %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   "%d of %d checks reported ok" % (len(CHECKS) - len(missing),
                                                    len(CHECKS))
                   if missing else "all %d checks reported ok" % len(CHECKS))

        fails = [l for l in out.splitlines() if l.endswith("FAIL")
                 or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails
                   else "; ".join(fails))

        tally = re.search(r"OBJC-SMOKE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally
                                                  else "missing"))

        self.check("exit-status", "OBJC-SMOKE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
