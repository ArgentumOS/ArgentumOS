/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * kernel_pty_read — ARE A PTY'S TWO READ PATHS ACTUALLY WOKEN? A Foundation-free reproducer.
 * docs/design/foundation-plan.md §58.1b, where these two were the gating items.
 *
 * WHY IT EXISTS. §58.1's cure (arm the channel, then look, then commit — kernel/sleep.c) was applied to eleven
 * waiters. Two of them went in UNVERIFIED because nothing in the suite could reach them, and an unverified cure
 * is exactly what this project has learned not to accept:
 *
 *   * `pty_read` (drivers/char/pty.c) is THE MASTER'S READ — it drains the master's cooked_q, which the SLAVE's
 *     write feeds. Nothing in the suite read a master, so its cure had no behaviour test at all.
 *   * `tty_read`'s two interior arms (drivers/char/tty.c, inside `if(VTIME > 0)`) are reachable ONLY by setting
 *     VMIN/VTIME through termios on a tty, and NOTHING in the suite ever sets them — so those two sites are not
 *     merely unverified, they are the reason a cure was deferred there rather than copied in.
 *
 * SO THIS PROBE IS A BEHAVIOUR TEST FIRST AND A GATE SECOND, and it is written that way on purpose: it must
 * establish what the paths DO before anything is changed, so that a change has something to be measured against.
 * A pty is used for both halves because it is a tty this probe owns — setting VMIN/VTIME on it cannot disturb the
 * console the harness is reading, which is why the console is the wrong place for the VTIME half.
 *
 * THE SHAPE, and each choice is a measured fact about this kernel rather than a preference:
 *
 *   * THE MASTER AND SLAVE ARE DIFFERENT FSOPs, and that is the whole point of testing both:
 *     `pty_master_driver_fsop` reads with `pty_read`; `pty_slave_driver_fsop` reads with `tty_read`. The
 *     multiplexer is `/System/Devices/PTS/ptmx` and the slave is `/System/Devices/PTS/pts/N` (devfs clone nodes;
 *     the dance is the one userland/tools/pty_test.c already used for devfs M3).
 *   * THE BLOCKING READS ARE NOT POLLED FIRST. A `poll()` before the read would defeat the two checks that
 *     matter most — a read that waits and is then WOKEN is the property under test, and data already waiting is
 *     a different path. A SIGALRM watchdog bounds the probe instead, so a lost wake reports as a failure with a
 *     named iteration rather than hanging the case.
 *   * THE CHILD REPORTS THROUGH ITS EXIT CODE, not the console: two processes interleaving on one serial line is
 *     how a diagnostic becomes unreadable (kernel_loopback_tcp's rule, kept).
 *
 * Output: KERNEL-PTY <check> ok|FAIL <detail>, then RESULT/STATUS/DONE.
 */

#define _GNU_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef TIOCGPTN
#define TIOCGPTN	0x80045430	/* Get Pty Number (of pty-mux device) */
#endif
#ifndef TIOCSPTLCK
#define TIOCSPTLCK	0x40045431	/* Lock/unlock Pty */
#endif

#define PTMX_PATH	"/System/Devices/PTS/ptmx"
#define SLAVE_FMT	"/System/Devices/PTS/pts/%d"

/* THE CHILD'S EXIT CODES: this is how a writer-child reports, so the console stays single-writer. */
#define CHILD_OK		0
#define CHILD_WRITE_FAILED	1

/* THE WATCHDOG, and it is load-bearing here: every interesting read in this probe is a BLOCKING read on purpose,
 * so the only thing standing between "the wake arrived" and "the case hung" is this alarm. It prints the tally
 * that exists at the moment it fires, so a wedged run reports rather than dying silently. */
#define WATCHDOG_S	20

static int okc, failc;
static volatile sig_atomic_t timed_out = 0;
static volatile sig_atomic_t phase = -1;

static void on_alarm(int sig)
{
	(void)sig;
	timed_out = 1;
	printf("KERNEL-PTY blocked-in-blocking-io FAIL a blocking read was not woken within %ds\n", WATCHDOG_S);
	if (phase >= 0) {
		printf("KERNEL-PTY-DIAG the probe was PARKED in phase %d\n", (int)phase);
	}
	printf("KERNEL-PTY RESULT ok=%d fail=%d\n", okc, failc);
	printf("KERNEL-PTY-STATUS=1\n");
	printf("KERNEL-PTY DONE\n");
	_exit(1);
}

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("KERNEL-PTY %s ok\n", name);
	} else {
		failc++;
		printf("KERNEL-PTY %s FAIL %s\n", name, detail != NULL ? detail : "");
	}
	fflush(stdout);
}

