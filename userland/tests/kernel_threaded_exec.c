/* Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 *
 * THE KERNEL BUG'S MINIMAL REPRODUCER — and the reason docs/design/foundation-plan.md §45 exists.
 *
 * A `0x7f00_000008084x` instruction fetch with EVERY GENERAL REGISTER ZERO, in a process that has been
 * launching children. Found through Foundation's NSTask (W6d) and reproduced here WITH NO FOUNDATION
 * ANYWHERE: mode 1's children are /System/Tools/true, so the library is not merely innocent, it is absent.
 *
 * THE SHAPE, and the three things that separate it from a program that does NOT reproduce:
 *   * a worker thread REAPS with waitpid(2) while the main thread launches (the first clean experiment
 *     had a thread that only slept, and 300 iterations of it were clean);
 *   * every child gets a PIPE on stdin;
 *   * the loop is long enough for a rare event to land.
 * Both faults observed so far share rip == cr2, the 0x7f00 high half, and all-zero registers — the
 * signature the kernel prints as "Page Fault at ... " with an all-zero register dump.
 *   mode 1: child = /System/Tools/true                     (fast, baseline)
 *   mode 2: child = the FOUNDATION PROBE --child-cat       (the exact failing shape)
 * A worker thread REAPS (waitpid) while the main thread launches; every child gets a PIPE on stdin.
 * argv[3] is a token the program prints REVERSED, so a case can wait for the END without matching the
 * echoed command line (the guest shell echoes the command, marker and all - the known trap). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>

#define PROBE "/System/Shared/tests/foundation_task"

static volatile int stop_flag = 0;
static volatile int reaped = 0, signaled = 0, nonzero = 0, lastsig = 0, lastcode = 0;

/* ---- MODE 3: THE SHAPE THAT WAS MISSING ------------------------------------------------
 *
 * A WORKER THREAD BLOCKS in waitpid(SPECIFIC pid, ..., 0) for a child the MAIN thread forked. Every other
 * mode here POLLS with waitpid(-1, ..., WNOHANG), and a poll survives a MISSING WAKEUP by asking again -
 * which is exactly why the bug hid: this file was the reproducer that proved "a thread's waitpid reaps",
 * and it never once BLOCKED on a specific pid.
 *
 * THE BUG (fixed 603a22e2): do_exit notified `current->ppid` ALONE - wakeup_proc(parent), plus a SIGCHLD
 * whose sigpending was set on that one proc - so a waiter that is a DIFFERENT task of the same process
 * (POSIX: any thread may reap the process's children, and sys_wait4 already matches by TGID) slept
 * FOREVER. NSTask's reaper thread is that waiter, and it hung the whole process.
 *
 * THE CHILD MUST STILL BE RUNNING when the thread starts waiting: had it already exited, the blocking
 * waitpid would find the zombie on its FIRST scan and return without needing a wakeup at all, and a
 * missing wakeup would not show. Hence the child's usleep.
 *
 * A BOUNDED WAIT, because a regression must REPORT rather than hang: the case waits for BLOCKING-WAIT-HUNG
 * as well as for the DONE line, so a regression fails in seconds with a marker. */
static pid_t bw_target = -1;
static volatile int bw_done = 0;
static int bw_got = -1, bw_status = -1;

static void *bw_waiter(void *arg)
{
	int st = 0;
	pid_t p;

	(void)arg;
	printf("TRACE bw: thread BLOCKING in waitpid(%d, status, 0)\n", (int)bw_target);
	fflush(stdout);
	p = waitpid(bw_target, &st, 0);	/* BLOCKING + SPECIFIC PID: the whole point of this mode */
	bw_got = (int)p;
	bw_status = st;
	bw_done = 1;
	printf("TRACE bw: waitpid returned %d status=%d\n", (int)p, st);
	fflush(stdout);
	return NULL;
}

