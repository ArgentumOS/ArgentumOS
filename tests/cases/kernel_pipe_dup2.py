# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The pipe/fork/dup2 wedge — a MINIMAL reproducer.

docs/design/foundation-plan.md §45. Twenty lines of ordinary POSIX with no Foundation, no threads and no
signals: pipe(), fork(), dup2() onto stdin in the child, a six-byte write in the parent. It exists because
the NSTask reproducer wedges there once its kernel fault is fixed, and a reproducer made of plain POSIX is
one nobody can argue with.
"""

from harness import BaseCase

PROBE = "/System/Shared/tests/kernel_pipe_dup2"


class Case(BaseCase):
    title = "kernel: pipe + fork + dup2 + a six-byte write"
    tier = "slow"
    timeout = 400

    def run(self, ctx):
        session = ctx.boot()
        if not session.shell_ready(150):
            self.check("shell-ready", False, "no serial shell")
            return

        mark = len(session.log_text())
        session.run("%s 50; echo PIPEDBG-STATUS=$?" % PROBE)
        session.wait_for(r"PIPEDBG DONE=|CHILD-STUCK", 300)
        out = session.output_since(mark)

        self.check("completes-all-50",
                   "PIPEDBG DONE=50 stuck=0" in out,
                   "50 pipe+fork+dup2 cycles answered: " + out.strip()[-300:])
        self.check("no-stuck-child",
                   "CHILD-STUCK" not in out,
                   "a child never exited: " + out.strip()[-300:])

        # THE ONE VARIABLE: the same loop with a LIVE THREAD while forking.
        mark = len(session.log_text())
        session.run("%s 50 thread; echo PIPEDBG-T-STATUS=$?" % PROBE)
        session.wait_for(r"PIPEDBG DONE=|CHILD-STUCK", 300)
        out = session.output_since(mark)
        self.check("threaded-completes", "PIPEDBG DONE=50 stuck=0" in out,
                   "with a live thread: " + out.strip()[-300:])
        self.check("threaded-no-stuck-child", "CHILD-STUCK" not in out,
                   "with a live thread a child never exited: " + out.strip()[-300:])

        # THE LAST DIFFERENCE: the child EXECs, with the pipe duped onto its stdin.
        mark = len(session.log_text())
        session.run("%s 50 et; echo PIPEDBG-E-STATUS=$?" % PROBE)
        session.wait_for(r"PIPEDBG DONE=|CHILD-STUCK", 300)
        out = session.output_since(mark)
        self.check("exec-child-completes", "PIPEDBG DONE=50 stuck=0" in out,
                   "child execs, with a live thread: " + out.strip()[-300:])
        self.check("exec-child-no-stuck", "CHILD-STUCK" not in out,
                   "child execs, with a live thread: a child never exited: " + out.strip()[-300:])

        # THE LAST UNTESTED DIFFERENCE: a THREAD reaps while the main thread forks (the real shape).
        mark = len(session.log_text())
        session.run("%s 50 er; echo PIPEDBG-R-STATUS=$?" % PROBE)
        session.wait_for(r"PIPEDBG DONE=|CHILD-STUCK|REAP-STUCK", 300)
        out = session.output_since(mark)
        self.check("reaping-thread-completes", "PIPEDBG DONE=50 stuck=0" in out,
                   "thread reaps + child execs: " + out.strip()[-300:])
        self.check("reaping-thread-no-stuck",
                   "REAP-STUCK" not in out and "CHILD-STUCK" not in out,
                   "thread reaps + child execs: " + out.strip()[-300:])

        # THE A/B: identical program and children; the MAIN thread reaps instead of the reaper thread.
        mark = len(session.log_text())
        session.run("%s 50 em; echo PIPEDBG-M-STATUS=$?" % PROBE)
        session.wait_for(r"PIPEDBG DONE=|MAINREAP-STUCK|CHILD-STUCK", 300)
        out = session.output_since(mark)
        self.check("main-thread-reaps", "PIPEDBG DONE=50 stuck=0" in out,
                   "the MAIN thread reaps: " + out.strip()[-300:])
