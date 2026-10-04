# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The long tail of small constants, plus the two thread notifications with real producers — §62.103.

EVERY PUBLISHED VALUE IS COMPARED WITH IT AND EVERY VALUE THAT IS THIS LIBRARY'S IS PINNED, so the two
sentinel calendar units, the OpenStep reserved base, the string ceiling, the bookmark option (ours at 1<<5
where Apple's is 256 — the header says which is which), the XML entity kind's number in this tree's own enum
and the undo run-loop ordering's 350000 cannot drift silently.

THE THREAD NOTIFICATIONS ARE CHECKED BY MAKING THEM HAPPEN: an observer is registered, a thread is started and
finishes, and both the will-become-multithreaded notice and the thread-will-exit notice must arrive — with the
exit notice's OBJECT being the thread that went away. That is the only way this probe can tell a posted name
from a declared one, and it is why those two names shipped with producers rather than as bare constants.

THE REST ARE NAMES WHOSE VALUE IS THEIR NAME (the archive root key, the progress kind, the file-protection
class, the stream service type, the discardable key, the two failing-URL keys, the cookie notice), compared
against their own names because that string is what a plist or a log spells.

The probe is `/System/Shared/tests/foundation_constants`, ONE unit, importing only `<Foundation/Foundation.h>`.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_constants"
CHECKS = (
    "calendar-unit-sentinels",
    "openstep-reserved-base-and-string-ceiling",
    "bookmark-option-and-its-type",
    "url-resource-keys-name-themselves",
    "thread-notifications-are-really-posted",
    "the-cookie-notice-and-the-single-threaded-name",
    "file-handle-monitor-modes",
    "keys-and-kinds-value-their-names",
    "undo-run-loop-ordering-and-the-xml-entity-kind",
    "error-user-info-key-type",
    # §62.104: the last two enums — the comparison-predicate options type and the sort options, the second
    # MEASURED through both doors rather than declared.
    "sort-options-and-the-stability-promise",
    # §62.106: the KVC exception name (Apple declares it in the scripting header this tree does not have).
    "kvc-exception-name-is-what-implementors-raise",
)


class Case(BaseCase):
    title = "the constants long tail, and the two thread notifications that are really posted (§62.103)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_constants")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CONSTANTS-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CONSTANTS "):
                self.note(line)

        done = "FOUNDATION-CONSTANTS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CONSTANTS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-CONSTANTS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CONSTANTS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CONSTANTS-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