/* AN EXACT NAP, WHICH IS THE WHOLE LESSON OF THIS PROBE'S FIRST VERSION (foundation-plan.md §58.1c). The bound
 * below used `usleep(10000)` and called itself "5s at 10ms" - but usleep(3) on this guest is NOT a 10ms nap, and
 * the 500 of them expired so much sooner than 5s that the reap gave up BEFORE the forked child had even started.
 * The parent then printed reaped=0 and sent SIGKILL; the child went on to block in its read, be woken by its
 * VTIME callout and return 0 with nobody listening. That reads EXACTLY like a kernel that cannot wake a VTIME
 * sleeper, and it cost a real fix (the callout's pointer width) plus two flips of the check to discover that the
 * probe was the thing at fault.
 *
 * AND THE FIRST "FIX" FOR IT WAS NOT A NAP EITHER, WHICH IS THE SECOND HALF OF THE SAME LESSON. This function
 * originally polled ONE DESCRIPTOR THAT WAS `-1` - a pure timeout, per POSIX and per this kernel's own
 * `sys_poll` ("negative fds are ignored") - and the read it bounds STILL appeared to wedge (4 of 4 runs). If an
 * fd-less poll returns immediately here, then this naps nothing, the parent SPINS through its 500 iterations
 * instead of yielding, and on a single-CPU guest it STARVES the very child it is waiting for: the child never
 * runs, `reaped` is 0, and the probe reports a wedged kernel that is not wedged. So the nap is taken on a REAL
 * descriptor that is never ready - a pipe with its write end held open and nothing ever written - which is a
 * wait under every implementation rather than under a reading of one. `never` is deliberately leaked: one pipe
 * per process, and keeping the write end open is what stops the read end reporting EOF. */
static void nap_ms(int ms)
{
	static int never = -1;
	struct pollfd pfd;

	if (never < 0) {
		int pp[2];

		if (pipe(pp) == 0) {
			never = pp[0];	/* pp[1] stays open on purpose: with no writer this end is EOF */
		}
	}
	pfd.fd = never;			/* a real descriptor, never ready; -1 only if pipe() itself failed */
	pfd.events = POLLIN;
	pfd.revents = 0;
	poll(&pfd, 1, ms);
}

/* A writer-child: nap, then write, then exit with a code the parent can read. The nap is what makes the parent's
 * read a WAIT rather than a read of data already queued - and a read of data already queued passes even when every
 * wake in the kernel is broken, so it would prove nothing. The nap is nap_ms() for the reason above: a child that
 * writes too early silently turns a wake test into a no-op. */
static pid_t spawn_writer(int fd, const char *payload, int delay_ms)
{
	pid_t pid = fork();

	if (pid == 0) {
		nap_ms(delay_ms);
		if (write(fd, payload, strlen(payload)) != (ssize_t)strlen(payload)) {
			_exit(CHILD_WRITE_FAILED);
		}
		_exit(CHILD_OK);
	}
	return pid;
}

/* A bounded reap, so a child that never exits is REPORTED (kernel_pipe_dup2's rule: a probe whose whole subject
 * is a path that may be wedged must bound its own waits too) - and the bound is now the bound it claims. */
static int reap_bounded(pid_t pid, int *status)
{
	int i;

	for (i = 0; i < 500; i++) {		/* 5s at 10ms, EXACTLY: nap_ms, not usleep */
		pid_t w = waitpid(pid, status, WNOHANG);

		if (w == pid) {
			return 1;
		}
		if (w < 0) {
			/* THIS BRANCH WAS THE BUG THAT COST THE WHOLE INVESTIGATION (§58.1c): waitpid(pid, &status,
			 * WNOHANG) returned -1/ECHILD for a child that was ALIVE, so this "escape hatch" fired on
			 * iteration 0 and every report of "the read did not return" was really "the reap gave up".
			 * The kernel's sys_wait4 was fixed (a live matching child is a WNOHANG answer of 0, not
			 * ECHILD) and the read now returns 0 on its timer, reaped on the first or second iteration.
			 * Kept as a plain bail-out, because with WNOHANG a non-zero return here now really does mean
			 * the child cannot be waited for. */
			return 0;
		}
		nap_ms(10);
	}
	printf("KERNEL-PTY-DIAG reap exhausted its 500 iterations without reaping\n");
	fflush(stdout);
	kill(pid, SIGKILL);
	waitpid(pid, status, 0);
	return 0;
}

