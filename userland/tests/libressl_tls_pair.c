/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * libressl_tls_pair — A REAL TLS HANDSHAKE ON FNX, through the FIRST-PARTY libtls API with BLOCKING
 * sockets. This is L1's substance, separated from one variable.
 *
 * WHY IT EXISTS SEPARATELY FROM libressl_l1.sh. That script drives `openssl s_server` and `s_client`,
 * and the handshake stalled with the client's `-state` trace stopping at `write client hello A`. But
 * `s_client` MULTIPLEXES — it selects on the socket AND on stdin — so its stall has two readings: a
 * kernel readiness gap it trips over, or its own loop. This program removes that variable: it is the
 * `libtls` API this project ships (`tls_server`/`tls_client`, `tls_handshake`, `tls_read`/`tls_write`)
 * over BLOCKING sockets, which is the surface a first-party consumer would actually use.
 *
 * IT IS TWO PROCESSES, like the script: a client (this process) and a peer (a child that accepts,
 * handshakes, reads a request and answers it). The peer reports through its EXIT CODE, so the console
 * stays single-writer.
 *
 * AND IT IS BOUNDED: a `SIGALRM` watchdog prints the verdict if the handshake does not finish, because
 * this probe's whole subject is a step that has already stalled once.
 *
 * Usage: libressl_tls_pair <cert.pem> <key.pem>
 * Output: LIBRESSL-PAIR <check> ok|FAIL <detail>, then RESULT/STATUS/DONE.
 */

#define _GNU_SOURCE 1

#include <tls.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT		46465
#define SERVERNAME	"localhost"
#define REQUEST		"GET / HTTP/1.0\r\n\r\n"
#define REPLY		"HTTP/1.0 200 ok\r\n\r\nlibtls-pair-reply"
#define WATCHDOG_S	20

/* THE PEER'S EXIT CODES — one per way it can fail, which is how it reports. */
#define PEER_OK		0
#define PEER_TLS_INIT	1
#define PEER_CONFIG	2
#define PEER_CERT	3
#define PEER_KEY	4
#define PEER_ACCEPT	5
#define PEER_SOCKET	6
#define PEER_HANDSHAKE	7
#define PEER_READ	8
#define PEER_WRITE	9

static int okc, failc;
static pid_t g_peer = -1;

/*
 * EVERY CHECK IS ACCOUNTED FOR, EVEN WHEN THE PROBE RETURNS EARLY. The first version printed only the
 * checks it reached, so a handshake failure produced a tally of SEVEN for EIGHT checks — and the case's
 * tally check caught it. A check that is never reported must not be able to read as a pass.
 */
static const char *ALL_CHECKS[] = {
	"tls-library-initialises", "listener-bound", "peer-forked", "client-configured",
	"client-connect", "tls-handshake-completed", "application-data-flowed",
	"peer-completed-a-handshake", NULL
};
#define N_CHECKS 8
static int reported[N_CHECKS];
static const char *PROGRESS = "peer.progress";

/* THE PEER'S PROGRESS GOES TO A FILE, because its exit code is LOST when the watchdog fires — and the
 * watchdog firing is exactly the case that needs explaining. Each line is flushed as it happens. */
static FILE *g_progress;

static void note(const char *what)
{
	if (g_progress != NULL) {
		fprintf(g_progress, "%s\n", what);
		fflush(g_progress);
	}
}

static void dump_progress(void)
{
	FILE *fh = fopen(PROGRESS, "r");
	char line[256];

	if (fh == NULL) {
		printf("LIBRESSL-PAIR-DIAG %s: (no file - the peer never started)\n", PROGRESS);
		return;
	}
	while (fgets(line, sizeof(line), fh) != NULL) {
		size_t n = strlen(line);

		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
			line[--n] = '\0';
		}
		printf("LIBRESSL-PAIR-DIAG %s| %s\n", PROGRESS, line);
	}
	fclose(fh);
}

