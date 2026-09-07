/*
 * fnx/kernel/kconf.c
 *
 * FNX kernel.conf subset parser, docs/design/kernel-conf-plan.md (M1) and
 * docs/design/config-design.md §12.
 *
 * A lean, read-only .conf grammar matching the canonical output of the
 * userland libconfig parser (userland/libconfig.c parse_conf): line-based
 * `key = value` settings with
 *   - `#` full-line comments and blank lines,
 *   - dot-nested keys kept as one flat key (console, system.kernel.root),
 *   - bare or "quoted" string values, `true`/`false`, integers incl. `0x`,
 *   - `key =` (empty value) accepted,
 *   - duplicate keys: last wins (each key/value pair is emitted in file
 *     order; the caller applies the last one),
 *   - no scopes/arrays (kernel subset; `{`/`}` lines are rejected and
 *     skipped with a warning, never a halt).
 *
 * The parser has no kernel dependencies so the same object can be built
 * on the host for the conformance corpus (tools/kconf_corpus.sh): both
 * this parser and the userland parser must produce the same key/value
 * sequence from the same file.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/string.h>
#include "kconf.h"

static int kconf_is_keychar(char c)
{
	if(c >= 'a' && c <= 'z') {
		return 1;
	}
	if(c >= 'A' && c <= 'Z') {
		return 1;
	}
	if(c >= '0' && c <= '9') {
		return 1;
	}
	return c == '_' || c == '-' || c == '.' || c == '/' || c == '+';
}

static int kconf_hexval(char c)
{
	if(c >= '0' && c <= '9') {
		return c - '0';
	}
	if(c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if(c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

/*
 * kconf_next() - parse the next setting from 'data' (size-bounded, may
 * also be NUL-terminated) starting at *off. Returns 1 and fills *out on
 * a setting (kind KCONF_KV_ERR for a skipped malformed line), 0 at EOF.
 */
int kconf_next(const char *data, unsigned int size, unsigned int *off,
	       struct kconf_kv *out)
{
	const char *p, *end, *eq;
	char key[128], val[512];
	int n, kind, is_true;

	out->kind = KCONF_KV_ERR;
	for(;;) {
		/* find the next line */
		p = data + *off;
		end = p;
		while(end < data + size && *end != '\n') {
			end++;
		}
		*off = (unsigned int)(end - data);
		if(end < data + size) {
			(*off)++;	/* consume '\n' */
		}

		/* skip leading WS, strip a trailing CR */
		while(p < end && (*p == ' ' || *p == '\t')) {
			p++;
		}
		while(end > p && (end[-1] == '\r' || end[-1] == ' ' ||
				  end[-1] == '\t')) {
			end--;
		}

		if(end == p) {
			if(p >= data + size && *off >= size) {
				return 0;	/* clean EOF */
			}
			continue;	/* blank line in the middle */
		}
		if(*p == '#') {
			continue;	/* comment */
		}
		if(*p == '{' || *p == '}') {
			return 1;	/* KCONF_KV_ERR: no scopes in the subset */
		}
		break;
	}

	/* split at the first '=' */
	eq = p;
	while(eq < end && *eq != '=') {
		eq++;
	}
	if(eq == end) {
		return 1;	/* no '=': malformed */
	}

	/* key: trim trailing WS before '=' */
	n = 0;
	while(p < eq) {
		if(n >= (int)sizeof(key) - 1) {
			return 1;	/* key too long */
		}
		key[n++] = *p++;
	}
	while(n > 0 && (key[n - 1] == ' ' || key[n - 1] == '\t')) {
		n--;
	}
	if(n == 0) {
		return 1;
	}
	{
		int i;

		for(i = 0; i < n; i++) {
			if(!kconf_is_keychar(key[i])) {
				return 1;	/* invalid key */
			}
		}
	}
	key[n] = 0;

	/* value: trim leading WS; empty `key =` is accepted */
	p = eq + 1;
	while(p < end && (*p == ' ' || *p == '\t')) {
		p++;
	}
	kind = KCONF_KV_STRING;
	is_true = 0;
	if(p == end) {
		/* empty value */
		val[0] = 0;
	} else if(*p == '"') {
		/* quoted string (backslash-escaped \" and \\) */
		n = 0;
		p++;
		while(p < end && *p != '"') {
			if(*p == '\\' && p + 1 < end &&
			   (p[1] == '"' || p[1] == '\\')) {
				p++;
			}
			if(n < (int)sizeof(val) - 1) {
				val[n++] = *p;
			}
			p++;
		}
		if(p == end) {
			return 1;	/* unterminated quote */
		}
		val[n] = 0;
	} else {
		/* bare value: a single token to end-of-line */
		n = 0;
		while(p < end && n < (int)sizeof(val) - 1) {
			val[n++] = *p++;
		}
		val[n] = 0;
		/* trailing WS was already trimmed from the line */

		/* classify: bool / int / string */
		if(!strcmp(val, "true") || !strcmp(val, "false")) {
			kind = KCONF_KV_BOOL;
			is_true = !strcmp(val, "true");
		} else {
			const char *q = val;
			int isnum = 1;

			if(*q == '-') {
				q++;	/* signed ints are accepted */
			}
			if(!*q) {
				isnum = 0;
			} else if(q[0] == '0' && q[1] == 'x') {
				int i;

				q += 2;
				for(i = 0; q[i]; i++) {
					if(kconf_hexval(q[i]) < 0) {
						isnum = 0;
					}
				}
				if(i == 0) {
					isnum = 0;
				}
			} else {
				int i;

				for(i = 0; q[i]; i++) {
					if(q[i] < '0' || q[i] > '9') {
						isnum = 0;
					}
				}
			}
			if(isnum) {
				kind = KCONF_KV_INT;
			}
		}
	}

	strncpy(out->key, key, sizeof(out->key));
	strncpy(out->value, val, sizeof(out->value));
	out->kind = kind;
	out->is_true = is_true;
	return 1;
}
