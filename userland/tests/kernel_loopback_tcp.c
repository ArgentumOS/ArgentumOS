/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * kernel_loopback_tcp — CAN THIS KERNEL'S LOOPBACK CARRY A PAYLOAD? A Foundation-free reproducer.
 * docs/design/libressl-plan.md §5/L1, where the question was raised.
 *
 * WHY IT EXISTS. L1's TLS handshake STALLED: `openssl s_client` printed `CONNECTED(fd)` and
 * `openssl s_server` logged `ACCEPT` — so the TCP connect and accept on loopback work — but with
 * `-state` on, the client's handshake trace was EMPTY, i.e. NO BYTES MOVED. That is a statement about
 * the network layer and not about TLS, and it needs to be settled by a program with no SSL, no
 * Foundation and no third-party library anywhere near it: two plain processes, one payload, one reply.
 *
 * THE SHAPE, and each choice is a measured fact about this kernel rather than a preference:
 *
 *   * A FIXED PORT, NOT AN EPHEMERAL ONE. `bind(2)` to port 0 SUCCEEDS here and leaves the port
 *     unresolved, and the `listen(2)` that follows then fails — measured while landing `NSSocketPort`
 *     (docs/design/foundation-plan.md W6b). So the port is a number, not a hope.
 *   * BLOCKING I/O UNDER A WATCHDOG, WHICH IS WHAT TLS ACTUALLY USES. The first version used
 *     non-blocking sockets and a bounded `poll(2)`, and its result — "the write never becomes
 *     possible" — has an innocent explanation that reading could not exclude: `poll(2)` may simply not
 *     report a connected loopback socket here. So the exchange is performed the way `s_client`
 *     performs it, with BLOCKING calls, and a `SIGALRM` watchdog bounds it — a wedged path reports
 *     instead of hanging the case.
 *   * THE PEER IS A CHILD PROCESS and reports through its EXIT CODE, not through the console: two
 *     processes interleaving on one serial line is how a diagnostic becomes unreadable.
 *
 * Output: KERNEL-LOOPBACK <check> ok|FAIL <detail>, then RESULT/STATUS/DONE.
 */

#define _GNU_SOURCE 1

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT		46464
#define PAYLOAD		"loopback-payload-0123456789"
#define REPLY		"loopback-reply-9876543210"
#define TIMEOUT_MS	5000

/* THE CHILD'S EXIT CODES, one per way the peer can fail — this is how the peer reports. */
#define PEER_OK			0
#define PEER_SOCKET_FAILED	1
#define PEER_ACCEPT_FAILED	2
#define PEER_READ_FAILED	3
#define PEER_WRITE_FAILED	4
#define PEER_BAD_PAYLOAD	5

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("KERNEL-LOOPBACK %s ok\n", name);
	} else {
		failc++;
		printf("KERNEL-LOOPBACK %s FAIL %s\n", name, detail != NULL ? detail : "");
	}
}

/*
 * THE WATCHDOG, AND WHY IT REPLACED poll(2). The first version of this probe used non-blocking sockets
 * and a bounded poll, and it measured "the write never becomes possible". That is ONE reading of the
 * evidence, and it has an innocent explanation the measurement could not exclude: poll(POLLOUT) may
 * simply not report a connected loopback socket here, while BLOCKING I/O would work fine. TLS uses
 * BLOCKING I/O — which is what stalled in L1 — so blocking is what must be tested. The hazard of
 * blocking is that a wedged path hangs the probe, and that is what the watchdog is for: SIGALRM at
 * WATCHDOG_S prints the verdict and exits, so the probe still reports its own result.
 */
#define WATCHDOG_S 10

static volatile sig_atomic_t timed_out = 0;
static pid_t peer_pid = -1;

static void on_alarm(int sig)
{
	(void)sig;
	timed_out = 1;
	if (peer_pid > 0) {
		kill(peer_pid, SIGKILL);
	}
	printf("KERNEL-LOOPBACK blocked-in-blocking-io FAIL the exchange did not complete within %ds\n",
	       WATCHDOG_S);
	printf("KERNEL-LOOPBACK RESULT ok=5 fail=1\n");
	printf("KERNEL-LOOPBACK-STATUS=1\n");
	printf("KERNEL-LOOPBACK DONE\n");
	_exit(1);
}

/* Does `poll(2)` report a CONNECTED loopback socket? That is a separate question from whether data
 * can cross, and it is the one that matters to every program that MULTIPLEXES instead of blocking —
 * LibreSSL's s_client/s_server among them, which is where this trail started. */
