#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include "libconfig.h"

static int fails = 0;

static void check(int ok, const char *what)
{
	if(!ok) {
		fprintf(stderr, "FAIL: %s\n", what);
		fails++;
	}
}

static void mkdir_p(const char *path)
{
	char tmp[1024];
	size_t i;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for(i = 1; tmp[i]; i++) {
		if(tmp[i] == '/') {
			tmp[i] = '\0';
			mkdir(tmp, 0755);
			tmp[i] = '/';
		}
	}
	mkdir(tmp, 0755);
}

static void put(const char *scope, const char *domain, const char *txt)
{
	const char *root = getenv("FNX_CONFIG_ROOT");
	char p[1024];

	if(!root || !*root) root = "/";
	snprintf(p, sizeof(p), "%s/%s/Configuration", root, scope);
	mkdir_p(p);
	snprintf(p, sizeof(p), "%s/%s/Configuration/%s.conf", root, scope, domain);
	{
		FILE *f = fopen(p, "w");
		if(!f) { perror("put"); exit(1); }
		fputs(txt, f);
		fclose(f);
	}
}

static int slurp(const char *scope, const char *domain, char *buf, size_t cap)
{
	const char *root = getenv("FNX_CONFIG_ROOT");
	char p[1024];
	FILE *f;
	size_t n;

	snprintf(p, sizeof(p), "%s/%s/Configuration/%s.conf", root, scope, domain);
	f = fopen(p, "r");
	if(!f) return -1;
	n = fread(buf, 1, cap - 1, f);
	fclose(f);
	buf[n] = '\0';
	return 0;
}

int main(void)
{
	config_err_t e;
	char a[16384], b[16384];

	/* ---- canonical writer fixed point: write/read/write is
	 * byte-identical for a rules-shaped (array-of-record) domain - */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "rules");
	put("System", "rules",
		"cachedir = \"/System/Variable Data/fontconfig\"\n"
		"rules = [\n"
		"  {\n"
		"    match = pattern\n"
		"    tests = [\n"
		"      {\n"
		"        object = family\n"
		"        value = \"Sans\"\n"
		"      }\n"
		"    ]\n"
		"    edits = [\n"
		"      {\n"
		"        object = family\n"
		"        mode = assign\n"
		"        value = \"DejaVu Sans\"\n"
		"      }\n"
		"    ]\n"
		"  },\n"
		"  {\n"
		"    match = pattern\n"
		"    edits = [\n"
		"      {\n"
		"        object = family\n"
		"        mode = prepend\n"
		"        value = \"DejaVu Serif\"\n"
		"      }\n"
		"    ]\n"
		"  }\n"
		"]\n");
	e = config_set_int(CONFIG_SCOPE_SYSTEM, "rules", "rescan", 1);
	check(e == CONFIG_OK, "write into rules-shaped domain");
	if(e == CONFIG_OK) {
		check(slurp("System", "rules", a, sizeof(a)) == 0,
		      "rules domain written");
	}
	/* second write -> byte-identical */
	e = config_set_int(CONFIG_SCOPE_SYSTEM, "rules", "rescan", 1);
	check(e == CONFIG_OK, "rewrite rules-shaped domain");
	if(e == CONFIG_OK && slurp("System", "rules", b, sizeof(b)) == 0) {
		check(!strcmp(a, b), "canonical writer fixed point (byte-identical)");
	}
	if(strcmp(a, b)) {
		fprintf(stderr, "--- first ---\n%s\n--- second ---\n%s\n", a, b);
	}

	/* values survive: surgical element edit then read back */
	e = config_set_string(CONFIG_SCOPE_SYSTEM, "rules",
			      "rules[1].edits[0].value", "DejaVu Serif Bold");
	check(e == CONFIG_OK, "surgical rules[1].edits[0].value edit");
	{
		const char *s;

		e = config_get_string("rules", "rules[1].edits[0].value", &s);
		check(!e && s && !strcmp(s, "DejaVu Serif Bold"),
		      "edited value reads back");
		e = config_get_string("rules", "rules[0].tests[0].value", &s);
		check(!e && s && !strcmp(s, "Sans"),
		      "untouched element still reads");
	}
	{
		int64_t rv;
		e = config_get_int("rules", "rescan", &rv);
		check(!e && rv == 1, "flat rescan survives");
	}

	/* record value as a whole key writes nested */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "rec");
	put("System", "rec",
		"user = {\n"
		"  admin = {\n"
		"    uid = 0\n"
		"  }\n"
		"}\n");
	e = config_set_string(CONFIG_SCOPE_SYSTEM, "rec", "user.bob.gecos",
			      "Bobby");
	check(e == CONFIG_OK, "extend a record domain (M2 writer)");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "rules");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "rec");

	printf(fails ? "%d FAILURE(S)\n" : "ALL PASS\n", fails);
	return fails ? 1 : 0;
}
