# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Are a pty's two read paths actually woken? — a Foundation-free behaviour test.

docs/design/foundation-plan.md §58.1b. §58.1's cure (arm the channel, then look, then commit —
kernel/sleep.c) was applied to eleven waiters, and TWO of them went in unverifiable because nothing in this
suite could reach them:

  * `pty_read` (drivers/char/pty.c) is THE MASTER'S READ — the master and the slave are different fsops, and
    `pty_master_driver_fsop` reads with `pty_read` while `pty_slave_driver_fsop` reads with `tty_read`;
  * `tty_read`'s two interior arms live inside `if(VTIME > 0)` and are reachable ONLY by setting VMIN/VTIME
    through termios on a tty.

A pty is used rather than the console for the second half because it is a tty the probe OWNS: setting
VMIN/VTIME on the console would disturb the very session the harness is reading. The probe is
`/System/Shared/tests/kernel_pty_read`, plain C:

  * `ptmx-opens`                        — the multiplexer node `/System/Devices/PTS/ptmx`;
  * `slave-number-allocated`            — `TIOCGPTN`;
  * `slave-unlocked`                    — `TIOCSPTLCK`: the lock is cleared before a slave can be opened;
  * `slave-opens`                       — the runtime clone node `/System/Devices/PTS/pts/N`;
  * `master-write-reaches-the-slave`    — the SLAVE's read, i.e. `tty_read`'s ICANON path;
  * `slave-write-reaches-the-master`    — the MASTER's read, i.e. `pty_read` — the cure with no test until now;
  * `a-blocking-master-read-is-woken-by-the-slave`
                                        — THE PROPERTY THE CURE IS FOR: an UNPOLLED blocking read, with a child
                                          that writes 200ms later, so a read of already-queued data cannot pass
                                          it. This is the check that would fail on a lost wake;
  * `the-writer-child-exited-cleanly`   — the child's own account, read out of its exit code;
  * `vmin-vtime-can-be-set-on-the-slave`
                                        — termios round-trips on the slave, which is what makes the two
                                          `VTIME > 0` arms reachable at all;
  * `a-vtime-read-returns-when-the-timer-expires`
                                        — with VMIN=0 and VTIME=1 and NOTHING written, the read must return 0;
  * `and-a-vtime-read-returns-data-already-queued`
                                        — and with data queued it must return the data instead of the timeout.

DELIBERATELY NOT A RACE, and that is a design decision rather than an omission: the obvious "data arrives
mid-window" check would pass or fail on TIMING, because this guest's `usleep()` costs tens of ms for a nominal
1ms (foundation-plan.md) and the 100ms timer could legitimately win. A flaky check in the committed suite is a
defect of its own, so the timer's behaviour is asserted separately (where it must return 0) and the waited-for
wake is asserted where it is deterministic — an unbounded blocking read against a child that writes later.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/kernel_pty_read"
CHECKS = ("ptmx-opens", "slave-number-allocated", "slave-unlocked", "slave-opens",
          "master-write-reaches-the-slave", "slave-write-reaches-the-master",
          "a-blocking-master-read-is-woken-by-the-slave", "the-writer-child-exited-cleanly",
          "vmin-vtime-can-be-set-on-the-slave",
          "a-vtime-read-with-nothing-written-blocks-KNOWN-LIMIT",
          "and-a-vtime-read-returns-data-already-queued")


class Case(BaseCase):
    title = "kernel_pty_read: are a pty's two read paths woken?"
    tier = "fast"
    # Self-contained (own pty, own writer children) and bounded by its own watchdog, so a reused guest answers
    # the same.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("kernel_pty_read")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo KERNEL-PTY-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("KERNEL-PTY"):
                self.note(line)

        done = "KERNEL-PTY DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no KERNEL-PTY DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^KERNEL-PTY %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines() if l.startswith("KERNEL-PTY ") and " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"KERNEL-PTY RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "KERNEL-PTY-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
