/* sec_test.c — kernel security regression probe for FNX round-3 audit.
 * Runs as root, spawns a sleeping root child, drops to uid 1000 and
 * probes the fixed privilege/escalation surfaces. Prints PASS/FAIL per
 * check; exit code = number of failures.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif
#ifndef ITIMER_REAL
#define ITIMER_REAL 0
#endif
#ifndef MREMAP_MAYMOVE
#define MREMAP_MAYMOVE 1
#endif
#define IN_CLOEXEC 02000000
#define IN_ALL_EVENTS 0x00000fff

static int fails = 0;
static int total = 0;
static pid_t rootchild = -1;

static void check(const char *what, int ok, int got, int want)
{
	total++;
	if(ok) {
		printf("PASS %s\n", what);
	} else {
		printf("FAIL %s (got %d want %d errno %d)\n", what, got, want, errno);
		fails++;
	}
}

static int sys_inotify_init1(int flags)
{
	return syscall(294, flags);
}

static int sys_inotify_add_watch(int fd, const char *path, unsigned m)
{
	return syscall(254, fd, path, m);
}

int main(void)
{
	struct itimerval itv;
	struct sockaddr_un sun;
	struct sockaddr *bogus = (struct sockaddr *)0x1;
	int s, s2, len;
	char buf[512];
	unsigned long *p;
	int i, fd, got, want;

	umask(0);	/* the sticky dir must really be 0777|S_ISVTX */

	/* background ROOT child (stays alive while we drop privileges) */
	rootchild = fork();
	if(rootchild == 0) {
		sleep(30);
		_exit(0);
	}

	/* root-phase setup: sticky dir + a root-owned file inside it */
	mkdir("/tmp", 0777);
	mkdir("/tmp/sticky", 0777 | S_ISVTX);
	fd = open("/tmp/sticky/rootfile", O_CREAT | O_WRONLY, 0666);
	if(fd >= 0) {
		write(fd, "root", 4);
		close(fd);
	}
	/* a directory that must be un-unlinkable by a plain unlink */
	mkdir("/tmp/dirtest", 0755);
	/* a long symlink for the readlink bound check */
	symlink("/aaaaaaaaaa/bbbbbbbbbb/cccccccccc/dddddddddd/eeeeeeeeee/ffffffffff", "/tmp/lnk");

	/* drop to uid 1000 */
	if(setgid(1000) || setuid(1000)) {
		printf("FAIL setuid-drop (errno %d)\n", errno);
		fails++;
	}

	/* 1. raw/packet sockets are root-only */
	errno = 0;
	got = socket(AF_INET, SOCK_RAW, 0);
	check("socket(AF_INET, SOCK_RAW) EPERM", got < 0 && errno == EPERM, got, -1);
	errno = 0;
	got = socket(AF_PACKET, SOCK_DGRAM, 0);
	check("socket(AF_PACKET) EPERM", got < 0 && errno == EPERM, got, -1);
	errno = 0;
	got = socket(AF_UNIX, SOCK_STREAM, 0);
	check("socket(AF_UNIX) allowed", got >= 0, got, -1);
	if(got >= 0) {
		close(got);
	}

	/* 2. unlinkat on a directory must fail */
	errno = 0;
	got = syscall(263, AT_FDCWD, "/tmp/dirtest", 0);	/* unlinkat */
	check("unlinkat(directory) EPERM", got < 0 && (errno == EPERM || errno == EISDIR), got, -1);

	/* 3. sticky-dir protection: cannot unlink root's file */
	errno = 0;
	got = syscall(263, AT_FDCWD, "/tmp/sticky/rootfile", 0);
	check("sticky unlink(root file) EPERM", got < 0 && errno == EPERM, got, -1);
	/* own file in the sticky dir: allowed */
	fd = open("/tmp/sticky/myfile", O_CREAT | O_WRONLY, 0666);
	write(fd, "me", 2);
	close(fd);
	errno = 0;
	got = syscall(263, AT_FDCWD, "/tmp/sticky/myfile", 0);
	check("sticky unlink(own file) ok", got == 0, got, 0);

	/* 4. /proc/PID privacy: another user's PID dir is closed */
	errno = 0;
	got = open("/proc/self/stat", O_RDONLY);
	check("read own /proc/self/stat", got >= 0, got, -1);
	if(got >= 0) {
		close(got);
	}
	errno = 0;
	{
		char p[64];
		snprintf(p, sizeof(p), "/proc/%d/stat", (int)rootchild);
		got = open(p, O_RDONLY);
		check("other-user /proc/PID/stat EACCES", got < 0 && (errno == EACCES || errno == ENOENT), got, -1);
		if(got >= 0) {
			close(got);
		}
	}

	/* 5. setitimer(NULL) cancels; getitimer(NULL) is EFAULT (no panic) */
	errno = 0;
	got = setitimer(ITIMER_REAL, NULL, NULL);
	check("setitimer(NULL) ok", got == 0, got, 0);
	errno = 0;
	got = getitimer(ITIMER_REAL, NULL);
	check("getitimer(NULL) EFAULT", got < 0 && errno == EFAULT, got, -1);
	errno = 0;
	got = getitimer(ITIMER_REAL, &itv);
	check("getitimer(valid) ok", got == 0, got, 0);
	errno = 0;
	got = setitimer(ITIMER_REAL, &itv, NULL);
	check("setitimer(valid) ok", got == 0, got, 0);

	/* 6. inotify_add_watch with a bogus pathname: EFAULT, no panic */
	errno = 0;
	fd = sys_inotify_init1(0);
	got = fd >= 0 ? sys_inotify_add_watch(fd, (const char *)0x1, IN_ALL_EVENTS) : -1;
	check("inotify_add_watch(bogus path) EFAULT", got < 0 && errno == EFAULT, got, -1);
	if(fd >= 0) {
		close(fd);
	}

	/* 7. getsockname with a bogus addr: EFAULT, no panic */
	s = socket(AF_INET, SOCK_STREAM, 0);
	errno = 0;
	len = sizeof(struct sockaddr_in);
	got = getsockname(s, bogus, &len);
	check("getsockname(bogus addr) EFAULT", got < 0 && errno == EFAULT, got, -1);

	/* 8. mremap with a huge size: ENOMEM, no vma corruption */
	p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	errno = 0;
	got = (int)mremap(p, 4096, 0x7fffffffffffUL, 0);
	check("mremap(huge) ENOMEM", got < 0 && errno == ENOMEM, got, -1);
	errno = 0;
	{
		void *q = mremap(p, 4096, 8192, 0);
		check("mremap(grow small) ok", q == p && errno == 0, (int)(long)q, -1);
		if(q == p) {
			munmap(q, 8192);
		} else if(q != MAP_FAILED) {
			munmap(q, 8192);
		} else {
			munmap(p, 4096);
		}
	}

	/* 9. dup2 beyond the fd table: EINVAL */
	errno = 0;
	got = dup2(0, 256);
	check("dup2(256) EINVAL", got < 0 && errno == EINVAL, got, -1);

	/* 10. kill with an out-of-range signal: EINVAL */
	errno = 0;
	got = kill(rootchild, 32);
	check("kill(sig 32) EINVAL", got < 0 && errno == EINVAL, got, -1);
	errno = 0;
	got = kill(rootchild, 0);
	check("kill(0) perm check ok", got == 0, got, 0);

	/* 11. readlink bound: no OOB, result capped at bufsize-1 */
	memset(buf, 0x55, sizeof(buf));
	errno = 0;
	got = (int)readlink("/tmp/lnk", buf, 16);
	check("readlink capped at 15", got == 15, got, 15);
	check("readlink NUL-terminated", buf[15] == 0, buf[15], 0);

	/* 12. unix recvfrom with a 110-byte peer sockaddr: no stack smash */
	s = socket(AF_UNIX, SOCK_DGRAM, 0);
	s2 = socket(AF_UNIX, SOCK_DGRAM, 0);
	if(s >= 0 && s2 >= 0) {
		memset(&sun, 0x41, sizeof(sun));
		sun.sun_family = AF_UNIX;
		strcpy(sun.sun_path, "/tmp/sec_recv.sock");
		bind(s, (struct sockaddr *)&sun, sizeof(sun));	/* 110-byte addrlen */
		memset(buf, 0, sizeof(buf));
		fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
		len = sizeof(sun);
		got = (int)recvfrom(s, buf, 8, 0, (struct sockaddr *)&sun, &len);
		check("recvfrom no-msg (EAGAIN)", got < 0 && errno == EAGAIN, got, -1);
		unlink("/tmp/sec_recv.sock");
		close(s);
		close(s2);
	}

	/* 13. procfs maps heap guard: thousands of vmas must not corrupt
	 * the kernel heap (the /proc read is capped at PAGE_SIZE) */
	for(i = 0; i < 1500; i++) {
		mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	}
	fd = open("/proc/self/maps", O_RDONLY);
	got = -2;
	if(fd >= 0) {
		got = (int)read(fd, buf, sizeof(buf));
		close(fd);
	}
	check("read /proc/self/maps ok", got > 0, got, -1);

	/* 14. the kernel must still be healthy: normal file io works */
	fd = open("/tmp/sticky/myfile", O_CREAT | O_WRONLY, 0600);
	got = write(fd, "still-alive", 11);
	close(fd);
	check("post-probe file write ok", got == 11, got, 11);

	kill(rootchild, SIGKILL);
	waitpid(rootchild, NULL, 0);

	printf("SEC_TEST: %d/%d passed\n", total - fails, total);
	return fails ? 1 : 0;
}
