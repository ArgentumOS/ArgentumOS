#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void put_sys(const char *domain, const char *txt)
{
	const char *root = getenv("FNX_CONFIG_ROOT");
	char path[1024];
	FILE *f;

	if(!root || !*root) {
		root = "/";
	}
	snprintf(path, sizeof(path), "%s/System/Configuration/%s.conf", root, domain);
	f = fopen(path, "w");
	if(!f) {
		perror("put_sys fopen");
		exit(1);
	}
	fputs(txt, f);
	fclose(f);
}

int main(void)
{
	config_value_t v;
	config_err_t e;
	const char *s;

	config_remove_domain(CONFIG_SCOPE_SYSTEM, "fonts");

	/* ---- v2: array of records (fontconfig rules shape) ---------- */
	put_sys("fonts",
		"cachedir = \"/System/Variable Data/fontconfig\"\n"
		"rules = [\n"
		"  {\n"
		"    match = pattern\n"
		"    tests = [\n"
		"      {\n"
		"        object = family\n"
		"        compare = eq\n"
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

	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "fonts", "rules", &v);
	check(e == CONFIG_OK, "read rules array");
	if(e == CONFIG_OK) {
		check(v.type == CONFIG_TYPE_ARRAY, "rules is an array");
		if(v.type == CONFIG_TYPE_ARRAY) {
			check(v.v.array.count == 2, "rules has 2 elements");
			if(v.v.array.count == 2) {
				check(v.v.array.items[0].type == CONFIG_TYPE_RECORD,
				      "rules[0] is a record");
				check(v.v.array.items[1].type == CONFIG_TYPE_RECORD,
				      "rules[1] is a record");
			}
		}
		config_value_free(&v);
	}

	/* flat key still readable */
	{
		const char *s;

		e = config_get_string("fonts", "cachedir", &s);
		check(!e && s && !strcmp(s, "/System/Variable Data/fontconfig"),
		      "flat cachedir read");
	}

	/* ---- block name reads back as a synthesized record -------- */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "pwtest");
	put_sys("pwtest",
		"user = {\n"
		"    admin = {\n"
		"        uid = 0\n"
		"        gecos = \"Admin\"\n"
		"    }\n"
		"    bob = {\n"
		"        uid = 1\n"
		"    }\n"
		"}\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "pwtest", "user", &v);
	check(e == CONFIG_OK, "read block name user");
	if(e == CONFIG_OK) {
		config_value_t *admin, *uid;

		check(v.type == CONFIG_TYPE_RECORD, "user is a record");
		e = config_record_child(&v, "admin", &admin);
		check(e == CONFIG_OK && admin &&
		      admin->type == CONFIG_TYPE_RECORD,
		      "record_child(user, admin)");
		if(e == CONFIG_OK && admin) {
			e = config_record_child(admin, "uid", &uid);
			check(e == CONFIG_OK && uid &&
			      uid->type == CONFIG_TYPE_INT &&
			      uid->v.integer == 0,
			      "record_child(admin, uid) == 0");
		}
		config_value_free(&v);
	}

	/* ---- v2-shaped (array-of-records) write rejects cleanly in M0
	 * (the canonical writer is M2; a record/array value cannot be
	 * serialized to v1 text yet) --------------------------------- */
	put_sys("wtest",
		"rules = [\n"
		"  {\n"
		"    m = 1\n"
		"  }\n"
		"]\n");
	e = config_set_int(CONFIG_SCOPE_SYSTEM, "wtest", "other", 1);
	check(e == CONFIG_ERR_INVALID,
	      "config_set on array-of-record domain rejects cleanly (M0)");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "pwtest");
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "wtest");
	/* ---- §10 parse diagnostics in v2 shapes ---------------------- */
	/* dup record-valued fields in one element: parse error */
	config_remove_domain(CONFIG_SCOPE_SYSTEM, "diag");
	put_sys("diag",
		"rules = [\n"
		"  {\n"
		"    a = {\n"
		"      x = 1\n"
		"    }\n"
		"    a = {\n"
		"      y = 2\n"
		"    }\n"
		"  }\n"
		"]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "rules", &v);
	check(e == CONFIG_ERR_PARSE, "dup record fields in element is a parse error");

	/* scalar-vs-container conflict in one element */
	put_sys("diag",
		"rules = [\n"
		"  {\n"
		"    a = 1\n"
		"    a = {\n"
		"      y = 2\n"
		"    }\n"
		"  }\n"
		"]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "rules", &v);
	check(e == CONFIG_ERR_PARSE, "leaf vs record conflict in element is a parse error");

	/* quoted '{' value inside a record element: string, not block */
	put_sys("diag",
		"rules = [\n"
		"  {\n"
		"    a = \"{notablock\"\n"
		"  }\n"
		"]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "rules", &v);
	check(e == CONFIG_OK, "quoted { value in element parses");
	if(e == CONFIG_OK) {
		config_value_t *r0 = &v.v.array.items[0];
		config_value_t *a;

		e = config_record_child(r0, "a", &a);
		check(e == CONFIG_OK && a && a->type == CONFIG_TYPE_STRING &&
		      a->v.string && !strcmp(a->v.string, "{notablock"),
		      "element string value is {notablock");
		config_value_free(&v);
	}

	/* unclosed bracket is a parse error */
	put_sys("diag", "rules = [\n  {\n    a = 1\n  }\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "rules", &v);
	check(e == CONFIG_ERR_PARSE, "unclosed [ is a parse error");

	/* stray ']' is a parse error */
	put_sys("diag", "]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "rules", &v);
	check(e == CONFIG_ERR_PARSE, "stray ] is a parse error");

	/* empty array and empty record element parse */
	put_sys("diag", "empty = []\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "empty", &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_ARRAY &&
	      v.v.array.count == 0, "empty [] parses");
	if(e == CONFIG_OK) config_value_free(&v);
	put_sys("diag", "r = [ { } ]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "r", &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_ARRAY &&
	      v.v.array.count == 1 &&
	      v.v.array.items[0].type == CONFIG_TYPE_RECORD &&
	      v.v.array.items[0].v.record.count == 0,
	      "[ { } ] parses as one empty record element");
	if(e == CONFIG_OK) config_value_free(&v);

	/* single-line bracketed arrays */
	put_sys("diag", "a = [ 1, 2, 3 ]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "a", &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_ARRAY &&
	      v.v.array.count == 3, "[ 1, 2, 3 ] parses inline");
	if(e == CONFIG_OK) config_value_free(&v);

	/* mixed scalar + record elements in one array */
	put_sys("diag",
		"mix = [\n"
		"  first\n"
		"  ,\n"
		"  {\n"
		"    v = 1\n"
		"  }\n"
		"]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "mix", &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_ARRAY &&
	      v.v.array.count == 2 &&
	      v.v.array.items[0].type == CONFIG_TYPE_STRING &&
	      v.v.array.items[1].type == CONFIG_TYPE_RECORD,
	      "mixed scalar+record array");
	if(e == CONFIG_OK) config_value_free(&v);

	/* keys after a multi-line array still parse */
	put_sys("diag",
		"rules = [\n"
		"  {\n"
		"    m = 1\n"
		"  }\n"
		"]\n"
		"tail = ok\n");
	e = config_get_string("diag", "tail", &s);
	check(!e && s && !strcmp(s, "ok"), "key after array parses");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "rules", &v);
	check(e == CONFIG_OK, "rules still readable");
	if(e == CONFIG_OK) config_value_free(&v);

	/* a v2 array nested inside a named (v1) block */
	put_sys("diag",
		"app = {\n"
		"    dirs = [\n"
		"      /a\n"
		"      ,\n"
		"      /b\n"
		"    ]\n"
		"}\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "app.dirs", &v);
	check(e == CONFIG_OK && v.type == CONFIG_TYPE_ARRAY &&
	      v.v.array.count == 2, "v2 array inside named block");
	if(e == CONFIG_OK) config_value_free(&v);

	/* nested arrays / brace characters are NOT scalar elements */
	put_sys("diag", "bad = [ [1, 2] ]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "bad", &v);
	check(e == CONFIG_ERR_PARSE, "nested [ element is a parse error");
	put_sys("diag", "bad = [ {x = 1} ]\n");
	e = config_read_scope(CONFIG_SCOPE_SYSTEM, "diag", "bad", &v);
	check(e == CONFIG_ERR_PARSE, "record element must be on its own line");

	config_remove_domain(CONFIG_SCOPE_SYSTEM, "diag");
	printf(fails ? "%d FAILURE(S)\n" : "ALL PASS\n", fails);
	return fails ? 1 : 0;
}
