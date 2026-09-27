# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSHost — §62.63's acceptance, and the row that completes the Sockets family.

docs/design/foundation-plan.md §62.63. NSHost is Apple-DEPRECATED and ships anyway, because the user's decision
of 2026-09-26 (§62.24) is that deprecated API is a PORTING TARGET rather than an exclusion — the ledger keeps its
`deprecated` label, which says what KIND of work a row is.

The class is a VIEW over the system's name service and nothing more, which is Apple's own sentence: the doors
"use the available network administration services to discover this information but do NOT CONTACT THE HOST
ITSELF". So every check is a question the LOCAL resolver can answer — `localhost` and the loopback addresses are
in it by definition, and a name reserved never to resolve (`.invalid`, RFC 2606) is not — and NOTHING here needs
a network, which is what lets the probe run in a guest with no NIC.

The probe is `/System/Shared/tests/foundation_host`, ONE unit, importing only `<Foundation/Foundation.h>`.

  * `the-current-host-answers-a-name` — `+currentHost` names the machine the process runs on, and the singular
                                     door's answer is one of the plural door's;
  * `a-name-resolves-to-its-addresses-and-a-name` — `localhost` answers a loopback address AND at least one name;
  * `an-address-resolves-to-its-names` — the address door does the reverse lookup;
  * `a-host-is-equal-to-the-one-rebuilt-from-its-own-address` — the identity rule, checked by rebuilding a host
                                     from the first one's own address (so it does not depend on which loopback
                                     form the service answers with);
  * `two-addresses-are-not-one-host` — the other half of that rule, which a class answering YES to everything
                                     would fail;
  * `a-name-the-service-does-not-know-has-no-host` — `.invalid` cannot resolve, so there is no host;
  * `a-nil-name-or-address-has-no-host` — nil in, nil out;
  * `the-cache-doors-answer-as-apple-documents` — Apple's own annotation on all three cache doors is "Caching no
                                     longer supported", so `+isHostCacheEnabled` answers NO and the other two do
                                     nothing: a published answer, not a stub.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_host"
CHECKS = ("the-current-host-answers-a-name",
          "a-name-resolves-to-its-addresses-and-a-name",
          "an-address-resolves-to-its-names",
          "a-host-is-equal-to-the-one-rebuilt-from-its-own-address",
          "two-addresses-are-not-one-host",
          "a-name-the-service-does-not-know-has-no-host",
          "a-nil-name-or-address-has-no-host",
          "the-cache-doors-answer-as-apple-documents")


class Case(BaseCase):
    title = "NSHost: the host as a name-service answer, and the deprecated row that completes Sockets"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_host")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-HOST-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-HOST "):
                self.note(line)

        done = "FOUNDATION-HOST DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-HOST DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-HOST %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-HOST RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-HOST-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
