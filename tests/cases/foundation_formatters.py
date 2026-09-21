# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The W11 formatter family — six classes, and the one whose arithmetic is ours.

docs/design/foundation-plan.md §12.3 W11 and §28. Four of these six are BINDINGS (ICU holds the CLDR
list, interval and relative-date patterns), one is a GRAMMAR (ISO 8601 is fixed, so the formatter's job
is the OPTIONS), and one — NSByteCountFormatter — is ARITHMETIC THIS TREE WROTE, because ICU has no
byte-count formatter. It also carries the two members folded in from §11's debt: NSSecureCoding and the
three NSFormatter doors Apple declares and we did not.

The probe is `/System/Shared/tests/foundation_formatters`, ONE unit, importing only
`<Foundation/Foundation.h>` — which is also what proves the umbrella exports all six new headers, and
what retires the risk that a class lands reachable only by a direct include.

  * `fmt-*`      — the three NSFormatter members Apple declares (each against its DOCUMENTED default),
                   plus the NSCoding conformance;
  * `pnc-*`      — the name bag: nil means absent, setters copy, -copy is DEEP in the nested value;
  * `list-*`     — CLDR list patterns: "Alice, Bob, and Charlie", the two-item form, the German "und";
  * `iso-*`      — the grammar and its OPTIONS (basic vs extended, offsets, fractional seconds), and
                   the parse that refuses trailing junk;
  * `dif-*`      — interval patterns: the common part prints ONCE, and the clock is the LOCALE's;
  * `bcf-*`      — the arithmetic: decimal vs binary on the SAME input, the units mask, zero padding,
                   "Zero KB", and the W12 measurement members asserted ABSENT by name;
  * `rdf-*`      — relative dates: "1 day ago" vs "yesterday", "two months ago", "gestern".
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_formatters"
CHECKS = (
    # the NSFormatter debt folded in
    "fmt-attributed-default", "fmt-editing-invokes", "fmt-partial-delegates",
    "fmt-partial-nil-string", "fmt-nscoding",
    # NSPersonNameComponents
    "pnc-absent", "pnc-bag", "pnc-setter-copies", "pnc-copy-deep", "pnc-securecoding",
    # NSListFormatter
    "list-join-en", "list-join-two", "list-join-de", "list-item-formatter",
    "list-empty", "list-object-value", "list-locale-resettable",
    # NSISO8601DateFormatter
    "iso-round-trip", "iso-shape", "iso-date-only", "iso-fractional",
    "iso-space-separator", "iso-basic-format", "iso-offset-zone",
    "iso-options-compose", "iso-refusals", "iso-class-and-copy",
    # NSDateIntervalFormatter
    "dif-range", "dif-collapse", "dif-template-locale", "dif-no-style",
    "dif-interval-object", "dif-resettable", "dif-template-copies",
    # NSByteCountFormatter (the arithmetic that is ours) + the measurement doors W12 landed
    "bcf-count-styles", "bcf-magnitude", "bcf-units-mask", "bcf-zeropad",
    "bcf-nonnumeric", "bcf-adaptive", "bcf-object-value", "bcf-measurement-landed",
    # NSRelativeDateTimeFormatter
    "rdf-numeric", "rdf-named", "rdf-spellout", "rdf-interval-and-components",
    "rdf-capitalization", "rdf-locale", "rdf-object-value",
    # NSDateComponentsFormatter (W11b: OUR rules over ICU'S data)
    "dcf-styles", "dcf-plural", "dcf-zero-behaviours", "dcf-positional-default",
    "dcf-positional-clock", "dcf-unit-rules", "dcf-phrases", "dcf-date-doors",
    "dcf-object-and-parse", "dcf-class-method",
    # W12's first slice: the unit machinery and the value that carries a unit
    "unit-identity", "unit-linear-converter", "unit-converter-raises",
    "unit-information-storage", "unit-conversion", "unit-measurement-arithmetic",
    "unit-measurement-value",
    # W12's dimensional families (the offset one first, then the ratio families)
    "unit-temperature-offset", "unit-duration", "unit-length",
    "unit-mass", "unit-area", "unit-angle",
    "unit-speed", "unit-acceleration", "unit-frequency", "unit-energy", "unit-power",
    "unit-electric", "unit-single-unit-families", "unit-fuel-efficiency",
    "unit-volume", "unit-pressure", "unit-concentration",
    # NSMeasurementFormatter (W12's last class)
    "unit-measurement-formatter", "unit-measurement-formatter-options",
)


class Case(BaseCase):
    title = "NSByteCountFormatter, NSListFormatter, NSISO8601DateFormatter, NSDateIntervalFormatter, NSRelativeDateTimeFormatter, NSPersonNameComponents: W11"
    tier = "fast"
    # This case boots once and reads the probe's output; it needs no clean guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_formatters")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FORMATTERS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FORMATTERS "):
                self.note(line)

        done = "FOUNDATION-FORMATTERS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no DONE marker; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FORMATTERS %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FORMATTERS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FORMATTERS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
