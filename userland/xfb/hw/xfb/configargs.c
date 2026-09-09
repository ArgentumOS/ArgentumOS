/*
 * hw/xfb/configargs.c — FNX: Xfb configuration via libconfig.
 *
 * Every Xfb command-line option has a config key in the system.xfb
 * domain whose value is the option's *default* (docs/design/x11-xvfb-fb-plan.md,
 * "Update (decided): Xfb configuration via libconfig"). A real
 * command-line occurrence of an option suppresses its config-derived
 * occurrence (per-key override), then the server's own last-wins parser
 * semantics apply to the real arguments.
 *
 * Implementation: build a config-derived argv prefix and let the
 * untouched ProcessCommandLine() parse config-argv + real argv. This
 * file is deliberately free of X headers so it can be host-compiled for
 * unit tests (tools/xfb_config_test.c).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libconfig.h>

#define XFB_DOMAIN "system.xfb"

/* system.xfb `shadow` key — Xfb shadow (S2 of
 * docs/design/xfb-shadow-buffer-plan.md): render into an off-screen
 * shadow and damage-flush to /dev/fb0 (frame-atomic scanout), default
 * ON. Owned by InitOutput.c (extern int); set here before the screens
 * init. (configargs.c has no X headers, so int not Bool.) */
int xfb_shadow_config = 1;

static void
xfb_apply_shadow_key(char **keys, config_value_t *values, size_t nkeys)
{
	size_t i;

	for (i = 0; i < nkeys; i++) {
		if (strcmp(keys[i], "shadow"))
			continue;
		if (values[i].type != CONFIG_TYPE_BOOL)
			continue;
		xfb_shadow_config = values[i].v.boolean;
		break;
	}
}

/*
 * Emission kinds (docs/design/x11-xvfb-fb-plan.md "Arity table"):
 *   K_FLAG    boolean key: emit `on` when true, nothing when false.
 *   K_PAIR    boolean key with an explicit off token: emit `on`/`off`.
 *   K_ARG     emit `on` + the value text (string/int/float).
 *   K_SCREEN  emit `-screen` + fixed "0" + the value text.
 *   K_DISPLAY emit ":" + the value text (the positional :N display).
 *             Its "on" token is never scanned; presence is a ":"-arg.
 */
enum {
	K_FLAG,
	K_PAIR,
	K_ARG,
	K_SCREEN,
	K_DISPLAY,
};

struct optkey {
	const char *key;
	int kind;
	const char *on;		/* token when true / the flag for K_ARG */
	const char *off;	/* K_PAIR: token when false */
};

