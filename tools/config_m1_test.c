/* config_m1_test.c — M1 probe: precedence inversion system -> user ->
 * shared (docs system-config-files-plan.md §4/M1, D4). Covers: System
 * authoritative when the same key exists in all three scopes; user
 * overrides shared (System absent); shared resolves alone; a user -u
 * write cannot shadow a System value; config_get_all values follow the
 * new order.
 *
 * Run with $FNX_CONFIG_ROOT at a scratch tree. Prints PASS/FAIL per
 * check; exit status = number of failures.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libconfig.h>

#define DOM "system.config.m1"

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

int main(void)
{
	config_scope_t scope;
	config_err_t e;
	int64_t i;
	config_value_t v;

	config_remove_domain(CONFIG_SCOPE_USER, DOM);
	config_remove_domain(CONFIG_SCOPE_SHARED, DOM);
	config_remove_domain(CONFIG_SCOPE_SYSTEM, DOM);

	/* same key in all three scopes: system wins */
	e = config_set_int(CONFIG_SCOPE_SYSTEM, DOM, "theme.size", 11);
	check(!e, "set system theme.size = 11");
	e = config_set_int(CONFIG_SCOPE_SHARED, DOM, "theme.size", 12);
	check(!e, "set shared theme.size = 12");
	e = config_set_int(CONFIG_SCOPE_USER, DOM, "theme.size", 13);
	check(!e, "set user theme.size = 13");
	e = config_read(DOM, "theme.size", &scope, &v);
	check(!e && scope == CONFIG_SCOPE_SYSTEM && v.v.integer == 11,
	      "system value wins over user+shared");
	config_value_free(&v);
	e = config_get_int(DOM, "theme.size", &i);
	check(!e && i == 11, "typed getter returns the system value (11)");

	/* key only in user + shared: user wins (System has no value) */
	e = config_set_string(CONFIG_SCOPE_SHARED, DOM, "theme.color", "red");
	check(!e, "set shared theme.color = red");
	e = config_set_string(CONFIG_SCOPE_USER, DOM, "theme.color", "blue");
	check(!e, "set user theme.color = blue");
	e = config_read(DOM, "theme.color", &scope, &v);
	check(!e && scope == CONFIG_SCOPE_USER && v.v.string &&
	      !strcmp(v.v.string, "blue"),
	      "user value overrides the shared default");
	config_value_free(&v);

	/* key only in shared: shared resolves (System + user absent) */
	e = config_set_bool(CONFIG_SCOPE_SHARED, DOM, "widget.alpha", true);
	check(!e, "set shared widget.alpha");
	e = config_read(DOM, "widget.alpha", &scope, &v);
	check(!e && scope == CONFIG_SCOPE_SHARED && v.v.boolean,
	      "shared value resolves alone");
	config_value_free(&v);

	/* a user -u write of a key System owns does not change the value */
	e = config_set_int(CONFIG_SCOPE_USER, DOM, "theme.size", 99);
	check(!e, "user -u write of a system-owned key");
	e = config_get_int(DOM, "theme.size", &i);
	check(!e && i == 11, "user -u write does not shadow the System value");

	/* get_all: per-key values follow the new precedence */
	{
		char **keys;
		config_value_t *vals;
		size_t n, k;
		int size_seen = 0, color_seen = 0, alpha_seen = 0;

		e = config_get_all(DOM, "", &keys, &vals, &n);
		check(!e && n == 3, "get_all sees the three merged keys");
		for(k = 0; !e && k < n; k++) {
			if(!strcmp(keys[k], "theme.size") &&
			   vals[k].v.integer == 11) {
				size_seen = 1;
			}
			if(!strcmp(keys[k], "theme.color") &&
			   vals[k].v.string &&
			   !strcmp(vals[k].v.string, "blue")) {
				color_seen = 1;
			}
			if(!strcmp(keys[k], "widget.alpha") &&
			   vals[k].v.boolean) {
				alpha_seen = 1;
			}
		}
		check(size_seen && color_seen && alpha_seen,
		      "get_all values resolved system-first");
		if(!e) {
			config_free_keys(keys, vals, n);
		}
	}

	/* cleanup so host reruns are fresh */
	config_remove_domain(CONFIG_SCOPE_USER, DOM);
	config_remove_domain(CONFIG_SCOPE_SHARED, DOM);
	config_remove_domain(CONFIG_SCOPE_SYSTEM, DOM);

	if(fails) {
		printf("%d FAILURE(S)\n", fails);
	} else {
		printf("ALL PASS\n");
	}
	return fails;
}
