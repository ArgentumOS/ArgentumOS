# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""CoreFoundation on the guest — M1's acceptance (docs/design/foundation-cf-core-plan.md §7).

The adopted CoreFoundation (`third_party/swift-corelibs-foundation` at pin `44cd6163`, Apache-2.0 with
the Runtime Library Exception) built as its own shared library, `libcorefoundation.so.1`, linked against
the vendored libdispatch and the tree's own ICU.

THIS CASE JUDGES THE C CORE AS A C LIBRARY, and that is the deliberate scope. M1's claim is *vendor and
build*: the library loads, the runtime beneath a CF object works, and a CFString and a CFArray carry
their bytes and their count. The probe is C rather than Objective-C so that a failure here cannot be a
failure of the plan's toll-free bridge (D3/D8) — there is no bridging in it to fail. M2's acceptance is
therefore the EXISTING Foundation probes (`make test TESTS=foundation_string` and its neighbours), not
this one: a re-base that passes those has changed no behaviour the tree ever asserted, which is the
plan's whole verification argument (§8).

  * `cfstring-create-succeeds`             — the runtime allocates a CF object at all;
  * `cfstring-length-is-the-character-count` — `CFStringGetLength` agrees with `strlen`;
  * `cfstring-round-trips-its-bytes`       — the bytes read back OUT of the object equal the bytes put
                                             in, which a length check alone would not catch;
  * `cfarray-create-succeeds`              — the array runtime, over the same string;
  * `cfarray-counts-and-indexes`           — count AND the identity of the element at index 0;
  * `cfarray-and-cfstring-have-different-type-ids` — two types that answer one `CFTypeID` would make
                                             every type check above meaningless.

Every object the probe creates is RELEASED, so the deallocate path runs and is not merely compiled.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/corefoundation_smoke"
CHECKS = (
    "cfstring-create-succeeds",
    "cfstring-length-is-the-character-count",
    "cfstring-round-trips-its-bytes",
    "cfarray-create-succeeds",
    "cfarray-counts-and-indexes",
    "cfarray-and-cfstring-have-different-type-ids",
)


class Case(BaseCase):
    title = "CoreFoundation on the guest: the adopted C core loads and carries bytes"
    tier = "fast"
    # It only runs a probe and reads its output, so the same guest can answer it after another case.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("corefoundation_smoke")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo COREFOUNDATION-SMOKE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            # The measurements are the point of this run: surface them, because the assertions are
            # written from what the probe reports rather than from a guess.
            if line.startswith("COREFOUNDATION-SMOKE "):
                self.note(line)

        done = "COREFOUNDATION-SMOKE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no COREFOUNDATION-SMOKE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^COREFOUNDATION-SMOKE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"COREFOUNDATION-SMOKE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "COREFOUNDATION-SMOKE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
