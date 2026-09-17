# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The Foundation's collections — F3's acceptance.

docs/design/foundation-plan.md §5 (F3). The probe is
`/System/Shared/tests/foundation_collection`, built from two translation units.

  * `array-basic`     — count/index/first/last/contains, out of range is nil;
  * `array-equality`  — content equality, and that ORDER matters;
  * `array-mutable`   — add/insert/remove, and `-copy` as a snapshot;
  * `array-enumerate` — `for (id x in array)` visits every element in order,
                        which is clang's fast-enumeration lowering over the
                        protocol whose name, selectors and state layout are ABI;
  * `dict-basic`      — set/get, overwrite without growing the count, missing key
                        is nil;
  * `dict-key-copy`   — the key decision, measured both ways: a key is COPIED, so
                        mutating the caller's object after insert still finds the
                        value AND the original's retain count is unchanged;
  * `dict-equality`   — the same pairs built in a different ORDER are equal and
                        hash alike (which is why the dictionary's hash is a sum);
  * `dict-enumerate`  — `for (id k in dict)` yields every key once, and a nested
                        collection is held by reference;
  * `ownership`       — an array element is retained on insert, released on
                        removal (measured with the runtime's retain count).
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_collection"
CHECKS = ("array-basic", "array-equality", "array-mutable", "array-enumerate",
          "dict-basic", "dict-key-copy", "dict-equality", "dict-enumerate",
          "ownership", "number-key", "cocoa-spellings",
          "array-api-complete", "array-extras")


class Case(BaseCase):
    title = "the Foundation's collections: arrays, dictionaries, fast enumeration"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_collection")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-COLLECTION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-COLLECTION "):
                self.note(line)

        done = "FOUNDATION-COLLECTION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-COLLECTION DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-COLLECTION %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS),
                           ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-COLLECTION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-COLLECTION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
