# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURLRequest / NSMutableURLRequest / NSURLResponse / NSHTTPURLResponse — W7 slice 1's acceptance.

docs/design/foundation-plan.md §46. W7 is the URL loading system, and its FIRST slice is the half that
needs no transport: a request is a DESCRIPTION of an exchange and a response is its answer's metadata.
There is no socket, no session and no loader in this unit — the class that PERFORMS an exchange is a
later slice — so the whole probe is a value probe.

The probe is `/System/Shared/tests/foundation_urlrequest`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `request-defaults`                        — the convenience door's policy and 60-second timeout;
  * `request-designated-initializer`          — the three-argument door stores what it was given;
  * `request-mutable-initializer-is-mutable`  — the mutable class is reachable through it;
  * `request-http-defaults`                   — GET, cookies handled, no pipelining, cellular allowed;
  * `mutable-setters-round-trip`              — every setter, read back;
  * `headers-are-case-insensitive`            — RFC 9110 §5.1, and the STORED spelling is the caller's;
  * `add-value-appends`                       — `-addValue:forHTTPHeaderField:` uses §5.2's list rule;
  * `set-value-replaces`                      — and `-setValue:forHTTPHeaderField:` REPLACES: the two
                                                doors differ, which is why both are asserted;
  * `header-snapshot-is-stable`               — a dictionary handed out earlier cannot change under it;
  * `request-equality-and-hash`               — equality and the hash move together;
  * `copy-of-a-mutable-is-an-immutable-snapshot` — the mutability boundary, and an immutable answer;
  * `mutable-copy-is-independent`             — a mutable copy is equal at first and separate after;
  * `response-properties`                     — the four facts, and the unknown-length sentinel;
  * `response-suggested-filename`             — the last path component, or "Unknown";
  * `http-response-derives-from-headers`      — MIME type, charset and length derived from the headers;
  * `response-unknown-length`                 — no Content-Length means the sentinel, not zero;
  * `http-response-status-phrase`             — RFC 9110 §15's own rows, and nil outside the registry;
  * `url-request-enum-values`                 — the enums' numbers, which are OURS under D2;
  * `url-request-api-inventory`               — the audited inventory: owed selectors exist, refused
                                                doors are absent.

THE LAST TWO ARE THE POINT: one takes its expected values from the document that defines the status
phrases, and the other makes the class's coverage a property of the code rather than of a comment.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlrequest"
CHECKS = ("request-defaults", "request-designated-initializer",
          "request-mutable-initializer-is-mutable", "request-http-defaults",
          "mutable-setters-round-trip", "headers-are-case-insensitive", "add-value-appends",
          "set-value-replaces", "header-snapshot-is-stable", "request-equality-and-hash",
          "copy-of-a-mutable-is-an-immutable-snapshot", "mutable-copy-is-independent",
          "response-properties", "response-suggested-filename",
          "http-response-derives-from-headers", "response-unknown-length",
          "http-response-status-phrase", "url-request-enum-values",
          "url-request-api-inventory", "urlrequest-network-policy-defaults", "urlrequest-network-policy-round-trip", "urlrequest-network-policy-survives-a-copy")


class Case(BaseCase):
    title = "NSURLRequest/NSURLResponse: the request and response value types (W7 slice 1)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlrequest")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLREQUEST-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLREQUEST "):
                self.note(line)

        done = "FOUNDATION-URLREQUEST DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLREQUEST DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLREQUEST %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLREQUEST RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLREQUEST-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
