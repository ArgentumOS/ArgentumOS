# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""THE KERNEL BUG'S REPRODUCER — a RED case until the kernel is fixed.

docs/design/foundation-plan.md §45. It was found through Foundation's NSTask and it is NOT a Foundation
bug: the probe is plain C, has no Foundation in it, and its children are /System/Tools/true.

WHAT IT PINS, in the kernel's own words: a `0x7f00_000008084x` instruction fetch (rip == cr2) with EVERY
GENERAL REGISTER ZERO, in a process that has been launching children from a parent whose worker thread
REAPS with waitpid(2). Two faults with that exact shape have been observed: one in the NSTask probe, one
here.

THE WAIT IS DELIBERATE: it looks for the loop's END **or** the kernel's fault line, whichever comes first,
so a regression fails in seconds instead of burning the case's whole budget — and the check's detail
quotes the fault it found.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/kernel_threaded_exec"


class Case(BaseCase):
    title = "kernel: fork+exec from a parent whose thread reaps"
    tier = "slow"
    timeout = 400

    def run(self, ctx):
        session = ctx.boot()
        if not session.shell_ready(150):
            self.check("shell-ready", False, "no serial shell")
            return

        mark = len(session.log_text())
        # the token comes back REVERSED, so it cannot be matched off the guest's echo of the command line
        session.run("%s 1 400 tokmode1" % PROBE)
        session.wait_for(r"token=1edomkot|Page Fault at", 300)
        out = session.output_since(mark)
        whole = session.log_text()
        fault = [l for l in whole.splitlines() if "Page Fault at" in l]

        self.check("parent-survives-400-children",
                   "THREADED-EXEC-DONE" in out,
                   "the parent did not finish; the kernel said: "
                   + (fault[-1].strip() if fault else "no fault line"))

        self.check("no-instruction-fetch-fault",
                   not fault,
                   "*** REPRODUCED ***: " + (fault[-1].strip() if fault else "clean"))

        # MODE 3: a WORKER THREAD BLOCKS in waitpid(SPECIFIC pid) for a child the MAIN thread forked. Every
        # other mode here POLLS with waitpid(-1, WNOHANG), and a poll survives a MISSING WAKEUP by asking
        # again - which is how the do_exit bug (fixed 603a22e2) hid until NSTask's reaper, which does block,
        # hung the whole process. The child sleeps, so it is still RUNNING when the thread blocks; the
        # program reports BLOCKING-WAIT-HUNG on its own ceiling, so a regression fails in seconds.
        mark = len(session.log_text())
        session.run("%s 3 0 bwmode3" % PROBE)
        session.wait_for(r"token=3edomwb|BLOCKING-WAIT-HUNG", 60)
        out = session.output_since(mark)
        self.check("a-thread-blocks-in-waitpid",
                   "BLOCKING-WAIT-DONE" in out and "token=3edomwb" in out,
                   "a thread's BLOCKING waitpid(specific pid) reaped the child: " + out.strip()[-300:])
