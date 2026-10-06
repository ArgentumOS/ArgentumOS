# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSFileHandle / NSPipe — W6c's acceptance.

docs/design/foundation-plan.md §44. A descriptor with an object round it, whose ASYNCHRONOUS half is
why this class needed W6a and W6b first: `-readInBackgroundAndNotify` and the two handler doors are all
current API, and every one of them is a descriptor registered with a run loop.

THE PROBE IS `/System/Shared/tests/foundation_filehandle`, ONE unit, importing
`<Foundation/Foundation.h>` plus the POSIX headers — because a file handle wraps a descriptor and half
the checks ask the KERNEL what happened to it (`fcntl(2)` for the ownership rules, a zero-byte read for
end of file, the notification's own userInfo for the asynchronous half).

THE FAMOUS HALF OF THE CLASS IS THE DEPRECATED ONE and is therefore absent by §11.5: `-readDataToEndOfFile`,
`-readDataOfLength:`, `-writeData:`, `-offsetInFile`, `-seekToEndOfFile`, `-seekToFileOffset:`,
`-closeFile`, `-synchronizeFile`, `-truncateFileAtOffset:` and `NSFileHandleNotificationMonitorModes`.
What replaced them is the same operations carrying an error out-parameter, which is what the checks below
exercise.

NOT GATED, AND WHY: `-acceptConnectionInBackgroundAndNotify` is implemented exactly as Apple describes and
CANNOT FIRE ON THIS SYSTEM — `select(2)` does not report a LISTENING descriptor as readable, measured in
§43 with `accept(2)` returning the connection in the same run. A check for it would either fail for a
kernel reason or be vacuous, so it is recorded in the header and the plan instead.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_filehandle"
CHECKS = (
	  "pipe-ends-are-distinct", "pipe-carries-data", "pipe-eof-when-writer-closes", "pipe-is-one-way",
	  "file-handle-reads-a-file", "file-handle-seeks", "file-handle-updates-a-file", "file-handle-write-is-complete",
	  "file-handle-truncates", "file-handle-close-then-errors", "legacy-read-and-seek", "legacy-write-truncate-and-close",
	  "file-handle-owns-what-it-opened", "file-handle-adopts-what-it-did-not", "standard-handles", "background-read-posts-data",
	  "background-read-is-one-shot", "background-read-rearms", "background-wait-does-not-read", "background-read-to-end",
	  "readability-handler-repeats", "writeability-handler-runs", "background-read-on-closed-handle-raises", "file-handle-archiving-refused",
	  "standard-device-handles-are-singletons-with-their-own-descriptors", "the-descriptor-constructor-and-the-error-carrying-doors", "the-null-device-swallows-writes-and-answers-nothing", "the-url-factories-mirror-the-path-factories",
)


class Case(BaseCase):
    title = "NSFileHandle / NSPipe: a descriptor, and the operations scheduled against it"
    tier = "fast"
    # It runs a probe and reads its output; the probe builds its files under /System/Temporary Files and
    # removes them, so a reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_filehandle")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-FILEHANDLE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-FILEHANDLE "):
                self.note(line)

        done = "FOUNDATION-FILEHANDLE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-FILEHANDLE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-FILEHANDLE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-FILEHANDLE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-FILEHANDLE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
