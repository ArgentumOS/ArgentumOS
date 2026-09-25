# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The mount table's volume keys — W8p slice 6e's acceptance (foundation-plan.md §60).

THIS SYSTEM PUBLISHES ITS MOUNTS: `/proc/mounts` prints `device mountpoint fstype rw|ro 0 0` for every mount
that is not kernel-internal (measured in `fs/procfs/data.c`), and that is what these keys read. One of them is
an IMPROVEMENT on slice 6c: `NSURLVolumeIsReadOnlyKey` was answered there by probing a write and looking for
EROFS — whose point was that a mount's read-onlyness is not a permission — and the table says it outright, so
it is read from the flag field now.

A URL's volume is THE LONGEST MOUNT POINT THAT PREFIXES ITS PATH, which is what puts `/proc/version` on the
procfs volume rather than on the root that contains the mount point — and the probe asserts exactly that,
because a longest-prefix rule is the one thing a naive "first matching mount" gets wrong.

The probe is `/System/Shared/tests/foundation_substratekeys`, ONE unit, importing only
`<Foundation/Foundation.h>`. NO FIXTURE: it asks about the volumes it is running on.

  * `masses-the-door-lists-the-table` — and the guest has at least the root, procfs and the device tree;
  * `masses-a-file-reports-the-volume-holding-it` — the longest-prefix rule;
  * `masses-the-name-and-type-come-from-the-table` — the mount point's own name (there are no volume labels
                                 here) and the file system's name;
  * `masses-the-identifier-is-the-device` — opaque, and the same for two files on one volume;
  * `masses-read-only-comes-from-the-table-flag` — the guest mounts both read-write, so the check asserts the
                                 TABLE was read rather than the opposite;
  * `masses-is-volume-and-is-mount-trigger` — the root of a mounted file system, and a directory a mount
                                 landed on, are one statement about the table;
  * `masses-the-resource-count-and-size-support-come-from-the-file-system` — answered by asking the file
                                 system, which is what those keys are about;
  * `masses-the-door-prefetches-the-keys`.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_substratekeys"
CHECKS = ("keys-a-gapped-file-is-not-sparse", "keys-extended-attributes-answer-per-volume",
          "keys-the-block-size-comes-from-the-file-system", "keys-hard-links-are-proved-not-claimed",
          "keys-exclusive-renaming-is-refused-by-the-substrate", "keys-the-entry-count-is-two-sided",
          "keys-the-plain-facts-are-true", "keys-the-unavailable-ones-answer-without-error")


class Case(BaseCase):
    title = "The substrate keys: measured, then asserted"
    tier = "fast"
    # No fixture and no file system work: it can share a guest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_substratekeys")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-SUBSTRATEKEYS-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            # the MEASUREMENTS are the point of this run: surface them, because the assertions are written
            # from these numbers rather than from a guess.
            if line.startswith("FOUNDATION-SUBSTRATEKEYS ") or line.startswith("SUBSTRATE "):
                self.note(line)

        done = "FOUNDATION-SUBSTRATEKEYS DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-SUBSTRATEKEYS DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-SUBSTRATEKEYS %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-SUBSTRATEKEYS RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS)) and tally.group(2) == "0",
                   "the probe's own tally: %s" % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-SUBSTRATEKEYS-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
