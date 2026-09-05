/* config_m0_test.c — M0 probe: group-record grammar (docs
 * system-config-files-plan.md §3) in libconfig. Covers nested-block
 * parsing into dotted keys, canonical nested spelling on rewrite, flat
 * domains staying flat, duplicate-record-name + '{'-prefixed-value parse
 * errors, leaf/container conflicts, unbalanced braces, empty blocks,
 * and the config_record_first/next enumeration API.
 *
 * Run with $FNX_CONFIG_ROOT pointing at a scratch tree (created by the
 * caller: System/Configuration etc. are made on demand by libconfig;
 * the System files here are crafted directly under the root). Prints
 * PASS/FAIL per check; exit status = number of failures.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libconfig.h>

#define DOM "system.m0"
#define FLATDOM "system.m0flat"

static int fails;

static void check(int cond, const char *what)
{
	if(cond) {
		printf("PASS: %s\n", what);
	} else {
		printf("FAIL: %s\n", what);
		fails++;
	}
}

/* Write a domain file at <root>/System/Configuration/<domain>.conf */
static int put_sys(const char *domain, const char *content)
{
	const char *root = getenv("FNX_CONFIG_ROOT");
	char path[1024];
	FILE *f;

	if(!root || !*root) {
		root = "/";
	}
	snprintf(path, sizeof(path), "%s/System/Configuration/%s.conf",
		 root, domain);
	f = fopen(path, "w");
	if(!f) {
		printf("FAIL: cannot write %s\n", path);
		fails++;
		return -1;
	}
	fputs(content, f);
	fclose(f);
	return 0;
}

