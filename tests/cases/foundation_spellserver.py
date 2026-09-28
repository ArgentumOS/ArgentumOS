# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSSpellServer and its delegate protocol — §62.85's acceptance.

This closes `Fundamentals / Spelling and Grammar`: the server side of a spell-checking service — the API a
SERVICE implements, the protocol it answers, and the three grammar keys.

THE ENGINE IS DEFERRED, AND THAT IS WHY THIS UNIT COULD LAND (docs/design/spelling-plan.md §3): the first
candidate was withdrawn on LANGUAGE grounds — a word list cannot recognise a Finnish or Hungarian inflection or a
German compound as a word, nor establish where a Chinese or Thai word ends — so no engine is chosen while the
question is decided on language grounds rather than licence grounds. `NSSpellServer` is the API a service
implements and it knows nothing about words, which is exactly what makes it writable today.

The probe is `/System/Shared/tests/foundation_spellserver`, ONE unit, importing `<Foundation/Foundation.h>` plus
the internal dispatch seam `<Foundation/FNSpellServerDispatch.h>`.

THE SEAM IS DRIVEN DIRECTLY, WHICH IS WHAT MAKES THE PROTOCOL LIVE: the delegate's seven doors are reached by a
CLIENT, no client is shipped (Apple's wire between the two is a private distributed-objects protocol), so the
probe calls the dispatch functions a future client protocol will call, and asserts what each does — including
what happens when a delegate writes NO door at all.

  * `class-and-protocol-declared`           — the class and the protocol exist;
  * `every-delegate-door-is-declared-optional` — all seven doors of the protocol, asked OF THE PROTOCOL;
  * `the-three-grammar-keys-are-their-names` — the keys are exported with their documented spellings;
  * `registration-accepts-a-language-and-vendor` / `repeating-a-registration-is-the-same-intent` /
    `registration-refuses-empty-names` — the registry, including the §62.83 repetition rule and our stated grounds;
  * `the-delegate-round-trips-and-starts-nil` — the accessor pair, and that a fresh server has none;
  * `an-unknown-word-is-not-a-user-word` / `learning-a-word-makes-it-a-user-word` /
    `forgetting-removes-it-from-both-answers` / `the-documents-ignored-word-answers-too` — the two stores behind
    `-isWordInUserDictionaries:caseSensitive:`, in both directions and at both sensitivities;
  * `the-classic-doors-reach-the-delegate` / `the-guess-door-answers-the-delegates-array` /
    `the-completion-door-passes-the-range-through` / `the-grammar-door-answers-details-in-the-three-shapes` /
    `the-unified-door-answers-and-counts` — every door, driven through the seam, asserting the DELEGATE's answer
    rather than merely that something came back;
  * `a-delegate-that-writes-nothing-gets-the-documented-defaults` — the fallbacks: `NSMakeRange(NSNotFound, 0)`,
    a nil array, a zero count, and never an exception or a fabricated answer;
  * `run-is-declared-and-deliberately-not-called` — present, and not called: its contract is a loop that never
    returns and nothing is connected to it yet.
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_spellserver"
CHECKS = ("class-and-protocol-declared",
          "every-delegate-door-is-declared-optional",
          "the-three-grammar-keys-are-their-names",
          "registration-accepts-a-language-and-vendor",
          "repeating-a-registration-is-the-same-intent",
          "registration-refuses-empty-names",
          "the-delegate-round-trips-and-starts-nil",
          "an-unknown-word-is-not-a-user-word",
          "learning-a-word-makes-it-a-user-word",
          "forgetting-removes-it-from-both-answers",
          "the-documents-ignored-word-answers-too",
          "the-classic-doors-reach-the-delegate",
          "the-guess-door-answers-the-delegates-array",
          "the-completion-door-passes-the-range-through",
          "the-grammar-door-answers-details-in-the-three-shapes",
          "the-unified-door-answers-and-counts",
          "a-delegate-that-writes-nothing-gets-the-documented-defaults",
          "run-is-declared-and-deliberately-not-called")


class Case(BaseCase):
    title = "NSSpellServer: the server side of a spell-checking service (§62.85)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_spellserver")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-SPELLSERVER-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-SPELLSERVER "):
                self.note(line)

        done = "FOUNDATION-SPELLSERVER DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-SPELLSERVER DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-SPELLSERVER %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-SPELLSERVER RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-SPELLSERVER-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
