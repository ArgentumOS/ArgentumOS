# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The byte-order conversions — §62.51's acceptance.

Apple deprecated the thirty `NSSwap*` functions at 10.9 in favour of the `CFSwap*` spelling; §62.24's policy puts
them back, and the twelve CROSS directions come with them because a caller who needs big-endian bytes on a
little-endian machine has no other door. All forty-two are declared in `NSByteOrder.h` and implemented beside the
host-order function in `NSObject.m` — one helper per width, one direction rule, and the host asked rather than
assumed.

THIS FAMILY IS PROVABLE RATHER THAN MERELY PRESENT, WHICH IS WHY THE PROBE PROVES IT FOUR WAYS: a known value
becomes the known value per width (and a float and a double by their BYTES, since their reversed values are not
readable numbers); swapping twice is the identity for every function in the family; a host conversion is the
identity when the direction is the host's own and a reversal when it is not, with `NSHostByteOrder()` printed so the
reading is checkable; and a conversion between two NON-host orders is a reversal whatever the host is.

THE LAST CHECK IS THE CONTROL: a 32-bit implementation would pass every 32-bit check above and fail the 64-bit one —
the kind of half-right that a check on "something changed" would miss.

The probe is `/System/Shared/tests/foundation_byteorder`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-plain-swaps-reverse-the-bytes`
  * `swapping-twice-is-the-identity-for-the-whole-family`
  * `the-host-conversions-are-the-identity-or-a-reversal`
  * `the-host-doors-mirror-the-host-conversions`
  * `a-conversion-between-two-non-host-orders-is-always-a-reversal`
  * `the-wide-swap-moves-a-wide-value`
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_byteorder"
CHECKS = (
    "the-plain-swaps-reverse-the-bytes",
    "swapping-twice-is-the-identity-for-the-whole-family",
    "the-host-conversions-are-the-identity-or-a-reversal",
    "the-host-doors-mirror-the-host-conversions",
    "a-conversion-between-two-non-host-orders-is-always-a-reversal",
    "the-wide-swap-moves-a-wide-value",
)


class Case(BaseCase):
    title = "The byte-order conversions: four proofs and a control (§62.51)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_byteorder")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-BYTEORDER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-BYTEORDER "):
                self.note(line)

        done = "FOUNDATION-BYTEORDER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-BYTEORDER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-BYTEORDER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-BYTEORDER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-BYTEORDER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
