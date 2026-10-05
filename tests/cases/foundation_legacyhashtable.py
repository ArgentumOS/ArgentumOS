# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`NSHashTable`'s legacy C API — §62.45's acceptance, the same design as `NSMapTable`'s.

Apple deprecated the pre-10.5 functions and the call-back STRUCT when `NSPointerFunctions` arrived, and §62.24's
policy put them back on the work list. The design question was settled by the map table's unit: Apple's call-backs
are promised THE TABLE, a C function pointer cannot close over one, so the implementation is a private subclass that
hands itself to every call-back — AND IT SHARES THE MAP TABLE'S SCAN rather than writing a second one: the hash
table's legacy mode stores each element as the inner table's KEY and passes no value call-backs at all.

TWO OF THE FAMILY'S TWENTY-EIGHT NAMES ARE **NOT** HERE: `NSCreateHashTableWithZone` and `NSCopyHashTableWithZone`
take an `NSZone`, and this library removed the type on purpose (`NSObjCRuntime.h` records the sequence). Their rows
stay owed with that ground; the other twenty-six land.

The probe is `/System/Shared/tests/foundation_legacyhashtable`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-eight-call-back-sets-promise-what-their-names-say` — value vs identity, owned vs non-owned, and the option;
  * `the-table-exists-and-is-empty`                  — `NSCreateHashTable` and an empty table's member;
  * `the-table-is-handed-to-a-call-back-and-the-scan-compares` — the hand, and the scan's use of `isEqual`;
  * `insert-if-absent-answers-the-member-that-was-there` — the answer and the count;
  * `the-known-absent-form-added-its-member`         — the contract form;
  * `remove-takes-one-away-and-gives-it-back-through-the-release` — the caller's ownership;
  * `the-enumeration-walks-every-object`             — `NSEnumerateHashTable`/`…Item`/`…End`;
  * `reset-empties-it`                               — every member released exactly once;
  * `an-equal-but-distinct-object-is-the-same-member` — the value-based personality;
  * `compare-answers-content-and-the-set-relations-are-answered` — four doors, answered not inherited;
  * `a-copy-is-its-own-table`                        — a copy, not a second name for one;
  * `intersect-keeps-what-is-in-both`                — the mutating set relation.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_legacyhashtable"
CHECKS = (
    "the-eight-call-back-sets-promise-what-their-names-say",
    "the-table-exists-and-is-empty",
    "the-table-is-handed-to-a-call-back-and-the-scan-compares",
    "insert-if-absent-answers-the-member-that-was-there",
    "the-known-absent-form-added-its-member",
    "remove-takes-one-away-and-gives-it-back-through-the-release",
    "the-enumeration-walks-every-object",
    "reset-empties-it",
    "an-equal-but-distinct-object-is-the-same-member",
    "compare-answers-content-and-the-set-relations-are-answered",
    "a-copy-is-its-own-table",
    "intersect-keeps-what-is-in-both",
)


class Case(BaseCase):
    title = "NSHashTable's legacy C API: the same hand, and one shared scan (§62.45)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_legacyhashtable")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-LEGACYHASHTABLE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-LEGACYHASHTABLE "):
                self.note(line)

        done = "FOUNDATION-LEGACYHASHTABLE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-LEGACYHASHTABLE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-LEGACYHASHTABLE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-LEGACYHASHTABLE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-LEGACYHASHTABLE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
