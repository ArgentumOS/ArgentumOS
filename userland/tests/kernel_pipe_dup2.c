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
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
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
	int use_mainreap = (argc > 2 && strchr(argv[2], 'm') != NULL);
	int use_thread = (argc > 2 && strchr(argv[2], 't') != NULL);

	printf("PIPEDBG start n=%d thread=%d\n", n, use_thread);
	fflush(stdout);

	/* 'p': A PIPE READER'S EOF - THE SHAPE EVERY MODE HERE WALKS AROUND. Every child below dups the pipe
	 * onto stdin and exits WITHOUT READING, so nothing in this file has ever tested the thing NSTask's
	 * --child-cat child does: block in read(2) and expect EOF when the LAST WRITER closes.
	 *
	 * MEASURED (foundation-plan.md): the child entered read() and NEVER saw EOF after its parent closed the
	 * write end, so the parent's read-to-end-of-file waited forever. THAT leaves two candidates - the kernel
	 * never wakes a pipe reader on the last close, or the closer never closed - and this mode separates them
	 * with pure POSIX and no Foundation anywhere.
	 *
	 * BOUNDED, with markers, so it ANSWERS instead of hanging: the child reports `total=` and `last=` (0 is
	 * EOF, -1 an error) and the parent reports whether it reaped the child or found it STUCK. */
	if (argc > 2 && strchr(argv[2], 'p') != NULL) {
		int fds[2], st = 0, i;
		pid_t pid;

		if (pipe(fds) != 0) {
			printf("PIPEEOF pipefail\n");
			return 1;
		}
		pid = fork();
		if (pid == 0) {
			char buf[64];
			ssize_t got;
			long total = 0;

			close(fds[1]);		/* the child is a READER ONLY: it must hold NO write end */
			while ((got = read(fds[0], buf, sizeof(buf))) > 0) {
				total += (long)got;
			}
			/* got == 0 is EOF (the loop ended the way it should); got < 0 is an error. */
			printf("PIPEEOF child total=%ld last=%ld\n", total, (long)got);
			fflush(stdout);
			_exit(0);
		}
		if (pid < 0) {
			printf("PIPEEOF forkfail\n");
			return 1;
		}
		close(fds[0]);
		if (write(fds[1], "hello\n", 6) != 6) {
			printf("PIPEEOF shortwrite\n");
		}
		/* LET THE READER BLOCK FIRST, AND THIS IS THE WHOLE POINT. Closing before the child ever reaches
		 * read(2) leaves it a pipe that has data and no writers, so its FIRST read returns the data and
		 * the NEXT returns 0 - EOF WITHOUT EVER BLOCKING, a DIFFERENT code path from the one that
		 * matters. NSTask's --child-cat child has already read its payload and IS blocked when the last
		 * writer closes, and THAT close is the event which must wake it with EOF. Sleeping here is what
		 * makes this mode test that event instead of the easy one. */
		usleep(300000);
		close(fds[1]);
		for (i = 0; i < 1500; i++) {		/* 3s at 2ms - a bounded wait, never a hang */
			if (waitpid(pid, &st, WNOHANG) == pid) {
				break;
			}
			usleep(2000);
		}
		printf("PIPEEOF parent %s waited=%d\n", i < 1500 ? "reaped" : "STUCK", i);
		fflush(stdout);
		stop_flag = 1;
		return 0;
	}

	/* 's': SELECT/POLL ON A REGULAR FILE - THE KERNEL FIX'S FOUNDATION-FREE REPRODUCER (plan §45-Z).
	 *
	 * A REGULAR FILE IS ALWAYS READY, because an I/O on one cannot block. AN EMPTY PIPE IS NOT, until it has
	 * bytes - and the pipe is here as the CONTROL: a select(2) that answered "ready" for everything would
	 * pass the file half on its own, and that is exactly the mistake do_check() made, because it knew one
	 * rule (the pipe's fsop->select) and applied it to every descriptor. No Foundation is involved. */
	if (argc > 2 && strchr(argv[2], 's') != NULL) {
		int fd = open("/System/Shared/tests/kernel_pipe_dup2", O_RDONLY);
		int fds[2];
		int file_select = -1, file_poll = -1, pipe_select = -1, pipe_poll = -1;

		if (fd >= 0) {
			fd_set set;
			struct timeval tv;
			struct pollfd pfd;

			FD_ZERO(&set);
			FD_SET(fd, &set);
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			file_select = select(fd + 1, &set, NULL, NULL, &tv);

			pfd.fd = fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			file_poll = (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) ? 1 : 0;
			close(fd);
		}
		if (pipe(fds) == 0) {
			fd_set set;
			struct timeval tv;
			struct pollfd pfd;

			FD_ZERO(&set);
			FD_SET(fds[0], &set);
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			pipe_select = select(fds[0] + 1, &set, NULL, NULL, &tv);

			pfd.fd = fds[0];
			pfd.events = POLLIN;
			pfd.revents = 0;
			pipe_poll = (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) ? 1 : 0;
			close(fds[0]);
			close(fds[1]);
		}
		printf("SELECT-FILE regular-file-select=%d regular-file-poll=%d\n", file_select, file_poll);
		printf("SELECT-FILE empty-pipe-select=%d empty-pipe-poll=%d\n", pipe_select, pipe_poll);
		/* NO TOKEN HERE, and no need of one: this marker cannot be matched off the guest's echo of the
		 * command line, which carries the STATUS spelling rather than this one. */
		printf("SELECT-FILE DONE regular-file-select=%d regular-file-poll=%d "
		       "empty-pipe-select=%d empty-pipe-poll=%d\n",
		       file_select, file_poll, pipe_select, pipe_poll);
		fflush(stdout);
		return 0;
	}

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

		if (use_reap || use_mainreap) {
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

	if (use_reap || use_mainreap) {
		/* THE A/B: identical program, identical children, identical timing - the ONLY difference is
		 * which thread calls waitpid. 'r' = a reaper thread; 'm' = the MAIN thread, here. */
		for (t = 0; t < 500; t++) {		/* bounded: never a hang, always an answer */
			if (use_mainreap) {
				int st2;

				while (waitpid(-1, &st2, WNOHANG) > 0) {
					reaped++;
				}
			}
			/* In 'r' mode the main thread READS the count and touches nothing else. If the reaper
			 * thread's waitpid reaps, reaped climbs; if it does not, it stays 0. No third party. */
			if (reaped >= n) break;
			usleep(10000);
		}
		done = reaped;
		if (reaped < n) {
			printf("PIPEDBG %s reaped=%d of %d\n",
				use_mainreap ? "MAINREAP-STUCK" : "REAP-STUCK", reaped, n);
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
