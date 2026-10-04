# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The toll-free bridge, exercised — M2's first acceptance (docs/design/foundation-cf-core-plan.md D3).

CoreFoundation is built here as Objective-C (86/86), with the four dispatch macros real rather than the
no-op stubs upstream's C-only branch defines. What that buys is the thing D3 promised: *"a bridged object
IS an ObjC object"* — an `NSString` this tree's Foundation made, handed to CF as a `CFStringRef`, is
answered by THAT CLASS's code.

The mechanism, because the checks are written around it: `CF_IS_OBJC` is an ISA COMPARISON (derived from
upstream's own comment on `__CFISAForTypeID`, not from Apple's APSL source, which the plan forbids
reading). An object is Objective-C when its first word is not the CF runtime class registered for the type
it claims. So the probe checks BOTH DIRECTIONS, and the second is what makes the first mean anything:

  * `a-literal-string-dispatches-to-its-class` — a `@"..."` literal, whose isa the COMPILER chose;
  * `a-factory-string-dispatches-to-its-class` — so the result does not rest on the constant-string path;
  * `an-nsarray-dispatches-to-its-class` — a SECOND bridged type, so the result does not rest on
    CFString's doors alone: an `NSArray` literal answered by `-count` (`CFArray`'s dispatch was compiled
    from the first day of this work and had never run);
  * `a-parameterised-site-crosses-the-bridge` — the dispatch with an ARGUMENT, not only a receiver
    (`-characterAtIndex:` with the answer cast to `UniChar`);
  * `a-cf-native-string-still-takes-cfs-own-path` — a string CF created ITSELF must still run CF's C
    implementation. A bridge whose test were true for every object would pass the first three and be
    worthless; this check is what separates them.

`CFStringGetCString` is deliberately NOT called: its dispatch site wants `-getCString:maxLength:encoding:`,
and a Foundation that does not answer that selector would crash the probe instead of reporting a check —
the useful form of that information is the LIST of selectors CF expects and Foundation lacks, which is M2's
own work list.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/corefoundation_bridge"
CHECKS = (
    "a-literal-string-dispatches-to-its-class",
    "a-factory-string-dispatches-to-its-class",
    "a-parameterised-site-crosses-the-bridge",
    "an-nsarray-dispatches-to-its-class",
    "a-cf-door-needing-private-foundation-crosses-the-bridge",
    "a-cf-in-place-mutation-crosses-the-bridge",
    "a-cf-native-string-still-takes-cfs-own-path",
)


class Case(BaseCase):
    title = "The CoreFoundation bridge: CF runs the ObjC class's code, and its own for its own objects"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("corefoundation_bridge")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo COREFOUNDATION-BRIDGE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("COREFOUNDATION-BRIDGE "):
                self.note(line)

        done = "COREFOUNDATION-BRIDGE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no COREFOUNDATION-BRIDGE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^COREFOUNDATION-BRIDGE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"COREFOUNDATION-BRIDGE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "COREFOUNDATION-BRIDGE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
