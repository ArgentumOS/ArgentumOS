/* config.c — the 'config' CLI (docs/design/config-design.md §4), built on
 * libconfig (userland/libconfig.h, userland/libconfig.c).
 *
 *   config list [-s|-g|-u]                    list domains (default all)
 *   config read [-s|-g|-u] <domain> [key]     read the effective value
 *                                             (no key: whole domain)
 *   config write [-s|-g|-u] <domain> <key> <value> [-type T]
 *   config delete [-s|-g|-u] <domain> [key]   delete a key or a domain
 *
 * Default scope is -u (user). `read` without a scope flag resolves
 * system -> user -> shared per key and (whole-domain reads) prints the
 * winning scope next to each value; with a flag it reads only that
 * scope. `write`/`delete` always act on the one requested scope.
 * Without -type, write infers the type exactly as reading does. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libconfig.h>

static const char *prog;

static void usage(void)
{
	fprintf(stderr,
"usage:\n"
"  %s list [-s|-g|-u]\n"
"  %s read  [-s|-g|-u] <domain> [key]\n"
"  %s write [-s|-g|-u] <domain> <key> <value> [-type bool|int|float|string|array]\n"
"  %s delete [-s|-g|-u] <domain> [key]\n"
"scopes: -u user (default)  -g shared  -s system\n"
"`read` with no flag resolves system -> user -> shared (whole-domain\n"
"reads annotate the winning scope).\n", prog, prog,
	    prog, prog);
}

static config_scope_t parse_scope(const char *flag)
{
	if(!strcmp(flag, "-u") || !strcmp(flag, "--user")) {
		return CONFIG_SCOPE_USER;
	}
	if(!strcmp(flag, "-g") || !strcmp(flag, "--shared")) {
		return CONFIG_SCOPE_SHARED;
	}
	if(!strcmp(flag, "-s") || !strcmp(flag, "--system")) {
		return CONFIG_SCOPE_SYSTEM;
	}
	return (config_scope_t)-1;
}

/* ---- value display ------------------------------------------------- */

static bool str_needs_quotes(const char *s)
{
	if(!*s) {
		return true;
	}
	for(; *s; s++) {
		if(*s == ' ' || *s == '\t' || *s == '#' || *s == '=' ||
		   *s == ',' || *s == '"') {
			return true;
		}
	}
	return false;
}

/* display form: strings are quoted when the file grammar needs it, so
 * "key = value" lines round-trip through the parser */
static void display_text(const config_value_t *v, char *out, size_t outsz)
{
	switch(v->type) {
	case CONFIG_TYPE_BOOL:
		snprintf(out, outsz, "%s", v->v.boolean ? "true" : "false");
		break;
	case CONFIG_TYPE_INT:
		snprintf(out, outsz, "%lld", (long long)v->v.integer);
		break;
	case CONFIG_TYPE_FLOAT:
		snprintf(out, outsz, "%g", v->v.floating);
		break;
	case CONFIG_TYPE_STRING: {
		const char *s = v->v.string ? v->v.string : "";
		size_t n = 0;

		if(!str_needs_quotes(s)) {
			snprintf(out, outsz, "%s", s);
			break;
		}
		n = snprintf(out, outsz, "\"");
		for(; *s && n + 3 < outsz; s++) {
			if(*s == '"' || *s == '\\') {
				out[n++] = '\\';
				out[n++] = *s;
			} else {
				out[n++] = *s;
			}
		}
		if(n + 2 < outsz) {
			out[n++] = '"';
			out[n] = '\0';
		} else {
			out[0] = '\0';
		}
		break;
	}
	case CONFIG_TYPE_ARRAY: {
		size_t i, n = 0;

		out[0] = '\0';
		for(i = 0; i < v->v.array.count; i++) {
			char el[256];

			display_text(&v->v.array.items[i], el, sizeof(el));
			n += snprintf(out + n, outsz - n, "%s%s",
				      i ? ", " : "", el);
			if(n >= outsz) {
				break;
			}
		}
		break;
	}
	}
}

