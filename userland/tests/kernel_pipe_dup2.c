/* Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 *
 * A MINIMAL REPRODUCER for the wedge the NSTask reproducer hit after its kernel fault was fixed:
 * pipe() + fork() + dup2() + a six-byte write, with no Foundation, no threads and no signals.
 *
 * docs/design/foundation-plan.md §45. The child dups the pipe onto stdin and exits WITHOUT exec (so the
 * test separates dup2/pipe from exec), and the parent waits with a BOUNDED waitpid loop, reporting a child
 * that never exits instead of blocking on it - the probe must say what happened, not hang saying nothing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

/* THE ONE VARIABLE: "thread" mode starts a live thread before forking, and does nothing else differently.
 * The threaded NSTask reproducer wedges here; the plain POSIX one does not. */
static volatile int stop_flag = 0;
static volatile int reaped = 0;

static void *idle(void *arg)
{
	(void)arg;
	while (!stop_flag)
		usleep(500);
	return NULL;
}

/* THE LAST UNTESTED DIFFERENCE FROM THE REAL REPRODUCER: a thread that REAPS while the main thread forks.
 * 'r' in argv[2] selects it, and then the parent does not wait at all. */
static void *reaper(void *arg)
{
	(void)arg;
	while (!stop_flag) {
		int st;

		if (waitpid(-1, &st, WNOHANG) > 0) {
			reaped++;
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

	int n = (argc > 1) ? atoi(argv[1]) : 50;
	int done = 0, stuck = 0, t;

	pthread_t thr;
	int use_reap = (argc > 2 && strchr(argv[2], 'r') != NULL);
	int use_thread = (argc > 2 && strchr(argv[2], 't') != NULL);

	printf("PIPEDBG start n=%d thread=%d\n", n, use_thread);
	fflush(stdout);
	if (use_reap) {
		if (pthread_create(&thr, NULL, reaper, NULL) != 0) {
			printf("PIPEDBG no-thread\n");
			return 1;
		}
	} else if (use_thread && pthread_create(&thr, NULL, idle, NULL) != 0) {
		printf("PIPEDBG no-thread\n");
		return 1;
	}

	for (int i = 0; i < n; i++) {
		int fds[2], st = 0;
		pid_t pid;

		if (pipe(fds)) { printf("PIPEDBG pipefail i=%d\n", i); break; }
		pid = fork();
		if (pid == 0) {
			if (i < 3) { printf("PIPEDBG child i=%d forked\n", i); fflush(stdout); }
			dup2(fds[0], 0);
			if (i < 3) { printf("PIPEDBG child i=%d dup2\n", i); fflush(stdout); }
			close(fds[0]);
			close(fds[1]);
			/* 'e' in argv[2]: the child EXECs - the last difference from the NSTask reproducer,
			 * whose children exec /System/Tools/true with the pipe duped onto stdin. */
			if(strchr(argv[2] ? argv[2] : "", 'e')) {
				execl("/System/Tools/true", "true", (char *)NULL);
				_exit(127);
			}
			_exit(0);
		}
		if (pid < 0) { close(fds[0]); close(fds[1]); printf("PIPEDBG forkfail i=%d\n", i); break; }
		if (i < 3) { printf("PIPEDBG parent i=%d forked\n", i); fflush(stdout); }
		close(fds[0]);
		if (write(fds[1], "hello\n", 6) != 6) { printf("PIPEDBG i=%d shortwrite\n", i); }
		if (i < 3) { printf("PIPEDBG parent i=%d wrote\n", i); fflush(stdout); }
		close(fds[1]);

		if (use_reap) {
			/* the THREAD reaps; the parent does not wait at all - exactly the real reproducer's
			 * shape. done is counted at the end from what the reaper saw. */
			continue;
		}
		for (t = 0; t < 300; t++) {		/* 3s at 10ms - a bounded wait, never a hang */
			pid_t w = waitpid(pid, &st, WNOHANG);

			if (w == pid) { done++; break; }
			usleep(10000);
		}
		if (t == 300) {
			printf("PIPEDBG i=%d CHILD-STUCK\n", i);
			fflush(stdout);
			stuck++;
			break;
		}
		if ((i + 1) % 10 == 0) { printf("PIPEDBG progress i=%d\n", i + 1); fflush(stdout); }
	}

	if (use_reap) {
		for (t = 0; t < 500; t++) {		/* bounded: never a hang, always an answer */
			if (reaped >= n) break;
			usleep(10000);
		}
		done = reaped;
		if (reaped < n) {
			printf("PIPEDBG REAP-STUCK reaped=%d of %d\n", reaped, n);
			fflush(stdout);
			stuck++;
		}
	}
	stop_flag = 1;
	if (use_thread || use_reap) {
		pthread_join(thr, NULL);
	}

	printf("PIPEDBG DONE=%d stuck=%d\n", done, stuck);
	return 0;
}
