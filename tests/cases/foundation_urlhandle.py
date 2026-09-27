# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""`NSURLHandle` and its client protocol — §62.47's acceptance.

Apple deprecated this family at 10.4 in favour of `NSURLConnection`/`NSURLDownload`; §62.24's policy restored it.
The sixteen rows the ledger owed were the VOCABULARY — eleven property keys, `NSURLHandleStatus` and its four cases,
and the client protocol — and the CLASS is here because a key needs something to key into.

WHAT IS MEASURED IS THE MEANING, NOT THE NAMES. A key declared and never filed, a status that never becomes
`LoadSucceeded`, a client protocol nobody calls: all three would compile and pass a check that only counted
declarations. So the probe loads a real resource THROUGH THE LIBRARY'S OWN TRANSPORT — a registered
`NSURLProtocol` answering in process, which is this tree's recipe for an HTTP probe — and asserts what the class
FILED and WHOM it TOLD.

ONE PATH, ITS OWN TRANSPORT: nothing here shares a listener or a session with another probe.

The probe is `/System/Shared/tests/foundation_urlhandle`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-eleven-property-keys-are-declared-and-distinct` — the vocabulary, and the four statuses distinct;
  * `a-handle-answers-its-url-and-starts-not-loaded` — a fresh handle's three answers;
  * `the-registry-answers-a-class-and-the-cache-the-same-handle` — capability and the cache;
  * `a-property-the-caller-writes-comes-back-and-nil-removes-it` — the bag's own rules;
  * `the-foreground-load-returns-the-body-through-this-librarys-transport` — a real load;
  * `the-response-is-filed-under-apples-keys`         — the keys, filled by the class;
  * `available-data-is-what-arrived-and-a-flush-forgets-it` — the resource data;
  * `a-flush-forgets-the-body-and-keeps-the-properties` — what a flush does and does not drop;
  * `a-background-load-tells-its-client-begin-then-the-bytes-then-finish` — the protocol, called in order;
  * `a-failed-load-answers-no-data-a-failed-status-and-a-reason` — the failure door.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlhandle"
CHECKS = (
    "the-eleven-property-keys-are-declared-and-distinct",
    "a-handle-answers-its-url-and-starts-not-loaded",
    "the-registry-answers-a-class-and-the-cache-the-same-handle",
    "a-property-the-caller-writes-comes-back-and-nil-removes-it",
    "the-foreground-load-returns-the-body-through-this-librarys-transport",
    "the-response-is-filed-under-apples-keys",
    "available-data-is-what-arrived-and-a-flush-forgets-it",
    "a-flush-forgets-the-body-and-keeps-the-properties",
    "a-background-load-tells-its-client-begin-then-the-bytes-then-finish",
    "a-failed-load-answers-no-data-a-failed-status-and-a-reason",
)


class Case(BaseCase):
    title = "NSURLHandle: the vocabulary, the load, and the client it tells (§62.47)"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlhandle")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLHANDLE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLHANDLE "):
                self.note(line)

        done = "FOUNDATION-URLHANDLE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLHANDLE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLHANDLE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLHANDLE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLHANDLE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
