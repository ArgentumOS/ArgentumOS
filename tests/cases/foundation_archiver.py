# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The classic archiver pair — §62.86's acceptance.

This closes `Files and Data Persistence / Deprecated`: `NSArchiver`, `NSUnarchiver` and the legacy
`NXReadNSObjectFromCoder`, the sequential pair that `NSKeyedArchiver` replaced.

THE WIRE IS THIS LIBRARY'S OWN, SO THE CONTRACT IS WHAT IS TESTED. Apple's classic archiver wrote
`typedstream`, whose structure is not published; nothing here produces or consumes it. What is honoured is what
the class promises: a graph goes out and comes back, ONE root per archive, order and type ARE the protocol (no
keys, no coercion), identity survives (a container referenced twice is one object, and a self-containing
container terminates), substitution works on both sides — and the one interoperability rule Apple states in
words: **a keyed archive cannot be read by a sequential unarchiver.**

The probe is `/System/Shared/tests/foundation_archiver`, ONE unit, importing only `<Foundation/Foundation.h>`.

IT ALSO PINS THE TWO FAMILIES' MUTUAL EXCLUSION FROM BOTH SIDES — a sequential door on a keyed archiver raises,
a keyed door on a sequential one raises, and each message names the family that answers — and it asserts the
three refusals a graph can produce: an object that is not `NSCoding` (by name), a class that speaks keys (which
reaches a keyed door and raises there), and the zone/class-name doors that are ABSENT rather than stubbed
because this library has no `NSZone` and `NSKeyedArchiver` already refuses the equivalent.

THE `CHECKS` TUPLE BELOW IS DERIVED FROM THE PROBE'S OWN NAMES, NOT TYPED — a hand-copied list is a second
source of truth that goes stale the moment a check is added.
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_archiver"
CHECKS = (
    "the-pair-is-declared-coder-side",
    "the-refused-doors-are-absent-rather-than-stubbed",
    "a-keyed-door-on-the-sequential-archiver-raises",
    "a-sequential-door-on-the-keyed-archiver-raises",
    "a-graph-round-trips",
    "identity-survives-the-round-trip",
    "a-self-containing-array-terminates-and-identity-holds",
    "an-nscoding-object-round-trips-with-its-fields",
    "an-object-that-is-not-nscoding-is-refused-by-name",
    "an-object-that-speaks-keys-is-refused-and-told-which-family-to-use",
    "a-nil-data-argument-raises-on-both-sides",
    "a-second-root-object-raises",
    "the-archiver-keeps-the-data-it-was-given",
    "data-that-is-not-an-archive-answers-nil",
    "a-truncated-archive-raises-instead-of-reading-past-its-end",
    "a-keyed-archive-is-refused-by-the-sequential-reader",
    "reading-a-value-as-the-wrong-type-raises",
    "the-writing-side-substitutes",
    "isatend-is-false-before-and-true-after-the-root",
    "the-reading-side-substitutes",
    "isatend-is-true-once-the-whole-archive-has-been-decoded",
    "the-version-door-raises-rather-than-fabricating",
    "nxreadnsobjectfromcoder-is-the-object-door",
    # §63.43: the SEQUENTIAL VALUE AND GEOMETRY doors, which the wire could not spell until a struct had a
    # tag of its own. `-encodePoint:`/`-decodePoint` and their four siblings are the doors Apple describes
    # as "invoke -encodeValueOfObjCType:at: and must be matched by a -decodePoint in order".
    "a-struct-round-trips-through-the-type-code-door",
    "the-unkeyed-geometry-doors-round-trip",
    "the-array-doors-round-trip",
    "the-sized-reading-door-refuses-a-buffer-that-is-too-small",
    "the-un-sized-reading-door-still-reads",
    "decodebyteswithminimumlength-reads-a-long-enough-run",
    "decodebyteswithminimumlength-refuses-a-short-run",
    "a-sequential-geometry-door-on-the-keyed-archiver-raises",
    "archiver-class-map", "archiver-class-map-clears", "archiver-secure-doors")


class Case(BaseCase):
    title = "NSArchiver/NSUnarchiver: the classic sequential pair (§62.86)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_archiver")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-ARCHIVER-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-ARCHIVER "):
                self.note(line)

        done = "FOUNDATION-ARCHIVER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-ARCHIVER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-ARCHIVER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-ARCHIVER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-ARCHIVER-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
