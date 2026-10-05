# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The new Foundation's first acceptance: one object, one lifetime, both worlds.

The library's day-one claim is that a Foundation object is an OBJECTIVE-C object CoreFoundation accepts as
its own, with ONE retain count. The old library could only ever prove half of that, and the half it could
not prove is what this case is built around: CFRetain/CFRelease did not reach its objects, so a CFArray
created with kCFTypeArrayCallBacks DROPPED one. A container that silently loses its contents is not a
tolerable way to be CF-integrated, so the checks below are the two that fail when the ownership arm is
missing, plus the identity and description doors that make the two worlds agree rather than coexist.

  * `an-object-of-this-library-is-allocated`        — the root class allocates at all;
  * `its-class-is-the-class-that-was-asked-for`     — and the isa is what CF_IS_OBJC will compare;
  * `a-subclass-is-a-kind-of-its-superclass`        — the class walk works, which every type check needs;
  * `cf-retain-keeps-an-object-of-this-library-alive` — CF's OWN retain holds one of our objects;
  * `cf-release-lets-it-go`                         — and CF's release is what ends it;
  * `a-cf-array-holds-an-object-of-this-library`    — a CF container with CF's OWN callbacks retains it;
  * `and-hands-the-same-object-back`                — and returns the very object it was given;
  * `a-cf-array-releases-it-when-the-array-goes`    — and the container is what ends it;
  * `the-description-door-builds-a-cf-string`       — -description builds CF's own string type;
  * `and-that-string-names-the-class`               — which CF's own code path can then read.

The instrument for "alive" versus "quietly gone" is a dealloc counter in a probe subclass, because the base
class cannot report its own death: its -dealloc frees through the runtime.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_object"
CHECKS = (
    "an-object-of-this-library-is-allocated",
    "its-class-is-the-class-that-was-asked-for",
    "a-subclass-is-a-kind-of-its-superclass",
    "cf-retain-keeps-an-object-of-this-library-alive",
    "cf-release-lets-it-go",
    "the-objects-first-word-is-its-class",
    "cf-retain-moves-the-count-the-class-owns",
    "a-cf-array-holds-an-object-of-this-library",
    "and-hands-the-same-object-back",
    "a-cf-array-releases-it-when-the-array-goes",
    "the-description-door-builds-a-cf-string",
    "and-that-string-names-the-class",
    "a-cf-native-string-has-a-class",
    "and-is-messageable-as-an-ns-string",
    "and-casts-back-to-cf",
    "and-answers-its-own-characters",
    "and-compares-equal-to-an-equal-string",
    "a-cfstr-literal-is-an-object",
    "and-is-messageable",
    "and-casts-to-cfstringref",
)


class Case(BaseCase):
    title = "Foundation's new object model: one lifetime shared with CoreFoundation"
    tier = "fast"
    # It runs a probe and reads its output, so the same guest can answer another case afterwards.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_object")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-OBJECT-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            # The probe's own lines are the measurement, so surface them rather than only their summary.
            if line.startswith("FOUNDATION-OBJECT "):
                self.note(line)

        # THE ASSERTIONS ARE THIS CASE'S OWN — nothing in the harness infers them, which is the mistake
        # this case made first time round: it booted, ran the probe, noted its output and asserted only
        # that a shell had answered, so a probe that never ran would have passed.
        self.check("probe-ran", "FOUNDATION-OBJECT DONE" in out,
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

        status = [l for l in out.splitlines() if "FOUNDATION-OBJECT-STATUS=" in l]
        self.check("exit-status", bool(status) and status[-1].rstrip().endswith("STATUS=0"),
                   "the probe exited non-zero: " + (status[-1].strip() if status else "no status line"))
