# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSArray, whose storage IS a CFArray.

An ordered collection is where "an NS object IS a CF object" is easiest to fake: a class keeping its own item
vector and building a CFArray on demand satisfies every signature while the two worlds hold different
objects. So the two checks that carry this case are the ones that fail on a copy:

  * `and-CFs-own-C-door-sees-this-object-as-a-CFArray` — CFArrayGetCount, CF's own code, over an object this
    library built. That is the cast, and it is the library's whole claim;
  * `the-array-holds-its-items-with-CFs-own-callbacks` and `and-the-array-is-what-ends-it` — an item whose
    caller reference is dropped must survive, and must die when the ARRAY is released. Ownership by CF rather
    than by hand.

The remaining checks are the doors, each compared against an independently known answer rather than against
the class's own other door: a door that agrees with itself is not evidence. `-indexOfObject:` is checked
against kCFNotFound too, because that sentinel is CF's and this tree defines no NSNotFound.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_collection"
CHECKS = (
    "an-object-can-be-allocated-to-put-in",
    "an-array-can-be-made-from-a-vector",
    "the-class-declares-the-nsobject-protocol-itself",
    "retainCount-answers-CFs-count-and-not-the-element-count",
    "and-count-reports-what-was-put-in",
    "objectAtIndex-returns-the-object-that-went-in",
    "the-subscript-door-agrees-with-objectAtIndex",
    "and-an-index-past-the-end-raises",
    "firstObject-and-lastObject-are-the-two-ends",
    "containsObject-finds-a-member",
    "and-does-not-claim-a-stranger",
    "indexOfObject-answers-the-position",
    "and-answers-CFs-sentinel-for-a-stranger",
    "an-empty-array-has-no-first-and-no-last-object",
    "the-array-holds-its-items-with-CFs-own-callbacks",
    "and-the-array-is-what-ends-it",
    "and-CFs-own-C-door-sees-this-object-as-a-CFArray",
)


class Case(BaseCase):
    title = "Foundation collections: an NSArray whose storage is a CFArray"
    tier = "fast"
    # It runs a probe and reads its output, so the same guest can answer another case afterwards.
    shared_session = True
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

        self.check("probe-ran", "FOUNDATION-COLLECTION DONE" in out,
                   "the probe did not reach its end marker; output tail: " + out[-300:])
        failed = [l.strip() for l in out.splitlines() if " FAIL " in l]
        self.check("no-fail-lines", not failed, "; ".join(failed[:3]))

        ok = fail = -1
        result = [l for l in out.splitlines() if "RESULT ok=" in l]
        if result:
            m = re.search(r"ok=(-?\d+) fail=(-?\d+)", result[-1])
            if m:
                ok, fail = int(m.group(1)), int(m.group(2))
        self.check("result-line", ok >= 0, "the probe printed no RESULT line")
        self.check("every-check-passed", ok == len(CHECKS) and fail == 0,
                   "the probe reported ok=%d fail=%d against %d checks" % (ok, fail, len(CHECKS)))

        status = [l for l in out.splitlines() if "FOUNDATION-COLLECTION-STATUS=" in l]
        self.check("exit-status", bool(status) and status[-1].rstrip().endswith("STATUS=0"),
                   "the probe exited non-zero: " + (status[-1].strip() if status else "no status line"))
