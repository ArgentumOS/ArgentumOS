# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""KVO — F13.9's acceptance for the observer registry.

docs/design/foundation-plan.md §10. The KVC header closed this door by name ("KVO is a REGISTRY of
observers with a dependency graph. It is a service, not a rule"); a registry is storage, and this is
the door being opened. The probe is `/System/Shared/tests/foundation_kvo`, ONE unit, importing only
`<foundation/Foundation.h>`.

  * `kvo-notifies-on-a-kvc-write`        — `-setValue:forKey:` reaches the observer with the key
                                    path, the object and the NEW value;
  * `kvo-old-and-new-cross-the-change`   — the OLD and NEW values around one change, which is the
                                    pair the will/did split exists to produce;
  * `kvo-context-comes-back-untouched`   — the context pointer the observer registered with;
  * `kvo-initial-option`                 — the callback arrives DURING `-addObserver:` with the
                                    value as it stands;
  * `kvo-prior-option`                   — a `.Prior` observer is called TWICE per change, the first
                                    time flagged as prior;
  * `kvo-manual-pair-notifies`           — the documented manual pair around a DIRECT setter, which
                                    is how a runtime-invisible change is announced;
  * `kvo-remove-stops-it`                — no further callbacks after removal;
  * `kvo-observation-info-round-trips`   — Cocoa's own per-object storage doors.

NAMED LIMITS, asserted nowhere because they are not implemented: a directly-called setter does not
NOTIFY by itself (that needs per-class interception), and a dotted key path is registered as the
literal string it was given rather than decomposed into its segments.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_kvo"
CHECKS = ("kvo-notifies-on-a-kvc-write", "kvo-old-and-new-cross-the-change",
          "kvo-context-comes-back-untouched", "kvo-initial-option", "kvo-prior-option",
          "kvo-manual-pair-notifies", "kvo-remove-stops-it",
          "kvo-observation-info-round-trips")


class Case(BaseCase):
    title = "KVO: the registry the KVC header refused by name"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_kvo")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-KVO-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-KVO "):
                self.note(line)

        done = "FOUNDATION-KVO DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-KVO DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-KVO %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-KVO RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-KVO-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
