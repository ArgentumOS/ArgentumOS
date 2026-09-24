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

/* A writer-child: sleep, then write, then exit with a code the parent can read. The sleep is what makes the
 * parent's read a WAIT rather than a read of data already queued - and a read of data already queued passes even
 * when every wake in the kernel is broken, so it would prove nothing. */
static pid_t spawn_writer(int fd, const char *payload, unsigned int delay_us)
{
	pid_t pid = fork();

	if (pid == 0) {
		usleep(delay_us);
		if (write(fd, payload, strlen(payload)) != (ssize_t)strlen(payload)) {
			_exit(CHILD_WRITE_FAILED);
		}
		_exit(CHILD_OK);
	}
	return pid;
}

/* A bounded reap, so a child that never exits is REPORTED (kernel_pipe_dup2's rule: a probe whose whole subject
 * is a path that may be wedged must bound its own waits too). */
static int reap_bounded(pid_t pid, int *status)
{
	int i;

	for (i = 0; i < 500; i++) {		/* 5s at 10ms */
		pid_t w = waitpid(pid, status, WNOHANG);

		if (w == pid) {
			return 1;
		}
		if (w < 0) {
			return 0;
		}
		usleep(10000);
	}
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
		pid = spawn_writer(slave, "late-master\n", 200000);	/* 200ms: the read is already blocking */
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
			 * MEASURED: with VMIN=0, VTIME=1 and NOTHING written, this read DOES NOT RETURN - the probe's
			 * own 20s watchdog parks in phase 2 - where POSIX requires 0 once the timer expires.
			 *
			 * LAYER 1, THE CHANNEL, FIXED: tty_read sleeps on &tty->read_q while the VTIME callout
			 * (wait_vtime_off) woke &tty->cooked_q - and NOTHING IN THIS TREE SLEEPS ON &tty->cooked_q
			 * (grep it). wakeup() matches ON THE CHANNEL, so that wake could never find a waiter.
			 * Both VTIME sites now name &tty->read_q, the channel the reader is actually on.
			 *
			 * LAYER 2, THE TRUNCATION, STILL OPEN - AND IT IS WHY FIXING LAYER 1 CHANGED NOTHING
			 * (measured: the read still did not return). The callout API CARRIES A POINTER IN 32 BITS:
			 *     include/fnx/timer.h:  struct callout_req { void (*fn)(unsigned int); unsigned int arg; };
			 * and these very sites store a 64-bit address in it - `creq.arg = (addr_t)&tty->read_q;`. The
			 * value is truncated AT THE ASSIGNMENT, so wait_vtime_off() concludes by calling
			 * wakeup((void *)(uint32_t)address), which hashes the wrong bucket and - even after layer 1 -
			 * matches no sleeper. This is a 64-BIT PORTING DEFECT of the class this port has met before
			 * (an `unsigned int` where a pointer belongs), and it is why the timer has never been able to
			 * wake anything on this kernel, whichever channel it names.
			 *
			 * THE FIX IS THEREFORE THE `arg` TYPE (`addr_t`, plus the eight callbacks' parameter, which
			 * pass small integers and are unaffected in behaviour) - AND THIS CHECK IS ITS GATE.
			 *
			 * UNTIL THEN THE CHECK ASSERTS THE LIMIT (this project's §45-Y pattern: it passes WHILE the
			 * behaviour is the limit, and its failure text says to flip it), run in a CHILD under a bounded
			 * reap so that "it did not return" is an observation rather than a hung probe. */
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
				check("a-vtime-read-with-nothing-written-blocks-KNOWN-LIMIT", !reaped,
				      reaped ? "the VMIN=0/VTIME=1 read RETURNED - THE LIMIT IS GONE, so flip this check to "
					       "assert that it returns 0 on the timer (plan §58.1c)"
					     : NULL);
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