static int poll_report(const char *what, int fd, short events, int ms)
{
	struct pollfd p;
	int r;

	p.fd = fd;
	p.events = events;
	p.revents = 0;
	r = poll(&p, 1, ms);
	printf("KERNEL-LOOPBACK-DIAG poll(%s, %dms) = %d revents=0x%x\n", what, ms, r, (unsigned)p.revents);
	return r > 0 && (p.revents & events) ? 1 : 0;
}

/* Blocking read of exactly n bytes. EINTR is retried only while the watchdog has NOT fired. */
static ssize_t read_exactly(int fd, char *buf, size_t n)
{
	size_t got = 0;

	while (got < n) {
		ssize_t r = read(fd, buf + got, n - got);

		if (r > 0) {
			got += (size_t)r;
			continue;
		}
		if (r == 0) {
			break;		/* peer closed */
		}
		if (errno == EINTR && !timed_out) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return -2;	/* unexpectedly non-blocking */
		}
		return -1;
	}
	return (ssize_t)got;
}

static ssize_t write_all(int fd, const char *buf, size_t n)
{
	size_t sent = 0;

	while (sent < n) {
		ssize_t w = write(fd, buf + sent, n - sent);

		if (w > 0) {
			sent += (size_t)w;
			continue;
		}
		if (w < 0 && errno == EINTR && !timed_out) {
			continue;
		}
		if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return -2;	/* the socket is not actually writable */
		}
		return -1;
	}
	return (ssize_t)sent;
}

/* Print a procfs file line by line, so the console stays readable and the lines are greppable. */
static void dump_file(const char *path)
{
	char buf[512];
	FILE *fh = fopen(path, "r");

	if (fh == NULL) {
		printf("KERNEL-LOOPBACK-DIAG %s: cannot open (%s)\n", path, strerror(errno));
		return;
	}
	while (fgets(buf, sizeof(buf), fh) != NULL) {
		size_t n = strlen(buf);

		while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
			buf[--n] = '\0';
		}
		if (n > 0) {
			printf("KERNEL-LOOPBACK-DIAG %s| %s\n", path, buf);
		}
	}
	fclose(fh);
}

/* THE PEER. Accepts one connection, reads the payload, answers it. Reports by exit code. */
static void peer(int ls)
{
	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	char buf[128];
	ssize_t n;
	int c = accept(ls, (struct sockaddr *)&from, &fromlen);

	if (c < 0) {
		_exit(PEER_ACCEPT_FAILED);
	}
	n = read_exactly(c, buf, strlen(PAYLOAD));
	if (n != (ssize_t)strlen(PAYLOAD)) {
		close(c);
		_exit(PEER_READ_FAILED);
	}
	if (memcmp(buf, PAYLOAD, strlen(PAYLOAD)) != 0) {
		close(c);
		_exit(PEER_BAD_PAYLOAD);
	}
	if (write_all(c, REPLY, strlen(REPLY)) != (ssize_t)strlen(REPLY)) {
		close(c);
		_exit(PEER_WRITE_FAILED);
	}
	close(c);
	_exit(PEER_OK);
}

