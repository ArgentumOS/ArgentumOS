# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSUserDefaults — W5's acceptance.

docs/design/foundation-plan.md §41. The settings store: one plist per domain in the FSH's Configuration/
scopes, with the scope axis as the precedence and the property list as the value model. The probe is
`/System/Shared/tests/foundation_defaults`, ONE unit, importing only `<Foundation/Foundation.h>` plus
<pwd.h>/<unistd.h>/<stdlib.h> for the scratch root and the user name.

THE PROBE IS LAUNCHED WITH AN ARGUMENT, and that is part of what it tests: `-ProbeArgument from-argv` is
the only way to observe the argument domain, which is a fact about the launching process rather than about
the store. `defaults-argument-domain` fails if this case forgets it, which is the check working.

The 36 checks, in the probe's own order:

  * `defaults-registration` / `-volatile`      registerDefaults: answers, and writes NO file
  * `defaults-search-order` / `-volatile`      the earlier domain wins, shown by putting the key there
  * `defaults-persist-round-trip`              a SECOND object over the same root reads what the first wrote
  * `defaults-types-round-trip`                every plist type, each read back through a fresh object
  * `defaults-absent-answers`                  each absent key answers its documented zero
  * `defaults-string-array-strict`             -stringArrayForKey: refuses an array holding a non-string
  * `defaults-coercion`                        Apple's documented coercion: 1.0, "YES", "true", "1" are true
  * `defaults-mutation-after-set`              a store, not a live alias: mutating the set object changes
                                               nothing on disk
  * `defaults-immutable-read`                  the answer is not an NSMutableString
  * `defaults-scope-system-wins` / `-user-next` / `-shared-last`
                                               SYSTEM > USER > SHARED, MEASURED by removing a file at a time
  * `defaults-forced-key`                      -objectIsForcedForKey: is YES exactly when SYSTEM supplies it
  * `defaults-suite-lookup` / `-is-not-write-target` / `-removed`
                                               a suite is searched, is not where a write lands, and leaves
  * `defaults-volatile-built-ins` / `-set` / `-removed`
                                               NSArgumentDomain and NSRegistrationDomain always exist; a
                                               caller's volatile domain is searched until removed
  * `defaults-argument-domain`                 `-ProbeArgument from-argv`, and an unpassed key is absent
  * `defaults-dictionary-representation`       the union of the search list, in search order
  * `defaults-persistent-domain-shared-only` / `-set` / `-replaces` / `-remove`
                                               whole-domain doors, including SHARED-only and REPLACE
  * `defaults-change-notification` / `-no-notification-for-no-change`
                                               DidChange fires on a write and not on an absent-key removal
  * `defaults-refuses-non-plist-value` / `-empty-key` / `-escaping-domain`
                                               what is refused, and that a domain cannot name a file
                                               outside Configuration/
  * `defaults-corrupt-file-refused`            a malformed plist RAISES rather than reading as empty
  * `defaults-size-limit`                      crossing the maximum notifies AND still saves
  * `defaults-store-location` / `-cleanup`     THE ONE CHECK AT THE REAL ROOT:
                                               /Users/<user>/Configuration/<domain>.plist exists after a
                                               write, and the probe removes it again
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_defaults"
# The argument the probe's `defaults-argument-domain` check expects, and it is REQUIRED: see the docstring.
ARGS = "-ProbeArgument from-argv"
CHECKS = (
    "defaults-registration", "defaults-registration-volatile",
    "defaults-search-order", "defaults-search-order-volatile",
    "defaults-persist-round-trip", "defaults-types-round-trip",
    "defaults-absent-answers", "defaults-string-array-strict",
    "defaults-coercion", "defaults-mutation-after-set", "defaults-immutable-read",
    "defaults-scope-system-wins", "defaults-scope-user-next", "defaults-scope-shared-last",
    "defaults-forced-key",
    "defaults-suite-lookup", "defaults-suite-is-not-write-target", "defaults-suite-removed",
    "defaults-volatile-built-ins", "defaults-volatile-set", "defaults-volatile-removed",
    "defaults-argument-domain", "defaults-dictionary-representation",
    "defaults-persistent-domain-shared-only", "defaults-persistent-domain-set",
    "defaults-persistent-domain-replaces", "defaults-persistent-domain-remove",
    "defaults-change-notification", "defaults-no-notification-for-no-change",
    "defaults-refuses-non-plist-value", "defaults-refuses-empty-key",
    "defaults-refuses-escaping-domain", "defaults-corrupt-file-refused",
    "defaults-size-limit",
    "defaults-store-location", "defaults-store-location-cleanup",
    "the-legacy-localization-keys-are-pinned",
    "the-ubiquity-notifications-are-named-and-never-posted",
)


class Case(BaseCase):
    title = "NSUserDefaults: the settings store"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it runs a probe and reads its output. The probe
    # builds its store under /System/Temporary Files and removes it, and the one check at the real root
    # removes the file it made.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_defaults")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s %s; echo FOUNDATION-DEFAULTS-STATUS=$?" % (PROBE, ARGS))
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DEFAULTS "):
                self.note(line)

        done = "FOUNDATION-DEFAULTS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DEFAULTS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DEFAULTS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DEFAULTS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DEFAULTS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
