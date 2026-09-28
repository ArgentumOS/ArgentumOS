# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSUserNotification + NSUserNotificationCenter — §62.68's acceptance, and the third family closed.

docs/design/foundation-plan.md §62.68. Apple-deprecated (replaced by the UserNotifications framework) and shipping
anyway, per §62.24: the surface exists so that an older application compiles.

Four types and eleven ledger rows: the notification, its action, the centre, the centre's delegate protocol, the
five activation types and the default sound name.

The probe is `/System/Shared/tests/foundation_usernotification`, ONE unit, importing only
`<Foundation/Foundation.h>` — plus the INTERNAL seam FNUserNotification.h for the one door a user's click would
reach, since an activation is something a user does through a service this system does not have.

TWO OF APPLE'S OWN SENTENCES ARE CHECKS HERE, because they are the class's content:

  * "The `isPresented` property ... will ALWAYS be set to true if a notification is delivered using this method"
    — `deliverNotification:` presents even when the delegate would refuse;
  * "Bulk setting new scheduled notifications UNSCHEDULES existing notifications" — the setter replaces the queue.

The rest:

  * `a-notifications-values-are-snapshots-not-references` — Apple's `copy` ownership, proven by mutating the
                                     caller's string afterwards;
  * `a-copy-is-a-value-copy` — the NSCopying conformance Apple's page lists;
  * `a-notification-starts-unpresented-unactivated-and-unremote` — the read-only record, before anything happens;
  * `a-scheduled-notification-is-queued-before-it-is-due` / `...-delivered-when-its-date-arrives` — the queue and
                                     the run-loop timer that carries it out;
  * `a-refused-notification-is-delivered-without-being-presented` / `a-delegate-without-the-door-means-present` —
                                     the presentation rule, which is OURS and marked so (this system has no
                                     frontmost-application signal, so the delegate is the signal);
  * `removing-a-scheduled-notification-is-quiet-and-preventing` / `a-removed-notification-never-arrives` — Apple's
                                     own note that this door is quiet when the notification is not in the queue;
  * `an-activation-records-its-type-response-and-action` / `the-delegate-hears-about-an-activation` — through the
                                     internal seam;
  * `a-delivered-notification-can-be-removed` / `all-delivered-notifications-can-be-removed`.

WHAT THIS SYSTEM HONESTLY DOES NOT HAVE, stated rather than faked: no presentation service (so "presented" is a
record the centre keeps), no push service (`isRemote` is always NO), and no UI to send an activation (hence the
seam). The probe asserts the RECORD and says so.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_usernotification"
CHECKS = ("the-default-center-is-one-shared-one",
          "the-default-sound-name-and-the-five-activation-types",
          "an-action-carries-its-identifier-and-title",
          "a-notifications-values-are-snapshots-not-references",
          "a-copy-is-a-value-copy",
          "a-notification-starts-unpresented-unactivated-and-unremote",
          "delivering-presents-and-records-a-delivery-date",
          "the-delegate-hears-about-a-delivery",
          "a-scheduled-notification-is-queued-before-it-is-due",
          "a-scheduled-notification-is-delivered-when-its-date-arrives",
          "a-refused-notification-is-delivered-without-being-presented",
          "a-delegate-without-the-door-means-present",
          "removing-a-scheduled-notification-is-quiet-and-preventing",
          "a-removed-notification-never-arrives",
          "bulk-setting-the-queue-unschedules-what-was-there",
          "an-activation-records-its-type-response-and-action",
          "the-delegate-hears-about-an-activation",
          "a-delivered-notification-can-be-removed",
          "all-delivered-notifications-can-be-removed")


class Case(BaseCase):
    title = "NSUserNotification: the value, the centre, and what delivery means here"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_usernotification")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-USERNOTIFICATION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-USERNOTIFICATION "):
                self.note(line)

        done = "FOUNDATION-USERNOTIFICATION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-USERNOTIFICATION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-USERNOTIFICATION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-USERNOTIFICATION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-USERNOTIFICATION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
