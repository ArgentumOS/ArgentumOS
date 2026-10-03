# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURL — F8's acceptance.

docs/design/foundation-plan.md §5 (F8). The probe is
`/System/Shared/tests/foundation_url`, built from two translation units; the
support unit imports ONLY the umbrella header, so a complete
`<Foundation/Foundation.h>` — NSURL included — is part of what is being checked.

  * `url-parse`          — the RFC 3986 parts, and the spelling round trip;
  * `url-refusals`       — THE PARSE IS THE REFUSAL: a relative reference, an
                           invalid scheme or an empty string answers nil, and the
                           loading system (the 10.9 components family) is
                           ABSENT rather than half-built;
  * `url-file`           — the FSH rule: `file:///` + an ENCODED path, with an
                           empty authority, and `-path` back to the original;
  * `url-file-refusals`  — a relative or empty path is not a path this system
                           names;
  * `url-append-path`    — the path arithmetic, adding (and encoding a space);
  * `url-delete-path`    — the path arithmetic, removing, with Cocoa's dot rules;
  * `url-equality`       — by SPELLING, case-sensitively, as the header states;
  * `url-identity`       — `-absoluteURL`, `-relativeString`, `-description`;
  * `cross-tu`           — a URL built in the other unit behaves locally.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_url"
CHECKS = (
    "url-load-resource-data-notifies-the-client-of-failure",
    "url-resource-data-using-cache-answers-nil-for-an-unreachable-url",
    "url-set-resource-data-answers-no-for-a-url-that-cannot-hold-data",
    "url-security-scoped-stop-balances-nothing",
    "url-security-scoped-pair-answers-no",
    "url-promised-item-bulk-read-answers-a-dictionary",
    "url-promised-item-reachability-delegates",
    "url-promised-item-content-tied-key-answers-yes-with-nil",
    "url-promised-item-value-agrees-with-the-ordinary-door",
    "url-bookmark-init-by-resolving-refuses",
    "url-bookmark-creation-refuses",
    "url-bookmark-write-refuses",
    "url-bookmark-resource-values-refuses",
    "url-bookmark-data-with-contents-refuses",
    "url-bookmark-resolution-refuses",
    "url-bookmark-alias-resolution-refuses","url-parse", "url-refusals", "url-request-values-shipped",
          "urlprotocol-seam-shipped", "url-shipped",
          "rfc3986-normal-examples", "rfc3986-abnormal-examples",
          "url-file", "url-file-refusals",
          "url-append-path", "url-delete-path", "url-equality", "url-identity",
          "urlconnection-shipped",
          "cross-tu",
          # THE REST OF THE VALUE SURFACE (2026-09-30): the "Accessing the Parts", "Creating",
          # "Converting", "Querying" and pure "Deprecated" doors whose substrate this tree already had.
          "url-parts-extra", "url-standardized", "url-file-system-representation",
          "url-create-relative", "url-data-representation", "url-file-create", "url-file-fs-rep",
          "url-convert", "url-append-component-dir", "url-resolve-symlinks", "url-init-with-parts",
          # THE NSURLHandle-BACKED DEPRECATED DOORS AND THE PARAMETER STRING (2026-10-01): the three handle
          # doors are DELEGATIONS that ask for no fetch (a handle is constructed without one, a set property
          # reads back before any load), and the parameter string is a pure parse of the path's ';' tail.
          "url-handle-deprecated", "url-parameter-string")


class Case(BaseCase):
    title = "NSURL: the URL as a value, and the FSH's file-path rules"
    tier = "fast"
    # Its filesystem reads are read-only FIXTURES in the image; the host-clean list is a different
    # question (a HOST build has no /System), so it can share a guest like the rest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_url")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URL-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URL "):
                self.note(line)

        done = "FOUNDATION-URL DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URL DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URL %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS),
                           ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URL RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URL-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
