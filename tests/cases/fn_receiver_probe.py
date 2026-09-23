# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""fn_receiver_probe - the local receiver, alone. Passes iff the probe SURVIVES and reaches its marker."""

from harness import BaseCase

PROBE = "/System/Shared/tests/fn_receiver_probe"
CHECKS = ()


class Case(BaseCase):
    title = "fn_receiver_probe: does netcat -l start, and does a posted body reach it?"
    tier = "fast"
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("fn_receiver_probe")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready else "no shell")
        if not ready:
            return
        mark = len(session.log_text())
        session.run("%s; echo FNRCV-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FNRCV"):
                self.note(line)
        done = "FNRCV DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "did not reach its end marker; tail: " + out.strip()[-400:])

        # THE KERNEL PROPERTY, ASSERTED WHERE IT IS ACTUALLY VISIBLE. netcat passes a -1 to setsockopt (its
        # unchecked socket() path), so this is a REAL negative descriptor arriving at the syscall - and the
        # answer must be EBADF, decided WITHOUT reading current->fd[-1]. Before the fix the string here was
        # NONDETERMINISTIC ("Bad file descriptor" in one run, "Not a socket" in the next) because the check
        # was passing on garbage from past the array; deterministic EBADF is the property that the bound is
        # now checked. A caller that is buggy in its own right is a perfectly good way to test the kernel.
        refused = "setsockopt: Bad file descriptor" in out
        self.check("negative-descriptor-is-refused-with-EBADF", refused,
                   "a negative descriptor to setsockopt answers EBADF, without an out-of-bounds read"
                   if refused else
                   "expected 'Bad file descriptor' on the console (the pre-fix run said 'Not a socket'); "
                   "tail: " + out.strip()[-400:])