static const char *sysfile_text(const char *domain)
{
	const char *root = getenv("FNX_CONFIG_ROOT");
	static char buf[8192];
	char path[1024];
	FILE *f;
	size_t n;

	if(!root || !*root) {
		root = "/";
	}
	snprintf(path, sizeof(path), "%s/System/Configuration/%s.conf",
		 root, domain);
	f = fopen(path, "r");
	if(!f) {
		return NULL;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	return buf;
}

int main(void)
{
	config_value_t v;
	config_scope_t sc = CONFIG_SCOPE_SYSTEM;
	config_err_t e;
	int64_t i;
	char *name;
	const char *txt;

	config_remove_domain(sc, DOM);
	config_remove_domain(sc, FLATDOM);

	/* ---- nested record domain parses into dotted keys ----------- */
	put_sys(DOM,
		"user = {\n"
		"    admin = {\n"
		"        uid = 0\n"
		"        gid = 0\n"
		"        gecos = \"Root Admin\"\n"
		"        home = /Users/Admin\n"
		"        shell = /System/Tools/sh\n"
		"    }\n"
		"    bob = {\n"
		"        uid = 1\n"
		"    }\n"
		"}\n");
	e = config_get_int(DOM, "user.admin.uid", &i);
	check(!e && i == 0, "read nested int user.admin.uid == 0");
	e = config_get_int(DOM, "user.bob.uid", &i);
	check(!e && i == 1, "read nested int user.bob.uid == 1");
	{
		const char *s;

		e = config_get_string(DOM, "user.admin.gecos", &s);
		check(!e && s && !strcmp(s, "Root Admin"),
		      "read nested string with space");
	}
	e = config_read_scope(sc, DOM, "user.admin.uid", &v);
	check(!e && v.type == CONFIG_TYPE_INT && v.v.integer == 0,
	      "scope-explicit read of a nested key");
	if(!e) {
		config_value_free(&v);
	}

	/* ---- record enumeration ------------------------------------- */
	e = config_record_first(sc, DOM, "user", &name);
	check(!e && name && !strcmp(name, "admin"),
	      "config_record_first(user) == admin");
	if(!e) {
		free(name);
	}
	e = config_record_next(sc, DOM, "user", "admin", &name);
	check(!e && name && !strcmp(name, "bob"),
	      "config_record_next(admin) == bob");
	if(!e) {
		free(name);
	}
	e = config_record_next(sc, DOM, "user", "bob", &name);
	check(e == CONFIG_ERR_NOT_FOUND, "config_record_next(bob) == NOT_FOUND");
	e = config_record_next(sc, DOM, "user", "zed", &name);
	check(e == CONFIG_ERR_INVALID, "config_record_next(zed) == INVALID");
	e = config_record_first(sc, DOM, "group", &name);
	check(e == CONFIG_ERR_NOT_FOUND, "record_first on absent group");
	e = config_record_first(sc, DOM, "", &name);
	check(!e && name && !strcmp(name, "user"),
	      "top-level record enumeration finds user");
	if(!e) {
		free(name);
	}

	/* ---- canonical rewrite stays nested ------------------------- */
	e = config_set_string(sc, DOM, "user.bob.gecos", "Bobby");
	check(!e, "set a new field in the record domain");
	txt = sysfile_text(DOM);
	check(txt && strstr(txt, "user = {\n")
	      && strstr(txt, "    admin = {\n")
	      && strstr(txt, "        uid = 0\n")
	      && strstr(txt, "    bob = {\n")
	      && strstr(txt, "        gecos = Bobby\n")
	      && strstr(txt, "        uid = 1\n")
	      && strstr(txt, "    }\n}\n"),
	      "rewrite keeps canonical nested spelling (4-space indent)");
	/* round-trip: reading the rewritten file gives the same keys */
	{
		char **keys;
		config_value_t *vals;
		size_t n, k;
		int found = 0;

		e = config_get_all(DOM, "", &keys, &vals, &n);
		check(!e && n == 7, "round-trip: 7 keys after rewrite");
		for(k = 0; !e && k < n; k++) {
			if(!strcmp(keys[k], "user.bob.gecos") &&
			   vals[k].type == CONFIG_TYPE_STRING &&
			   !strcmp(vals[k].v.string, "Bobby")) {
				found = 1;
			}
		}
		check(found, "round-trip: added field readable after rewrite");
		if(!e) {
			config_free_keys(keys, vals, n);
		}
	}

	/* ---- a pure flat dotted domain stays flat -------------------- */
	put_sys(FLATDOM,
		"window.width = 100\n"
		"dock.pos = 1\n"
		"window.height = 300\n");
	e = config_set_int(sc, FLATDOM, "window.height", 400);
	check(!e, "set a key in a flat dotted domain");
	txt = sysfile_text(FLATDOM);
	check(txt && strstr(txt, "window.width = 100\n")
	      && strstr(txt, "dock.pos = 1\n")
	      && strstr(txt, "window.height = 400\n")
	      && !strstr(txt, "window = {"),
	      "flat domain rewritten flat, byte layout preserved");

	/* ---- parse errors -------------------------------------------- */
	put_sys(DOM,
		"user = {\n"
		"    a = {\n"
		"        x = 1\n"
		"    }\n"
		"    a = {\n"
		"        y = 2\n"
		"    }\n"
		"}\n");
	e = config_read_scope(sc, DOM, "user.a.x", &v);
	check(e == CONFIG_ERR_PARSE, "duplicate record name is a parse error");

	put_sys(DOM, "key = {nope\n");
	e = config_read_scope(sc, DOM, "key", &v);
	check(e == CONFIG_ERR_PARSE,
	      "bare value beginning with '{' is a parse error");

	put_sys(DOM, "key = \"{ok\"\n");
	{
		const char *s;

		e = config_get_string(DOM, "key", &s);
		check(!e && s && !strcmp(s, "{ok"),
		      "quoted '{...' value parses as a string");
	}

	put_sys(DOM, "user = {\n");
	e = config_read_scope(sc, DOM, "user", &v);
	check(e == CONFIG_ERR_PARSE, "unbalanced '{' is a parse error");
	put_sys(DOM, "}\n");
	e = config_read_scope(sc, DOM, "x", &v);
	check(e == CONFIG_ERR_PARSE, "stray '}' is a parse error");

	put_sys(DOM, "a.b = 1\na = 2\n");
	e = config_read_scope(sc, DOM, "a", &v);
	check(e == CONFIG_ERR_PARSE, "leaf under existing container fails");
	put_sys(DOM, "a = 2\na.b = 1\n");
	e = config_read_scope(sc, DOM, "a.b", &v);
	check(e == CONFIG_ERR_PARSE, "container under existing leaf fails");

	/* ---- empty + nested blocks, mixed flat/nested spelling ------ */
	put_sys(DOM, "x = {}\ny = 1\n");
	e = config_get_int(DOM, "y", &i);
	check(!e && i == 1, "empty block is valid; file still parses");
	put_sys(DOM,
		"user.admin.uid = 0\n"
		"user = {\n"
		"    admin = {\n"
		"        gecos = Root\n"
		"    }\n"
		"}\n");
	e = config_get_int(DOM, "user.admin.uid", &i);
	check(!e && i == 0, "mixed flat+nested spelling of a record key");
	{
		const char *s;

		e = config_get_string(DOM, "user.admin.gecos", &s);
		check(!e && s && !strcmp(s, "Root"),
		      "mixed spelling: sibling field from the block");
	}

	/* ---- duplicate flat keys inside one block: last wins -------- */
	put_sys(DOM, "user = {\n    uid = 1\n    uid = 2\n}\n");
	e = config_get_int(DOM, "user.uid", &i);
	check(!e && i == 2, "duplicate leaf keys inside a block stay last-wins");

	if(fails) {
		printf("%d FAILURE(S)\n", fails);
	} else {
		printf("ALL PASS\n");
	}
	return fails;
}
