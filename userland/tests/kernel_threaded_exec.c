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
#include <sys/wait.h>

#define PROBE "/System/Shared/tests/foundation_task"

static volatile int stop_flag = 0;
static volatile int reaped = 0, signaled = 0, nonzero = 0, lastsig = 0, lastcode = 0;

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
			printf("CHILD pid=%d\n", (int)getpid());
			fflush(stdout);
			dup2(fds[0], 0);
			if (nullfd >= 0)
				dup2(nullfd, 1);	/* keep the serial console out of the loop */
			close(fds[0]);
			close(fds[1]);
			if (mode == 2)
				execl(PROBE, "foundation_task", "--child-cat", (char *)NULL);
			else
				execl("/System/Tools/true", "true", (char *)NULL);
			_exit(127);
		}
		if (pid < 0) { close(fds[0]); close(fds[1]); forkfail++; continue; }
		close(fds[0]);
		if (write(fds[1], "hello\n", 6) < 0) { /* ignore */ }
		close(fds[1]);

		if ((i + 1) % 100 == 0)
			printf("THREADED-EXEC-PROGRESS mode=%d i=%d signaled=%d\n", mode, i + 1, signaled);
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
