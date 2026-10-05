# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The table personalities and the legacy zeroing-weak flag — §62.95's acceptance.

TEN NAMES, all of them Apple's spellings of options this library already implements: five beside `NSMapTable`,
four beside `NSHashTable`, and `NSPointerFunctionsZeroingWeakMemory` among them. The aliases are macros aliasing
THIS LIBRARY'S NSPointerFunctions options, which is what Apple declares too — so the first two checks compare each
name against the option it names, because a macro pointing at a different bit would compile and silently change a
table's policy.

THE BEHAVIOUR IS CHECKED THROUGH REAL TABLES, ON BOTH SIDES EVERY TIME. CopyIn is asserted by inserting a MUTABLE
string, mutating it, and reading the key back — with the SAME table without CopyIn as the control, so "the table
stored something" cannot pass for "the table copied". The identity personality is asserted with two
equal-but-distinct strings, with a default table as the control. And the four map-table convenience constructors
and `+weakObjectsHashTable` are exercised because they are built FROM these options.

THE ONE DECLARED-NOT-INTERPRETED FLAG IS PINNED AS A DECLARATION: `NSPointerFunctionsZeroingWeakMemory` equals
the shipped `NSHashTableZeroingWeakMemory` and is NOT the weak-memory option, which is the whole content of what
NSPointerFunctions.h says about it.

The probe is `/System/Shared/tests/foundation_tableoptions`, ONE unit, importing only `<Foundation/Foundation.h>`.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_tableoptions"
CHECKS = (
    "the-map-table-aliases-are-the-pointer-functions-options",
    "the-hash-table-aliases-are-the-pointer-functions-options",
    "the-legacy-zeroing-weak-flag-is-one-name-in-two-places",
    "copy-in-copies-the-key-on-insertion",
    "the-identity-personality-compares-by-identity",
    "the-convenience-constructors-still-make-working-tables",
)


class Case(BaseCase):
    title = "NSMapTable/NSHashTable: the option names and the personalities they set (§62.95)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_tableoptions")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-TABLEOPTIONS-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-TABLEOPTIONS "):
                self.note(line)

        done = "FOUNDATION-TABLEOPTIONS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-TABLEOPTIONS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-TABLEOPTIONS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-TABLEOPTIONS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-TABLEOPTIONS-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
