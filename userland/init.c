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
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <string.h>

static void try_mount(const char *fstype, const char *target)
{
	if (mount(fstype, target, fstype, 0, NULL) < 0)
		fprintf(stderr, "INIT: mount %s on %s: %m\n", fstype, target);
}

/* start the session compositor (owns /dev/fb0) + the animated demo as
 * daemons, so `make run-uefi` shows the desktop without any typing.
 * The demo runs attach-only (GUI_NO_SPAWN): init started the compositor,
 * so it must never fork a second one. */
/* the widgets_demo needs the real mouse; "nogui" on the kernel
 * cmdline boots headless for input-chain tests */
static int boot_has_nogui(void)
{
	FILE *f = fopen("/proc/cmdline", "r");
	char buf[256];

	if (!f)
		return 0;
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	return strstr(buf, "nogui") != NULL;
}

static void start_gui(void)
{
	pid_t pid;

	if (boot_has_nogui())
		return;
	if (access("/dev/fb0", F_OK) < 0)
		return;	/* no framebuffer: nothing to paint */

	pid = fork();
	if (pid == 0) {
		char *argv[] = { "compositor", NULL };
		char *envp[] = { "HOME=/", NULL };
		int fd;

		/* keep the serial console clean: the compositor is a
		 * display server, not a console client */
		fd = open("/dev/null", O_WRONLY);
		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
		}
		execve("/bin/compositor", argv, envp);
		_exit(127);
	}
	pid = fork();
	if (pid == 0) {
		char *argv[] = { "widgets_demo", NULL };
		char *envp[] = { "HOME=/", "GUI_NO_SPAWN=1", NULL };

		execve("/bin/widgets_demo", argv, envp);
		_exit(127);
	}
}

int main(void)
{
	pid_t pid;

	puts("INIT: FNX initrd alive");
	fflush(stdout);

	/* the mount points exist in the root image (Makefile userland64) */
	try_mount("proc", "/proc");
	try_mount("devpts", "/dev/pts");

	/* graphical boots: the compositor + demo paint the display */
	start_gui();

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
