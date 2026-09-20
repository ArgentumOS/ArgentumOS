# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURLComponents / NSURLQueryItem — F13.15's acceptance.

docs/design/foundation-plan.md §10. A URL as EIGHT FIELDS rather than one string: what an EDITOR
needs (change the host, list the query items) rather than what a value needs. F8 refused this family
by name, and refused `+[NSURL URLWithString:relativeToURL:]` in the same breath — because the
resolution that door exists FOR is the component-wise algorithm, so the two arrive together.

The probe is `/System/Shared/tests/foundation_urlcomponents`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `components-parse-the-field`  — one URL into its eight fields;
  * `components-decodes-and-keeps` — the two accessors that DIFFER: `-path` decodes percent escapes
                                     and `-percentEncodedPath` does not, which is the whole reason a
                                     components object exists beside NSURL;
  * `components-render`           — the string given comes back, and an edited field changes it;
  * `components-query-items`      — `"a=1&b=2&flag"` as three items, the third with a nil value;
  * `components-ipv6-and-port`    — a bracketed literal, where a colon INSIDE the brackets is not a
                                     port separator;
  * `components-url-and-copy`     — `-URL`, equality, and a copy that is not the same object;
  * `rfc3986-normal-examples`     — RFC 3986 §5.4.1's own table, ten rows;
  * `rfc3986-abnormal-examples`   — §5.4.2's, eight rows, where dot-segment removal is really tested;
  * `url-relative-door`           — `+[NSURL URLWithString:relativeToURL:]`, the door F8 refused.

THE LAST THREE ARE THE POINT: their expected values come from the DOCUMENT that defines the
algorithm, not from anything the probe or the library could have been written to match.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlcomponents"
CHECKS = ("components-parse-the-field", "components-decodes-and-keeps", "components-render",
          "components-query-items", "components-ipv6-and-port", "components-url-and-copy",
          "rfc3986-normal-examples", "rfc3986-abnormal-examples", "url-relative-door")


class Case(BaseCase):
    title = "NSURLComponents: a URL as eight fields, with RFC 3986's resolution"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlcomponents")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLCOMPONENTS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLCOMPONENTS "):
                self.note(line)

        done = "FOUNDATION-URLCOMPONENTS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLCOMPONENTS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLCOMPONENTS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLCOMPONENTS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLCOMPONENTS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
