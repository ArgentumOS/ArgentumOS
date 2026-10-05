# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The JSON reading options Apple publishes and this library refused — §62.93's acceptance.

TWO GRAMMARS, AND THE STRICT ONE HAD TO BE MADE STRICT. `NSJSONSerialization.h` had refused
`json5Allowed` and `topLevelDictionaryAssumed` on the ground that "a value invented for them would be a
difference a program could see", and held the deprecated `allowFragments` spelling out because §11.5's
second exclusion kept deprecated API out. Both halves were wrong: Apple's own `NSJSONSerialization.h`
publishes every one of those bits, and that deprecation ground was RETIRED on 2026-09-26 (plan row D7,
§62.24). So the names ship — and with them the behaviours behind them, because a declared option with
no behaviour would be a stub.

READING THE PARSER TO DO IT FOUND THE REAL WORK. It accepted a MISSING COMMA (`[1 2]`), a trailing
comma and `+1`, so JSON5's rules could not have been observed at all: a probe that tested only JSON5
would have passed against the old parser for the wrong reason. The strict path is now RFC 8259's, and
the probe asserts BOTH directions of every difference — what strict JSON refuses, and that the same
text parses under `json5Allowed`.

THE PROBE FOUND A SECOND, UNRELATED DEFECT AND THIS UNIT FIXED IT TOO: JSON5's `\\0` writes a NUL
character, and `+stringWithString:` / `-initWithString:` built a UTF-8 C string from the source and
re-parsed it, so the NUL TRUNCATED the copy (a one-character string answered length 0). All three copy
doors now copy by characters, and the `nul_diag` measurement is recorded in the plan.

THE TWO BOUNDARIES THE HEADER ADMITS ARE ASSERTED RATHER THAN ASSUMED: an unquoted key is ASCII plus
`\\u` escapes (a non-ASCII key is refused BY NAME), and `topLevelDictionaryAssumed` only ADDS the
brace-less dictionary form — a document beginning with `{` or `[` parses exactly as it did.

The probe is `/System/Shared/tests/foundation_json`, ONE unit, importing only `<Foundation/Foundation.h>`.

THE `CHECKS` TUPLE IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED (§62.86's rule).
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_json"
CHECKS = (
    "strict-numbers-are-rfc-8259s",
    "strict-member-lists-need-their-commas",
    "strict-leaves-the-other-json5-spellings-alone",
    "json5-comments-are-skipped",
    "json5-strings-come-in-two-quotes-and-more-escapes",
    "json5-numbers",
    "json5-member-lists-may-end-in-a-comma",
    "json5-keys-may-be-unquoted",
    "json5-leaves-a-strict-document-alone",
    "json5-blank-space-is-not-only-ascii",
    "the-assumed-top-level-dictionary-form",
    "the-assumed-form-only-adds",
    "the-deprecated-allow-fragments-spelling-carries-the-same-bit",
)


class Case(BaseCase):
    title = "NSJSONSerialization: JSON5, the assumed dictionary, and two grammars that differ (§62.93)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_json")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-JSON-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-JSON "):
                self.note(line)

        done = "FOUNDATION-JSON DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-JSON DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-JSON %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-JSON RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-JSON-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
