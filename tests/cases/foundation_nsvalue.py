# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSValue and NSNull — F13.8c's acceptance.

docs/design/foundation-plan.md §10. `NSValue` is the box for things that are not objects (a boxed
`NSNumber` is for NUMBERS, which is a different job), and `NSNull` is the object that stands for
nothing so a collection can hold a hole. The probe is `/System/Shared/tests/foundation_nsvalue`, ONE
unit, importing only `<Foundation/Foundation.h>`.

The case is named `foundation_nsvalue` because `foundation_value` was ALREADY TAKEN: it is F2/F8's
probe for NSNumber/NSData/NSDate, tracked with its own header and support unit. A new probe's name
has to be checked against the suite first.

  * `value-bytes-roundtrip`       — a padded structure in and out, with its encoding preserved;
  * `value-copies-exactly-its-size` — THE MEASUREMENT THAT MATTERS, and a CANARY rather than a plain
                                    round trip: an oversized source of 0xAA and a destination of
                                    0x55, so copying too much smears and copying too little leaves
                                    0x55 inside the structure. A round trip alone cannot tell either
                                    apart, because `-getValue:` copies back what it was told;
  * `value-pointer`               — an address stored BY VALUE and handed back equal;
  * `value-range`                 — `NSRange` support and Cocoa's readable `NSRange: {2, 5}` text;
  * `value-point` / `value-size` / `value-rect` — the three FOUNDATION-GEOMETRY boxes, each built by its own
                                    door and read back by its own reader over the payload primitive; the point
                                    is additionally compared against a box made with the CG spelling, because
                                    `NSPoint` and `CGPoint` are one type here (`@encode` proves it);
  * `value-cgpoint` / `value-cgsize` / `value-cgrect` / `value-cgvector` / `value-cgaffinetransform` — the
                                    five COREGRAPHICS-geometry boxes, each read back through its own reader with
                                    its encoding checked (the CGPoint also against the Foundation spelling);
  * `value-edge-insets`           — `NSEdgeInsets` boxed and read back; the encoding asserted is this tree's
                                    `@encode(NSEdgeInsets)` (`"{_NSEdgeInsets=dddd}"`, a struct-tag deviation
                                    from Apple's `"{NSEdgeInsets=dddd}"` — same layout, different string);
  * `value-nonretained-object`    — a BORROWED object: `-objCType` is `"^v"`, `-pointerValue` is the same
                                    address, and `-nonretainedObjectValue` hands the SAME object back;
  * `value-init-with-bytes`       — the instance spelling `-initWithBytes:objCType:` agrees with
                                    `+valueWithBytes:objCType:`;
  * `value-in-a-container`        — two equal boxes collapse to ONE member of an `NSSet`, which is
                                    the `-hash`/`-isEqual:` contract working, plus `-copy` being
                                    the box itself;
  * `null-is-one-object`          — the singleton enforced at ALLOCATION, so a fresh alloc is `+null`;
  * `null-holds-a-place-in-a-collection` — an `NSNull` as an array element and as a DICTIONARY KEY,
                                    which is the load-bearing use, plus Cocoa's `"<null>"`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_nsvalue"
CHECKS = (
	  "cg-spelled-geometry-doors-are-the-coregraphics-tier-s", "value-bytes-roundtrip", "value-copies-exactly-its-size", "value-pointer",
	  "value-range", "value-point", "value-size", "value-rect",
	  "value-edge-insets", "value-nonretained-object", "value-init-with-bytes", "value-in-a-container",
	  "null-is-one-object", "null-holds-a-place-in-a-collection",
)


class Case(BaseCase):
    title = "NSValue / NSNull: a box for bytes, and the object that is nothing"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_nsvalue")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-NSVALUE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-NSVALUE "):
                self.note(line)

        done = "FOUNDATION-NSVALUE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-NSVALUE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-NSVALUE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-NSVALUE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-NSVALUE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
