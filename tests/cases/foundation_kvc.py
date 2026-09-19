# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSKeyValueCoding — F9's acceptance.

docs/design/foundation-plan.md §5 (F9). The probe is
`/System/Shared/tests/foundation_kvc`, built from two translation units; the
support unit defines the OBJECTS and imports ONLY the umbrella header, so the
cross-unit lookup — which is the family's own claim, that the name is resolved
through the runtime rather than through a compile-time list — is part of what is
being checked.

  * `kvc-accessors`   — the accessor forms, with the first letter's case folded;
  * `kvc-ivar`        — an object with no accessors: the runtime's ivar arm,
                        scalar ivars included;
  * `kvc-scalar-accessor` — a SCALAR accessor PAIR: the read has to be BOXED and
                        the write UNBOXED (the first run of this probe found it by
                        crashing — the number 20 read as a pointer);
  * `kvc-ivar-super`  — an ivar a SUPERCLASS declared, which is the case that
                        tells an absolute ivar offset from an inherited-view one;
  * `kvc-nil-ivar`    — an object ivar holding nil is FOUND, not skipped and then
                        raised on;
  * `kvc-keypath`     — a key path is the same lookup one dot at a time;
  * `kvc-operators`   — the operators that are folds;
  * `kvc-collection-unions` — @unionOfArrays / @distinctUnionOfArrays and @unionOfSets /
                        @distinctUnionOfSets: the operators whose VALUES are collections, and
                        therefore the ones that needed an NSSet to exist at all;
  * `kvc-collections` — the array MAPS and the dictionary LOOKS UP (or folds on @);
  * `kvc-null-shape` — a nil answer in a mapped array becomes NSNull, so the mapping keeps its
                        SHAPE (the count follows the receiver) and the hole is the singleton itself;
  * `kvc-operator-receivers` — the three spellings of one family AGREE: an array, a set and a
                        dictionary all fold, where the operator arm used to accept only an array;
  * `kvc-undefined`   — THE HONEST DEFAULTS: an undefined key raises, a nil
                        through a scalar raises, and an override is what answers;
  * `kvc-validate`    — the -validate<Key>:error: rule, present and absent;
  * `kvc-refusals`    — WHAT IS STILL ABSENT, and it is a shorter list than it used to be: the
                        KVO observer protocol's callback and Apple's private observation-info
                        class. The mutable proxies' reachability is PRINTED (`proxy-state`) rather
                        than asserted, because -mutableArrayValueForKey: is declared while the
                        proxies are not implemented;
  * `kvc-kvo-present`  — the surface KVO SHIPPED with at F13.9 is DEMANDED. This check exists
                        because its absence-asserting predecessor went red the moment KVO landed
                        and nobody looked: a probe asserting an absence is asserting a fact about
                        the tree (§11);
  * `cross-tu`        — an object built in the other unit answers here, by name.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_kvc"
CHECKS = ("kvc-accessors", "kvc-ivar", "kvc-scalar-accessor", "kvc-ivar-super",
          "kvc-nil-ivar", "kvc-keypath", "kvc-operators", "kvc-collection-unions",
          "kvc-collections", "kvc-null-shape", "kvc-operator-receivers", "kvc-undefined", "kvc-validate", "kvc-refusals",
          "kvc-kvo-present", "cross-tu")


class Case(BaseCase):
    title = "NSKeyValueCoding: the naming rules, on the runtime's ivar table"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_kvc")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-KVC-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-KVC "):
                self.note(line)

        done = "FOUNDATION-KVC DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-KVC DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-KVC %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-KVC RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-KVC-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