/* raw value for a single-key read (scripts): strings unquoted */
static void raw_text(const config_value_t *v, char *out, size_t outsz)
{
	switch(v->type) {
	case CONFIG_TYPE_BOOL:
		snprintf(out, outsz, "%s", v->v.boolean ? "true" : "false");
		break;
	case CONFIG_TYPE_INT:
		snprintf(out, outsz, "%lld", (long long)v->v.integer);
		break;
	case CONFIG_TYPE_FLOAT:
		snprintf(out, outsz, "%g", v->v.floating);
		break;
	case CONFIG_TYPE_STRING:
		snprintf(out, outsz, "%s", v->v.string ? v->v.string : "");
		break;
	case CONFIG_TYPE_ARRAY:
		display_text(v, out, outsz);	/* comma-joined */
		break;
	}
}

/* ---- write-value inference (mirrors the .conf grammar) ------------- */

static bool parse_int(const char *s, int64_t *v)
{
	char *end;

	errno = 0;
	*v = strtoll(s, &end, 0);
	if(errno || *end) {
		return false;
	}
	return true;
}

static bool parse_float(const char *s, double *v)
{
	char *end;

	errno = 0;
	*v = strtod(s, &end);
	if(errno || *end) {
		return false;
	}
	return true;
}

/* split text at commas outside double quotes; returns a NULL-terminated
 * array of malloc'd element strings */
static char **split_array(const char *text, size_t *count)
{
	char **els = NULL;
	size_t n = 0, cap = 0;
	const char *p = text;
	bool inq = false;
	char cur[4096];
	size_t cl = 0;

	while(1) {
		if(!*p || (!inq && *p == ',')) {
			cur[cl] = '\0';
			/* trim */
			while(cl && (cur[cl - 1] == ' ' || cur[cl - 1] == '\t')) {
				cur[--cl] = '\0';
			}
			{
				char *s2 = cur;

				while(*s2 == ' ' || *s2 == '\t') {
					s2++;
				}
				if(n == cap) {
					char **nn;

					cap = cap ? cap * 2 : 8;
					nn = realloc(els, cap * sizeof(char *));
					if(!nn) {
						goto fail;
					}
					els = nn;
				}
				els[n] = strdup(s2);
				if(!els[n]) {
					goto fail;
				}
				n++;
			}
			cl = 0;
			if(!*p) {
				break;
			}
			p++;
			continue;
		}
		if(*p == '"' && !inq) {
			inq = true;
		} else if(*p == '"' && inq) {
			inq = false;
		} else if(cl + 1 < sizeof(cur)) {
			cur[cl++] = *p;
		}
		p++;
	}
	if(inq) {
		goto fail;
	}
	if(els) {
		els[n] = NULL;
	}
	*count = n;
	return els;
fail:
	while(n) {
		free(els[--n]);
	}
	free(els);
	return NULL;
}

/* parse one element of an array from text (quoted -> string, else the
 * scalar inference) */
static int infer_element(const char *tok, config_value_t *out)
{
	int64_t i;
	double d;

	memset(out, 0, sizeof(*out));
	if(tok[0] == '"') {
		size_t len = strlen(tok);

		if(len >= 2 && tok[len - 1] == '"') {
			char *s = strndup(tok + 1, len - 2);

			if(!s) {
				return -1;
			}
			out->type = CONFIG_TYPE_STRING;
			out->v.string = s;
			return 0;
		}
	}
	if(parse_int(tok, &i)) {
		out->type = CONFIG_TYPE_INT;
		out->v.integer = i;
		return 0;
	}
	if(parse_float(tok, &d)) {
		out->type = CONFIG_TYPE_FLOAT;
		out->v.floating = d;
		return 0;
	}
	if(!strcmp(tok, "true") || !strcmp(tok, "false")) {
		out->type = CONFIG_TYPE_BOOL;
		out->v.boolean = !strcmp(tok, "true");
		return 0;
	}
	out->type = CONFIG_TYPE_STRING;
	out->v.string = strdup(tok);
	return out->v.string ? 0 : -1;
}

/* parse a comma-separated array text into items; returns the element
 * count or -1 on error (frees everything it allocated) */
