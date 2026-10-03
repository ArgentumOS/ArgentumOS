# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURLSessionConfiguration — W7 slice 2c's session half, first row.

docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c. The session
family is the largest in the ledger (11 classes + 6 protocols + 46 cases), so it is split the way the plan
split 2c itself. This is the class a session cannot be created without — and it is a VALUE with documented
defaults, so it lands the way slice 1's request/response pair did: as a value probe.

The probe is `/System/Shared/tests/foundation_urlsession_config`, ONE unit, importing only
`<Foundation/Foundation.h>`. No session is created.

  * `session-configuration-defaults`                   — Apple's documented values, in one place;
  * `ephemeral-configuration-differs-only-by-identity` — the honest statement of what the ephemeral door
                                                         can differ by HERE (no persistent cache/cookie/
                                                         credential state), pinned rather than assumed;
  * `background-configuration-carries-its-identifier`  — the third door;
  * `configuration-setters-round-trip`                 — every property is readwrite;
  * `protocol-classes-are-snapshotted`                 — the caller's array is copied in (and this is the
                                                         property that reaches slice 2a's registry);
  * `configuration-copy-is-a-snapshot`                 — a REAL copy, which is what distinguishes it from
                                                         the `-retain` the immutable classes answer with;
  * `configuration-api-inventory`                      — owed selectors exist, INCLUDING the four
                                                         storage/header doors (their classes are shipped
                                                         today); the coder doors are ABSENT.

`ephemeral-configuration-differs-only-by-identity` AND `configuration-copy-is-a-snapshot` ARE THE POINT:
the first refuses to let a door imply behaviour this tree cannot have, and the second pins the one
property that a readwrite value class must get right.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlsession_config"
CHECKS = (
          "session-configuration-new-doors-do-not-leak-between-instances",
          "session-configuration-new-values-are-copied-not-held",
          "session-configuration-new-flags-round-trip","session-configuration-defaults",
          "ephemeral-configuration-differs-only-by-identity",
          "background-configuration-carries-its-identifier",
          "configuration-setters-round-trip",
          "protocol-classes-are-snapshotted",
          "configuration-copy-is-a-snapshot",
          "configuration-api-inventory")


class Case(BaseCase):
    title = "NSURLSessionConfiguration: the session's settings as a value (W7 slice 2c, session)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlsession_config")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLSESSION-CONFIG-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLSESSION-CONFIG "):
                self.note(line)

        done = "FOUNDATION-URLSESSION-CONFIG DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLSESSION-CONFIG DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLSESSION-CONFIG %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLSESSION-CONFIG RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLSESSION-CONFIG-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
