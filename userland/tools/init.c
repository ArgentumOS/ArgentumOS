/*
 * FNX /System/Tools/init — dynamic musl x86_64 (PID 1).
 *
 * Dynamic since M1 (docs/design/shared-libraries-plan.md §2.4: the kernel mounts
 * the root before exec'ing init, so ld.so on the root is available; no
 * bootstrap deadlock). Built by `make userland64` into
 * .build/rootfs64/System/Tools/init and
 * packed into the AGFS root image. PID 1 already has fd 0/1/2 open to
 * /System/Devices/TTY/console. It mounts the virtual filesystems the
 * userland tools expect (/System/Processes, devpts under Devices) and
 * then spawns a shell on /System/Tools/sh, restarting it when it exits.
 * The layout is the FNX hierarchy (docs/design/fsh-proposal.md): the root has
 * only Applications/, Shared/, System/, Users/, Volumes/.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define PATH_DEFAULT	"/System/Tools:/Applications"
#define NETWORK_DOMAIN	"/System/Configuration/system.network.conf"
#define MOUNTS_DOMAIN	"/System/Configuration/system.mounts.conf"

static void try_mount(const char *source, const char *fstype,
		      const char *target)
{
	if (mount(source, target, fstype, 0, NULL) < 0)
		fprintf(stderr, "INIT: mount %s on %s: %m\n", source, target);
}

/* M5: set the kernel nodename from the network domain (plan §5.3).
 * `hostname` = value may be bare or quoted; comments (#) and other
 * assignments are ignored. A read failure or an empty value leaves the
 * kernel default ("(none)") in place. */
static void set_hostname_from_domain(void)
{
	FILE *f;
	char line[256];
	char name[128];

	f = fopen(NETWORK_DOMAIN, "r");
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		char *p, *eq, *v, *end;

		for (p = line; *p == ' ' || *p == '\t'; p++)
			;
		if (*p == '#' || !*p)
			continue;
		if (strncmp(p, "hostname", 8) ||
		    (p[8] != '=' && p[8] != ' ' && p[8] != '\t'))
			continue;
		v = p + 8;
		while (*v == ' ' || *v == '\t')
			v++;
		if (*v != '=')
			continue;
		v++;
		while (*v == ' ' || *v == '\t')
			v++;
		if (*v == '"') {
			v++;
			end = strchr(v, '"');
			if (end)
				*end = 0;
		} else {
			end = v + strlen(v);
			while (end > v && (end[-1] == '\n' || end[-1] == '\r' ||
					    end[-1] == ' ' || end[-1] == '\t'))
				*--end = 0;
		}
		if (!*v)
			break;
		if (strlen(v) >= sizeof name)
			break;
		strcpy(name, v);
		if (sethostname(name, strlen(name)) == 0) {
			fprintf(stderr, "INIT: hostname '%s' (network domain)\n",
				name);
		}
		break;
	}
	fclose(f);
}

/* M6: mount the boot table from system.mounts.conf (plan §5.5). The
 * domain is dedicated to mount records, so each top-level group is one
 * record (system.mounts.processes - no 'mount' container wrapper, the
 * domain name already says it). Record order is mount order; the mount
 * source defaults to the filesystem type. A record missing
 * fstype/target is skipped with a clear error; structural problems
 * (nesting, unbalanced braces) stop the parse with an error. The old
 * hardcoded try_mount calls are gone: the boot mount set is machine
 * config. */
