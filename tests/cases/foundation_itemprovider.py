# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSItemProvider: an ordered registration list, the loads that coerce it, and the two protocols an object travels by.

The probe is `/System/Shared/tests/foundation_itemprovider`, ONE unit importing only `<Foundation/Foundation.h>`.

THE FIXTURE CLASS CONFORMS TO BOTH PROTOCOLS, which is what makes the object doors real: the provider asks its class
which identifiers it can read, loads one as DATA, and hands the bytes to
`+objectWithItemProviderData:typeIdentifier:error:` — so one round trip through `-registerObject:` and
`-loadObjectOfClass:` exercises the reading protocol, the writing protocol and the provider at once.

THE PROBE WRITES ITS OWN FILE FIXTURE into the temporary directory, so its file-backed checks depend on the same
directory the LIBRARY writes its temporary copies into (the guest's is an FSH path that does not exist on the host —
which is why the four file/temp checks cannot pass in a host run and the probe is a guest probe in the same way
`foundation_filemanager` and `foundation_url` are; the other twelve checks pass on the host).

  * `an-empty-provider-refuses-every-door-by-domain-and-code` — an empty provider answers the item-unavailable error
    at every door, in this library's own domain;
  * `the-identifiers-come-back-in-registration-order-and-a-repeat-replaces-in-place`,
    `the-replacement-is-the-handler-that-answers` — the store is a LIST, and the replacement rule is ours and stated;
  * `a-data-registration-answers-its-bytes-before-the-load-returns` — the synchrony the header states, plus the
    finished progress object;
  * `a-value-that-cannot-be-coerced-to-data-is-refused-by-that-name`;
  * `a-file-registration-answers-the-file-its-bytes-and-in-place-when-asked` — the two URL doors and the whole
    content of the open-in-place option;
  * `a-data-registration-is-copied-to-a-file-named-by-suggested-name` (with the `-1` collision rule),
    `a-suggested-name-cannot-choose-a-directory`, `with-no-suggested-name-the-type-identifier-names-the-copy` —
    the three temporary-copy naming rules;
  * `an-item-is-handed-back-at-the-item-door-and-archived-at-the-data-door` — and the round trip through this
    tree's own unarchiver, which is the check that found the coercion ordering bug;
  * `an-objects-own-types-are-registered-and-an-object-comes-back-through-them` — the round trip through BOTH
    protocols, with the negative half of `-canLoadObjectOfClass:` beside it;
  * `a-class-registration-makes-its-object-on-demand-and-loads-through-it`,
    `the-file-options-decide-which-identifiers-a-query-answers`,
    `a-load-handler-is-given-no-expected-class-and-the-callers-options` (the boundary the header names),
    `the-preview-door-uses-the-handler-and-otherwise-refuses`,
    `a-copy-is-an-independent-provider-over-the-same-registrations`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_itemprovider"
CHECKS = ("an-empty-provider-refuses-every-door-by-domain-and-code",
          "the-identifiers-come-back-in-registration-order-and-a-repeat-replaces-in-place",
          "the-replacement-is-the-handler-that-answers",
          "a-data-registration-answers-its-bytes-before-the-load-returns",
          "a-value-that-cannot-be-coerced-to-data-is-refused-by-that-name",
          "a-file-registration-answers-the-file-its-bytes-and-in-place-when-asked",
          "a-data-registration-is-copied-to-a-file-named-by-suggested-name",
          "a-suggested-name-cannot-choose-a-directory",
          "with-no-suggested-name-the-type-identifier-names-the-copy",
          "an-item-is-handed-back-at-the-item-door-and-archived-at-the-data-door",
          "an-objects-own-types-are-registered-and-an-object-comes-back-through-them",
          "a-class-registration-makes-its-object-on-demand-and-loads-through-it",
          "the-file-options-decide-which-identifiers-a-query-answers",
          "a-load-handler-is-given-no-expected-class-and-the-callers-options",
          "the-preview-door-uses-the-handler-and-otherwise-refuses",
          "a-copy-is-an-independent-provider-over-the-same-registrations")


class Case(BaseCase):
    title = "NSItemProvider: registrations, the coerced loads, and the two item-provider protocols"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_itemprovider")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-ITEMPROVIDER-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-ITEMPROVIDER "):
                self.note(line)

        done = "FOUNDATION-ITEMPROVIDER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-ITEMPROVIDER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-ITEMPROVIDER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-ITEMPROVIDER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-ITEMPROVIDER-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