static int make_array_items(const char *text, config_value_t **out_items)
{
	size_t n = 0, i;
	char **els = split_array(text, &n);
	config_value_t *items;

	if(!els || !n) {
		if(els) {
			while(n) {
				free(els[--n]);
			}
			free(els);
		}
		fprintf(stderr, "%s: bad array '%s'\n", prog, text);
		return -1;
	}
	items = calloc(n, sizeof(config_value_t));
	if(!items) {
		while(n) {
			free(els[--n]);
		}
		free(els);
		return -1;
	}
	for(i = 0; i < n; i++) {
		if(infer_element(els[i], &items[i])) {
			fprintf(stderr, "%s: bad array element '%s'\n",
				prog, els[i]);
			while(i) {
				config_value_free(&items[--i]);
			}
			free(items);
			while(n) {
				free(els[--n]);
			}
			free(els);
			return -1;
		}
	}
	for(i = 0; i < n; i++) {
		free(els[i]);
	}
	free(els);
	*out_items = items;
	return (int)n;
}

/* Build a config_value_t from the CLI text + optional forced type.
 * For arrays, *items returns malloc'd elements owned by the caller. */
static config_value_t *value_from_text(const char *text, const char *type,
				       config_value_t **array_items,
				       size_t *array_count)
{
	config_value_t *v = malloc(sizeof(config_value_t));

	if(!v) {
		return NULL;
	}
	memset(v, 0, sizeof(*v));
	if(type && !strcmp(type, "string")) {
		v->type = CONFIG_TYPE_STRING;
		v->v.string = strdup(text);
		if(!v->v.string) {
			free(v);
			return NULL;
		}
		return v;
	}
	if(type && !strcmp(type, "bool")) {
		if(!strcmp(text, "true") || !strcmp(text, "false")) {
			v->type = CONFIG_TYPE_BOOL;
			v->v.boolean = !strcmp(text, "true");
			return v;
		}
		fprintf(stderr, "%s: bad bool '%s' (want true/false)\n",
			prog, text);
		free(v);
		return NULL;
	}
	if(type && !strcmp(type, "int")) {
		int64_t i;

		if(parse_int(text, &i)) {
			v->type = CONFIG_TYPE_INT;
			v->v.integer = i;
			return v;
		}
		fprintf(stderr, "%s: bad int '%s'\n", prog, text);
		free(v);
		return NULL;
	}
	if(type && !strcmp(type, "float")) {
		double d;

		if(parse_float(text, &d)) {
			v->type = CONFIG_TYPE_FLOAT;
			v->v.floating = d;
			return v;
		}
		fprintf(stderr, "%s: bad float '%s'\n", prog, text);
		free(v);
		return NULL;
	}
	if((type && !strcmp(type, "array")) || (!type && strchr(text, ','))) {
		int n = make_array_items(text, array_items);

		if(n < 0) {
			free(v);
			return NULL;
		}
		v->type = CONFIG_TYPE_ARRAY;
		v->v.array.items = *array_items;
		v->v.array.count = (size_t)n;
		*array_count = (size_t)n;
		return v;
	}
	if(type) {
		fprintf(stderr, "%s: unknown type '%s' (bool|int|float|"
			"string|array)\n", prog, type);
		free(v);
		return NULL;
	}
	{
		int64_t i;
		double d;

		if(parse_int(text, &i)) {
			v->type = CONFIG_TYPE_INT;
			v->v.integer = i;
			return v;
		}
		if(parse_float(text, &d)) {
			v->type = CONFIG_TYPE_FLOAT;
			v->v.floating = d;
			return v;
		}
	}
	if(!strcmp(text, "true") || !strcmp(text, "false")) {
		v->type = CONFIG_TYPE_BOOL;
		v->v.boolean = !strcmp(text, "true");
		return v;
	}
	v->type = CONFIG_TYPE_STRING;
	v->v.string = strdup(text);
	if(!v->v.string) {
		free(v);
		return NULL;
	}
	return v;
}

/* ---- subcommands ---------------------------------------------------- */

static int do_list(config_scope_t only)
{
	config_scope_t s;

	for(s = CONFIG_SCOPE_USER; s <= CONFIG_SCOPE_SYSTEM; s++) {
		char **doms;
		size_t n, i;
		config_err_t e;

		if(only != (config_scope_t)-1 && s != only) {
			continue;
		}
		e = config_list_domains(s, &doms, &n);
		if(e) {
			fprintf(stderr, "%s: list %s: %s\n", prog,
				config_scope_name(s), config_strerror(e));
			return 1;
		}
		for(i = 0; i < n; i++) {
			if(only == (config_scope_t)-1) {
				printf("%s\t%s\n", config_scope_name(s), doms[i]);
			} else {
				printf("%s\n", doms[i]);
			}
		}
		config_free_domains(doms, n);
	}
	return 0;
}


