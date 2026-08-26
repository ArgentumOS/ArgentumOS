/*
 * FNX /sbin/init — static musl x86_64 (PID 1).
 *
 * Built by `make userland64` into .build/rootfs64/sbin/init and packed into
 * the ext2 root image. PID 1 already has fd 0/1/2 open to /dev/console.
 * It mounts the virtual filesystems the userland tools expect (/proc, /dev/pts)
 * and then spawns a shell on /bin/sh, restarting it when it exits.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <stdlib.h>

static void try_mount(const char *fstype, const char *target)
{
	if (mount(fstype, target, fstype, 0, NULL) < 0)
		fprintf(stderr, "INIT: mount %s on %s: %m\n", fstype, target);
}

int main(void)
{
	pid_t pid;

	puts("INIT: FNX initrd alive");
	fflush(stdout);

	/* the mount points exist in the root image (Makefile userland64) */
	try_mount("proc", "/proc");
	try_mount("devpts", "/dev/pts");

	for (;;) {
		pid = fork();
		if (pid < 0) {
			perror("INIT: fork");
			sleep(1);
			continue;
		}
		if (pid == 0) {
			char *argv[] = { "sh", NULL };
			char *envp[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", "HOME=/", "PS1=# ", NULL };
			execve("/bin/sh", argv, envp);
			perror("INIT: execve /bin/sh");
			_exit(127);
		}
		waitpid(pid, NULL, 0);
		puts("INIT: shell exited, restarting");
		fflush(stdout);
	}
	return 0;
}
