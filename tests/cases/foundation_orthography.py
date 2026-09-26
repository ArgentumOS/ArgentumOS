# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSOrthography — two values, the invariant that binds them, and the one piece of data that determines a script.

The probe is `/System/Shared/tests/foundation_orthography`, ONE unit importing only `<Foundation/Foundation.h>`.

IT EXISTS BECAUSE §62.18 NEEDED IT: `NSTextCheckingResult` shipped Apple's whole surface, `+orthographyCheckingResultWithRange:orthography:`
and its `-orthography` payload among it, and the class those two members speak in did not exist — so the pair could
only be handed nil. The ninth check below is that door.

  * `apple-s-example-for-hindi-holds-exactly` — APPLE'S OWN DOCUMENTED EXAMPLE IS THE CHECK: for Hindi the map has
    one key, `Deva`, whose array holds `hi`, and the dominant language is `hi`;
  * `four-more-languages-get-the-script-the-same-table-answers` — Russian Cyrillic, Arabic Arabic, Japanese Jpan,
    English Latn, from the same ICU likely-subtags data the factory binds;
  * `an-unknown-tag-falls-back-and-an-empty-one-is-refused` — the two cases that are ours and are stated in the
    header: a tag ICU does not know becomes ISO 15924's undetermined script `Zyyy`, and an empty tag raises;
  * `the-identical-door-builds-what-the-initializer-does` — two maps, the same class, two dominant scripts;
  * `the-five-ways-the-invariant-can-be-broken-each-refuse-by-name` — the dominant script must be a key of the map
    with a non-empty array, so a nil script, a nil map, an absent script, a non-array value and an empty array each
    raise `NSInvalidArgumentException` (the refusal is this library's choice, so which one it hit is asserted);
  * `all-scripts-are-sorted-and-all-languages-are-grouped-by-them` — the order IS ours and is stated: the map is a
    hash table, so the scripts are sorted and the languages are the concatenation of each script's OWN array, whose
    order is real data (index 0 is the dominant language);
  * `the-script-queries-answer-in-the-array-s-own-order-and-miss-with-nil`;
  * `a-value-compares-by-its-two-fields-and-copies-to-itself` — equality, hashing, description and `-copy` as the
    receiver, all overrides of NSObject's own doors rather than additions;
  * `a-text-checking-result-carries-an-orthography-through-its-own-door` — the tie-back to §62.18;
  * `the-two-values-survive-an-archive-and-answer-again` — NSCoding/NSSecureCoding through this tree's archiver,
    with the coder's own enforcement gap named in NSCoding.h.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_orthography"
CHECKS = ("apple-s-example-for-hindi-holds-exactly",
          "four-more-languages-get-the-script-the-same-table-answers",
          "an-unknown-tag-falls-back-and-an-empty-one-is-refused",
          "the-identical-door-builds-what-the-initializer-does",
          "the-five-ways-the-invariant-can-be-broken-each-refuse-by-name",
          "all-scripts-are-sorted-and-all-languages-are-grouped-by-them",
          "the-script-queries-answer-in-the-array-s-own-order-and-miss-with-nil",
          "a-value-compares-by-its-two-fields-and-copies-to-itself",
          "a-text-checking-result-carries-an-orthography-through-its-own-door",
          "the-two-values-survive-an-archive-and-answer-again")


class Case(BaseCase):
    title = "NSOrthography: the script a language is written in, and the map that binds it"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_orthography")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-ORTHOGRAPHY-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-ORTHOGRAPHY "):
                self.note(line)

        done = "FOUNDATION-ORTHOGRAPHY DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-ORTHOGRAPHY DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-ORTHOGRAPHY %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-ORTHOGRAPHY RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-ORTHOGRAPHY-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
