# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSExtensionItem — §62.69's acceptance, and the last row of `App Support / Attachments`.

docs/design/foundation-plan.md §62.69. Four ledger rows: the class and the three wire keys.

The class is a VALUE OBJECT WITH A WIRE FORM — four copied properties, plus `NSCopying` and `NSSecureCoding`,
which Apple's page lists.

The probe is `/System/Shared/tests/foundation_extensionitem`, ONE unit, importing only
`<Foundation/Foundation.h>`.

  * `the-three-keys-exist-are-distinct-and-are-names-not-paths` — the constants are names, not file paths;
  * `an-item-starts-empty-and-that-is-valid` — every property is optional on Apple's page;
  * `the-keys-are-the-fields-names-on-the-wire` — the contract the constants exist for, asserted in both
                                     directions: a payload keyed by them carries the item's fields, and a reader
                                     that only has the keys gets the values back;
  * `the-properties-are-snapshots-not-references` — Apple's `copy` ownership, proven by mutating the caller's
                                     mutable string afterwards;
  * `a-copy-is-a-value-copy` / `changing-the-original-does-not-change-the-copy`;
  * `secure-coding-is-claimed` — NSSecureCoding means saying YES;
  * `an-item-round-trips-through-a-keyed-archiver` — the coder doors against the shipped keyed archiver;
  * `an-uncodable-payload-is-refused-rather-than-silently-dropped` — the limit is asserted AS a limit, and it is
                                     the archiver's refusal on the sending side, not this class's.

AND THIS PROBE FOUND A REAL DEFECT, which is why the round-trip check matters: `NSAttributedString`'s coder doors
were unreachable. It asked the property-list serializer for the BINARY format, which this library NAMES AND
REJECTS by design, so `-encodeWithCoder:` raised for EVERY attributed string; and it decoded with
`-decodeBytesForKey:returningLength:` where Apple and the rest of the tree spell it `returnedLength:`, so the
decoder door was a selector nobody implements. Both were compiled silently because the file saw only a FORWARD
DECLARATION of NSCoder: clang warns (-Wobjc-method-access) and assumes `id` rather than failing. Three faces, one
result: a class Apple documents as NSSecureCoding could not be archived at all.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_extensionitem"
CHECKS = ("the-three-keys-exist-are-distinct-and-are-names-not-paths",
          "an-item-starts-empty-and-that-is-valid",
          "the-keys-are-the-fields-names-on-the-wire",
          "the-properties-are-snapshots-not-references",
          "a-copy-is-a-value-copy",
          "changing-the-original-does-not-change-the-copy",
          "secure-coding-is-claimed",
          "an-item-round-trips-through-a-keyed-archiver",
          "an-uncodable-payload-is-refused-rather-than-silently-dropped")


class Case(BaseCase):
    title = "NSExtensionItem: the value object and its wire form"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_extensionitem")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-EXTENSIONITEM-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-EXTENSIONITEM "):
                self.note(line)

        done = "FOUNDATION-EXTENSIONITEM DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-EXTENSIONITEM DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-EXTENSIONITEM %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-EXTENSIONITEM RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-EXTENSIONITEM-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
