/*
 * Fiwix64 /sbin/init — static musl i386 (PID 1).
 *
 * Built by `make userland` into .build/rootfs/sbin/init and packed into the
 * minix-v1 initrd by tools/mkinitrd.py. PID 1 already has fd 0/1/2 open to
 * /dev/console, so this just spawns a shell on /bin/sh and restarts it when
 * it exits.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

int main(void)
{
	pid_t pid;

	puts("INIT: Fiwix64 initrd alive");
	fflush(stdout);

	for (;;) {
		pid = fork();
		if (pid < 0) {
			perror("INIT: fork");
			sleep(1);
			continue;
		}
		if (pid == 0) {
			char *argv[] = { "sh", NULL };
			char *envp[] = { "PATH=/bin:/sbin", "HOME=/", "PS1=# ", NULL };
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