/* Q6: when a System value wins over a user/shared value of the same key,
 * warn that the lower value has no effect (no deletion). Only meaningful
 * for resolved (non-scope-explicit) reads. */
static void warn_shadowed(const char *domain, const char *key,
			  config_scope_t winner)
{
	config_value_t v;
	config_err_t e;

	if(winner != CONFIG_SCOPE_SYSTEM) {
		return;
	}
	if(config_is_pinned(domain)) {
		/* pinned domains are one file on the ESP: there is no
		 * user/shared scope to shadow it */
		return;
	}
	e = config_read_scope(CONFIG_SCOPE_USER, domain, key, &v);
	if(e) {
		e = config_read_scope(CONFIG_SCOPE_SHARED, domain, key, &v);
	}
	if(!e) {
		config_value_free(&v);
		fprintf(stderr,
			"%s: warning: '%s' also set in user/shared scope "
			"(ignored: the System value wins)\n", prog, key);
	}
}

static int do_read(int has_scope, config_scope_t scope, int argc,
		   char **argv)
{
	const char *domain;
	const char *key = NULL;

	if(argc < 1) {
		usage();
		return 2;
	}
	domain = argv[0];
	if(argc >= 2) {
		key = argv[1];
	}
	if(has_scope) {
		/* read only that scope's file */
		if(key) {
			config_value_t v;
			config_err_t e = config_read_scope(scope, domain, key,
							   &v);

			if(e) {
				fprintf(stderr, "%s: %s: %s\n", prog, key,
					config_strerror(e));
				return 1;
			}
			{
				char out[512];

				raw_text(&v, out, sizeof(out));
				printf("%s\n", out);
			}
			config_value_free(&v);
			return 0;
		}
		/* whole domain in one scope: show exactly the keys that
		 * scope defines (get_all merges scopes; filter by scope) */
		{
			char **keys;
			config_value_t *vals;
			size_t n, i;
			config_err_t e = config_get_all(domain, "", &keys,
							 &vals, &n);
			int shown = 0;

			if(e) {
				fprintf(stderr, "%s: %s: %s\n", prog, domain,
					config_strerror(e));
				return 1;
			}
			for(i = 0; i < n; i++) {
				config_value_t v;
				char out[512];

				e = config_read_scope(scope, domain, keys[i], &v);
				if(e) {
					continue;
				}
				display_text(&v, out, sizeof(out));
				printf("%s = %s\n", keys[i], out);
				shown++;
				config_value_free(&v);
			}
			config_free_keys(keys, vals, n);
			if(!shown) {
				fprintf(stderr, "%s: %s: no such domain in %s\n",
					prog, domain, config_scope_name(scope));
				return 1;
			}
			return 0;
		}
	}
	/* full precedence */
	if(key) {
		config_value_t v;
		config_scope_t fs;
		config_err_t e = config_read(domain, key, &fs, &v);

		if(e == CONFIG_ERR_NOT_FOUND) {
			/* maybe a prefix of dot-nested keys */
			char **keys;
			config_value_t *vals;
			size_t n, i;

			e = config_get_all(domain, key, &keys, &vals, &n);
			if(e || !n) {
				fprintf(stderr, "%s: %s: not found\n", prog,
					key);
				return 1;
			}
			for(i = 0; i < n; i++) {
				char out[512];
				config_value_t v;
				config_scope_t fs;

				display_text(&vals[i], out, sizeof(out));
				e = config_read(domain, keys[i], &fs, &v);
				if(!e) {
					printf("%s = %s (%s)\n", keys[i], out,
					       config_scope_name(fs));
					warn_shadowed(domain, keys[i], fs);
					config_value_free(&v);
				} else {
					printf("%s = %s\n", keys[i], out);
				}
			}
			config_free_keys(keys, vals, n);
			return 0;
		}
		if(e) {
			fprintf(stderr, "%s: %s: %s\n", prog, key,
				config_strerror(e));
			return 1;
		}
		{
			char out[512];

			raw_text(&v, out, sizeof(out));
			printf("%s\n", out);
			warn_shadowed(domain, key, fs);
		}
		config_value_free(&v);
		return 0;
	}
	{
		char **keys;
		config_value_t *vals;
		size_t n, i;
		config_err_t e = config_get_all(domain, "", &keys, &vals, &n);

		if(e || !n) {
			fprintf(stderr, "%s: %s: not found\n", prog, domain);
			return 1;
		}
		for(i = 0; i < n; i++) {
			char out[512];
			config_value_t v;
			config_scope_t fs;

			display_text(&vals[i], out, sizeof(out));
			e = config_read(domain, keys[i], &fs, &v);
			if(!e) {
				printf("%s = %s (%s)\n", keys[i], out,
				       config_scope_name(fs));
				warn_shadowed(domain, keys[i], fs);
				config_value_free(&v);
			} else {
				printf("%s = %s\n", keys[i], out);
			}
		}
		config_free_keys(keys, vals, n);
		return 0;
	}
}

