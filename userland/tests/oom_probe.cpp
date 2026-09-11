/* oom_probe.cpp — S4.3d: a process that eats memory until the kernel can
 * no longer fault a page in, to pin down what the fault path must do with
 * that: SAY SO (a report naming the process and the address) and hand the
 * process SIGBUS - which is catchable, so this probe can shut itself down -
 * instead of the silent SIGKILL it used to be (a session used to vanish
 * with not one line in the log saying which process died or why).
 *
 * The kernel names this process in its report through argv0, so the path
 * matters: run it as /System/Shared/tests/oom_probe.
 */
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>

static const unsigned long CHUNK = 4UL << 20;

static void
on_bus(int sig)
{
	(void) sig;
	printf("OOM-PROBE-SIGBUS\n");
	fflush(stdout);
	_exit(3);
}

int
main()
{
	unsigned long total = 0;

	setvbuf(stdout, nullptr, _IOLBF, 0);
	printf("OOM-PROBE-START\n");
	fflush(stdout);
	signal(SIGBUS, on_bus);

	for (;;) {
		char *p = (char *) mmap(nullptr, CHUNK, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (p == MAP_FAILED) {
			printf("OOM-PROBE-MMAP-FAILED %lu MB\n", total >> 20);
			fflush(stdout);
			_exit(4);
		}
		for (unsigned long o = 0; o < CHUNK; o += 4096) {
			*(volatile char *) (p + o) = 1;	/* fault it in */
		}
		total += CHUNK;
		if ((total & ((16UL << 20) - 1)) == 0) {
			printf("OOM-PROBE-TOUCHED %lu MB\n", total >> 20);
			fflush(stdout);
		}
		if (total > (400UL << 20)) {
			printf("OOM-PROBE-NO-FAILURE %lu MB\n", total >> 20);
			fflush(stdout);
			_exit(5);
		}
	}
}
