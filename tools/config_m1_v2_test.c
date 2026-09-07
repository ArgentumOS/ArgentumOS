#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include "libconfig.h"

static char userdir[256] = "Users/root";

static void user_scope_dir(void)
{
	struct passwd *pw = getpwuid(getuid());

	if(pw && pw->pw_name && *pw->pw_name) {
		snprintf(userdir, sizeof(userdir), "Users/%s", pw->pw_name);
	}
}

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
	if(!tmp[0]) {
		return;
	}
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
	char p[1024], d[512];
	FILE *f;

	if(!root || !*root) {
		root = "/";
	}
	snprintf(d, sizeof(d), "%s", scope);	/* System|Shared|Users/root */
	snprintf(p, sizeof(p), "%s/%s/Configuration", root, d);
	mkdir_p(p);
	snprintf(p, sizeof(p), "%s/%s/Configuration/%s.conf", root, d, domain);
	f = fopen(p, "w");
	if(!f) {
		perror("put");
		exit(1);
	}
	fputs(txt, f);
	fclose(f);
}

int main(void)
{
	config_value_t v;
	config_value_t *items;
	size_t n;
	config_scope_t fs;
	config_err_t e;
	const char *s;

	user_scope_dir();
	/* ---- bracket addressing into an array of records ----------- */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "fonts");
	put("System", "fonts",
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

	e = config_read("fonts", "rules[1].edits[0].value", &fs, &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_STRING &&
	      v.v.string && !strcmp(v.v.string, "DejaVu Serif"),
	      "rules[1].edits[0].value == DejaVu Serif");
	if(e == CONFIG_OK) config_value_free(&v);

	e = config_read("fonts", "rules[0].tests.0.value", &fs, &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_STRING &&
	      v.v.string && !strcmp(v.v.string, "Sans"),
	      "bare-digit addressing rules[0].tests.0.value == Sans");
	if(e == CONFIG_OK) config_value_free(&v);

	e = config_read("fonts", "rules[0]", &fs, &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_RECORD,
	      "rules[0] reads as a record");
	if(e == CONFIG_OK) config_value_free(&v);

	e = config_read("fonts", "rules[9].edits[0].value", &fs, &v);
	check(e == CONFIG_ERR_NOT_FOUND, "out-of-range index is NOT_FOUND");

	/* config_get_* through a bracket path */
	e = config_get_string("fonts", "rules[0].edits[0].value", &s);
	check(!e && s && !strcmp(s, "DejaVu Sans"),
	      "config_get_string rules[0].edits[0].value");

	/* whole-array read (additive single scope == just system) */
	e = config_get_array("fonts", "rules", &items, &n);
	check(!e && n == 2, "whole rules array has 2 elements");

	/* ---- additive arrays across scopes ------------------------- */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "acc");
	config_remove_domain(CONFIG_SCOPE_USER, "acc");
	put("System", "acc", "accept = [ \"a\", \"b\" ]\n");
	put(userdir, "acc", "accept = [ \"c\" ]\n");

	e = config_get_array("acc", "accept", &items, &n);
	check(!e && n == 3 && items[0].v.string[0] == 'a' &&
	      items[1].v.string[0] == 'b' && items[2].v.string[0] == 'c',
	      "two-scope array concatenates system->user->shared");

	e = config_get_string("acc", "accept[2]", &s);
	check(!e && s && !strcmp(s, "c"),
	      "additive array index addresses user element");

	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "acc", "accept", &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_ARRAY &&
	      v.v.array.count == 2, "per-scope (system) array inspection");
	if(e == CONFIG_OK) config_value_free(&v);

	e = config_read_scope(CONFIG_SCOPE_USER, "acc", "accept", &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_ARRAY &&
	      v.v.array.count == 1, "per-scope (user) array inspection");
	if(e == CONFIG_OK) config_value_free(&v);

	/* scope missing = skipped: user+shared merge without system */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "acc");
	e = config_get_array("acc", "accept", &items, &n);
	check(!e && n == 1 && items[0].v.string[0] == 'c',
	      "missing system scope is skipped");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "acc");
	config_remove_domain(CONFIG_SCOPE_USER, "acc");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "fonts");

	/* ---- M1 writes: bracket-path element set/unset round-trip -- */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "accw");
	put("System", "accw", "dirs = [ /a, /b, /c ]\n");
	e = config_set_string(CONFIG_SCOPE_SYSTEM, "accw", "dirs[1]", "/B");
	check(e == CONFIG_OK, "config_set_string dirs[1]");

	/* bare-digit addresses are read-only (files only carry ident
	 * segments); set/unset must reject them */
	e = config_set_string(CONFIG_SCOPE_SYSTEM, "accw", "dirs.1", "/x");
	check(e == CONFIG_ERR_INVALID, "bare-digit set rejects (INVALID)");
	e = config_unset(CONFIG_SCOPE_SYSTEM, "accw", "dirs.1");
	check(e == CONFIG_ERR_INVALID, "bare-digit unset rejects (INVALID)");
	e = config_get_string("accw", "dirs[1]", &s);
	check(!e && s && !strcmp(s, "/B"), "dirs[1] reads back /B");
	e = config_get_array("accw", "dirs", &items, &n);
	check(!e && n == 3 && items[0].v.string[0]=='/' &&
	      !strcmp(items[0].v.string,"/a") && !strcmp(items[2].v.string,"/c"),
	      "dirs array intact after element write");

	e = config_unset(CONFIG_SCOPE_SYSTEM, "accw", "dirs[1]");
	check(e == CONFIG_OK, "config_unset dirs[1]");
	e = config_get_array("accw", "dirs", &items, &n);
	check(!e && n == 2 && !strcmp(items[0].v.string,"/a") &&
	      !strcmp(items[1].v.string,"/c"),
	      "dirs[1] removed; array shifted");

	/* out-of-range unset reports NOT_FOUND (matches read) */
	e = config_unset(CONFIG_SCOPE_SYSTEM, "accw", "dirs[9]");
	check(e == CONFIG_ERR_NOT_FOUND, "out-of-range unset is NOT_FOUND");

	/* unset of the last element drops the whole entry (an empty
	 * array would serialize as "" and reload as an empty string) */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "accw");
	put("System", "accw", "one = [ /x ]\n");
	e = config_unset(CONFIG_SCOPE_SYSTEM, "accw", "one[0]");
	check(e == CONFIG_OK, "unset last element");
	e = config_get_string("accw", "one", &s);
	check(e == CONFIG_ERR_NOT_FOUND,
	      "empty array entry removed after unsetting last element");

	/* record-array surgical writes are M2 (canonical writer); M1
	 * rejects cleanly */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "rw");
	put("System", "rw",
		"rules = [\n"
		"  {\n"
		"    edits = [\n"
		"      {\n"
		"        value = old\n"
		"      }\n"
		"    ]\n"
		"  }\n"
		"]\n");
	e = config_set_string(CONFIG_SCOPE_SYSTEM, "rw",
			      "rules[0].edits[0].value", "new");
	check(e == CONFIG_ERR_INVALID,
	      "record-array leaf write rejects in M1 (M2 writer)");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "accw");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "rw");

	printf(fails ? "%d FAILURE(S)\n" : "ALL PASS\n", fails);
	return fails ? 1 : 0;
}