static int do_write(config_scope_t scope, int nargs, char **argv,
		   const char *type)
{
	const char *domain, *key, *value;
	config_err_t e;
	config_value_t *v;
	config_value_t *items = NULL;
	size_t count = 0;

	if(nargs < 3) {
		usage();
		return 2;
	}
	domain = argv[0];
	key = argv[1];
	value = argv[2];
	v = value_from_text(value, type, &items, &count);
	if(!v) {
		return 1;
	}
	switch(v->type) {
	case CONFIG_TYPE_BOOL:
		e = config_set_bool(scope, domain, key, v->v.boolean);
		break;
	case CONFIG_TYPE_INT:
		e = config_set_int(scope, domain, key, v->v.integer);
		break;
	case CONFIG_TYPE_FLOAT:
		e = config_set_float(scope, domain, key, v->v.floating);
		break;
	case CONFIG_TYPE_ARRAY:
		e = config_set_array(scope, domain, key, v->v.array.items,
				     v->v.array.count);
		break;
	default:
		e = config_set_string(scope, domain, key,
				      v->v.string ? v->v.string : "");
		break;
	}
	if(items) {
		size_t j;

		for(j = 0; j < count; j++) {
			config_value_free(&items[j]);
		}
		free(items);
	}
	free(v);
	if(e) {
		fprintf(stderr, "%s: %s: %s\n", prog, key,
			config_strerror(e));
		return 1;
	}
	return 0;
}

static int do_delete(config_scope_t scope, int argc, char **argv)
{
	config_err_t e;

	if(argc < 1) {
		usage();
		return 2;
	}
	if(argc >= 2) {
		e = config_unset(scope, argv[0], argv[1]);
	} else {
		e = config_remove_domain(scope, argv[0]);
	}
	if(e) {
		fprintf(stderr, "%s: %s: %s\n", prog, argv[0],
			config_strerror(e));
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd = NULL;
	const char *pos[64];
	const char *type = NULL;
	config_scope_t scope = CONFIG_SCOPE_USER;
	int has_scope = 0;
	int np = 0;
	int i;

	prog = argv[0];
	/* flags (-u/-g/-s scope, -type T for write) may appear before or
	 * after the subcommand; everything else keeps its order */
	for(i = 1; i < argc; i++) {
		config_scope_t s = parse_scope(argv[i]);

		if(s != (config_scope_t)-1) {
			scope = s;
			has_scope = 1;
			continue;
		}
		if(!strcmp(argv[i], "-type")) {
			if(i + 1 < argc) {
				type = argv[++i];
			} else {
				usage();
				return 2;
			}
			continue;
		}
		if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage();
			return 0;
		}
		if(!cmd) {
			cmd = argv[i];
		} else if(np < (int)(sizeof(pos) / sizeof(pos[0]))) {
			pos[np++] = argv[i];
		} else {
			fprintf(stderr, "%s: too many arguments\n", prog);
			return 2;
		}
	}
	if(!cmd) {
		usage();
		return 2;
	}
	if(!strcmp(cmd, "list")) {
		return do_list(has_scope ? scope : (config_scope_t)-1);
	}
	if(!strcmp(cmd, "read")) {
		return do_read(has_scope, scope, np, (char **)pos);
	}
	if(!strcmp(cmd, "write")) {
		return do_write(scope, np, (char **)pos, type);
	}
	if(!strcmp(cmd, "delete")) {
		return do_delete(scope, np, (char **)pos);
	}
	usage();
	return 2;
}
