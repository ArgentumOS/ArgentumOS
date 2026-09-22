# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURLProtocol / NSURLProtocolClient / NSCachedURLResponse / NSURLCacheStoragePolicy — W7 slice 2a.

docs/design/foundation-plan.md §46 and docs/design/foundation-transport-plan.md §4. This slice is THE
SEAM: the point a transport attaches to, plus the cached answer's value contract. It has NO build
dependency, which is why it comes before the transport that will be its first implementation — a
`NSURLProtocol` subclass is how *any* protocol plugs in, so slice 2c's libcurl bridge is itself one.

The probe is `/System/Shared/tests/foundation_urlprotocol`, ONE unit, importing only
`<Foundation/Foundation.h>`. It defines its own subclasses, because an override point is invisible until
something overrides it.

  * `cache-storage-policy-values`               — 0/1/2, which are OURS under D2;
  * `cached-response-convenience-door-answers-the-defaults` — storage allowed, no user info;
  * `cached-response-full-init-stores-what-it-is-handed`    — the four-argument door;
  * `cached-response-snapshots-its-inputs`      — editing the caller's dictionary cannot reach it;
  * `urlprotocol-base-claims-nothing`           — NO, unchanged, and equality as the default;
  * `request-properties-are-per-instance`       — the identity rule, and a copy starts empty;
  * `request-property-nil-value-removes`        — a nil value removes, leaving neighbours alone;
  * `request-property-remove`                   — the second way out of the table;
  * `registration-refuses-the-base-and-non-subclasses` — only a subclass registers;
  * `registration-accepts-a-subclass`           — claimed requests reach it, unclaimed reach nobody;
  * `registration-order-decides-precedence`     — most-recently-registered first;
  * `unregister-restores-the-previous-class`    — the registry compacts rather than tombstones;
  * `unregister-removes-the-class`              — and an unregistered class is gone;
  * `urlprotocol-instance-carries-its-request`  — kept as a value, a mutable one copied in;
  * `urlprotocol-instance-carries-its-client`   — both nullable, as Apple declares them;
  * `urlprotocol-base-loading-is-a-no-op`       — asserted as "the client was told nothing";
  * `urlprotocol-client-protocol-shape`         — all six callbacks, and conformance;
  * `finish-loading-carries-no-protocol-argument` — the one callback without a `protocol:` prefix;
  * `urlprotocol-api-inventory`                 — the audited inventory: owed selectors exist, and the
                                                  two authentication callbacks plus the coder doors are
                                                  ABSENT (each because the type or format behind it is
                                                  not shipped).

THE TWO THAT EARN THEIR PLACE: `request-properties-are-per-instance` pins the identity-and-retention rule
that keeps a freed request's address from answering another request's properties, and
`urlprotocol-api-inventory` makes coverage a property of the code rather than of a comment.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlprotocol"
CHECKS = ("cache-storage-policy-values",
          "cached-response-convenience-door-answers-the-defaults",
          "cached-response-full-init-stores-what-it-is-handed",
          "cached-response-snapshots-its-inputs",
          "urlprotocol-base-claims-nothing",
          "request-properties-are-per-instance",
          "request-property-nil-value-removes",
          "request-property-remove",
          "registration-refuses-the-base-and-non-subclasses",
          "registration-accepts-a-subclass",
          "registration-order-decides-precedence",
          "unregister-restores-the-previous-class",
          "unregister-removes-the-class",
          "urlprotocol-instance-carries-its-request",
          "urlprotocol-instance-carries-its-client",
          "urlprotocol-base-loading-is-a-no-op",
          "urlprotocol-client-protocol-shape",
          "finish-loading-carries-no-protocol-argument",
          "urlprotocol-api-inventory")


class Case(BaseCase):
    title = "NSURLProtocol/NSCachedURLResponse: the transport seam and the cached value (W7 slice 2a)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlprotocol")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLPROTOCOL-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLPROTOCOL "):
                self.note(line)

        done = "FOUNDATION-URLPROTOCOL DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLPROTOCOL DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLPROTOCOL %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLPROTOCOL RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLPROTOCOL-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
