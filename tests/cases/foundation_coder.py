# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The coder family — F13.12's acceptance.

docs/design/foundation-plan.md §10. `NSCoding` is the protocol a class adopts to be archivable,
`NSCoder` is the abstract base, and `NSKeyedArchiver`/`NSKeyedUnarchiver` are what an object is
archived INTO and back out of. The probe is `/System/Shared/tests/foundation_coder`, ONE unit,
importing only `<Foundation/Foundation.h>`.

  * `coder-round-trip-scalars`     — a name, an integer, a double and a bool across one archive;
  * `coder-round-trip-collections` — an array of strings and a dictionary of mixed values;
  * `coder-round-trip-sets`        — a set and a mutable set, deduplicated BY VALUE, with the archive
                                     naming the PUBLIC class, and a counted set's multiplicities
                                     surviving (they are a SECOND payload: `-allObjects` answers each
                                     distinct member once and cannot carry them);
  * `coder-shared-objects`         — the SAME object referenced twice comes back as ONE object,
                                     asserted BY POINTER (the memo table's whole purpose);
  * `coder-cycle`                  — an object whose link points back at its parent, which does not
                                     terminate unless an index is reserved BEFORE the contents are
                                     written;
  * `coder-null-and-nil`           — a nil link is nil again and an `NSNull` is an `NSNull`;
  * `coder-archive-shape`          — the data IS a property list with `$version`/`$objects`/`$top`,
                                     `$null` at index 0, and a class entry carrying its `$classes`;
  * `coder-base-raises`            — the abstract `NSCoder`'s doors raise rather than answering a zero
                                     that looks like data.

THE ONE DEPARTURE FROM COCOA, named because it is the only one: a reference is `{"$ref": n}` rather
than Cocoa's `UID` property-list type, which this library's plist reader and writer cannot express.
The TABLE structure is Cocoa's. An archive written here is readable here and is NOT byte-compatible
with Cocoa's. Also absent, and named in the headers: `NSSecureCoding`, class-name substitution,
delegates, and the codec's non-keyed doors.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_coder"
CHECKS = ("coder-round-trip-scalars", "coder-round-trip-collections", "coder-round-trip-sets",
          "the-set-archive-names-the-public-class", "coder-round-trip-counted-set",
          "coder-shared-objects",
          "coder-cycle", "coder-null-and-nil", "coder-archive-shape", "coder-base-raises",
          # W9: the two delegates (through Cocoa's instance flow) and the secure transformer
          "coder-archiver-delegate", "coder-instance-flow", "coder-unarchiver-delegate",
          "value-transformer-secure-unarchive")


class Case(BaseCase):
    title = "NSCoding / NSCoder / NSKeyedArchiver: an object graph that survives a round trip"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_coder")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CODER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CODER "):
                self.note(line)

        done = "FOUNDATION-CODER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CODER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CODER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CODER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CODER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
