# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Resume, end to end, with a real server — §62.31's acceptance.

docs/design/foundation-plan.md §62.31 gives `NSURLSessionDownloadTask` the doors Apple's resume story needs —
`-cancelByProducingResumeData:`, `-downloadTaskWithResumeData:` and the delegate's
`didResumeAtOffset:expectedTotalBytes:` — and `NSURLConnectionDownloadDelegate`'s resume door stays refused, because
an `NSURLConnection` has no way to be given resume data at all.

The probe is `/System/Shared/tests/foundation_downloadresume`, ONE unit, importing only `<Foundation/Foundation.h>`,
AND IT IS ITS OWN SERVER: a hand-written socket that sends HALF a 20-byte body and then STALLS mid-transfer. The
stall is what makes the cancel deterministic — there is no race about which side is faster — and it is also why this
cannot live in the connection probe, which is server-free by design.

  * `the-probe-binds-its-own-listener`            — the probe is the server;
  * `the-first-request-asks-for-the-whole-body`   — a fresh download sends NO Range, which is what makes the
                                                   second one's Range a resume rather than a habit;
  * `the-interrupted-transfer-produced-resume-data` — the cancel produced opaque data, and reported NO ending
                                                   (a cancel is not a failure);
  * `a-blob-we-did-not-write-is-not-a-resume`     — a blob with nothing of ours in it answers nil;
  * `the-resumed-request-asks-for-the-rest`       — `Range: bytes=10-` on the wire, sent by the transport's
                                                   existing header pass-through;
  * `the-resumed-transfer-reports-its-offset`     — the delegate was told it resumed at 10;
  * `the-resumed-download-holds-the-whole-body`   — the file holds all twenty bytes, the ten from before the
                                                   cancel and the ten from after;
  * `a-failed-download-carries-resume-data-on-its-error` — a dropped connection's ERROR carries the data under
                                                   Apple's `NSURLSessionDownloadTaskResumeData` key.

THE LAST ONE IS THE ONE A CALLER ACTUALLY USES: a transfer that fails on its own has to hand back the way to
continue it, or resume is only for callers who asked to stop.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_downloadresume"
CHECKS = ("the-probe-binds-its-own-listener",
          "the-first-request-asks-for-the-whole-body",
          "the-interrupted-transfer-produced-resume-data",
          "a-blob-we-did-not-write-is-not-a-resume",
          "the-resumed-request-asks-for-the-rest",
          "the-resumed-transfer-reports-its-offset",
          "the-resumed-download-holds-the-whole-body",
          "a-failed-download-carries-resume-data-on-its-error")


class Case(BaseCase):
    title = "Download resume: cancel, resume data, the Range request, and the whole body (§62.31)"
    tier = "fast"
    # It listens on its own loopback port and reads nothing from the image; the shared guest is enough.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_downloadresume")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-DOWNLOADRESUME-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-DOWNLOADRESUME "):
                self.note(line)

        done = "FOUNDATION-DOWNLOADRESUME DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-DOWNLOADRESUME DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-DOWNLOADRESUME %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-DOWNLOADRESUME RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-DOWNLOADRESUME-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
