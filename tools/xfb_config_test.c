/* xfb_config_test.c — host test for Xfb's config-argv synthesis
 * (userland/xfb/hw/xfb/configargs.c, domain system.xfb).
 *
 * Run with $FNX_CONFIG_ROOT pointing at a scratch tree:
 *   cc -D_DEFAULT_SOURCE -Iinclude hw/xfb/configargs.c userland/libconfig.c \
 *      tools/xfb_config_test.c -o /tmp/xfb_cfg_test
 *   mkdir -p /tmp/cfg && /tmp/xfb_cfg_test /tmp/cfg
 * Prints PASS/FAIL per check; exit status = number of failures.
 */
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libconfig.h>

void xfb_config_args(int *argcp, char ***argvp);

#define DOM "system.xfb"

static int fails;

static void fail(const char *what)
{
	printf("FAIL: %s\n", what);
	fails++;
}

static void pass(const char *what)
{
	printf("PASS: %s\n", what);
}

static int mkdir_p(const char *path)
{
	char tmp[600];
	size_t i;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (i = 1; i < strlen(tmp); i++) {
		if (tmp[i] == '/') {
			tmp[i] = '\0';
			if (mkdir(tmp, 0755) && errno != EEXIST)
				return -1;
			tmp[i] = '/';
		}
	}
	if (mkdir(tmp, 0755) && errno != EEXIST)
		return -1;
	return 0;
}

static void write_conf(const char *root, const char *scope_dir,
		       const char *body)
{
	char path[700], dir[700];
	char *slash;
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s/%s.conf", root, scope_dir, DOM);
	snprintf(dir, sizeof(dir), "%s", path);
	slash = strrchr(dir, '/');
	if (slash)
		*slash = '\0';
	if (mkdir_p(dir)) {
		fail("cannot mkdir conf dir");
		exit(2);
	}
	f = fopen(path, "w");
	if (!f) {
		fail("cannot write conf");
		exit(2);
	}
	fputs(body, f);
	fclose(f);
}

/* Join argv[1..argc-1] with spaces for comparison. */
static void argv_join(int argc, char **argv, char *buf, size_t bufsz)
{
	int i;

	buf[0] = '\0';
	for (i = 1; i < argc; i++) {
		if (i > 1)
			strncat(buf, " ", bufsz - strlen(buf) - 1);
		strncat(buf, argv[i], bufsz - strlen(buf) - 1);
	}
}

static void check(const char *what, int argc, char **argv,
		  const char *expected)
{
	char got[1024];
	int nargc = argc;
	char **nargv = argv;

	xfb_config_args(&nargc, &nargv);
	argv_join(nargc, nargv, got, sizeof(got));
	if (strcmp(got, expected)) {
		char msg[1200];

		snprintf(msg, sizeof(msg), "%s: got '%s' want '%s'",
			 what, got, expected);
		fail(msg);
	} else {
		pass(what);
	}
}

int main(int argc, char *argv[])
{
	const char *root;
	char d[700];
	struct passwd *pw;
	char *orig_argv[] = { "Xfb", NULL };
	char *argv_ac[] = { "Xfb", "-ac", NULL };
	char *argv_disp[] = { "Xfb", ":0", NULL };
	char *argv_screen[] = { "Xfb", "-screen", "0", "1280x1024x24", NULL };
	char *argv_dpi[] = { "Xfb", "-dpi", "100", NULL };

	if (argc < 2) {
		fprintf(stderr, "usage: %s <scratch-config-root>\n", argv[0]);
		return 2;
	}
	root = argv[1];

	pw = getpwuid(getuid());
	if (!pw) {
		fprintf(stderr, "no passwd entry\n");
		return 2;
	}
	setenv("FNX_CONFIG_ROOT", root, 1);

	/* system scope: the shipped defaults
	 * (userland/configuration/system.xfb.conf) */
	write_conf(root, "System/Configuration",
		   "display = \"0\"\nac = true\nnolisten = \"tcp\"\n");

	/* display ":0" is emitted only when the argv has no :N arg */
	check("system defaults, bare argv", 1, orig_argv,
	      ":0 -ac -nolisten tcp");
	check("cmdline -ac suppresses config ac", 2, argv_ac,
	      ":0 -nolisten tcp -ac");
	check("cmdline :0 suppresses config display", 2, argv_disp,
	      "-ac -nolisten tcp :0");

	/* user scope: dpi override merges over system defaults */
	snprintf(d, sizeof(d), "Users/%s/Configuration", pw->pw_name);
	write_conf(root, d, "dpi = 120\n");
	check("user dpi merged with system defaults", 1, orig_argv,
	      ":0 -ac -nolisten tcp -dpi 120");
	check("user dpi suppressed by cmdline -dpi", 3, argv_dpi,
	      ":0 -ac -nolisten tcp -dpi 100");

	/* user scope: screen key emits -screen 0 WxHxD unless real argv
	 * already carries -screen */
	write_conf(root, d, "screen = \"640x480x8\"\n");
	check("config screen emitted when absent", 1, orig_argv,
	      ":0 -screen 0 640x480x8 -ac -nolisten tcp");
	check("cmdline -screen suppresses config screen", 4, argv_screen,
	      ":0 -ac -nolisten tcp -screen 0 1280x1024x24");

	/* unknown config keys are ignored, not fatal */
	write_conf(root, d, "boguskey = 1\n");
	check("unknown key ignored", 1, orig_argv,
	      ":0 -ac -nolisten tcp");

	/* no domain at all: fail-open to the real argv unchanged */
	{
		char nuke[700];

		snprintf(nuke, sizeof(nuke), "%s/System/Configuration/%s.conf",
			 root, DOM);
		unlink(nuke);
		write_conf(root, d, "");
		check("absent system domain falls back to argv", 2, argv_disp,
		      ":0");
	}

	if (!fails)
		printf("ALL PASS\n");
	return fails;
}