int main(void)
{
	struct sockaddr_in addr;
	int ls, c, on = 1, status = -1, w_ready = -1, r_ready = -1;
	pid_t pid;
	char buf[128];
	ssize_t n;

	ls = socket(AF_INET, SOCK_STREAM, 0);
	check("loopback-socket-created", ls >= 0,
	      ls < 0 ? strerror(errno) : "");
	if (ls < 0) {
		goto done;
	}
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(PORT);

	check("bind-to-127-0-0-1", bind(ls, (struct sockaddr *)&addr, sizeof(addr)) == 0,
	      strerror(errno));
	check("listen", listen(ls, 1) == 0, strerror(errno));

	/* THE INTERFACE ITSELF, READ OUT OF PROCFS. A connect that succeeds and a write that never
	 * becomes possible is consistent with a loopback interface that exists but is not UP — or is not
	 * there at all — so the state is measured here rather than guessed at afterwards. */
	dump_file("/proc/net/dev");
	dump_file("/proc/net/route");

	signal(SIGALRM, on_alarm);
	alarm(WATCHDOG_S);

	pid = fork();
	if (pid == 0) {
		alarm(0);			/* the peer may wait; the PARENT's watchdog bounds the test */
		peer(ls);
		_exit(PEER_SOCKET_FAILED);
	}
	peer_pid = pid;
	check("fork-the-peer", pid > 0, strerror(errno));
	/* placeholder: the poll limit is asserted after the exchange, where both answers are known */
	if (pid <= 0) {
		goto done;
	}

	c = socket(AF_INET, SOCK_STREAM, 0);
	if (c < 0 || connect(c, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		check("client-connect", 0, strerror(errno));
	} else {
		check("client-connect", 1, "");

		w_ready = poll_report("POLLOUT before the write", c, POLLOUT, 2000);
		n = write_all(c, PAYLOAD, strlen(PAYLOAD));
		printf("KERNEL-LOOPBACK-DIAG client sent %d of %d bytes to 127.0.0.1:%d%s\n",
		       (int)n, (int)strlen(PAYLOAD), PORT,
		       n == -2 ? " (EAGAIN on a BLOCKING socket - no connection behind it)" : "");
		usleep(300000);		/* let the peer answer, so the POLLIN question is fair */
		r_ready = poll_report("POLLIN before the read", c, POLLIN, 2000);
		n = read_exactly(c, buf, strlen(REPLY));
		buf[n > 0 ? n : 0] = '\0';
		printf("KERNEL-LOOPBACK-DIAG client read %d bytes: '%s'\n", (int)n, buf);
		check("response-arrived-at-the-client",
		      n == (ssize_t)strlen(REPLY) && memcmp(buf, REPLY, strlen(REPLY)) == 0,
		      n == -2 ? "the socket reported EAGAIN although it is blocking"
			      : n < 0 ? "the read failed" : "short or wrong reply");
		close(c);
	}

	/* --- PHASE 2: THE SHAPE CURL USES — A NON-BLOCKING connect() PLUS A WAIT FOR WRITABILITY --------
	 * WHY THIS EXISTS: libressl_l2's https fetch does not connect while `openssl s_client` against
	 * the very same server does — and the difference between those two clients is exactly this:
	 * curl connects NON-BLOCKING and then waits for the socket to become WRITABLE, while s_client
	 * just blocks in connect(). ipv4_select() answers SEL_W only for a socket whose peer is linked
	 * and whose state is SS_CONNECTED, so a socket still SS_CONNECTING is (correctly) not writable
	 * — and the question is whether accept()'s link-and-wake ever reaches this waiter.
	 *
	 * MEASUREMENT ONLY on this run: the three DIAG lines below are the instrument, and the
	 * assertions are added once the numbers are known (a limit asserted before it is measured is a
	 * guess dressed as a test).
	 */
	{
		int s2, fl, gr, soerr = -1;
		socklen_t solen = sizeof(soerr);
		int rc2, w2, w3;
		pid_t p2 = fork();

		if (p2 == 0) {
			peer(ls);		/* the same peer, on a SECOND connection */
			_exit(PEER_OK);
		}
		s2 = socket(AF_INET, SOCK_STREAM, 0);
		fl = fcntl(s2, F_GETFL, 0);
		fcntl(s2, F_SETFL, fl | O_NONBLOCK);
		errno = 0;
		rc2 = connect(s2, (struct sockaddr *)&addr, sizeof(addr));
		printf("KERNEL-LOOPBACK-DIAG nonblocking connect() = %d errno=%d (%s)\n",
		       rc2, rc2 ? errno : 0, strerror(rc2 ? errno : 0));
		w2 = poll_report("POLLOUT after a nonblocking connect()", s2, POLLOUT, 5000);
		errno = 0;
		gr = getsockopt(s2, SOL_SOCKET, SO_ERROR, &soerr, &solen);
		printf("KERNEL-LOOPBACK-DIAG getsockopt(SO_ERROR) = %d errno=%d soerr=%d\n",
		       gr, gr ? errno : 0, soerr);
		/* DOES A poll() TIMEOUT EXPIRE? And this is the question that every hang on this trail
		 * has been asking. It is posed HERE, deliberately: the peer is connected and IDLE (it is
		 * sitting in read(), nothing has crossed in either direction), so the ONLY correct answer
		 * is a timeout. Every poll in this probe until now had data already waiting, which means a
		 * timeout that never expires would have been INVISIBLE — and it would explain a curl that
		 * ignores --max-time, an s_client killed at 124, and harness windows with no check line at
		 * all. */
		/* The shape's FIRST fact, and now asserted rather than only narrated: a non-blocking connect() on
		 * loopback either completes at once or reports EINPROGRESS. Either is correct; SILENCE is not. */
		check("nonblocking-connect-returns", rc2 == 0 || errno == EINPROGRESS,
			strerror(rc2 ? errno : 0));

		w3 = poll_report("POLLIN with a timeout, peer connected and idle", s2, POLLIN, 1500);
		check("poll-timeout-expires", w3 == 0,
			w3 != 0 ? "poll reported readiness although neither side had sent anything" : "");

		fcntl(s2, F_SETFL, fl);		/* blocking again: the exchange is bounded by the alarm */
		if (w2 > 0) {
			n = write_all(s2, PAYLOAD, strlen(PAYLOAD));
			n = read_exactly(s2, buf, strlen(REPLY));
			buf[n > 0 ? n : 0] = '\0';
			printf("KERNEL-LOOPBACK-DIAG the nonblocking connection carried '%s'\n", buf);
		} else {
			printf("KERNEL-LOOPBACK-DIAG no writability was ever reported, so no exchange was "
			       "attempted on it\n");
		}
		close(s2);
		{
			int ticks = 0, st2 = -1, reaped2 = 0;

			while (ticks < 50) {
				pid_t r = waitpid(p2, &st2, WNOHANG);

				if (r == p2) {
					reaped2 = 1;
					break;
				}
				if (r < 0) {
					break;
				}
				usleep(100000);
				ticks++;
			}
			if (!reaped2) {
				kill(p2, SIGKILL);
				waitpid(p2, &st2, 0);
			}
			printf("KERNEL-LOOPBACK-DIAG second peer exit code = %d\n",
			       reaped2 && WIFEXITED(st2) ? WEXITSTATUS(st2) : -1);
		}
	}

	/* --- PHASE 4: DOES THE PEER'S CLOSE REACH THE CLIENT AS EOF? ------------------------------------
	 * WHY THIS EXISTS, and it is the third time this one file has found a kernel defect: an https
	 * fetch read its response and then HUNG, because `HTTP/1.0` tells a client to read until the
	 * connection closes and the close was never delivered. curl's own trace, one line before it
	 * stopped, was `{ [3930 bytes data]` - the body arriving, and then nothing.
	 *
	 * A CLOSE IS NOT AN ABSENCE OF DATA, IT IS A CONDITION, and the receive path only had the
	 * former. Both halves are asserted here because they fail independently: poll() must REPORT the
	 * closed connection as readable, and read() must RETURN 0 for it.
	 */
	{
		int s4, rc4, w4;
		ssize_t n4;
		pid_t p4 = fork();

		if (p4 == 0) {
			struct sockaddr_in from;
			socklen_t fromlen = sizeof(from);
			int cc = accept(ls, (struct sockaddr *)&from, &fromlen);

			close(cc);		/* accept, then CLOSE: this peer never sends a byte */
			_exit(cc < 0 ? PEER_ACCEPT_FAILED : PEER_OK);
		}
		s4 = socket(AF_INET, SOCK_STREAM, 0);
		rc4 = connect(s4, (struct sockaddr *)&addr, sizeof(addr));
		printf("KERNEL-LOOPBACK-DIAG phase 4 connect() = %d\n", rc4);
		usleep(300000);		/* let the peer accept and close */
		w4 = poll_report("POLLIN after the peer closed", s4, POLLIN, 3000);
		if (w4 > 0) {
			errno = 0;
			n4 = read(s4, buf, sizeof(buf));
			printf("KERNEL-LOOPBACK-DIAG read() after the peer closed = %d errno=%d (%s)\n",
			       (int)n4, n4 < 0 ? errno : 0, n4 < 0 ? strerror(errno) : "0 is EOF");
		} else {
			n4 = -2;
			printf("KERNEL-LOOPBACK-DIAG the peer's close was never reported readable\n");
		}
		check("peer-close-reported-readable", w4 > 0,
		      w4 > 0 ? "" : "poll did not report the closed connection as readable");
		check("peer-close-is-observed-as-eof", n4 == 0,
		      n4 == 0 ? "" : n4 == -2 ? "no readability, so the read was not attempted"
			      : n4 < 0 ? "the read failed instead of reporting EOF"
			      : "the read returned data a closed peer never sent");
		close(s4);
		{
			int ticks = 0, st4 = -1, reaped4 = 0;

			while (ticks < 50) {
				pid_t r = waitpid(p4, &st4, WNOHANG);

				if (r == p4) {
					reaped4 = 1;
					break;
				}
				if (r < 0) {
					break;
				}
				usleep(100000);
				ticks++;
			}
			if (!reaped4) {
				kill(p4, SIGKILL);
				waitpid(p4, &st4, 0);
			}
			printf("KERNEL-LOOPBACK-DIAG phase 4 peer exit code = %d\n",
			       reaped4 && WIFEXITED(st4) ? WEXITSTATUS(st4) : -1);
		}
	}

	/*
	 * IS A poll() WITH NO DEADLINE WOKEN BY ARRIVING DATA? THE THIRD READINESS CLASS.
	 *
	 *   * a STATE CHANGE - accept, connect, close, free - has always woken &do_select;
	 *   * a DRAIN, a reader taking a packet out of the queue, has always woken it too;
	 *   * DATA ARRIVING did not, and that is now fixed (net/ipv4.c's loopback_deliver, net/unix.c's twin).
	 *
	 * AND THIS LEG DOES NOT PROVE THAT FIX - MEASURED, NOT ASSUMED: it PASSES with the fix reverted, because
	 * the SENDER's own path wakes &do_select for its drain, and that channel is global - so a peer in ANOTHER
	 * PROCESS supplies the wake the receiver's data path is missing. That is what this comment records rather
	 * than hides: the leg below is a real test of the BEHAVIOUR (a poll with no deadline must be woken), and
	 * NOT a gate for the data-arrival wake.
	 *
	 * AND THE QUESTION THAT WAS LEFT HERE HAS SINCE BEEN ANSWERED BY READING, WHICH DISSOLVED IT: the guess was
	 * that a wake might not cross threads of one address space (the TLS probe's far end is a thread of the same
	 * process as its waiter). `kernel/sleep.c`'s wakeup() disproves it - the waiters live in ONE GLOBAL
	 * sleep_hash_table keyed by the canonicalized sleep address, so a wake finds every sleeper on that channel
	 * whatever its process, thread or address space. Cross-thread wakeups were never the problem.
	 *
	 * WHAT REMAINS, AND IT NEEDS A DIFFERENT LEG THAN THIS ONE: sys_poll CHECKS readiness and THEN sleeps, so a
	 * wake landing between those two finds nobody and is lost - and this leg cannot catch that, because its peer
	 * PAUSES first, which guarantees the waiter is already registered. Catching a window needs REPETITION: a peer
	 * that writes with no delay, polled with no deadline, over many iterations, with the watchdog naming the
	 * iteration that parks.
	 *
	 * THE DEADLINE IS THE POINT OF THIS LEG: it polls with -1 and the peer writes only AFTER a pause, so
	 * nothing but a wake can end it. The watchdog reports the stall if none arrives.
	 */
	{
		int s5, w5;
		ssize_t n5;
		pid_t p5 = fork();

		if (p5 == 0) {
			struct sockaddr_in from;
			socklen_t fromlen = sizeof(from);
			int cc = accept(ls, (struct sockaddr *)&from, &fromlen);

			if (cc < 0) {
				_exit(PEER_ACCEPT_FAILED);
			}
			usleep(300000);		/* by then the parent is already waiting in poll(-1) */
			write_all(cc, "wake", 4);
			_exit(PEER_OK);
		}
		s5 = socket(AF_INET, SOCK_STREAM, 0);
		printf("KERNEL-LOOPBACK-DIAG phase 5 connect() = %d\n",
		       connect(s5, (struct sockaddr *)&addr, sizeof(addr)));
		w5 = poll_report("POLLIN with NO DEADLINE, the peer writing after a pause", s5, POLLIN, -1);
		n5 = w5 > 0 ? read(s5, buf, sizeof(buf)) : -2;
		printf("KERNEL-LOOPBACK-DIAG phase 5 poll = %d, read = %d\n", w5, (int)n5);
		check("poll-without-a-deadline-is-woken-by-data", w5 > 0,
		      w5 > 0 ? "" : "poll(-1) was not woken by arriving data (the watchdog caught the stall)");
		check("and-the-woken-poll-had-the-payload-to-read", n5 == 4 && memcmp(buf, "wake", 4) == 0,
		      n5 == 4 ? "" : n5 == -2 ? "no readability, so the read was not attempted"
			      : "the read after the wake did not return the peer's four bytes");
		close(s5);
		{
			int ticks = 0, st5 = -1, reaped5 = 0;

			while (ticks < 50) {
				pid_t r = waitpid(p5, &st5, WNOHANG);

				if (r == p5) {
					reaped5 = 1;
					break;
				}
				if (r < 0) {
					break;
				}
				usleep(100000);
				ticks++;
			}
			if (!reaped5) {
				kill(p5, SIGKILL);
				waitpid(p5, &st5, 0);
			}
			printf("KERNEL-LOOPBACK-DIAG phase 5 peer exit code = %d\n",
			       reaped5 && WIFEXITED(st5) ? WEXITSTATUS(st5) : -1);
		}
	}

	/* THE PEER'S OWN ACCOUNT, read out of its exit code — the console stays single-writer.
	 *
	 * AND IT IS REAPED UNDER A DEADLINE, which the first version of this probe did NOT do: a peer
	 * stuck in accept() made the bare waitpid(2) hang, and the case was killed at 71s with no result
	 * line. A probe whose whole subject is a path that may be wedged must bound its own waits too. */
	{
		int ticks = 0, reaped = 0;

		while (ticks < 50) {			/* 5 s */
			pid_t r = waitpid(pid, &status, WNOHANG);

			if (r == pid) {
				reaped = 1;
				break;
			}
			if (r < 0) {
				break;
			}
			usleep(100000);
			ticks++;
		}
		if (!reaped) {
			printf("KERNEL-LOOPBACK-DIAG the peer never returned after 5s - killing it\n");
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			check("peer-exited-cleanly", 0, "the peer never returned (killed)");
			check("payload-arrived-at-the-peer", 0, "the peer never accepted the connection");
			goto done;
		}
	}
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);

		printf("KERNEL-LOOPBACK-DIAG peer exit code = %d\n", code);
		check("payload-arrived-at-the-peer", code == PEER_OK || code == PEER_WRITE_FAILED,
		      code == PEER_ACCEPT_FAILED ? "the peer never accepted the connection"
		      : code == PEER_READ_FAILED ? "the peer accepted but never got the payload"
		      : code == PEER_BAD_PAYLOAD ? "the peer got the wrong bytes"
		      : "peer exit code");
		check("peer-exited-cleanly", code != PEER_SOCKET_FAILED, "the peer could not make a socket");
	} else {
		check("payload-arrived-at-the-peer", 0, "the peer did not exit normally");
		check("peer-exited-cleanly", 0, "the peer did not exit normally");
	}
	/* THE ASYMMETRY IS THE FINDING, and this probe's own history is why it is stated as a pair.
	 *
	 * The first version used non-blocking sockets and concluded "the write never becomes possible";
	 * the second, using BLOCKING I/O — which is what TLS uses — found the payload crossing fine. Both
	 * were partly right, and the same `poll(2)` call answers DIFFERENTLY for the two directions:
	 *
	 *     poll(POLLOUT) on a connected loopback socket  ->  NEVER reported (0, revents=0x0)
	 *     poll(POLLIN)  on a connected loopback socket  ->  reported (1, revents=0x1)
	 *
	 * AND THE WRITE STILL SUCCEEDS when it is blocking. So the socket IS writable and the kernel just
	 * does not SAY so — which stalls every program that multiplexes and waits for writability before
	 * sending, LibreSSL's s_client and s_server among them. That is where this trail began.
	 *
	 * AND THE LIMIT IS GONE: ipv4_select() now answers SEL_W, so POLLOUT is reported and this check
	 * is a REGRESSION GUARD rather than a description of a defect. It was written the other way round
	 * on purpose — asserting the limit with the reason in the name (§45-Y's pattern) — and it FLIPPED
	 * the moment the kernel was fixed, saying so in its own failure text
	 * ("POLLOUT WAS REPORTED - promote this and delete the limit"). This is that promotion. */
	if (w_ready >= 0 && r_ready >= 0) {
		check("poll-reports-readiness-and-writability",
		      r_ready == 1 && w_ready == 1,
		      r_ready != 1 ? "poll(2) did not report a readable socket that had data"
		      : w_ready != 1 ? "poll(2) did not report a WRITABLE socket - the fix regressed"
		      : "unexpected");
	}

	alarm(0);
	close(ls);

done:
	printf("KERNEL-LOOPBACK RESULT ok=%d fail=%d\n", okc, failc);
	printf("KERNEL-LOOPBACK-STATUS=%d\n", failc ? 1 : 0);
	printf("KERNEL-LOOPBACK DONE\n");
	return failc ? 1 : 0;
}