static void check(const char *name, int ok, const char *detail)
{
	int i;

	for (i = 0; ALL_CHECKS[i] != NULL; i++) {
		if (strcmp(ALL_CHECKS[i], name) == 0) {
			reported[i] = 1;
		}
	}
	if (ok) {
		okc++;
		printf("LIBRESSL-PAIR %s ok\n", name);
	} else {
		failc++;
		printf("LIBRESSL-PAIR %s FAIL %s\n", name, detail != NULL ? detail : "");
	}
}

static void diag(const char *what, const char *value)
{
	printf("LIBRESSL-PAIR-DIAG %s: %s\n", what, value != NULL ? value : "(null)");
}

static void on_alarm(int sig)
{
	(void)sig;
	if (g_peer > 0) {
		kill(g_peer, SIGKILL);
	}
	dump_progress();
	printf("LIBRESSL-PAIR no-result-within-%ds FAIL the handshake did not finish in time\n", WATCHDOG_S);
	printf("LIBRESSL-PAIR RESULT ok=0 fail=1\n");
	printf("LIBRESSL-PAIR-STATUS=1\n");
	printf("LIBRESSL-PAIR DONE\n");
	_exit(1);
}

/* NARRATION WITH A DESTINATION, because the peer's goes to a file and the client's to stdout. */
static void progress(FILE *where, const char *fmt, ...)
{
	va_list ap;

	if (where == NULL) {
		return;
	}
	va_start(ap, fmt);
	vfprintf(where, fmt, ap);
	fprintf(where, "\n");
	va_end(ap);
	fflush(where);
}

/* A BOUNDED WAIT, so a readiness that never arrives REPORTS instead of hanging. */
static int wait_ready(int fd, short events, int ms)
{
	struct pollfd p;

	p.fd = fd;
	p.events = events;
	p.revents = 0;
	return poll(&p, 1, ms) > 0 && (p.revents & events) ? 1 : 0;
}

/*
 * THE HANDSHAKE DRIVEN THE WAY libtls DOCUMENTS IT, and driven this way ON PURPOSE.
 *
 * `tls_handshake` is NOT a blocking call that retries internally: `tls_ssl_error` maps SSL_ERROR_WANT_READ
 * and WANT_WRITE onto TLS_WANT_POLLIN and TLS_WANT_POLLOUT, i.e. the CALLER waits and calls again. The
 * first version of this probe called it once on a BLOCKING socket, which stalls somewhere inside with no
 * way to say where — and "somewhere inside" is exactly what this probe exists to locate.
 *
 * So the sockets are NON-BLOCKING and this loop narrates EVERY round: which readiness was asked for, and
 * whether it arrived. The round that never gets its readiness IS the finding.
 */
static int handshake_loop(struct tls *ctx, int fd, FILE *log, const char *who)
{
	int rounds = 0;

	for (;;) {
		int rv = tls_handshake(ctx);

		if (rv == 0) {
			progress(log, "%s: HANDSHAKE COMPLETE after %d round(s)", who, rounds);
			return 0;
		}
		if (rv == TLS_WANT_POLLIN) {
			progress(log, "%s: round %d wants POLLIN", who, rounds);
			if (!wait_ready(fd, POLLIN, 5000)) {
				progress(log, "%s: POLLIN NEVER ARRIVED (5s)", who);
				return -1;
			}
		} else if (rv == TLS_WANT_POLLOUT) {
			progress(log, "%s: round %d wants POLLOUT", who, rounds);
			if (!wait_ready(fd, POLLOUT, 5000)) {
				progress(log, "%s: POLLOUT NEVER ARRIVED (5s)", who);
				return -1;
			}
		} else {
			progress(log, "%s: handshake error: %s", who, tls_error(ctx) != NULL ? tls_error(ctx) : "?");
			return -1;
		}
		if (++rounds > 50) {
			progress(log, "%s: gave up after 50 rounds", who);
			return -1;
		}
	}
}

