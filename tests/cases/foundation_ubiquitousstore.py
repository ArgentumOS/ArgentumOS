# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSUbiquitousKeyValueStore — §62.87's acceptance.

This closes `Files and Data Persistence / iCloud key and value storage`: the store, its change notice and the
four change reasons.

THE LOCAL HALF IS REAL AND THE REMOTE HALF IS ABSENT — the shape §62.80 established, and the reason this unit
could land at all. The store holds values, answers them through the typed doors, enforces Apple's three limits,
and reports changes; what it does not have is iCloud. So **`-synchronize` answers NO**, which is asserted: YES
would claim a synchronization that never happened, and that is the one lie this class could tell.

The probe is `/System/Shared/tests/foundation_ubiquitousstore`, ONE unit, importing `<Foundation/Foundation.h>`
plus the internal service seam `<Foundation/FNSUbiquitousStore.h>`.

THE NOTICE IS DRIVEN THROUGH THE SEAM, which is what stops it being a name nobody can post: the seam does what a
service's delivery does — merge the incoming values, then post the notice with the two documented keys — and the
probe OBSERVES the notice (its object, its reason, its changed keys) rather than trusting it.

AND THE RULE THAT IS EASIEST TO GET WRONG IS ASSERTED DIRECTLY: the notice is about changes from OUTSIDE, so this
app's own successful write posts NOTHING. The one local posting is a quota violation, which is how Apple's store
reports it too.

  * `the-class-and-its-three-names-are-declared` / `the-four-change-reasons-are-distinct-and-in-apples-order`
                                          — the surface, with the values ours under §11.6.1 D2;
  * `the-default-store-is-one-object`      — Apple's "use the same store throughout your app";
  * `a-fresh-store-is-empty` / `every-typed-door-round-trips` /
    `the-reading-doors-answer-the-value-not-the-presence` / `the-store-holds-every-pair-it-was-given` /
    `removing-takes-the-key-away-including-through-nil` / `the-representation-is-a-snapshot-not-a-live-view`
                                          — the store's own behaviour, including that `nil` is an absence and that
                                            an absent or wrong-typed key answers nil/zero rather than raising;
  * `a-key-longer-than-128-characters-raises-and-128-does-not` /
    `a-value-that-is-not-a-property-list-is-refused-by-name` — Apple's hard limits, enforced at the write;
  * `this-apps-own-successful-write-posts-nothing` / `a-write-under-the-quota-is-stored` /
    `a-write-that-breaks-the-quota-is-refused-whole` / `the-quota-violation-is-reported-through-the-change-notice`
                                          — the quota, the atomicity, and the one local posting;
  * `a-delivered-change-merges-into-the-store` / `a-delivered-nil-value-removes-the-key` /
    `the-notice-carries-its-object-its-reason-and-its-keys` / `every-change-reason-can-arrive-through-the-notice` /
    `a-delivered-value-is-still-checked-for-being-a-property-list`
                                          — the service seam end to end;
  * `synchronize-answers-no-because-there-is-no-service` — the door that must not lie.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_ubiquitousstore"
CHECKS = (
    "the-class-and-its-three-names-are-declared",
    "the-four-change-reasons-are-distinct-and-in-apples-order",
    "the-default-store-is-one-object",
    "a-fresh-store-is-empty",
    "every-typed-door-round-trips",
    "the-reading-doors-answer-the-value-not-the-presence",
    "the-store-holds-every-pair-it-was-given",
    "removing-takes-the-key-away-including-through-nil",
    "the-representation-is-a-snapshot-not-a-live-view",
    "a-key-longer-than-128-characters-raises-and-128-does-not",
    "a-value-that-is-not-a-property-list-is-refused-by-name",
    "this-apps-own-successful-write-posts-nothing",
    "a-write-under-the-quota-is-stored",
    "a-write-that-breaks-the-quota-is-refused-whole",
    "the-quota-violation-is-reported-through-the-change-notice",
    "a-delivered-change-merges-into-the-store",
    "a-delivered-nil-value-removes-the-key",
    "the-notice-carries-its-object-its-reason-and-its-keys",
    "every-change-reason-can-arrive-through-the-notice",
    "a-delivered-value-is-still-checked-for-being-a-property-list",
    "synchronize-answers-no-because-there-is-no-service",
)


class Case(BaseCase):
    title = "NSUbiquitousKeyValueStore: the iCloud key/value store, locally (§62.87)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_ubiquitousstore")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-UBIQUITOUSSTORE-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-UBIQUITOUSSTORE "):
                self.note(line)

        done = "FOUNDATION-UBIQUITOUSSTORE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-UBIQUITOUSSTORE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-UBIQUITOUSSTORE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-UBIQUITOUSSTORE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-UBIQUITOUSSTORE-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