static const struct optkey optkeys[] = {
	/* --- display and DDX options (surface FNX uses) --- */
	{ "display",	K_DISPLAY,	":",		NULL },
	{ "screen",	K_SCREEN,	"-screen",	NULL },
	{ "pixdepths",	K_ARG,		"-pixdepths",	NULL },
	{ "render",	K_PAIR,		"+render",	"-render" },
	{ "blackpixel",	K_ARG,		"-blackpixel",	NULL },
	{ "whitepixel",	K_ARG,		"-whitepixel",	NULL },
	{ "linebias",	K_ARG,		"-linebias",	NULL },
	{ "fbdir",	K_ARG,		"-fbdir",	NULL },
	{ "shmem",	K_FLAG,		"-shmem",	NULL },
	{ "ac",		K_FLAG,		"-ac",		NULL },
	/* --- core options with real effect under Xfb --- */
	{ "auth",	K_ARG,		"-auth",	NULL },
	{ "nolisten",	K_ARG,		"-nolisten",	NULL },
	{ "listen",	K_ARG,		"-listen",	NULL },
	{ "reset",	K_PAIR,		"-noreset",	"-reset" },
	{ "fp",		K_ARG,		"-fp",		NULL },
	{ "dpi",	K_ARG,		"-dpi",		NULL },
	{ "cc",		K_ARG,		"-cc",		NULL },
	{ "deferglyphs",	K_ARG,		"-deferglyphs",	NULL },
	{ "background",	K_ARG,		"-background",	NULL },
	{ "maxclients",	K_ARG,		"-maxclients",	NULL },
	{ "maxbigreqsize", K_ARG,	"-maxbigreqsize", NULL },
	{ "seat",	K_ARG,		"-seat",	NULL },
	{ "fakescreenfps", K_ARG,	"-fakescreenfps", NULL },
	{ "audit",	K_ARG,		"-audit",	NULL },
	{ "a",		K_ARG,		"-a",		NULL },
	{ "t",		K_ARG,		"-t",		NULL },
	{ "f",		K_ARG,		"-f",		NULL },
	{ "p",		K_ARG,		"-p",		NULL },
	{ "s",		K_ARG,		"-s",		NULL },
	{ "displayfd",	K_ARG,		"-displayfd",	NULL },
	/* --- parseable but vestigial under Xfb (kept so every option
	 * obeys the config rule; docs table C) --- */
	{ "br",		K_FLAG,		"-br",		NULL },
	{ "bs",		K_PAIR,		"+bs",		"-bs" },
	{ "byteswappedclients", K_PAIR,	"+byteswappedclients",
							"-byteswappedclients" },
	{ "core",	K_FLAG,		"-core",	NULL },
	{ "nocursor",	K_FLAG,		"-nocursor",	NULL },
	{ "dpms",	K_PAIR,		"dpms",		"-dpms" },
	{ "iglx",	K_PAIR,		"+iglx",	"-iglx" },
	{ "nolock",	K_FLAG,		"-nolock",	NULL },
	{ "pn",		K_PAIR,		"-pn",		"-nopn" },
	{ "pogo",	K_FLAG,		"-pogo",	NULL },
	{ "autorepeat",	K_PAIR,		"r",		"-r" },
	{ "retro",	K_FLAG,		"-retro",	NULL },
	{ "terminate",	K_FLAG,		"-terminate",	NULL },
	{ "tst",	K_FLAG,		"-tst",		NULL },
	{ "blanking",	K_PAIR,		"v",		"-v" },
	{ "wr",		K_FLAG,		"-wr",		NULL },
	{ "dumbSched",	K_FLAG,		"-dumbSched",	NULL },
	{ "sigstop",	K_FLAG,		"-sigstop",	NULL },
	{ "schedInterval", K_ARG,	"-schedInterval", NULL },
	{ "schedMax",	K_ARG,		"-schedMax",	NULL },
	{ "xinerama",	K_PAIR,		"+xinerama",	"-xinerama" },
	{ "disablexineramaextension", K_FLAG, "-disablexineramaextension",
								NULL },
};

#define NOPTKEYS (sizeof(optkeys) / sizeof(optkeys[0]))

/* strdup is fine here (userland POSIX build). */
static char *text_of(const config_value_t *v)
{
	char buf[96];

	switch (v->type) {
	case CONFIG_TYPE_STRING:
		return strdup(v->v.string);
	case CONFIG_TYPE_INT:
		snprintf(buf, sizeof(buf), "%lld", (long long)v->v.integer);
		return strdup(buf);
	case CONFIG_TYPE_FLOAT:
		snprintf(buf, sizeof(buf), "%g", v->v.floating);
		return strdup(buf);
	default:
		return NULL;
	}
}

/* Is `tok` present in the real argv (positions 1..argc-1)? */
static int token_in_argv(const char *tok, int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!argv[i]) {
			break;
		}
		if (!strcmp(argv[i], tok)) {
			return 1;
		}
	}
	return 0;
}

/* Is a positional :N display present in the real argv? */
static int display_in_argv(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!argv[i]) {
			break;
		}
		if (argv[i][0] == ':') {
			return 1;
		}
	}
	return 0;
}

/*
 * Build the config-derived argv. On entry *argcp/*argvp hold the real
 * command line; on return they may point at a new, long-lived array
 * (config tokens first, then the real argv). Falls back to the original
 * argv unchanged on any config problem (fail-open: a broken
 * system.xfb.conf must never stop X from starting).
 */
