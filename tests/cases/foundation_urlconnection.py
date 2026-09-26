# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURLConnection and its two data-side protocols — §62.25's acceptance.

docs/design/foundation-plan.md §62.24 retired the deprecation ground (the user's policy, 2026-09-26:
"all items removed for being deprecated are un-deprecated in Argentum Foundation, and added to the work
list"), and §62.25 is the first row of that list to land: Apple's older way to perform an exchange,
written as a FACADE over the session that already exists rather than as a second transport.

The probe is `/System/Shared/tests/foundation_urlconnection`, ONE unit, importing only
`<Foundation/Foundation.h>`.

SERVER-FREE, WHICH IS THE PROBE'S ARRANGEMENT RATHER THAN A GAP: the round trips use `file://`, so they
are real transfers through NSURLConnection -> NSURLSession -> NSURLProtocol -> the bridge with nothing to
start first, and the one door that needs a 3xx (the redirect) is tested directly through the runtime.

  * `connection-class-declared`             — the class exists, derives from NSObject, answers the scheme question;
  * `delegate-protocols-declared`           — both protocols exist and the data one refines the base;
  * `can-handle-request-asks-the-registry`  — http is handled, a nonsense scheme is not (asked, not assumed);
  * `synchronous-round-trip`                — the bytes come back, with the response and no error;
  * `synchronous-failure-is-reported-not-raised` — a missing file: nil, and the error is the only report;
  * `asynchronous-streaming-round-trip`     — a delegate sees the whole body;
  * `the-calls-arrive-in-order`             — response, then data, then the ending (the order is the contract);
  * `original-and-current-request`          — the two requests agree when nothing redirected;
  * `redirect-door-follows-what-the-delegate-returns` — the delegate's value IS the session's answer;
  * `redirect-door-nil-means-do-not-follow`  — and nil means nil, which is "do not follow";
  * `redirect-door-passes-a-different-request` — nothing here rewrites a caller's request;
  * `redirect-door-with-no-delegate-door-follows` — a delegate that implements nothing is not asked;
  * `refused-doors-are-absent`              — the inventory: the run-loop pair, the five authentication
                                              doors and the three session-shape doors, each with a ground;
  * `download-protocol-declared`             — the download protocol exists, with the door that means it;
  * `download-round-trip`                    — a real download: the delegate is handed a file holding the body;
  * `the-data-doors-are-not-used-for-a-download` — a delegate implementing BOTH protocols is not fed by the
                                              data doors (the dispatch rule);
  * `the-download-progress-door-is-reported` — the progress door fires with this transfer's own totals;
  * `download-refusals-are-absent`           — RESUME is the one refusal left, with its measured ground;
  * `the-session-download-protocol-shape`    — the session's download protocol: file REQUIRED, progress optional;
  * `the-auth-doors-are-declared`            — the modern door and the deprecated pair §62.24 put in scope;
  * `the-modern-door-supersedes-the-deprecated-pair` — asked once, and answered through the SENDER (the
                                              continuation is not called by this class at all);
  * `the-deprecated-pair-is-asked-gate-first` — Apple's order for a delegate without the modern door;
  * `a-no-from-the-gate-means-no-authentication` — a NO means the transfer continues without credentials;
  * `no-auth-door-means-the-default-without-waiting` — the rule every door here keeps;
  * `the-declared-surface-is-what-ships`    — every door this slice ships is reachable.

THE LAST TWO ARE THE §11.2 DISCIPLINE: a door that is declared and never called is worse than an absent
one, so what is absent is asserted absent, by name, with the ground the header gives it.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urlconnection"
CHECKS = ("connection-class-declared", "delegate-protocols-declared",
          "can-handle-request-asks-the-registry", "synchronous-round-trip",
          "synchronous-failure-is-reported-not-raised", "asynchronous-streaming-round-trip",
          "the-calls-arrive-in-order", "original-and-current-request",
          "redirect-door-follows-what-the-delegate-returns",
          "redirect-door-nil-means-do-not-follow", "redirect-door-passes-a-different-request",
          "redirect-door-with-no-delegate-door-follows", "refused-doors-are-absent",
          "download-protocol-declared", "download-round-trip",
          "the-data-doors-are-not-used-for-a-download", "the-download-progress-door-is-reported",
          "download-refusals-are-absent", "the-session-download-protocol-shape",
          "the-auth-doors-are-declared", "the-modern-door-supersedes-the-deprecated-pair",
          "the-deprecated-pair-is-asked-gate-first", "a-no-from-the-gate-means-no-authentication",
          "no-auth-door-means-the-default-without-waiting",
          "the-declared-surface-is-what-ships")


class Case(BaseCase):
    title = "NSURLConnection: the deprecated family as a facade over the session (§62.25)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urlconnection")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLCONNECTION-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLCONNECTION "):
                self.note(line)

        done = "FOUNDATION-URLCONNECTION DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLCONNECTION DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLCONNECTION %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLCONNECTION RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLCONNECTION-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
