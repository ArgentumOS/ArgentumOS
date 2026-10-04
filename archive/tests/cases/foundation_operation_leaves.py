# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The two CONCRETE operations: NSBlockOperation and NSInvocationOperation.

The probe is `/System/Shared/tests/foundation_operation_leaves`, ONE unit importing only
`<Foundation/Foundation.h>`.

THE TWO CLASSES ARE THE OPPOSITE HALVES OF ONE IDEA — several units of work that belong to one operation, and one
method call held as an object — and both replace "subclass NSOperation and override -main". So the checks are about
what a caller can SEE: what ran, what it answered, and what it raised.

  * `a-block-operation-runs-its-block-and-finishes`, `several-blocks-run-in-the-order-they-were-added`;
  * `the-operation-is-not-finished-while-its-blocks-are-running` — Apple's sentence ("the operation itself is
    considered finished only when all blocks have finished executing") measured FROM INSIDE a block;
  * `adding-a-block-while-executing-or-finished-raises-by-name` — Apple's documented NSInvalidArgumentException,
    from both sides (after the run, and from inside a running block);
  * `a-block-added-from-a-dead-frame-still-runs-and-the-array-is-a-snapshot` — the block was COPIED, and
    `-executionBlocks` is an immutable snapshot rather than the operation's own list;
  * `a-cancelled-operation-that-never-ran-runs-nothing-and-is-finished` and
    `cancelling-from-inside-skips-the-blocks-not-yet-reached` — the granular rule this class states, since a block
    operation is the one concrete operation whose units a caller can count;
  * `an-operation-with-no-blocks-finishes-without-running-anything`,
    `a-dependent-operation-is-ready-only-after-its-dependency-has-run` (the INHERITED machinery, asserted through a
    subclass that did not touch it);
  * `an-invocation-operation-calls-a-selector-that-takes-nothing`, `the-object-is-passed-and-the-invocation-retains-it`
    (Apple's sentence at the designated door), `the-convenience-door-answers-nil-when-there-is-no-method-to-invoke`
    (Apple's documented nil, not a raise);
  * `result-answers-an-object-as-itself-and-a-scalar-as-an-nsvalue` — the object as itself, a scalar read back OUT
    of the NSValue, which is what proves the LENGTH and the TYPE were right rather than that a pointer came back;
  * `a-void-return-and-a-cancelled-operation-each-raise-their-own-name` — F4's two constants
    (`NSInvocationOperationVoidResultException`, `NSInvocationOperationCancelledException`), which have been declared
    in `NSException.h` since F4 and finally have a raiser;
  * `an-exception-from-the-run-is-raised-again-by-result` — not thrown out of `-start`, and waiting at `-result`,
    which is Apple's sentence. THIS CHECK FOUND A REAL DEFECT: the exception could not be caught at all, because the
    hand-written trampolines in `NSInvocation_amd64.S` had no `.cfi_*` directives and therefore no FDE, so
    `objc_exception_throw` aborted inside the unwind. Four directives per function are the fix, landed with this
    unit;
  * `the-designated-door-accepts-a-hand-built-invocation-and-refuses-nil`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_operation_leaves"
CHECKS = ("a-block-operation-runs-its-block-and-finishes",
          "several-blocks-run-in-the-order-they-were-added",
          "the-operation-is-not-finished-while-its-blocks-are-running",
          "adding-a-block-while-executing-or-finished-raises-by-name",
          "a-block-added-from-a-dead-frame-still-runs-and-the-array-is-a-snapshot",
          "a-cancelled-operation-that-never-ran-runs-nothing-and-is-finished",
          "cancelling-from-inside-skips-the-blocks-not-yet-reached",
          "an-operation-with-no-blocks-finishes-without-running-anything",
          "a-dependent-operation-is-ready-only-after-its-dependency-has-run",
          "an-invocation-operation-calls-a-selector-that-takes-nothing",
          "the-object-is-passed-and-the-invocation-retains-it",
          "the-convenience-door-answers-nil-when-there-is-no-method-to-invoke",
          "result-answers-an-object-as-itself-and-a-scalar-as-an-nsvalue",
          "a-void-return-and-a-cancelled-operation-each-raise-their-own-name",
          "an-exception-from-the-run-is-raised-again-by-result",
          "the-designated-door-accepts-a-hand-built-invocation-and-refuses-nil")


class Case(BaseCase):
    title = "NSBlockOperation + NSInvocationOperation: the two concrete operations"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_operation_leaves")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-OPERATION-LEAVES-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-OPERATION-LEAVES "):
                self.note(line)

        done = "FOUNDATION-OPERATION-LEAVES DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-OPERATION-LEAVES DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-OPERATION-LEAVES %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-OPERATION-LEAVES RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-OPERATION-LEAVES-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
