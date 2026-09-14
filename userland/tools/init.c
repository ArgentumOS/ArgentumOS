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
#include <sys/ioctl.h>
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

/* The display mode the session runs in comes from the system.display
 * domain; the System scope wins over the Shared default, matching
 * libconfig's precedence for the two scopes that exist before any user
 * logs in. The physical-size keys next to width/height (width_mm/
 * height_mm) belong to the points-per-pixel path, not to this one. */
#define DISPLAY_DOMAIN_SYS	"/System/Configuration/system.display.conf"
#define DISPLAY_DOMAIN_SHARED	"/Shared/Configuration/system.display.conf"

/* fb0's mode ioctls and the struct they carry (include/fnx/fb.h:20-27;
 * spelled out here because that header pulls in kernel-only types). */
#define IO_FB_GETMODE	4	/* arg: struct fb_mode * (filled in) */
#define IO_FB_SETMODE	5	/* arg: struct fb_mode * (request) */

struct fb_mode {
	unsigned int width;
	unsigned int height;
	unsigned int bpp;
	unsigned int pitch;	/* bytes per scanline */
};

static void try_mount(const char *source, const char *fstype,
		      const char *target)
{
	if (mount(source, target, fstype, 0, NULL) < 0)
		fprintf(stderr, "INIT: mount %s on %s: %m\n", source, target);
}

/* Read `<key> = <number>` from the display domain, System scope first.
 * Returns 1 when a value was found. The key must match EXACTLY: the same
 * file carries width/height_mm beside width/height. */
static int
display_key_int(const char *key, int *out)
{
	static const char *paths[] = {
		DISPLAY_DOMAIN_SYS,
		DISPLAY_DOMAIN_SHARED,
	};
	size_t klen = strlen(key);
	unsigned int i;

	for (i = 0; i < sizeof paths / sizeof paths[0]; i++) {
		FILE *f = fopen(paths[i], "r");
		char line[256];

		if (!f)
			continue;
		while (fgets(line, sizeof line, f)) {
			char *p = line, *v, *end;
			long n;

			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == '#' || *p == '\n' || *p == '\0')
				continue;
			if (strncmp(p, key, klen) != 0)
				continue;
			if (p[klen] != '=' && p[klen] != ' ' && p[klen] != '\t')
				continue;
			v = p + klen;
			while (*v == ' ' || *v == '\t')
				v++;
			if (*v != '=')
				continue;
			v++;
			while (*v == ' ' || *v == '\t')
				v++;
			n = strtol(v, &end, 10);
			if (end == v || n < 0 || n > 65535)
				continue;
			*out = (int) n;
			fclose(f);
			return 1;
		}
		fclose(f);
	}
	return 0;
}

/* Set the framebuffer mode the session will run in (system.display:
 * display.width/height/bpp; 0 = leave the mode the firmware chose).
 *
 * This is the kernel's Bochs-dispi mode-set through /dev/fb0
 * (drivers/char/fb.c -> video_gop_set_mode), which programs
 * VBE_DISPI_XRES/YRES/BPP and re-maps the kernel's linear framebuffer,
 * so the framebuffer console follows too. The firmware's own GOP mode
 * cannot be moved to 1920x1080 from the QEMU command line: OVMF takes
 * its mode from the EDID that QEMU's VGA device generates, whose
 * preferred mode is 1280x800 unless overridden and whose standard-timing
 * list has no 1920x1080 at all.
 *
 * It has to happen BEFORE the X server starts. Switching under a live X
 * server leaves X and input working but wipes the screen content, so init
 * does it here and Xfb picks the mode up when it opens /dev/fb0.
 *
 * Nothing below is fatal: an unset key, a missing /dev/fb0 or a mode the
 * controller rejects leaves the firmware's mode in place and boot goes
 * on. */
