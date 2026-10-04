# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSProcessInfo — F13.13's acceptance.

docs/design/foundation-plan.md §10. The running program describing itself, with each answer pinned to
a source rather than to a plausible value. The probe is
`/System/Shared/tests/foundation_processinfo`, ONE unit, importing only `<Foundation/Foundation.h>`
plus `<unistd.h>` for the one cross-check that matters.

  * `proc-shared-instance`    — `+processInfo` answers the same object twice;
  * `proc-identifier`         — the pid, checked against `getpid(2)` rather than merely positive;
  * `proc-name-and-arguments` — the kernel's `comm`, and argv[0] read from `/proc/self/cmdline`:
                                the path this probe was launched with, which no constant in the probe
                                could produce;
  * `proc-environment`        — the C library's `environ` as a dictionary containing `PATH`;
  * `proc-counts`             — processors, active processors and physical memory;
  * `proc-host-and-uptime`    — a host name, and a monotonic uptime;
  * `proc-unique-strings`     — TWO globally unique strings, which must DIFFER;
  * `proc-version`            — this system's OWN name and version, plus the comparison against it
                                including the case that must answer NO.

TWO NAMES ARE OURS AND NOT COCOA'S, and the header says why: `-operatingSystemName` answers
`NSArgentumOperatingSystem`, because Cocoa's constants name Mach and Windows NT and returning one of
those would be a lie a version string is cheap to avoid.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_processinfo"
CHECKS = ("proc-shared-instance", "proc-identifier", "proc-name-and-arguments",
          "proc-environment", "proc-counts", "proc-host-and-uptime",
          "proc-unique-strings", "proc-version",
          # §62.98: the vocabulary of -operatingSystemName and the thermal state that had no door.
          "proc-operating-system-names", "proc-thermal-state-and-its-notification",
          # §65: the account names, the version components, and the four platform-compatibility flags.
          "proc-user-names", "proc-version-components", "proc-platform-flags",
          "proc-activities", "proc-termination-and-platform-flags")


class Case(BaseCase):
    title = "NSProcessInfo: the running program describing itself"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_processinfo")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-PROCESSINFO-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-PROCESSINFO "):
                self.note(line)

        done = "FOUNDATION-PROCESSINFO DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-PROCESSINFO DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-PROCESSINFO %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-PROCESSINFO RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-PROCESSINFO-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