static int set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* THE PEER: accept one TCP connection, handshake as the SERVER, read the request, answer it. */
static void peer(const char *cert, const char *key, int ls)
{
	struct tls *ctx = NULL, *cctx = NULL;
	struct tls_config *cfg = NULL;
	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);
	char buf[256];
	int c, r, n;

	g_progress = fopen(PROGRESS, "w");
	note("peer: started");
	if (tls_init() == -1) {
		note("peer: tls_init failed");
		_exit(PEER_TLS_INIT);
	}
	if ((cfg = tls_config_new()) == NULL) {
		_exit(PEER_CONFIG);
	}
	/* The SERVER presents the certificate; these are the files the shell script generated. */
	if (tls_config_set_cert_file(cfg, cert) == -1) {
		_exit(PEER_CERT);
	}
	if (tls_config_set_key_file(cfg, key) == -1) {
		_exit(PEER_KEY);
	}
	if ((ctx = tls_server()) == NULL || tls_configure(ctx, cfg) == -1) {
		_exit(PEER_CONFIG);
	}
	tls_config_free(cfg);

	note("peer: waiting in accept()");
	c = accept(ls, (struct sockaddr *)&from, &fromlen);
	if (c < 0) {
		note("peer: accept failed");
		_exit(PEER_ACCEPT);
	}
	note("peer: accepted the connection");
	set_nonblocking(c);
	if (tls_accept_socket(ctx, &cctx, c) == -1) {
		note("peer: tls_accept_socket failed");
		_exit(PEER_SOCKET);
	}
	note("peer: entering the handshake loop");
	if (handshake_loop(cctx, c, g_progress, "peer") != 0) {
		_exit(PEER_HANDSHAKE);
	}
	note("peer: HANDSHAKE COMPLETED");
	note("peer: reading the request");
	r = tls_read(cctx, buf, sizeof(buf) - 1);
	if (r <= 0) {
		note("peer: tls_read returned nothing");
		_exit(PEER_READ);
	}
	note("peer: read the request");
	buf[r] = '\0';
	if (strncmp(buf, "GET ", 4) != 0) {
		_exit(PEER_READ);
	}
	n = tls_write(cctx, REPLY, strlen(REPLY));
	if (n != (int)strlen(REPLY)) {
		_exit(PEER_WRITE);
	}
	tls_close(cctx);
	tls_free(cctx);
	tls_close(ctx);
	tls_free(ctx);
	_exit(PEER_OK);
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	struct tls *ctx = NULL;
	struct tls_config *cfg = NULL;
	int ls = -1, c = -1, on = 1, status = -1, n;
	pid_t pid;
	char buf[256];

	if (argc < 3) {
		printf("LIBRESSL-PAIR usage FAIL need <cert.pem> <key.pem>\n");
		printf("LIBRESSL-PAIR RESULT ok=0 fail=1\n");
		printf("LIBRESSL-PAIR-STATUS=1\n");
		printf("LIBRESSL-PAIR DONE\n");
		return 1;
	}

	signal(SIGALRM, on_alarm);
	alarm(WATCHDOG_S);

	check("tls-library-initialises", tls_init() == 0, "tls_init() failed");

	/* The LISTENING SOCKET, then the peer: fixed port, because bind(2) to port 0 is broken here. */
	ls = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(PORT);
	check("listener-bound", ls >= 0 && bind(ls, (struct sockaddr *)&addr, sizeof(addr)) == 0
			       && listen(ls, 1) == 0, strerror(errno));

	pid = fork();
	if (pid == 0) {
		alarm(0);			/* the peer may wait; the PARENT's watchdog bounds the test */
		peer(argv[1], argv[2], ls);
		_exit(PEER_TLS_INIT);
	}
	g_peer = pid;
	check("peer-forked", pid > 0, strerror(errno));

	/* THE CLIENT, with BLOCKING sockets and no select anywhere in it. */
	if ((cfg = tls_config_new()) == NULL) {
		check("client-configured", 0, "tls_config_new failed");
		goto done;
	}
	if (tls_config_set_ca_file(cfg, argv[1]) == -1) {
		check("client-configured", 0, "could not load the CA file");
		goto done;
	}
	if ((ctx = tls_client()) == NULL || tls_configure(ctx, cfg) == -1) {
		check("client-configured", 0, tls_error(ctx) != NULL ? tls_error(ctx) : "tls_configure failed");
		goto done;
	}
	check("client-configured", 1, "");

	c = socket(AF_INET, SOCK_STREAM, 0);
	if (c < 0 || connect(c, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		check("client-connect", 0, strerror(errno));
		goto done;
	}
	check("client-connect", 1, "");

	if (tls_connect_socket(ctx, c, SERVERNAME) == -1) {
		check("tls-handshake-completed", 0,
		      tls_error(ctx) != NULL ? tls_error(ctx) : "tls_connect_socket failed");
		goto done;
	}
	set_nonblocking(c);
	diag("client", "entering the handshake loop");
	if (handshake_loop(ctx, c, stdout, "client") != 0) {
		check("tls-handshake-completed", 0,
		      tls_error(ctx) != NULL ? tls_error(ctx) : "tls_handshake did not complete");
		goto done;
	}
	check("tls-handshake-completed", 1, "");
	diag("negotiated version", tls_conn_version(ctx) != NULL ? tls_conn_version(ctx) : "(null)");
	diag("negotiated cipher", tls_conn_cipher(ctx) != NULL ? tls_conn_cipher(ctx) : "(null)");

	/* AND THE BYTES THROUGH THE TUNNEL: a request out, a reply back. */
	set_nonblocking(c);
	if (tls_write(ctx, REQUEST, strlen(REQUEST)) != (int)strlen(REQUEST)) {
		check("application-data-flowed", 0, "tls_write failed");
	} else {
		n = tls_read(ctx, buf, sizeof(buf) - 1);
		buf[n > 0 ? n : 0] = '\0';
		diag("reply read", n > 0 ? buf : "(nothing)");
		check("application-data-flowed",
		      n > 0 && strncmp(buf, "HTTP/1.0 200", 12) == 0,
		      n <= 0 ? "tls_read returned nothing" : "unexpected reply");
	}
	tls_close(ctx);

done:
	if (pid > 0) {
		/* BOUNDED, and with the watchdog STILL ARMED: a peer stuck in its own handshake must not
		 * hang the probe. (Disarming the alarm first is exactly how that happens.) */
		int ticks = 0, reaped = 0;

		while (ticks < 50) {
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
			printf("LIBRESSL-PAIR-DIAG the peer never returned after 5s - killing it\n");
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			check("peer-completed-a-handshake", 0, "the peer never returned (killed)");
			goto finish;
		}
		if (WIFEXITED(status)) {
			int code = WEXITSTATUS(status);

			printf("LIBRESSL-PAIR-DIAG peer exit code = %d\n", code);
			check("peer-completed-a-handshake", code == PEER_OK,
			      code == PEER_HANDSHAKE ? "the peer's OWN handshake failed"
			      : code == PEER_CERT ? "the peer could not load the certificate"
			      : code == PEER_KEY ? "the peer could not load the key"
			      : code == PEER_ACCEPT ? "the peer never accepted the connection"
			      : code == PEER_READ ? "the peer read nothing through TLS"
			      : "peer exit code");
		} else {
			check("peer-completed-a-handshake", 0, "the peer did not exit normally");
		}
	}

finish:
	alarm(0);
	dump_progress();
	if (c >= 0) {
		close(c);
	}
	if (ls >= 0) {
		close(ls);
	}

	/* ANY CHECK THE EARLY EXIT SKIPPED, reported as a failure with its reason rather than omitted. */
	{
		int i;

		for (i = 0; ALL_CHECKS[i] != NULL; i++) {
			if (!reported[i]) {
				check(ALL_CHECKS[i], 0, "not reached (an earlier step did not complete)");
			}
		}
	}

	printf("LIBRESSL-PAIR RESULT ok=%d fail=%d\n", okc, failc);
	printf("LIBRESSL-PAIR-STATUS=%d\n", failc ? 1 : 0);
	printf("LIBRESSL-PAIR DONE\n");
	return failc ? 1 : 0;
}