static void mount_from_table(void)
{
	FILE *f;
	char line[256];
	char cur[64], fstype[64], source[256], target[256];
	int depth = 0, bad = 0;

	f = fopen(MOUNTS_DOMAIN, "r");
	if (!f) {
		fprintf(stderr, "INIT: mounts: %s: %m\n", MOUNTS_DOMAIN);
		return;
	}
	cur[0] = fstype[0] = source[0] = target[0] = 0;
	while (fgets(line, sizeof line, f)) {
		char *p, *end, *eq, *v;

		for (p = line; *p == ' ' || *p == '\t'; p++)
			;
		end = p + strlen(p);
		while (end > p && (end[-1] == '\n' || end[-1] == '\r' ||
				    end[-1] == ' ' || end[-1] == '\t'))
			*--end = 0;
		if (!*p || *p == '#')
			continue;
		if (*p == '}') {
			if (depth == 1) {	/* end of a record: mount it */
				if (fstype[0] && target[0])
					try_mount(source[0] ? source : fstype,
						  fstype, target);
				else
					fprintf(stderr,
						"INIT: mounts: record '%s': missing %s\n",
						cur, fstype[0] ? "target" : "fstype");
				cur[0] = fstype[0] = source[0] = target[0] = 0;
			}
			depth--;
			if (depth < 0) {
				fprintf(stderr,
					"INIT: mounts: unbalanced '}'\n");
				bad = 1;
				break;
			}
			continue;
		}
		eq = strchr(p, '=');
		if (!eq) {
			fprintf(stderr, "INIT: mounts: malformed line: %s\n",
				p);
			bad = 1;
			break;
		}
		*eq = 0;
		end = eq - 1;
		while (end > p && (*end == ' ' || *end == '\t'))
			*end-- = 0;
		v = eq + 1;
		while (*v == ' ' || *v == '\t')
			v++;
		if (*v == '{') {	/* record open (top level, no wrapper) */
			if (depth == 0) {
				if (strlen(p) >= sizeof cur) {
					fprintf(stderr,
						"INIT: mounts: record name too long\n");
					bad = 1;
					break;
				}
				strcpy(cur, p);
				fstype[0] = source[0] = target[0] = 0;
			} else {
				fprintf(stderr,
					"INIT: mounts: nested '%s' unsupported\n",
					p);
				bad = 1;
				break;
			}
			depth++;
			if (depth > 1) {
				bad = 1;
				break;
			}
			continue;
		}
		/* assignment inside a record */
		if (depth == 1) {
			char *dst = !strcmp(p, "fstype") ? fstype :
				    !strcmp(p, "source") ? source :
				    !strcmp(p, "target") ? target : NULL;
			char *w;

			if (!dst) {
				fprintf(stderr,
					"INIT: mounts: record '%s': unknown field '%s'\n",
					cur, p);
				bad = 1;
				break;
			}
			w = dst;
			if (*v == '"') {
				v++;
				while (*v && *v != '"') {
					if (*v == '\\' && v[1])
						v++;
					if (w < dst + 250)
						*w++ = *v;
					v++;
				}
			} else {
				while (*v && *v != '#' && w < dst + 250)
					*w++ = *v++;
			}
			*w = 0;
		}
	}
	if (!bad && depth) {
		fprintf(stderr, "INIT: mounts: unbalanced '{'\n");
		bad = 1;
	}
	fclose(f);
}

/* Fork+exec a desktop client. */
static void spawn_gui(const char *path, char *const argv[], char *const envp[])
{
	pid_t p = fork();

	if (p == 0) {
		execve(path, argv, envp);
		_exit(127);
	}
}

/* The X11 desktop: Xfb owns /dev/fb0 and is the one graphics path. The
 * demo clients retry XOpenDisplay until the server is up (Xfb takes a
 * while to come up under TCG), so the console shell is not gated on the
 * server. */
static void start_xfb(void)
{
	/* the server execs xkbcomp to compile the keymap at startup, so its
	 * PATH must reach System/Shared/X11/bin */
	char *xpath = "PATH=" PATH_DEFAULT ":/System/Shared/X11/bin";
	char *gui_env[] = { xpath, "HOME=/", NULL };
	char *dpy_env[] = { xpath, "HOME=/", "DISPLAY=:0", NULL };
	pid_t p;

	mkdir("/System/Variable Data/log", 0755);
	/* the AGFS image persists across sessions: a stale lock from a
	 * killed/previous Xfb would make the fresh server refuse to start
	 * ("Server is already active for display 0") */
	unlink("/System/Temporary Files/.X0-lock");
	unlink("/System/Temporary Files/.X11-unix/X0");
	p = fork();

	if (p == 0) {
		int fd = open("/System/Variable Data/log/Xfb.log",
			      O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);
		}
		execve("/System/Shared/X11/bin/Xfb",
		       (char *const[]) { "Xfb", ":0", "-ac", NULL }, gui_env);
		_exit(127);
	}
	/* spawn the demo clients right away: they retry XOpenDisplay until
	 * the server is ready (Xfb takes a while to come up under TCG), so
	 * the console shell is not gated on the server */
	spawn_gui("/System/Shared/X11/bin/xdraw",
		  (char *const[]) { "xdraw", "100", "100", "400", "300", NULL },
		  dpy_env);
	spawn_gui("/System/Shared/X11/bin/xkey",
		  (char *const[]) { "xkey", NULL }, dpy_env);
	puts("XDESK: Xfb desktop launching (xdraw + xkey retry until :0 is up)");
	fflush(stdout);
}

int main(void)
{
	pid_t pid;

	puts("INIT: FNX userland alive");
	fflush(stdout);

	/* boot mount set comes from the system.mounts domain (M6) */
	mount_from_table();

	/* the network domain is authoritative for the machine name */
	set_hostname_from_domain();

	/* the X11 desktop owns the display; it starts before the shell */
	start_xfb();

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
