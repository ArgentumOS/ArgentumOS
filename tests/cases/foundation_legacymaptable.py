# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`NSMapTable`'s legacy C API — §62.44's acceptance.

docs/design/foundation-plan.md §62.24 put the pre-10.5 functions and call-back STRUCTS back on the work list (Apple
deprecated them when `NSPointerFunctions` arrived, and this is the largest family left on that list).

THE CHECK THIS CASE EXISTS FOR IS `the-call-backs-are-handed-their-table`, WHICH IS THE DESIGN'S WHOLE POINT. The
obvious implementation would have bridged these call-backs onto `NSPointerFunctions` — the internal engine's own
shape — and it cannot be done: the engine's function pointers take `(const void *item, …)` while Apple's take
`(NSMapTable *table, const void *key)`, and a C function pointer cannot close over its table. The implementation is
a private subclass that hands `self` to every call-back, and the probe registers a call-back that RECORDS WHAT IT
WAS HANDED — if the table were lost anywhere between the caller and the engine, that check is where it would show.

TWO OF THE FAMILY'S THIRTY-EIGHT NAMES ARE **NOT** HERE: `NSCreateMapTableWithZone` and `NSCopyMapTableWithZone`
take an `NSZone`, and this library has no zone type — `NSObjCRuntime.h` records the user-driven sequence that
removed every zone-taking method, then every zone-returning one, and finally the type. Their ledger rows stay owed
with that ground; the other thirty-six land.

The probe is `/System/Shared/tests/foundation_legacymaptable`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-three-sentinels-are-pinned`                 — the markers a call-back returns for "no answer";
  * `the-thirteen-call-back-sets-promise-what-their-names-say` — object sets retain, non-owned sets do not;
  * `the-table-exists-and-is-empty`                  — `NSCreateMapTable` and its count;
  * `get-and-count-and-the-table-handed-to-a-call-back` — and the hand itself, which is the design;
  * `the-table-is-handed-on-every-call`              — not once at creation, but on every door;
  * `member-answers-the-original-key-and-the-value`  — both out-parameters;
  * `insert-if-absent-answers-what-was-there`        — the old value, or NULL;
  * `insert-known-absent-adds-once`                  — the contract form;
  * `the-enumeration-walks-every-pair-once`          — `NSEnumerateMapTable`/`…Pair`/`…End`;
  * `keys-and-values-come-back-as-arrays`            — the two convenience walks;
  * `an-integer-key-is-its-own-value-and-absent-answers-null` — the integer personality;
  * `compare-answers-content-not-order`              — `NSCompareMapTables`;
  * `a-copy-is-its-own-table`                        — a copy, not a second name for one;
  * `reset-empties-it`                               — `NSResetMapTable`;
  * `a-release-call-back-is-called-for-every-pair-that-goes` — the caller's ownership, honoured.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_legacymaptable"
CHECKS = ("the-three-sentinels-are-pinned",
          "the-thirteen-call-back-sets-promise-what-their-names-say",
          "the-table-exists-and-is-empty",
          "get-and-count-and-the-table-handed-to-a-call-back",
          "the-table-is-handed-on-every-call",
          "member-answers-the-original-key-and-the-value",
          "insert-if-absent-answers-what-was-there",
          "insert-known-absent-adds-once",
          "the-enumeration-walks-every-pair-once",
          "keys-and-values-come-back-as-arrays",
          "an-integer-key-is-its-own-value-and-absent-answers-null",
          "compare-answers-content-not-order",
          "a-copy-is-its-own-table",
          "reset-empties-it",
          "a-release-call-back-is-called-for-every-pair-that-goes")


class Case(BaseCase):
    title = "NSMapTable's legacy C API: the call-backs, the table, and the hand (§62.44)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_legacymaptable")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-LEGACYMAPTABLE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-LEGACYMAPTABLE "):
                self.note(line)

        done = "FOUNDATION-LEGACYMAPTABLE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-LEGACYMAPTABLE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-LEGACYMAPTABLE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-LEGACYMAPTABLE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-LEGACYMAPTABLE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
