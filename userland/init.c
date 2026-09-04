/*
 * FNX /System/Tools/init — static musl x86_64 (PID 1).
 *
 * Built by `make userland64` into .build/rootfs64/System/Tools/init and
 * packed into the BFS root image. PID 1 already has fd 0/1/2 open to
 * /System/Devices/console. It mounts the virtual filesystems the
 * userland tools expect (/System/Processes, devpts under Devices) and
 * then spawns a shell on /System/Tools/sh, restarting it when it exits.
 * The layout is the FNX hierarchy (docs/fsh-proposal.md): the root has
 * only Applications/, Shared/, System/, Users/, Volumes/.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <string.h>

#define PATH_DEFAULT	"/System/Tools:/Applications"

static void try_mount(const char *fstype, const char *target)
{
	if (mount(fstype, target, fstype, 0, NULL) < 0)
		fprintf(stderr, "INIT: mount %s on %s: %m\n", fstype, target);
}

/* Fork+exec a GUI process on the desktop. If quiet, stdout/stderr go to
 * /System/Devices/null (the compositor spams per-damage lines on the
 * console). */
static void spawn_gui(const char *path, char *const argv[], char *const envp[],
		      int quiet)
{
	pid_t p = fork();

	if (p == 0) {
		if (quiet) {
			int fd = open("/System/Devices/null", O_WRONLY);

			if (fd >= 0) {
				dup2(fd, 1);
				dup2(fd, 2);
				close(fd);
			}
		}
		execve(path, argv, envp);
		_exit(127);
	}
}

/* The LVGL desktop: system compositor + two demo windows (each its own
 * app process). The compositor's mouse source: the second serial port
 * (/System/Devices/ttyS1) when a test harness feeds PS/2 packets over
 * it, else the emulated PS/2 / USB mouse (Devices/psaux, interactive
 * QEMU). */
static void start_gui(void)
{
	char *gui_env[] = { "PATH=" PATH_DEFAULT, "HOME=/",
			    "GUI_MOUSE=/System/Devices/ttyS1", NULL };
	char *sh_env[] = { "PATH=" PATH_DEFAULT, "HOME=/", "PS1=# ", NULL };

	if (access("/System/Devices/ttyS1", F_OK) == 0) {
		spawn_gui("/System/Tools/compositor",
			  (char *const[]) { "compositor", NULL },
			  gui_env, 1);
	} else {
		spawn_gui("/System/Tools/compositor",
			  (char *const[]) { "compositor", NULL },
			  sh_env, 1);
	}
	spawn_gui("/System/Tools/lv_demo",
		  (char *const[]) { "lv_demo", "A", "40", "40", NULL },
		  sh_env, 0);
	spawn_gui("/System/Tools/lv_demo",
		  (char *const[]) { "lv_demo", "B", "760", "320", NULL },
		  sh_env, 0);
}

int main(void)
{
	pid_t pid;

	puts("INIT: FNX userland alive");
	fflush(stdout);

	/* the mount points exist in the root image (Makefile userland64) */
	try_mount("proc", "/System/Processes");
	try_mount("devpts", "/System/Devices/pts");

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
			char *envp[] = { "PATH=" PATH_DEFAULT, "HOME=/", "PS1=# ", NULL };
			execve("/System/Tools/sh", argv, envp);
			perror("INIT: execve /System/Tools/sh");
			_exit(127);
		}
		waitpid(pid, NULL, 0);
		puts("INIT: shell exited, restarting");
		fflush(stdout);
	}
	return 0;
}
