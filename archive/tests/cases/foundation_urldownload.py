# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSURLDownload and its delegate protocol — §62.82's acceptance.

§62.24 retired the deprecation ground (the user's policy, 2026-09-26: "all items removed for being
deprecated are un-deprecated in Argentum Foundation, and added to the work list"), and this closes the
last rows of `Networking / Legacy / URL Download`: the download OBJECT, whose local half is real — it
drives an NSURLSessionDownloadTask through the session's completion-handler door (the one §62.76 proved
round-trips a real body) and translates that one ending into the legacy protocol's sequence.

The probe is `/System/Shared/tests/foundation_urldownload`, ONE unit, importing only
`<Foundation/Foundation.h>`.

IT REGISTERS NOTHING, AND THAT IS THE POINT: the library registers its own transport (FNCURLURLProtocol — `file`,
`http`, `https`) AT LOAD (§62.83), so the `file:` transfers below are real transfers that RUN ONLY IF that
registration happened — this probe never calls `+registerClass:`. Its first attempt did the opposite, read the
resulting NSURLErrorUnsupportedURL as a verdict about the URL, and was reverted over its own missing call.

  * `class-and-protocol-declared`   — the class exists, derives from NSObject, and the delegate protocol is
                                       declared;
  * `a-download-lands-the-fixture-where-the-destination-says` — a destination set BEFORE the bytes arrive
                                       is where the file ends up, byte for byte;
  * `the-callbacks-arrive-in-order-and-once` — begin, created and finish exactly once, and no failure;
  * `the-request-is-kept-and-the-failure-flag-defaults-to-yes` — `-request` is the request it began with,
                                       `-deletesFileUponFailure` defaults YES, `-resumeData` is nil until a
                                       cancel produces some;
  * `a-destination-the-delegate-chooses-is-where-the-file-lands` — Apple's asynchronous decision door is
                                       honoured: the delegate answers by calling `-setDestination:`;
  * `a-download-nobody-destines-lands-in-the-temporary-directory` — a delegate that answers NOTHING falls
                                       into the stated fallback, reported through the created-destination
                                       door so a caller still learns where the bytes are;
  * `decoded-resume-is-refused-as-a-fact` — `+canResumeDownloadDecodedWithEncodingMIMEType:` answers NO:
                                       this class resumes bytes, not decoded streams.
"""
import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_urldownload"
CHECKS = ("class-and-protocol-declared",
          "a-download-lands-the-fixture-where-the-destination-says",
          "the-callbacks-arrive-in-order-and-once",
          "the-request-is-kept-and-the-failure-flag-defaults-to-yes",
          "a-destination-the-delegate-chooses-is-where-the-file-lands",
          "a-download-nobody-destines-lands-in-the-temporary-directory",
          "decoded-resume-is-refused-as-a-fact")


class Case(BaseCase):
    title = "NSURLDownload: the legacy download object with a real engine (§62.82)"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_urldownload")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-URLDOWNLOAD-STATUS-ECHO=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-URLDOWNLOAD "):
                self.note(line)

        done = "FOUNDATION-URLDOWNLOAD DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-URLDOWNLOAD DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-URLDOWNLOAD %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-URLDOWNLOAD RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-URLDOWNLOAD-STATUS-ECHO=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
