# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSTask — W6d's acceptance.

docs/design/foundation-plan.md §45. The subprocess: the one class in this library that owns a PROCESS, so
it is the one that needs fork/exec, a second thread, and a status it can be asked for twice.

THE PROBE IS `/System/Shared/tests/foundation_task`, ONE unit, AND THE CHILD IS THE PROBE RE-EXECUTED: the
first thing main() does is look for a `--child…` mode in argv. That is why the checks can be exact — the
child's exit code, its output, its working directory and the signal it dies by are all OURS rather than
another program's behaviour that could change under us. A test that ran `/bin/sh` would be testing dash.

WHAT THE PROBE'S CHECKS ESTABLISH: launch/wait/status; the three standard descriptors through NSPipe (the
parent closes its own copy of the write end, which is the half of the pipe rule that is the CALLER's); the
arguments arriving without the program's own name; the environment REPLACING the inherited one; the working
directory read back by the child's own getcwd(3); a signal death reported as UncaughtSignal with the signal
number; terminate/interrupt delivering SIGTERM/SIGINT; a suspend/resume round trip; THE REAPER'S REASON FOR
EXISTING (the termination handler and the notification firing with nobody having called -waitUntilExit);
one launch per instance; a bad executable answering NO with an NSPOSIXErrorDomain error; nil arguments
RAISING; the static launcher; and the quality-of-service value being carried.

NOT GATED, AND WHY: `launchRequirement`/`launchRequirementData` are not declared (no code-signing
requirement subsystem exists here), and the four Apple-deprecated names — `-launchPath`, `-setLaunchPath:`,
`-launch`, `-currentDirectoryPath`/`-setCurrentDirectoryPath:`, `+launchedTaskWithLaunchPath:arguments:` —
are struck by §11.5.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_task"
CHECKS = (
    "task-runs-and-exits",
    "task-waits-and-reports",
    "task-captures-standard-output",
    "task-feeds-standard-input",
    "task-arguments-reach-the-child",
    "task-environment-reaches-the-child",
    "task-current-directory",
    "task-signal-death-is-uncaught",
    "task-terminate",
    "task-interrupt",
    "task-suspend-and-resume",
    "task-termination-handler-fires",
    "task-did-terminate-notification",
    "task-refuses-a-second-launch",
    "task-reports-a-bad-executable",
    "task-nil-arguments-raises",
    "task-static-launcher",
    "task-quality-of-service",
)


class Case(BaseCase):
    title = "NSTask: a subprocess, a reaper thread, and one status"
    tier = "fast"
    # It runs a probe that re-execs itself under /System/Temporary Files and cleans up after itself, so a
    # reused guest answers the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_task")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-TASK-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-TASK "):
                self.note(line)

        done = "FOUNDATION-TASK DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-TASK DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-TASK %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-TASK RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-TASK-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
