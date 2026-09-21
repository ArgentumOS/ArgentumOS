# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The pointer collections — W13's acceptance, slice A.

docs/design/foundation-plan.md §12.3 W13. `NSPointerFunctions` is the CALLOUTS a pointer collection
uses (they have to come from outside, because a raw pointer has no `-hash`, no `-isEqual:`, no
ownership and no `-description`), and `NSPointerArray` is the first client. The probe is
`/System/Shared/tests/foundation_pointers`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `pointer-functions-personalities` — the options really do select different DECISIONS: two equal but
                                       distinct objects agree under the object personality and
                                       DISAGREE under the object-pointer one, a C string compares by
                                       content across two buffers, and the opaque personality has no
                                       description function at all;
  * `pointer-array-slots`            — a NULL occupies a SLOT (`count` counts it), insertion and
                                       replacement keep the order, and only `-compact` closes the gap;
  * `pointer-array-ownership`        — the memory policy, measured by WHO DIES: an object created
                                       inside an autorelease pool survives the pool when a strong array
                                       holds it and does not when a weak one does — and the weak slot is
                                       NOT zeroed, which is the behaviour a caller must not misread;
  * `pointer-array-copy-in`          — `CopyIn` is a MEMORY decision, not a personality one: the array
                                       holds a copy, proven by mutating the original afterwards;
  * `pointer-array-archive`          — the object personalities round-trip through NSCoding, and a
                                       personality whose pointers are not objects is REFUSED by name
                                       rather than written as numbers a reader would take for objects.

TWO REAL BUGS WERE FOUND BY THESE CHECKS AND FIXED WHERE THEY BELONG (both recorded in the plan):

  * the keyed archiver writes the class it was handed, so an `NSMutableArray` inside an archive was
    written as `NSMutableArray` and the READER only knew `NSArray` — making such an archive unreadable
    ("NSMutableArray does not implement -initWithCoder:"). The existing coder checks used immutable
    literals and never reached it; `pointer-array-archive` does, because a pointer array's `-allObjects`
    is a mutable one;
  * an integer-personality array with STRONG memory sent `-retain` to the address 42. "Strong" means
    "retain the pointer as an OBJECT", so it is only meaningful for the object personalities, and the
    memory policy is now read together with the personality.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_pointers"
CHECKS = ("pointer-functions-personalities", "pointer-array-slots", "pointer-array-ownership",
          "pointer-array-copy-in", "pointer-array-archive",
          # W13b: the two collections built on the same callouts
          "hash-table-membership", "hash-table-growth", "hash-table-set-operations",
          "hash-table-enumeration", "map-table-pairs", "map-table-sides-and-enumeration")


class Case(BaseCase):
    title = "NSPointerFunctions / NSPointerArray: callouts, slots, ownership and the archive refusal"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_pointers")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-POINTERS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-POINTERS "):
                self.note(line)

        done = "FOUNDATION-POINTERS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-POINTERS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        seen = set(re.findall(r"FOUNDATION-POINTERS (\S+) (?:ok|FAIL)", out))
        missing = [name for name in CHECKS if name not in seen]
        self.check("every-check-reported", not missing,
                   "all %d checks reported" % len(CHECKS) if not missing
                   else "missing: " + ", ".join(missing))

        failed = [line for line in out.splitlines()
                  if line.startswith("FOUNDATION-POINTERS ") and " FAIL " in line]
        self.check("no-fail-lines", not failed,
                   "no check reported FAIL" if not failed else "; ".join(failed))

        tally = re.search(r"FOUNDATION-POINTERS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line", tally is not None and tally.group(2) == "0",
                   "the probe's own tally: " + (tally.group(0) if tally else "(absent)"))

        status = re.search(r"FOUNDATION-POINTERS-STATUS=(\d+)", out)
        self.check("exit-status", status is not None and status.group(1) == "0",
                   "the probe exited 0 (a non-zero status means a failed check)"
                   if status is not None and status.group(1) == "0"
                   else "status line: " + (status.group(0) if status else "(absent)"))
