/* config_test.c — libconfig probe (P0): typed round-trips, scope
 * precedence, prefix reads, arrays, atomic writes, validation.
 *
 * Run with $FNX_CONFIG_ROOT pointing at a scratch tree (host tests) or
 * with the real FSH dirs (guest). Prints PASS/FAIL per check; exit
 * status = number of failures. Scope dirs must be writable.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libconfig.h>

#define DOM "system.config.configtest"

static int fails;

static void fail(const char *what)
{
	printf("FAIL: %s\n", what);
	fails++;
}

static void ok(const char *what)
{
	printf("PASS: %s\n", what);
}

static void check(config_err_t e, const char *what)
{
	if(e) {
		fail(what);
	} else {
		ok(what);
	}
}

int main(void)
{
	config_value_t v;
	config_scope_t scope;
	config_err_t e;
	int64_t i;
	double d;
	bool b;
	const char *s;
	char path[512];

	/* fresh state */
	config_remove_domain(CONFIG_SCOPE_USER, DOM);
	config_remove_domain(CONFIG_SCOPE_SHARED, DOM);
	config_remove_domain(CONFIG_SCOPE_SYSTEM, DOM);

	/* ---- basic typed write/read --------------------------------- */
	e = config_set_string(CONFIG_SCOPE_USER, DOM, "greeting", "hi there");
	check(e, "set string with spaces");
	e = config_get_string(DOM, "greeting", &s);
	if(!e && !strcmp(s, "hi there")) {
		ok("get string round-trip");
	} else {
		fail("get string round-trip");
	}
	e = config_set_int(CONFIG_SCOPE_USER, DOM, "count", 42);
	check(e, "set int");
	e = config_get_int(DOM, "count", &i);
	if(!e && i == 42) {
		ok("get int round-trip");
	} else {
		fail("get int round-trip");
	}
	e = config_set_int(CONFIG_SCOPE_USER, DOM, "hex", -255);
	check(e, "set negative int");
	e = config_get_int(DOM, "hex", &i);
	if(!e && i == -255) {
		ok("negative int round-trip");
	} else {
		fail("negative int round-trip");
	}
	e = config_set_float(CONFIG_SCOPE_USER, DOM, "zoom", 1.5);
	check(e, "set float");
	e = config_get_float(DOM, "zoom", &d);
	if(!e && d == 1.5) {
		ok("float round-trip");
	} else {
		fail("float round-trip");
	}
	e = config_set_bool(CONFIG_SCOPE_USER, DOM, "autohide", true);
	check(e, "set bool");
	e = config_get_bool(DOM, "autohide", &b);
	if(!e && b) {
		ok("bool round-trip");
	} else {
		fail("bool round-trip");
	}

	/* ---- type errors --------------------------------------------- */
	e = config_get_int(DOM, "greeting", &i);
	if(e == CONFIG_ERR_TYPE) {
		ok("string read as int -> TYPE");
	} else {
		fail("string read as int -> TYPE");
	}

	/* ---- arrays --------------------------------------------------- */
	{
		config_value_t items[3];
		config_value_t *got;
		size_t n;

		items[0].type = CONFIG_TYPE_STRING;
		items[0].v.string = "Terminal";
		items[1].type = CONFIG_TYPE_INT;
		items[1].v.integer = 7;
		items[2].type = CONFIG_TYPE_BOOL;
		items[2].v.boolean = true;
		e = config_set_array(CONFIG_SCOPE_USER, DOM, "apps", items, 3);
		check(e, "set array");
		e = config_get_array(DOM, "apps", &got, &n);
		if(!e && n == 3 && got[0].type == CONFIG_TYPE_STRING &&
		   !strcmp(got[0].v.string, "Terminal") &&
		   got[1].type == CONFIG_TYPE_INT && got[1].v.integer == 7 &&
		   got[2].type == CONFIG_TYPE_BOOL && got[2].v.boolean) {
			ok("array round-trip (3 mixed elements)");
		} else {
			fail("array round-trip");
		}
		config_value_free(&v);
	}

	/* ---- dot-nested keys + prefix read ---------------------------- */
	e = config_set_int(CONFIG_SCOPE_USER, DOM, "window.x", 10);
	check(e, "set window.x");
	e = config_set_int(CONFIG_SCOPE_USER, DOM, "window.y", 20);
	check(e, "set window.y");
	e = config_set_string(CONFIG_SCOPE_USER, DOM, "window.title", "Main");
	check(e, "set window.title");
	{
		char **keys;
		config_value_t *vals;
		size_t n;

		e = config_get_all(DOM, "window", &keys, &vals, &n);
		if(!e && n == 3) {
			int x = -1, y = -1;

			for(size_t j = 0; j < n; j++) {
				if(!strcmp(keys[j], "window.x")) {
					x = (int)vals[j].v.integer;
				}
				if(!strcmp(keys[j], "window.y")) {
					y = (int)vals[j].v.integer;
				}
			}
			if(x == 10 && y == 20) {
				ok("prefix get_all finds window.x/y/title");
			} else {
				fail("prefix get_all values");
			}
		} else {
			fail("prefix get_all");
		}
		config_free_keys(keys, vals, n);
	}

	/* ---- scope precedence (system -> user -> shared, D4) ---------- */
	e = config_set_int(CONFIG_SCOPE_SYSTEM, DOM, "theme.size", 11);
	check(e, "set system theme.size");
	e = config_resolve(DOM, "theme.size", &scope);
	if(!e && scope == CONFIG_SCOPE_SYSTEM) {
		ok("resolve finds system");
	} else {
		fail("resolve finds system");
	}
	e = config_set_int(CONFIG_SCOPE_SHARED, DOM, "theme.size", 12);
	check(e, "set shared theme.size");
	e = config_resolve(DOM, "theme.size", &scope);
	if(!e && scope == CONFIG_SCOPE_SYSTEM) {
		ok("system wins over shared");
	} else {
		fail("system wins over shared");
	}
	e = config_set_int(CONFIG_SCOPE_USER, DOM, "theme.size", 13);
	check(e, "set user theme.size");
	e = config_get_int(DOM, "theme.size", &i);
	if(!e && i == 11) {
		ok("system wins over user (11)");
	} else {
		fail("system wins over user");
	}
	/* a user write cannot shadow a System value: the value is still 11 */
	e = config_unset(CONFIG_SCOPE_USER, DOM, "theme.size");
	check(e, "unset user theme.size");
	/* with no System value: user overrides shared */
	e = config_set_string(CONFIG_SCOPE_SHARED, DOM, "theme.color", "red");
	check(e, "set shared theme.color");
	e = config_resolve(DOM, "theme.color", &scope);
	if(!e && scope == CONFIG_SCOPE_SHARED) {
		ok("shared value resolves alone");
	} else {
		fail("shared value resolves alone");
	}
	e = config_set_string(CONFIG_SCOPE_USER, DOM, "theme.color", "blue");
	check(e, "set user theme.color");
	e = config_get_string(DOM, "theme.color", &s);
	if(!e && s && !strcmp(s, "blue")) {
		ok("user overrides shared");
	} else {
		fail("user overrides shared");
	}
	/* scope-isolated reads see the raw scopes */
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, DOM, "theme.size", &v);
	if(!e && v.v.integer == 11) {
		ok("read_scope(system) sees 11");
	} else {
		fail("read_scope(system) sees 11");
	}
	config_value_free(&v);

	/* a user-only key is invisible at the system scope */
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, DOM, "count", &v);
	if(e == CONFIG_ERR_NOT_FOUND) {
		ok("user-only key absent at system scope");
	} else {
		fail("user-only key absent at system scope");
	}

	/* ---- unset + delete ------------------------------------------- */
	e = config_unset(CONFIG_SCOPE_USER, DOM, "count");
	check(e, "unset count");
	e = config_get_int(DOM, "count", &i);
	if(e == CONFIG_ERR_NOT_FOUND) {
		ok("count gone after unset");
	} else {
		fail("count gone after unset");
	}
	e = config_remove_domain(CONFIG_SCOPE_SHARED, DOM);
	check(e, "remove shared domain");
	e = config_read(DOM, "theme.size", NULL, &v);
	if(!e && v.v.integer == 11) {
		ok("shared removal leaves system value");
	} else {
		fail("shared removal leaves system value");
	}
	config_value_free(&v);
	/* theme.color lived in shared + user only: the user override stays */
	e = config_get_string(DOM, "theme.color", &s);
	if(!e && s && !strcmp(s, "blue")) {
		ok("shared removal leaves the user override");
	} else {
		fail("shared removal leaves the user override");
	}
	e = config_remove_domain(CONFIG_SCOPE_USER, DOM);
	check(e, "remove user domain");
	e = config_read(DOM, "theme.size", NULL, &v);
	if(!e && v.v.integer == 11) {
		ok("user removal reveals system 11");
	} else {
		fail("user removal reveals system 11");
	}
	config_value_free(&v);
	e = config_read(DOM, "theme.color", NULL, &v);
	if(e == CONFIG_ERR_NOT_FOUND) {
		ok("user removal drops the user-only color");
	} else {
		fail("user removal drops the user-only color");
	}

	/* ---- validation ----------------------------------------------- */
	if(config_valid_domain("system.config.ok") && !config_valid_domain("..bad") &&
	   !config_valid_domain("com..fnx") && !config_valid_domain("a/b") &&
	   config_valid_key("window.x.y") && !config_valid_key("window..x") &&
	   !config_valid_key(".window") && !config_valid_key("win dow")) {
		ok("domain/key validation");
	} else {
		fail("domain/key validation");
	}
	e = config_get_string("bad domain!", "k", &s);
	if(e == CONFIG_ERR_INVALID) {
		ok("invalid domain -> INVALID");
	} else {
		fail("invalid domain -> INVALID");
	}

	/* ---- config_path ---------------------------------------------- */
	e = config_path(CONFIG_SCOPE_SYSTEM, DOM, path, sizeof(path));
	if(!e && strstr(path, "/System/Configuration/" DOM ".conf")) {
		ok("config_path points into the system scope");
	} else {
		fail("config_path");
	}

	/* ---- malformed file -> PARSE ---------------------------------- */
	{
		FILE *f;

		config_remove_domain(CONFIG_SCOPE_USER, DOM);
		if(config_path(CONFIG_SCOPE_USER, DOM, path, sizeof(path))) {
			fail("path for malformed test");
		} else {
			f = fopen(path, "w");
			if(!f) {
				fail("create malformed file");
			} else {
				fprintf(f, "ok = 1\nbroken key\n");
				fclose(f);
				e = config_get_int(DOM, "ok", &i);
				if(e == CONFIG_ERR_PARSE) {
					ok("malformed file -> PARSE");
				} else {
					fail("malformed file -> PARSE");
				}
				config_remove_domain(CONFIG_SCOPE_USER, DOM);
			}
		}
	}

	printf("CONFIGTEST: %d failure(s)\n", fails);
	return fails ? 1 : 0;
}
