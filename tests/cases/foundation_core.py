# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The Foundation's root class — F0's acceptance.

docs/design/foundation-plan.md §5. The probe is `/System/Shared/tests/foundation_core`,
built from TWO translation units (the subclass in `foundation_core_support.m`,
the checks in `foundation_core.m`) so cross-unit class registration stays under
test in the library as well as in the runtime.

What each check is for:

  * `lifecycle` — alloc/init/retain/release reach `-dealloc` exactly once, and the
                  root class's free runs without a double free or the runtime's
                  fast-ARC recursion (the `_ARCCompliantRetainRelease` marker);
  * `equality`  — the defaults (`isEqual:` is identity, `hash` is the pointer),
                  which is what a collection key must override;
  * `identity`  — `class`/`superclass`/`isKindOfClass:`/`isMemberOfClass:`/
                  `respondsToSelector:`;
  * `arc-pool`  — the RUNTIME's pools still work with the library linked. F0
                  ships no pool class on purpose: the runtime adopts any class
                  named NSAutoreleasePool as its own pool object (plan §6), so
                  ARC's `@autoreleasepool` is the pool interface;
  * `cross-tu`  — a method implemented in the other translation unit answers.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_core"
CHECKS = (
          "lifecycle", "equality", "identity", "arc-pool", "cross-tu", "nsobject-api-complete", "runtime-class-names", "runtime-selector-names", "runtime-range-string", "geometry-rects", "geometry-edges", "geometry-strings", "geometry-cg-types", "geometry-alignment", "c-byte-order", "kvc-operator-constants", "c-memory-pages", "c-size-and-alignment", "c-debug-switches", "methodsignature-parses", "methodsignature-grammar", "methodsignature-offsets", "methodsignature-sizes", "methodsignature-lookup", "invocation-api", "forwarding-hook", "forwarding-invocation", "forwarding-target", "nsobject-protocol", "affine-rotation-and-indexing", "affine-append-versus-prepend", "mutators-raise-out-of-range", "objectAtIndex-raises-out-of-range", "mrr-pool-refuses-retain", "mrr-pool-releases-on-drain", "mrr-proxy-forwards", "json-round-trip", "json-validity", "json-options", "json-reading-options", "undo-initial-state", "undo-registers", "undo-reverses-newest-first", "undo-redo-reapplies", "undo-remove-all-actions", "undo-grouping-level", "undo-grouping-unwinds", "undo-explicit-group-is-bounded", "undo-action-name-reaches-redo", "undo-registration-can-be-disabled", "copy-family-keeps-the-original",
          "undo-prepare-with-invocation-target", "undo-block-handler", "undo-notifications",
              "a-missing-method-raises-and-can-be-caught",
              # §62.99: the protocol pair Apple declares in NSObjCRuntime.h.
              "runtime-protocol-name-round-trip", "runtime-protocol-doors-refuse-what-is-absent",
)


class Case(BaseCase):
    title = "the Foundation's root class: lifetimes, identity, the runtime's pools"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_core")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CORE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CORE "):
                self.note(line)

        done = "FOUNDATION-CORE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CORE DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CORE %s ok$" % re.escape(c),
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

        tally = re.search(r"FOUNDATION-CORE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CORE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