int main(void)
{
	struct sigaction sa;
	int master, slave, ptn = -1, zero = 0;
	char path[64];
	char buf[64];
	ssize_t n;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	sigaction(SIGALRM, &sa, NULL);
	alarm(WATCHDOG_S);

	printf("KERNEL-PTY start\n");
	fflush(stdout);

	/* ---- THE FIXTURE: the multiplexer, the number, the unlock, the slave node ------------------------- */
	master = open(PTMX_PATH, O_RDWR);
	check("ptmx-opens", master >= 0, master >= 0 ? NULL : strerror(errno));
	if (master < 0) {
		goto out;
	}
	check("slave-number-allocated", ioctl(master, TIOCGPTN, &ptn) == 0 && ptn >= 0,
	      "TIOCGPTN did not report a pty number");

	/* THE UNLOCK IS NOT OPTIONAL: an unlocked pty is what a multiplexer hands out, and the slave cannot be
	 * opened while the lock stands. */
	check("slave-unlocked", ioctl(master, TIOCSPTLCK, &zero) == 0,
	      "TIOCSPTLCK could not clear the lock");

	snprintf(path, sizeof(path), SLAVE_FMT, ptn);
	slave = open(path, O_RDWR);
	check("slave-opens", slave >= 0, slave >= 0 ? NULL : path);
	if (slave < 0) {
		goto out;
	}

	/* ---- THE SLAVE'S READ: tty_read, the ICANON path (the arm cured and verified by the console) ------- */
	{
		struct pollfd pfd;
		int ready;

		if (write(master, "hello\n", 6) != 6) {
			check("master-write-reaches-the-slave", 0, "the write to the master failed");
		} else {
			pfd.fd = slave;
			pfd.events = POLLIN;
			pfd.revents = 0;
			/* HERE poll() IS RIGHT AND THE BOUND IS THE POINT: this check is about the bytes, not about
			 * being woken (the console already carries that one), so waiting up to a deadline and then
			 * reporting is the honest bound. */
			ready = poll(&pfd, 1, 5000) > 0;
			n = ready ? read(slave, buf, sizeof(buf)) : -1;
			check("master-write-reaches-the-slave", ready && n == 6 && memcmp(buf, "hello\n", 6) == 0,
			      ready ? "the slave read did not return the six bytes" : "the slave never became readable");
		}
	}

	/* ---- THE MASTER'S READ: pty_read, the cure that had NO behaviour test until now ------------------- */
	{
		struct pollfd pfd;
		int ready;

		if (write(slave, "answer\n", 7) != 7) {
			check("slave-write-reaches-the-master", 0, "the write to the slave failed");
		} else {
			pfd.fd = master;
			pfd.events = POLLIN;
			pfd.revents = 0;
			ready = poll(&pfd, 1, 5000) > 0;
			n = ready ? read(master, buf, sizeof(buf)) : -1;
			check("slave-write-reaches-the-master", ready && n == 7 && memcmp(buf, "answer\n", 7) == 0,
			      ready ? "the master read did not return the seven bytes" : "the master never became readable");
		}
	}

	/* ---- AND NOW THE PROPERTY THE CURE IS FOR: A BLOCKING MASTER READ IS WOKEN BY THE SLAVE ----------- */
	{
		pid_t pid;
		int status = 0, reaped;

		phase = 1;
		pid = spawn_writer(slave, "late-master\n", 200);	/* 200ms, EXACT (nap_ms): the read is
									 * already blocking when this lands, which is what makes
									 * the check a WAKE test rather than a queued-data test */
		n = (pid > 0) ? read(master, buf, sizeof(buf)) : -1;	/* NOT POLLED FIRST - that is the test */
		phase = -1;
		reaped = reap_bounded(pid, &status);
		check("a-blocking-master-read-is-woken-by-the-slave",
		      n == 12 && memcmp(buf, "late-master\n", 12) == 0,
		      n < 0 ? "the blocking master read was never woken (the watchdog would have fired first)"
			    : "the blocking master read returned the wrong bytes");
		check("the-writer-child-exited-cleanly",
		      reaped && WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK,
		      reaped ? "the writer child failed or was killed" : "the writer child never exited");
	}

	/* ---- AND THE VMIN/VTIME ARMS: reachable ONLY by setting termios, which is why they were deferred ---- */
	{
		struct termios t;
		int got = tcgetattr(slave, &t);

		if (!got) {
			t.c_lflag &= ~ICANON;		/* the VTIME arms live in the non-canonical path */
			t.c_cc[VMIN] = 0;
			t.c_cc[VTIME] = 1;		/* a tenth of a second */
			got = tcsetattr(slave, TCSANOW, &t) == 0;
		}
		check("vmin-vtime-can-be-set-on-the-slave", got == 1,
		      "termios on a pty slave could not be read or written");

		if (got == 1) {
			/* ---- THE FINDING, IN TWO LAYERS (plan §58.1c): THE TIMER'S WAKE WAS DOUBLY DEAD ------
			 *
			 * MEASURED, AND THE FIRST READING OF IT WAS WRONG IN A WAY WORTH KEEPING. This read does not
			 * return while the reap below is bounded by usleep(3) - but THAT IS THE PROBE, NOT THE KERNEL.
			 * The watchdog fired with "PARKED in phase 2" and everything after it inherited the premise; the
			 * printk pair added later (temporary, since removed) shows what really happens, and the GUEST
			 * LOG's own order is the whole proof:
			 *     VTIME-DIAG SLEEPING chan=...311008 ticks=751 timeout=10
			 *     VTIME-DIAG callout FIRED arg=...311008   <- the timer fires and the address MATCHES
			 *     VTIME-DIAG WOKE ticks=761 cooked=0       <- the sleeper is woken at exactly +10
			 *     KERNEL-PTY-DIAG child read returned 0    <- the read RETURNS 0, as POSIX says
			 * The child's own lines landed AFTER the parent had already printed reaped=0: 500 iterations of
			 * `usleep(10000)` are not the 5 seconds they claim on this guest, so the "bounded" reap gave up -
			 * and sent SIGKILL - before its child had even started. nap_ms() is the fix and the rule (see its
			 * comment), and it is why this file no longer calls usleep(3) at all.
			 *
			 * TWO REAL DEFECTS WERE FOUND ON THE WAY AND BOTH ARE FIXED - worth recording even though neither
			 * was the final cause, because both would have bitten any other waiter on these channels:
			 *   LAYER 1, THE CHANNEL: tty_read sleeps on &tty->read_q while the VTIME callout woke
			 *     &tty->cooked_q, a channel NOTHING IN THIS TREE SLEEPS ON (grep it). wakeup() matches ON THE
			 *     CHANNEL, so that wake could never find a waiter; both sites now name &tty->read_q.
			 *   LAYER 2, THE POINTER'S WIDTH: the callout API carried `unsigned int arg` while these sites
			 *     store a 64-bit address in it (`creq.arg = (addr_t)&tty->read_q;`), so the value was
			 *     truncated AT THE ASSIGNMENT and `wait_vtime_off()` woke a FABRICATED address. `arg` is
			 *     `addr_t` everywhere now - both structs, do_callouts_bh, struct console's cursor_blink field,
			 *     all eight callbacks and the five header declarations - a 64-bit porting defect of the class
			 *     this port has met before, and the compiler found two carriers that reading had missed.
			 *
			 * AND THEN IT BECAME INTERMITTENT, WHICH IS WHY THIS IS AN OBSERVATION AND NOT A CHECK. Two runs
			 * of the SAME kernel disagreed: one fired the callout (SLEEPING at tick 626, FIRED, WOKE at 636,
			 * read returned 0), the next printed `VTIME-DIAG SLEEPING` - a line emitted AFTER `add_callout()`
			 * has returned - and then NOTHING: `wait_vtime_off` was never called at all. So the callout was
			 * taken into the list and never came out of it alive, and THAT is a defect in the shared callout
			 * machinery (`add_callout`'s delta list, or `do_callouts_bh`'s wake path), not in these two lines.
			 * It would also strand the console cursor blink, the floppy motor timer and the ATA timeouts, which
			 * use the same list.
			 *
			 * SO NOTHING IS ASSERTED HERE, DELIBERATELY. An intermittent kernel defect cannot be asserted in
			 * either direction: asserting the block would fail whenever the timer DID fire, and asserting the
			 * return would fail whenever it did not - two flaky checks instead of one honest observation, and a
			 * flaky check in the committed suite is a defect of its own. The outcome is printed as a DIAG line
			 * for the record, the defect is written up in foundation-plan.md §58.1c with both measurements, and
			 * the deterministic half of this arm (data already queued) is asserted just below. */
			phase = 2;
			{
				pid_t pid = fork();
				int status = 0, reaped;

				if (pid == 0) {
					ssize_t r = read(slave, buf, sizeof(buf));

					_exit(r == 0 ? 0 : 1);	/* 0: returned on the timer, as POSIX says. 1: returned otherwise. */
				}
				reaped = reap_bounded(pid, &status);
				phase = -1;
				/* THE LIMIT IS GONE, SO THIS IS AN ASSERTION RATHER THAN AN OBSERVATION (§58.1c). The
				 * five-pass story is in the plan and in the comments above; the short version is what
				 * makes it assertable: the kernel's side of the handshake was measured CORRECT all along
				 * (arm at tick 671, callout fire at 681, commit returned), while `waitpid(pid, &st,
				 * WNOHANG)` returned -1/ECHILD for that LIVE child - so the probe's reap gave up on
				 * iteration 0 and reported a wedged read. With sys_wait4 fixed (a live matching child is a
				 * WNOHANG answer of 0, not ECHILD) this arm returns 0 on its timer in 3 of 3 runs, reaped
				 * on the first or second iteration. Nothing here is timing-dependent any more. */
				check("a-vtime-read-returns-on-its-timer",
				      reaped && WIFEXITED(status) && WEXITSTATUS(status) == 0,
				      reaped ? "the VMIN=0/VTIME=1 read returned something other than 0 - POSIX says the "
					       "timer expires and the read returns 0 (plan §58.1c)"
					     : "the VMIN=0/VTIME=1 read did not return within a reap bounded by nap_ms() - "
					       "either the VTIME timer is not reaching its sleeper, or waitpid(WNOHANG) is "
					       "refusing a live child again (plan §58.1c)");
			}

			/* AND THE SAME ARM WITH DATA ALREADY QUEUED: DELIBERATELY NOT A RACE. The obvious version of
			 * this check is a child writing 50ms into the 100ms window - and that check would pass or fail
			 * on TIMING rather than on the kernel: this guest's usleep() costs tens of ms for a nominal
			 * 1ms (foundation-plan.md), so the timer can win and the read then legitimately returns 0. A
			 * flaky check in the committed suite is a defect of its own, so what is asserted here is the
			 * deterministic half: with data queued, the arm returns the DATA instead of running out the
			 * clock (its guard is `!cooked_q.count`, so this path never reaches the timer at all). */
			{
				phase = 3;
				if (write(master, "vtime-data\n", 11) != 11) {
					check("and-a-vtime-read-returns-data-already-queued", 0,
					      "the write to the master failed");
				} else {
					n = read(slave, buf, sizeof(buf));
					check("and-a-vtime-read-returns-data-already-queued",
					      n == 11 && memcmp(buf, "vtime-data\n", 11) == 0,
					      n < 0 ? "the VTIME read returned an error with data queued"
						    : n == 0 ? "the read let the timer expire with data already queued"
							     : "the read returned the wrong bytes");
				}
				phase = -1;
			}
		}
	}

out:
	alarm(0);
	printf("KERNEL-PTY RESULT ok=%d fail=%d\n", okc, failc);
	printf("KERNEL-PTY-STATUS=%d\n", failc ? 1 : 0);
	printf("KERNEL-PTY DONE\n");
	fflush(stdout);
	return failc ? 1 : 0;
}
