# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The free functions the library lacked — §62.52's acceptance.

The stragglers in Apple's `NSObjCRuntime.h` and `NSGeometry.h`: logging, the page arithmetic, what the machine has,
a range from its own description, the extra retain count, the stack doors, the two rectangle predicates and the four
garbage-collector doors.

EVERY CHECK IS AN ABSOLUTE ONE OR A RELATION, NEVER "SOMETHING CHANGED": a page size must BE a power of two and
`NSLogPageSize()` must agree with the logarithm its own definition gives; a rounding must move a non-multiple and
leave a multiple alone; a rectangle that only touches must answer NO; a point on the minimum edge must be inside and
one on the maximum edge outside, in both coordinate systems.

TWO OF THE CHECKS EXIST BECAUSE OF WHAT THIS FAMILY COULD NOT DO. The stack doors are ASKED FOR LEVELS THEY CANNOT
HONOUR and must answer NULL rather than read a frame that may not be there — which is also why `NSCountFrames` is
absent rather than dangerous. And `NSLog` is measured BY REDIRECTING STANDARD ERROR to a file and reading back what
arrived, because a log function that wrote nowhere would pass any check that merely called it.

`NSCopyObject` IS NOT HERE, and the ground is measurable rather than a judgement: a faithful raw byte copy must give
the copy a RETAIN COUNT OF ONE, and this library's runtime publishes no way to SET a count — only to read, retain
and release.

The probe is `/System/Shared/tests/foundation_free_misc`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-page-size-is-a-power-of-two-and-log-page-size-is-its-logarithm`
  * `rounding-moves-a-non-multiple-and-leaves-a-multiple-alone`
  * `the-real-memory-available-is-a-plausible-whole-number-of-pages`
  * `a-range-comes-back-from-its-own-description-and-nonsense-is-empty`
  * `touching-is-not-intersecting-but-overlapping-and-contained-are`
  * `a-point-on-the-minimum-edge-is-inside-and-one-on-the-maximum-edge-is-not`
  * `the-extra-count-is-the-retain-count-beyond-the-basic-one`
  * `the-stack-doors-answer-level-zero-and-refuse-what-they-cannot-honour`
  * `the-collector-doors-are-the-documented-behaviour-for-an-uncollected-program`
  * `a-log-line-reaches-standard-error-with-its-arguments-formatted`
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_free_misc"
CHECKS = (
    "the-page-size-is-a-power-of-two-and-log-page-size-is-its-logarithm",
    "rounding-moves-a-non-multiple-and-leaves-a-multiple-alone",
    "the-real-memory-available-is-a-plausible-whole-number-of-pages",
    "a-range-comes-back-from-its-own-description-and-nonsense-is-empty",
    "touching-is-not-intersecting-but-overlapping-and-contained-are",
    "a-point-on-the-minimum-edge-is-inside-and-one-on-the-maximum-edge-is-not",
    "the-extra-count-is-the-retain-count-beyond-the-basic-one",
    "the-stack-doors-answer-level-zero-and-refuse-what-they-cannot-honour",
    "the-collector-doors-are-the-documented-behaviour-for-an-uncollected-program",
    "a-log-line-reaches-standard-error-with-its-arguments-formatted",
)


class Case(BaseCase):
    title = "The free functions: pages, ranges, rects, logs and the stack doors (§62.52)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_free_misc")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FREEFUNCS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FREEFUNCS "):
                self.note(line)

        done = "FOUNDATION-FREEFUNCS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FREEFUNCS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FREEFUNCS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FREEFUNCS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FREEFUNCS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
