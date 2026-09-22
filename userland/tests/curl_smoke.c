/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * curl_smoke — libcurl ON THE GUEST, which is what W7 slice 2b has to prove.
 * docs/design/foundation-transport-plan.md §4.
 *
 * NOT A FOUNDATION PROBE, deliberately: this is a THIRD-PARTY LIBRARY's landing, so the program that
 * judges it has no Foundation in it at all — the same reasoning kernel_pipe_dup2 follows for the
 * kernel. What it asserts is exactly what the landing claims:
 *
 *   curl-version-is-8-22      the PIN is the version that actually loaded;
 *   curl-has-no-tls-backend   this landing is TLS-LESS (libressl-plan.md owns TLS), asserted from the
 *                             version feature bits AND the version string rather than assumed;
 *   curl-file-fetch           BYTES MOVE: the probe writes a file, fetches it back over `file://`
 *                             with a PERCENT-ENCODED space, and compares — so the URL decode path is
 *                             exercised too, and no network is needed;
 *   curl-http-reaches-connect HTTP is a SUPPORTED protocol, and that is proven by the ERROR the
 *                             attempt gives (CURLE_COULDNT_CONNECT) rather than by
 *                             CURLE_UNSUPPORTED_PROTOCOL. No server is needed for that, and a loopback
 *                             server would be testing the server, not the transport;
 *   curl-https-refused        and `https://` FAILS CLEARLY (CURLE_UNSUPPORTED_PROTOCOL) rather than
 *                             silently degrading — the check the transport plan asked for by name.
 *
 * THE `-DIAG` LINES ARE NOT CHECKS AND ARE NOT DECORATION. The first run of this probe answered
 * `CURLE_OUT_OF_MEMORY` for all three fetches with NO kernel out-of-memory report anywhere, which is a
 * result with two very different readings: either this PROCESS cannot allocate (a guest/kernel matter)
 * or curl is failing internally (a build-configuration matter). The malloc ladder settles that, and the
 * error buffer and the feature word point at the rest. They are printed rather than checked because
 * what they mean depends on the answer.
 */

#define _GNU_SOURCE 1

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("CURL-SMOKE %s ok\n", name);
	} else {
		failc++;
		printf("CURL-SMOKE %s FAIL %s\n", name, detail != NULL ? detail : "");
	}
}

static char body[4096];
static size_t body_len;
static char errbuf[CURL_ERROR_SIZE];

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t n = size * nmemb;

	(void)userdata;
	if (body_len + n <= sizeof(body)) {
		memcpy(body + body_len, ptr, n);
		body_len += n;
	}
	return n;
}

/* One request, with the body discarded. The return is curl's own code, which is the thing the two
 * protocol checks read — a transport that answers the WRONG error either way is the failure mode. */
static CURLcode perform(const char *url)
{
	CURL *easy = curl_easy_init();
	CURLcode rc;

	if (easy == NULL) {
		return CURLE_FAILED_INIT;
	}
	body_len = 0;
	errbuf[0] = '\0';
	/* THE SETOPT CODES ARE PRINTED, not assumed: a setopt that answered OOM would explain everything
	 * downstream while looking like a perform failure. */
	{
		CURLcode a = curl_easy_setopt(easy, CURLOPT_URL, url);
		CURLcode b = curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
		CURLcode c = curl_easy_setopt(easy, CURLOPT_TIMEOUT, 10L);
		CURLcode d = curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf);

		printf("CURL-SMOKE-DIAG setopt url=%d write=%d timeout=%d errbuf=%d\n",
		       (int)a, (int)b, (int)c, (int)d);
	}
	rc = curl_easy_perform(easy);
	if (rc != CURLE_OK) {
		printf("CURL-SMOKE-DIAG %s -> rc=%d (%s) errbuf=%.120s\n",
		       url, (int)rc, curl_easy_strerror(rc),
		       errbuf[0] != '\0' ? errbuf : "(empty)");
	}
	curl_easy_cleanup(easy);
	return rc;
}

/* The same request with curl's own narration on: the last resort when a code is not enough. */
static void perform_verbose(const char *url)
{
	CURL *easy = curl_easy_init();

	if (easy == NULL) {
		return;
	}
	body_len = 0;
	errbuf[0] = '\0';
	curl_easy_setopt(easy, CURLOPT_URL, url);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
	printf("CURL-SMOKE-DIAG verbose %s rc=%d\n", url, (int)curl_easy_perform(easy));
	curl_easy_cleanup(easy);
}

/* CAN THIS PROCESS ALLOCATE AT ALL? That is the question the OOM checks raise, and it is the
 * difference between a guest that cannot give memory and a library failing on its own. */
static void malloc_ladder(void)
{
	static const size_t sizes[] = { 65536, 1048576, 8388608 };
	int i;

	for (i = 0; i < 3; i++) {
		void *p = malloc(sizes[i]);

		if (p != NULL) {
			memset(p, 0x5a, sizes[i]);
		}
		printf("CURL-SMOKE-DIAG malloc(%d) = %s\n", (int)sizes[i], p != NULL ? "ok" : "NULL");
		free(p);
	}
}