static void *reaper(void *arg)
{
	(void)arg;
	while (!stop_flag) {
		int st;
		pid_t p = waitpid(-1, &st, WNOHANG);

		if (p > 0) {
			reaped++;
			if (WIFSIGNALED(st)) { signaled++; lastsig = WTERMSIG(st); }
			else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
				nonzero++;
				lastcode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
			}
		} else {
			usleep(200);
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	/* Writing into a pipe whose reader has exited raises SIGPIPE, and the DEFAULT action is death.
	 * The exit status is 128+13 and the program simply stops - which looks exactly like a hang. */
	signal(SIGPIPE, SIG_IGN);

	pthread_t t;
	int mode = (argc > 1) ? atoi(argv[1]) : 1;
	int n = (argc > 2) ? atoi(argv[2]) : 300;
	const char *token = (argc > 3) ? argv[3] : "notoken";
	char rev[64];
	int forkfail = 0, pipefail = 0, nullfd;

	for (unsigned i = 0; i < strlen(token) && i < sizeof(rev) - 1; i++)
		rev[i] = token[strlen(token) - 1 - i];
	rev[strlen(token) < sizeof(rev) - 1 ? strlen(token) : sizeof(rev) - 1] = 0;

	/* THE WHO-DIED INSTRUMENT: a fork child carries the SAME executable name as its parent, so the
	 * kernel's "Process '/System/Shared/tests/kernel_threaded_exec'" line names both. The parent prints
	 * its pid first and every child prints once before exec()ing, so the log says which of the two
	 * faulted - and whether the faulting process ever ran a single instruction of its own. */
	printf("PARENT pid=%d mode=%d n=%d\n", (int)getpid(), mode, n);
	fflush(stdout);

	nullfd = open("/System/Devices/null", O_WRONLY);

	/* MODE 3 runs BEFORE the polling reaper exists, so the fork below is from a single-threaded process
	 * and the thread is created AFTER it - the same order -launchAndReturnError: uses. */
	if (mode == 3) {
		pthread_t bw;
		pid_t child;
		int i;

		child = fork();
		if (child == 0) {
			usleep(300000);		/* still RUNNING when the thread blocks - see the note above */
			_exit(4);
		}
		if (child < 0) {
			printf("BLOCKING-WAIT-FORKFAILED\n");
			return 1;
		}
		bw_target = child;
		printf("TRACE bw: main forked=%d, starting the waiter thread\n", (int)child);
		fflush(stdout);
		if (pthread_create(&bw, NULL, bw_waiter, NULL) != 0) {
			printf("BLOCKING-WAIT-NOTHREAD\n");
			return 1;
		}
		for (i = 0; i < 3000 && !bw_done; i++) {
			usleep(2000);		/* a 6s ceiling: a hang must report, not stall the case */
		}
		if (!bw_done) {
			printf("BLOCKING-WAIT-HUNG child=%d\n", (int)child);
			fflush(stdout);
			return 1;
		}
		if (bw_got == (int)child && WIFEXITED(bw_status) && WEXITSTATUS(bw_status) == 4) {
			printf("BLOCKING-WAIT-DONE child=%d reaped=%d code=4 token=%s\n",
			       (int)child, bw_got, rev);
			fflush(stdout);
			return 0;
		}
		printf("BLOCKING-WAIT-BAD child=%d reaped=%d status=%d\n",
		       (int)child, bw_got, bw_status);
		fflush(stdout);
		return 1;
	}

	if (pthread_create(&t, NULL, reaper, NULL) != 0) {
		printf("THREADED-EXEC-ERROR=no-thread\n");
		return 1;
	}

	for (int i = 0; i < n; i++) {
		int fds[2];
		pid_t pid;

		if (pipe(fds)) { pipefail++; continue; }
		pid = fork();
		if (pid == 0) {
			if ((i % 10) == 0) {		/* console traffic matters: every child printing is heavy */
				printf("CHILD pid=%d i=%d\n", (int)getpid(), i);
				fflush(stdout);
			}
			if (i < 3) { printf("TRACE child i=%d dup2\n", i); fflush(stdout); }
			dup2(fds[0], 0);
			if (nullfd >= 0)
				dup2(nullfd, 1);	/* keep the serial console out of the loop */
			if (i < 3) { printf("TRACE child i=%d exec\n", i); fflush(stdout); }
			close(fds[0]);
			close(fds[1]);
			if (mode == 2)
				execl(PROBE, "foundation_task", "--child-cat", (char *)NULL);
			else
				execl("/System/Tools/true", "true", (char *)NULL);
			_exit(127);
		}
		if (pid < 0) { close(fds[0]); close(fds[1]); forkfail++; continue; }
		if (i < 3) { printf("TRACE i=%d forked=%d\n", i, (int)pid); fflush(stdout); }
		close(fds[0]);
		if (write(fds[1], "hello\n", 6) < 0) { /* ignore */ }
		if (i < 3) { printf("TRACE i=%d wrote\n", i); fflush(stdout); }
		close(fds[1]);
		if (i < 3) { printf("TRACE i=%d closed\n", i); fflush(stdout); }

		if ((i + 1) % 10 == 0) {
			printf("THREADED-EXEC-PROGRESS mode=%d i=%d reaped=%d signaled=%d\n",
				mode, i + 1, reaped, signaled);
			fflush(stdout);
		}
	}

	/* wait for the reaper to finish the outstanding children, with a generous ceiling */
	for (int i = 0; i < 120000 && reaped < n - forkfail - pipefail; i++)
		usleep(1000);

	stop_flag = 1;
	pthread_join(t, NULL);
	printf("THREADED-EXEC-DONE mode=%d n=%d forkfail=%d pipefail=%d reaped=%d signaled=%d lastsig=%d nonzero=%d lastcode=%d token=%s\n",
	       mode, n, forkfail, pipefail, reaped, signaled, lastsig, nonzero, lastcode, rev);
	return 0;
}