void
xfb_config_args(int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	char **keys = NULL;
	config_value_t *values = NULL;
	size_t nkeys = 0, i;
	config_err_t e;
	char **out = NULL;
	char *tok[4];
	int ntok, total = 0, k;

	e = config_get_all(XFB_DOMAIN, NULL, &keys, &values, &nkeys);
	if (e == CONFIG_ERR_NOT_FOUND) {
		return;			/* domain not installed: server defaults */
	}
	if (e != CONFIG_OK) {
		fprintf(stderr, "Xfb: system.xfb unreadable (%s); "
			"using command line only\n", config_strerror(e));
		return;
	}
	xfb_apply_shadow_key(keys, values, nkeys);

	/* Emit in table order; suppress a key whose option appears in the
	 * real argv (per-key override). */
	for (k = 0; k < (int)NOPTKEYS; k++) {
		const struct optkey *ok = &optkeys[k];
		int found = -1;

		for (i = 0; i < nkeys; i++) {
			if (!strcmp(keys[i], ok->key)) {
				found = (int)i;
				break;
			}
		}
		if (found < 0) {
			continue;
		}

		ntok = 0;
		if (ok->kind == K_DISPLAY) {
			if (display_in_argv(argc, argv)) {
				continue;
			}
			tok[0] = text_of(&values[found]);
			if (!tok[0] || !*tok[0]) {
				continue;
			}
			/* ":" + value into one token */
			{
				char *d = malloc(strlen(tok[0]) + 2);

				if (!d) {
					continue;
				}
				d[0] = ':';
				strcpy(d + 1, tok[0]);
				free(tok[0]);
				tok[0] = d;
			}
			ntok = 1;
		} else if (ok->kind == K_SCREEN) {
			if (token_in_argv("-screen", argc, argv)) {
				continue;
			}
			tok[0] = strdup("-screen");
			tok[1] = strdup("0");
			tok[2] = text_of(&values[found]);
			if (!tok[0] || !tok[1] || !tok[2] || !*tok[2]) {
				free(tok[0]);
				free(tok[1]);
				free(tok[2]);
				continue;	/* skip this key only */
			}
			ntok = 3;
		} else if (ok->kind == K_ARG) {
			if (token_in_argv(ok->on, argc, argv)) {
				continue;
			}
			tok[0] = strdup(ok->on);
			tok[1] = text_of(&values[found]);
			if (!tok[1] || !*tok[1]) {
				continue;
			}
			ntok = 2;
		} else {		/* K_FLAG / K_PAIR: need a bool */
			if (values[found].type != CONFIG_TYPE_BOOL) {
				fprintf(stderr, "Xfb: system.xfb %s should be "
					"a boolean, ignored\n", ok->key);
				continue;
			}
			if (token_in_argv(ok->on, argc, argv) ||
			    (ok->off && token_in_argv(ok->off, argc, argv))) {
				continue;
			}
			if (!values[found].v.boolean) {
				if (!ok->off) {
					continue;	/* plain flag off */
				}
				tok[0] = strdup(ok->off);
			} else {
				tok[0] = strdup(ok->on);
			}
			ntok = 1;
		}

		if (!ntok) {
			continue;
		}
		for (i = 0; i < (size_t)ntok; i++) {
			if (!tok[i]) {
				ntok = 0;
				break;
			}
		}
		if (!ntok) {
			continue;
		}
		{
			char **nargv = realloc(out,
					      (size_t)(total + ntok + argc) *
					      sizeof(char *));

			if (!nargv) {
				goto done;
			}
			out = nargv;
		}
		for (i = 0; i < (size_t)ntok; i++) {
			out[total + i] = tok[i];
		}
		total += ntok;
	}

	/* warn about configured keys that are not Xfb options */
	for (i = 0; i < nkeys; i++) {
		int known = 0, k2;

		for (k2 = 0; k2 < (int)NOPTKEYS; k2++) {
			if (!strcmp(optkeys[k2].key, keys[i])) {
				known = 1;
				break;
			}
		}
		if (!known) {
			fprintf(stderr, "Xfb: unknown system.xfb key '%s' "
				"ignored\n", keys[i]);
		}
	}

	if (!total) {
		free(out);
		goto done;
	}

	/* out = [argv[0], config tokens..., argv[1..argc-1], NULL] */
	{
		char **full = malloc((size_t)(total + argc + 1) *
				     sizeof(char *));
		int n;

		if (!full) {
			free(out);
			goto done;
		}
		full[0] = argv[0];
		for (i = 0; i < (size_t)total; i++) {
			full[1 + i] = out[i];
		}
		free(out);
		for (n = 1; n < argc; n++) {
			full[1 + total + (n - 1)] = argv[n];
		}
		full[total + argc] = NULL;
		*argcp = total + argc;
		*argvp = full;
		fprintf(stderr, "Xfb: applied %d config option token(s) from "
			"%s\n", total, XFB_DOMAIN);
	}
done:
	config_free_keys(keys, values, nkeys);
}