int main(void)
{
	const char *path = "/System/Temporary Files/curl-smoke.txt";
	const char *url = "file:///System/Temporary%20Files/curl-smoke.txt";
	const char *payload = "curl-smoke-payload\n";
	curl_version_info_data *vi;
	CURLcode rc;
	FILE *fh;
	int wrote;

	rc = curl_global_init(CURL_GLOBAL_DEFAULT);
	check("curl-global-init", rc == CURLE_OK, curl_easy_strerror(rc));

	vi = curl_version_info(CURLVERSION_NOW);
	check("curl-version-is-8-22",
	      vi != NULL && strncmp(vi->version, "8.22", 4) == 0,
	      vi != NULL ? vi->version : "(no version info)");

	check("curl-has-no-tls-backend",
	      vi != NULL &&
	      (vi->features & CURL_VERSION_SSL) == 0 &&
	      vi->ssl_version == NULL &&
	      strstr(vi->version, "OpenSSL") == NULL &&
	      strstr(vi->version, "LibreSSL") == NULL &&
	      strstr(vi->version, "GnuTLS") == NULL &&
	      strstr(vi->version, "mbedTLS") == NULL &&
	      strstr(vi->version, "rustls") == NULL,
	      vi != NULL ? vi->version : "(no version info)");

	/* --- DIAGNOSTICS, before any fetch ------------------------------------------------------- */
	if (vi != NULL) {
		char line[512];

		snprintf(line, sizeof(line), "curl_version=%s features=0x%x asyncdns=%d",
			 vi->version, (unsigned)vi->features,
			 (vi->features & CURL_VERSION_ASYNCHDNS) ? 1 : 0);
		printf("CURL-SMOKE-DIAG %s\n", line);
		snprintf(line, sizeof(line), "curl_version_string=%s", curl_version());
		printf("CURL-SMOKE-DIAG %s\n", line);
	}
	malloc_ladder();

	/* THE ONE CALL THAT FAILS, interrogated four ways in one run: what is errno afterwards (a
	 * failing syscall would leave its mark), is the failure STICKY, does a prior easy handle change
	 * it, and does it depend on having happened after the malloc ladder. */
	{
		CURLM *m;
		int i;

		for (i = 0; i < 2; i++) {
			errno = 0;
			m = curl_multi_init();
			printf("CURL-SMOKE-DIAG curl_multi_init[%d]=%s errno=%d (%s)\n",
			       i, m != NULL ? "ok" : "NULL", m != NULL ? 0 : errno,
			       strerror(m != NULL ? 0 : errno));
			if (m != NULL) {
				curl_multi_cleanup(m);
			}
		}
	}
	{
		CURL *e = curl_easy_init();
		CURLM *m;

		errno = 0;
		m = curl_multi_init();
		printf("CURL-SMOKE-DIAG after-curl_easy_init(%s) curl_multi_init=%s errno=%d\n",
		       e != NULL ? "ok" : "NULL", m != NULL ? "ok" : "NULL", m != NULL ? 0 : errno);
		if (m != NULL) {
			curl_multi_cleanup(m);
		}
		if (e != NULL) {
			curl_easy_cleanup(e);
		}
	}

	/* WHAT PERFORM NEEDS BEFORE IT REACHES A PROTOCOL. curl_easy_perform calls curl_multi_init() on
	 * its easy handle's behalf and answers CURLE_OUT_OF_MEMORY if THAT fails — which would be an OOM
	 * for every URL including file:// and an IP literal, exactly what the first run showed. The
	 * wakeup pair is the other thing multi init builds, so it is measured too. */
	{
		int sp[2] = { -1, -1 };
		int rc_sp = socketpair(AF_UNIX, SOCK_STREAM, 0, sp);

		printf("CURL-SMOKE-DIAG socketpair(AF_UNIX,STREAM)=%d (%s) fds=%d,%d\n",
		       rc_sp, strerror(rc_sp != 0 ? errno : 0), sp[0], sp[1]);
		if (rc_sp == 0) {
			close(sp[0]);
			close(sp[1]);
		}
	}

	/* EVERY REMAINING STEP OF Curl_multi_handle THAT CAN FAIL, tested one at a time. The wakeup
	 * pair is a pipe2(O_NONBLOCK|O_CLOEXEC) when curl was built with HAVE_PIPE2 (it was), the IPv6
	 * probe is a socket(PF_INET6, SOCK_DGRAM), and USE_RESOLV_THREADED adds a thread pool. Whichever
	 * of these fails is the one that makes curl_multi_init answer NULL — and curl_easy_perform turns
	 * THAT into CURLE_OUT_OF_MEMORY for every URL, which is the misleading error this measures. */
	{
		int p1[2] = { -1, -1 };
		int r1 = pipe(p1);

		printf("CURL-SMOKE-DIAG pipe()=%d errno=%d (%s)\n", r1, r1 ? errno : 0,
		       strerror(r1 ? errno : 0));
		if (r1 == 0) {
			close(p1[0]);
			close(p1[1]);
		}
	}
	{
		int p2[2] = { -1, -1 };
		int r2 = pipe2(p2, O_NONBLOCK | O_CLOEXEC);

		printf("CURL-SMOKE-DIAG pipe2(O_NONBLOCK|O_CLOEXEC)=%d errno=%d (%s)  <-- what curl calls\n",
		       r2, r2 ? errno : 0, strerror(r2 ? errno : 0));
		if (r2 == 0) {
			close(p2[0]);
			close(p2[1]);
		}
	}
	{
		int p3[2] = { -1, -1 };
		int r3 = pipe2(p3, O_NONBLOCK);

		printf("CURL-SMOKE-DIAG pipe2(O_NONBLOCK)=%d errno=%d (%s)\n", r3, r3 ? errno : 0,
		       strerror(r3 ? errno : 0));
		if (r3 == 0) {
			close(p3[0]);
			close(p3[1]);
		}
	}
	{
		int p4[2] = { -1, -1 };
		int r4 = pipe2(p4, O_CLOEXEC);

		printf("CURL-SMOKE-DIAG pipe2(O_CLOEXEC)=%d errno=%d (%s)\n", r4, r4 ? errno : 0,
		       strerror(r4 ? errno : 0));
		if (r4 == 0) {
			close(p4[0]);
			close(p4[1]);
		}
	}
	/* THE CULPRIT, NAMED. curl's build-time probe sees `eventfd` in musl's HEADERS and links against
	 * it, so it defines USE_EVENTFD and builds its wakeup pair out of one — but a header probe cannot
	 * see whether the KERNEL implements the call. On FNX it does not: eventfd answers ENOSYS, curl's
	 * wakeup init fails, curl_multi_init answers NULL, and curl_easy_perform turns that into
	 * CURLE_OUT_OF_MEMORY for every URL. This line is that measurement, kept because it is the reason
	 * the build is configured the way it is (see tools/curl-build.sh). */
	{
		int efd = eventfd(0, EFD_CLOEXEC);

		printf("CURL-SMOKE-DIAG eventfd(0,EFD_CLOEXEC)=%d errno=%d (%s)\n",
		       efd, efd < 0 ? errno : 0, strerror(efd < 0 ? errno : 0));
		if (efd >= 0) {
			close(efd);
		}
	}
	{
		int tfd = timerfd_create(CLOCK_MONOTONIC, 0);

		printf("CURL-SMOKE-DIAG timerfd_create(CLOCK_MONOTONIC)=%d errno=%d (%s)\n",
		       tfd, tfd < 0 ? errno : 0, strerror(tfd < 0 ? errno : 0));
		if (tfd >= 0) {
			close(tfd);
		}
	}
	{
		int v6 = socket(AF_INET6, SOCK_DGRAM, 0);

		printf("CURL-SMOKE-DIAG socket(AF_INET6,SOCK_DGRAM)=%d errno=%d (%s)\n",
		       v6, v6 < 0 ? errno : 0, strerror(v6 < 0 ? errno : 0));
		if (v6 >= 0) {
			close(v6);
		}
	}

	/* THE BYTES, actually moved: write a file, fetch it back through file:// and compare. */
	fh = fopen(path, "w");
	wrote = fh != NULL && fputs(payload, fh) >= 0;
	if (fh != NULL) {
		fclose(fh);
	}
	printf("CURL-SMOKE-DIAG fixture wrote=%d path=%s\n", wrote, path);
	rc = perform(url);
	body[body_len < sizeof(body) ? body_len : sizeof(body) - 1] = '\0';
	check("curl-file-fetch",
	      wrote && rc == CURLE_OK && strcmp(body, payload) == 0,
	      !wrote ? "could not write the fixture"
		    : (rc != CURLE_OK ? curl_easy_strerror(rc) : body));

	/* HTTP IS SUPPORTED: the attempt reaches connect() and fails THERE. */
	rc = perform("http://127.0.0.1:9/");
	check("curl-http-reaches-connect",
	      rc == CURLE_COULDNT_CONNECT,
	      curl_easy_strerror(rc));

	/* AND HTTPS IS NOT: refused by name, not degraded. */
	rc = perform("https://example.com/");
	check("curl-https-refused",
	      rc == CURLE_UNSUPPORTED_PROTOCOL,
	      curl_easy_strerror(rc));

	/* THE NARRATION, last, so the checks above have already been answered without it. */
	perform_verbose(url);
	perform_verbose("http://127.0.0.1:9/");

	curl_global_cleanup();

	printf("CURL-SMOKE RESULT ok=%d fail=%d\n", okc, failc);
	printf("CURL-SMOKE-STATUS=%d\n", failc ? 1 : 0);
	printf("CURL-SMOKE DONE\n");
	return failc ? 1 : 0;
}