static void
set_display_mode_from_domain(void)
{
	/* the framebuffer node lives in the device topology, the same path
	 * Xfb opens (hw/xfb/InitOutput.c); /dev/fb0 is only a fallback */
	static const char *fbpaths[] = {
		"/System/Devices/Display/fb0",
		"/dev/fb0",
	};
	struct fb_mode cur, req;
	int w = 0, h = 0, bpp = 0;
	int fd = -1;
	unsigned int i;

	for (i = 0; i < sizeof fbpaths / sizeof fbpaths[0]; i++) {
		fd = open(fbpaths[i], O_RDWR);
		if (fd >= 0)
			break;
	}
	if (fd < 0) {
		fprintf(stderr, "INIT: display: %s: %m (no framebuffer mode set)\n",
			fbpaths[0]);
		return;
	}
	memset(&cur, 0, sizeof cur);
	if (ioctl(fd, IO_FB_GETMODE, &cur) < 0) {
		fprintf(stderr, "INIT: display: GETMODE: %m\n");
		close(fd);
		return;
	}
	display_key_int("width", &w);
	display_key_int("height", &h);
	display_key_int("bpp", &bpp);
	if (w == 0 || h == 0 || bpp == 0) {
		printf("INIT: display: keeping the firmware mode %ux%u %ubpp "
		       "(set display.width/height/bpp in %s to change it)\n",
		       cur.width, cur.height, cur.bpp, DISPLAY_DOMAIN_SYS);
		fflush(stdout);
		close(fd);
		return;
	}
	memset(&req, 0, sizeof req);
	req.width = (unsigned int) w;
	req.height = (unsigned int) h;
	req.bpp = (unsigned int) bpp;
	if (ioctl(fd, IO_FB_SETMODE, &req) < 0) {
		fprintf(stderr, "INIT: display: SETMODE %ux%ux%d: %m "
			"(keeping %ux%u %ubpp)\n", w, h, bpp,
			cur.width, cur.height, cur.bpp);
		close(fd);
		return;
	}
	memset(&cur, 0, sizeof cur);
	if (ioctl(fd, IO_FB_GETMODE, &cur) < 0) {
		fprintf(stderr, "INIT: display: GETMODE after SETMODE: %m\n");
		close(fd);
		return;
	}
	printf("INIT: display: mode %ux%u %ubpp pitch %u (asked for %ux%ux%d, "
	       "before the session starts)\n", cur.width, cur.height, cur.bpp,
	       cur.pitch, w, h, bpp);
	fflush(stdout);
	close(fd);
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

/* Which desktop session init should run, from /System/Configuration/
 * session.conf (the standard image writes `desktop = "kestrel"`; the
 * variant images write "xfb" / "uitest" / "zoo"). S5.1: the DEFAULT is
 * the Argentum desktop — a missing file or an unknown value boots
 * Kestrel, not the xdraw+xkey demo clients. The demo is still reachable
 * by name (`make run-xfb`, `desktop = "xfb"`). */
enum session_kind {
	SESSION_XFB,
	SESSION_UITEST,
	SESSION_ZOO,
	SESSION_KESTREL
};

static enum session_kind read_session(void)
{
	FILE *f;
	char line[256];
	enum session_kind kind = SESSION_KESTREL;	/* S5.1: the desktop */

	f = fopen("/System/Configuration/session.conf", "r");
	if (!f)
		return kind;
	while (fgets(line, sizeof line, f)) {
		char *p, *v, *end;

		for (p = line; *p == ' ' || *p == '\t'; p++)
			;
		if (*p == '#' || !*p)
			continue;
		if (strncmp(p, "desktop", 7) ||
		    (p[7] != '=' && p[7] != ' ' && p[7] != '\t'))
			continue;
		v = p + 7;
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
		if (strcmp(v, "uitest") == 0)
			kind = SESSION_UITEST;
		else if (strcmp(v, "zoo") == 0)
			kind = SESSION_ZOO;
		else if (strcmp(v, "kestrel") == 0)
			kind = SESSION_KESTREL;
		break;
	}
	fclose(f);
	return kind;
}

/* The X11 desktop: Xfb owns /dev/fb0 and is the one graphics path. The
 * demo clients retry XOpenDisplay until the server is up (Xfb takes a
 * while to come up under TCG), so the console shell is not gated on the
 * server. Which client runs after Xfb comes from session.conf: the
 * default demo desktop (xdraw + xkey), the uitest board
 * (theme_chrome, the S1.x acceptance probe), or the Widget Zoo
 * bundle (S2.2+S2.3's control catalog; desktop = "zoo"). */
static void start_xfb(void)
{
	/* the server execs xkbcomp to compile the keymap at startup, so its
	 * PATH must reach System/Shared/X11/bin */
	char *xpath = "PATH=" PATH_DEFAULT ":/System/Shared/X11/bin";
	char *gui_env[] = { xpath, "HOME=/", NULL };
	char *dpy_env[] = { xpath, "HOME=/", "DISPLAY=:0", NULL };
	enum session_kind session = read_session();
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
	/* spawn the session client right away: it retries XOpenDisplay
	 * until the server is ready (Xfb takes a while to come up under
	 * TCG), so the console shell is not gated on the server */
	if (session == SESSION_UITEST) {
		spawn_gui("/System/Shared/tests/theme_chrome",
			  (char *const[]) { "theme_chrome", NULL }, dpy_env);
		puts("XDESK: uitest session launching (theme_chrome on :0)");
	} else if (session == SESSION_ZOO) {
		/* S4: the zoo is the reference app — it runs UNDER
		 * Kestrel (spawned first so its MapRequest is
		 * redirected; if it maps first, Kestrel's manage-existing
		 * pass picks it up).  S5.2d: the app is a BUNDLE now, so
		 * the session reaches it at its payload — the same path
		 * the dock resolves from the manifest. */
		spawn_gui("/System/Tools/kestrel",
			  (char *const[]) { "kestrel", NULL }, dpy_env);
		spawn_gui("/Applications/Widget Zoo.app/bin/WidgetZoo",
			  (char *const[]) { "WidgetZoo", NULL }, dpy_env);
		puts("XDESK: zoo session launching (kestrel WM + the Widget Zoo "
		     "bundle on :0)");
	} else if (session == SESSION_KESTREL) {
		spawn_gui("/System/Tools/kestrel",
			  (char *const[]) { "kestrel", NULL }, dpy_env);
		/* W0a: the desktop shell app, which owns the surface (the
		 * wallpaper). AFTER the WM for the same reason the zoo is:
		 * spawned second, its MapRequest is redirected and the WM
		 * recognises it as the desktop; spawned first, the WM's
		 * manage-existing pass would frame it like any client. */
		spawn_gui("/Applications/Workspace.app/bin/Workspace",
			  (char *const[]) { "Workspace", NULL }, dpy_env);
		puts("XDESK: kestrel session launching (kestrel WM + the "
		     "Workspace shell on :0)");
	} else {
		spawn_gui("/System/Shared/X11/bin/xdraw",
			  (char *const[]) { "xdraw", "100", "100", "400", "300", NULL },
			  dpy_env);
		spawn_gui("/System/Shared/X11/bin/xkey",
			  (char *const[]) { "xkey", NULL }, dpy_env);
		puts("XDESK: Xfb desktop launching (xdraw + xkey retry until :0 is up)");
	}
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

	/* the mode the session runs in, before anything opens /dev/fb0 */
	set_display_mode_from_domain();

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
