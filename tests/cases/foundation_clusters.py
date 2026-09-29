# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The class-cluster mechanism — M0 of docs/design/foundation-clusters-plan.md.

The plan's §C.3 lists the contract the library will match in sixteen families. M0
lands the MECHANISM first and proves it on a cluster the probe defines ITSELF, so
that no shipped class changes behaviour in this unit. Two of these checks are the
ones nothing else in the suite can make:

  * `an-ordinary-class-substitutes-nothing` — the new `-classForCoder` default must
    be inert for every class that is not a cluster, which is the whole library;
  * `the-archive-names-no-private-class` — NSKeyedArchiver's archive must carry the
    PUBLIC class name and no private one. That is the hinge §C.4 is about, and it
    is measured by SEARCHING THE ARCHIVE'S OWN BYTES rather than by asking an object
    what it would answer: a check that only asked would pass even if the archiver
    went on recording `-class`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_clusters"
CHECKS = (
          "the-front-is-allocatable-and-inits-empty",
          "class-answers-the-concrete-class",
          "an-instance-is-still-kind-of-the-front",
          "plus-class-answers-the-front",
          "a-constructor-chooses-the-concrete-class-by-the-data",
          "class-for-coder-answers-the-front",
          "class-for-archiver-defaults-to-class-for-coder",
          "an-ordinary-class-substitutes-nothing",
          "the-archive-is-not-empty",
          "the-archive-names-the-public-class",
          "the-archive-names-no-private-class",
          "the-archive-round-trips-through-the-front",
          )


class Case(BaseCase):
    title = "the class-cluster mechanism: private concrete classes, and the archiver's hinge"
    tier = "fast"
    # The probe defines its own classes and touches no fixture, so it can share a guest like the rest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_clusters")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-CLUSTERS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-CLUSTERS "):
                self.note(line)

        done = "FOUNDATION-CLUSTERS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-CLUSTERS DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        # NOT ANCHORED AT `^`: the probe prints its checks INDENTED (the style the sibling probes use),
        # and the first version of this case anchored anyway — it matched nothing while the probe's own
        # tally said ok=12 fail=0. A FAIL line cannot match this either, because it ends with the detail
        # rather than with " ok".
        missing = [c for c in CHECKS
                   if not re.search(r"FOUNDATION-CLUSTERS %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS),
                           ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-CLUSTERS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-CLUSTERS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
