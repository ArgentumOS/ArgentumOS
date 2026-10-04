# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSUserActivity and its delegate — §62.89's acceptance.

This closes `App Support / Activity Sharing`: the activity object, its delegate protocol, the browsing-web
activity type and the persistent-identifier typealias.

THE ACTIVITY IS REAL AND THE OTHER DEVICE IS NOT. An activity's own state is entirely local — its type, title,
`userInfo`, required keys, the two URLs, the save cycle through the delegate and the lifecycle calls — and all of
it is implemented and enforced. What this system has no second device for is CONTINUITY: the streams door reports
`NSUserActivityConnectionUnavailableError` (an error code this library already declared), and the delegate's two
continuity doors arrive through the internal seam so the path is exercisable rather than a declaration nobody can
call.

The probe is `/System/Shared/tests/foundation_useractivity`, ONE unit, importing `<Foundation/Foundation.h>` plus
the internal seam `<Foundation/FNUserActivity.h>`.

ONE THING THIS CASE CANNOT ASSERT, AND THE PROBE SAYS SO: the rule that only ONE activity is current at a time is
enforced in the class, but Apple publishes NO getter for that state (`-isCurrent` is not public API and is not
declared), so no probe can distinguish "the previous activity resigned" from "nothing happened". What IS
observable is asserted: an INVALIDATED activity is no longer eligible for continuation, demonstrated by the seam
dropping one.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_useractivity"
CHECKS = (
    "the-class-protocol-and-two-names-are-declared",
    "all-three-delegate-doors-are-declared-optional",
    "there-is-no-iscurrent-door-because-apple-publishes-none",
    "the-activity-keeps-its-type-and-it-is-read-only",
    "a-nil-or-empty-type-is-a-programming-error",
    "the-inherited-init-raises-because-no-bundle-declares-activity-types",
    "title-userinfo-and-required-keys-are-copied-not-held",
    "merging-adds-and-the-incoming-value-wins",
    "the-two-urls-and-the-expiration-date-round-trip-and-can-be-cleared",
    "a-fresh-activity-has-no-delegate-and-does-not-want-saving",
    "the-delegate-round-trips",
    "setting-needssave-asks-the-delegate-and-clears-the-flag",
    "clearing-needssave-asks-nothing",
    "a-delegate-that-writes-nothing-is-not-a-crash",
    "a-continuation-arrives-through-the-seam",
    "streams-arrive-through-the-seam-and-are-the-callers",
    "an-invalidated-activity-is-no-longer-eligible-for-continuation",
    "the-streams-door-reports-that-there-is-no-connection",
    "the-streams-door-tolerates-a-missing-handler",
    "a-fresh-activity-answers-empty-identifiers-and-off-flags",
    "the-string-doors-and-the-keyword-set-round-trip-and-are-copied",
    "the-four-eligibility-flags-store-and-answer",
)


class Case(BaseCase):
    title = "NSUserActivity: the activity object with no second device (§62.89)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_useractivity")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-USERACTIVITY-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-USERACTIVITY "):
                self.note(line)

        done = "FOUNDATION-USERACTIVITY DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-USERACTIVITY DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-USERACTIVITY %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-USERACTIVITY RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-USERACTIVITY-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
