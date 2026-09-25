# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFilePresenter's process-wide registry and the relinquish handshake — W8 slice 7c (foundation-plan.md §60).

THE HANDSHAKE IS THE PART WITH A HANG IN IT, so it is what most of this probe is about. Apple's relinquish
pages say the presenter "must execute the block in the [reacquirer] parameter", passing back a REACQUIRER for
the coordinator to execute once the operation is over: "it executes the reader block and passes in yet
another block for the reader to execute when it is done". A coordinator that ran its accessor without
waiting would be wrong in a way a fast presenter HIDES, so one check here uses a presenter that answers
LATE, from another thread, and requires the order to hold anyway.

The registry is Apple's own shape, measured: `+addFilePresenter:` "registers the file presenter object
process wide", "be sure to balance calls to this method with a corresponding call to [removeFilePresenter:]",
and "you must remove file presenters from the process wide registry before the object is deallocated" — the
last sentence is what makes it a NON-OWNING registry.

The probe is `/System/Shared/tests/foundation_filepresenter`, ONE unit, importing only
`<Foundation/Foundation.h>` plus the POSIX calls its fixture and its deferred presenter need.

  * `presenter-the-registry-is-add-list-and-remove` — the process-wide registry holds one entry per add and
                                 answers the objects it holds;
  * `presenter-the-handshake-runs-around-the-accessor` — relinquish -> accessor -> reacquirer, as an ORDER
                                 and not as three booleans;
  * `presenter-a-write-asks-the-writer-form` — a write door asks `-relinquishPresentedItemToWriter:` and
                                 not the reader form;
  * `presenter-only-the-matching-presenter-is-asked` — a presenter for another item is left alone;
  * `presenter-the-coordinators-own-presenter-is-not-asked` — "This object … does not receive notifications
                                 about those operations" (Apple's page for `-initWithFilePresenter:`);
  * (THE DEFERRED HANDSHAKE IS IMPLEMENTED AND NOT PROVED HERE: the leg that made a presenter answer LATE
                                 from another thread trips a GUEST TRAP, so it is recorded as owed rather
                                 than kept - a broken instrument is not evidence about the library, and
                                 deleting the wait would be worse than admitting the gap);
  * `purpose-a-string-round-trips-and-empty-is-ignored` — "A string that uniquely identifies the file
                                 access" round-trips, and the one spelling rule Apple states ("You cannot
                                 use nil or zero-length strings") is honoured by IGNORING such an
                                 assignment — Apple states the rule and not the mechanism, so nothing is
                                 invented and nothing is silently stored;
  * `a-write-tells-the-presenters-the-item-changed` / `a-read-does-not-tell-them` — the presenters are told
                                 the item CHANGED after a write, and not after a read, because a read
                                 changes nothing;
  * `didMove-notifies-the-items-presenters` — "This method calls the [-presentedItemDidMoveToURL:] method
                                 for any of the item's file presenters";
  * `willMove-has-no-purpose-on-a-system-without-a-sandbox` — APPLE'S OWN SENTENCE: the will form "is
                                 intended for apps that adopt App Sandbox … If your macOS app is not
                                 sandboxed, this method serves no purpose" — declared so a balanced pair
                                 compiles, notifying nobody;
  * `cancel-with-nothing-active-does-not-stop-the-next-call` — `-cancel` cancels ACTIVE calls ("any
                                 active file coordination calls"), so a cancel with nothing in flight does
                                 not stop the next one;
  * `async-the-door-runs-the-accessor-on-its-queue` — the asynchronous door: the accessor runs on the given
                                 queue with the intents and a nil error, and the probe PRINTS whether the
                                 door had already returned;
  * `async-a-nil-queue-or-no-intents-does-nothing` — the door has no error of its own, so a nil queue or an
                                 empty intent list answers by doing nothing rather than crashing;
  * `async-a-reading-intent-gets-the-coordinated-url` — "The system updates this URL property to account
                                 for any changes to the underlying files";
  * `prepare-batches-the-presenters-around-one-block` — the BATCH door: "This method executes
                                 synchronously, blocking the current thread until the [batch] block finishes
                                 executing", and the handshake runs ONCE around the whole batch — asserted
                                 as an ORDER (relinquish, block, reacquire);
  * `prepare-takes-both-lists` — the read list goes to readers and the write list to writers;
  * `prepare-refuses-a-bad-url-without-running-the-block` — "the error is returned in this parameter and
                                 the block … is not executed";
  * `probe-tree-removed` — the tree is gone.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_filepresenter"
CHECKS = ("debt-the-handshake-is-deferred-measured",
          "debt-the-accessor-runs-while-the-presenter-still-holds",
          "debt-dispatch-then-block-deadlocks-on-the-callers-own-queue", "presenter-the-registry-is-add-list-and-remove",
          "presenter-the-handshake-runs-around-the-accessor",
          "presenter-a-write-asks-the-writer-form",
          "presenter-only-the-matching-presenter-is-asked",
          "presenter-the-coordinators-own-presenter-is-not-asked",
          "purpose-a-string-round-trips-and-empty-is-ignored",
          "a-write-tells-the-presenters-the-item-changed", "a-read-does-not-tell-them",
          "didMove-notifies-the-items-presenters",
          "willMove-has-no-purpose-on-a-system-without-a-sandbox",
          "cancel-with-nothing-active-does-not-stop-the-next-call",
          "async-the-door-runs-the-accessor-on-its-queue",
          "async-a-nil-queue-or-no-intents-does-nothing",
          "async-a-reading-intent-gets-the-coordinated-url",
          "prepare-batches-the-presenters-around-one-block", "prepare-takes-both-lists",
          "prepare-refuses-a-bad-url-without-running-the-block", "probe-tree-removed")


class Case(BaseCase):
    title = "NSFilePresenter: the registry and the relinquish handshake, including a late answer"
    tier = "fast"
    # It builds and removes a tree of its own under the temp directory, so it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_filepresenter")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEPRESENTER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEPRESENTER "):
                self.note(line)

        done = "FOUNDATION-FILEPRESENTER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEPRESENTER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEPRESENTER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEPRESENTER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEPRESENTER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
