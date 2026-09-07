/* libconfig.c — implementation of userland/libconfig.h (FNX userland).
 *
 * One plain-text "key = value" file per reverse-DNS domain in a
 * Configuration/ directory at each of the three scopes; reads resolve
 * system -> user -> shared; writes go to an explicit scope atomically
 * (temp + fsync + rename). Grammar: docs/design/config-design.md §10.
 *
 * Scope roots are absolute under the filesystem root:
 *   /System/Configuration, /Shared/Configuration,
 *   /Users/<user>/Configuration
 * $FNX_CONFIG_ROOT (test hook) re-roots all three (default: "/").
 *
 * This library is userland-only and single-threaded (per the header).
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libconfig.h>

/* ---- limits (docs §10, protective) -------------------------------- */

#define CONF_MAX_LINE		4096
#define CONF_MAX_FILE		(1024 * 1024)
#define CONF_MAX_SEGMENT	64
#define CONF_MAX_KEY		255

/* ---- one parsed key/value ------------------------------------------ */

struct entry {
	char *key;
	config_value_t val;
	struct entry *next;
};

/* ---- path helpers -------------------------------------------------- */

static const char *config_root(void)
{
	const char *r = getenv("FNX_CONFIG_ROOT");

	return (r && *r) ? r : "/";
}

static const char *user_name(void)
{
	static char buf[64];
	struct passwd *pw;
	const char *u;

	pw = getpwuid(getuid());
	if(pw && pw->pw_name && *pw->pw_name) {
		snprintf(buf, sizeof(buf), "%s", pw->pw_name);
		return buf;
	}
	u = getenv("USER");
	if(u && *u) {
		snprintf(buf, sizeof(buf), "%s", u);
		return buf;
	}
	return "root";
}

/* Builds "<root>/<scope>/Configuration" into out; the caller appends
 * "/<domain>.conf". */
static void scope_dir_path(config_scope_t scope, char *out, size_t outsz)
{
	switch(scope) {
	case CONFIG_SCOPE_USER:
		snprintf(out, outsz, "%s/Users/%s/Configuration",
			config_root(), user_name());
		break;
	case CONFIG_SCOPE_SHARED:
		snprintf(out, outsz, "%s/Shared/Configuration", config_root());
		break;
	default:
		snprintf(out, outsz, "%s/System/Configuration", config_root());
		break;
	}
}

/*
 * Pinned (single-file) domains: config-design §12 'system.kernel' lives
 * on the ESP at /System/ESP/EFI/BOOT/kernel.conf (where the EFI
 * stub reads it), not under the three scope roots.
 * The alias ignores the scope entirely (no system/user/shared merge) and
 * honors FNX_CONFIG_ROOT like every other path, so host tests can re-root
 * it (in the guest config_root() == "/" gives the exact §12 path).
 */
#define KERNEL_DOMAIN		"system.kernel"
/* the EFI stub reads kernel.conf from the bootloader's own directory
 * (EFI/BOOT/kernel.conf on the ESP volume); /System/ESP mounts the
 * volume ROOT, so the pinned path includes the EFI/BOOT prefix */
#define KERNEL_CONF_RELPATH	"/System/ESP/EFI/BOOT/kernel.conf"

static int pinned_domain(const char *domain)
{
	return domain && !strcmp(domain, KERNEL_DOMAIN);
}

bool config_is_pinned(const char *domain)
{
	return pinned_domain(domain);
}

/* Full on-disk path of a domain file in a scope. Returns 0 or
 * CONFIG_ERR_INVALID (bad scope/domain/truncation). */
static config_err_t domain_path(config_scope_t scope, const char *domain,
				char *out, size_t outsz)
{
	size_t base;

	if(pinned_domain(domain)) {
		/* the ESP alias: one file, no scope roots (§12) */
		if(strlen(KERNEL_CONF_RELPATH) + strlen(config_root()) + 1
		   > outsz) {
			return CONFIG_ERR_INVALID;
		}
		snprintf(out, outsz, "%s%s", config_root(),
			 KERNEL_CONF_RELPATH);
		return CONFIG_OK;
	}
	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM) {
		return CONFIG_ERR_INVALID;
	}
	scope_dir_path(scope, out, outsz);
	base = strlen(out);
	if(base + strlen(domain) + 7 > outsz) {	/* "/x.conf" + NUL */
		return CONFIG_ERR_INVALID;
	}
	snprintf(out + base, outsz - base, "/%s.conf", domain);
	return CONFIG_OK;
}

/* ---- validation ---------------------------------------------------- */

/* one segment: ^[A-Za-z_][A-Za-z0-9_-]*$, <= CONF_MAX_SEGMENT chars */
static bool valid_segment(const char *s, size_t len)
{
	size_t i;

	if(!len || len > CONF_MAX_SEGMENT) {
		return false;
	}
	if(!(isalpha((unsigned char)s[0]) || s[0] == '_')) {
		return false;
	}
	for(i = 1; i < len; i++) {
		unsigned char c = s[i];

		if(!(isalnum(c) || c == '_' || c == '-')) {
			return false;
		}
	}
	return true;
}

/* dot-separated non-empty segments */
static bool valid_dotted(const char *s, size_t len)
{
	const char *seg = s;
	size_t i, seglen = 0;

	if(!s || !len || len > CONF_MAX_KEY) {
		return false;
	}
	if(s[0] == '.' || s[len - 1] == '.') {
		return false;
	}
	for(i = 0; i <= len; i++) {
		if(i == len || s[i] == '.') {
			if(!valid_segment(seg, seglen)) {
				return false;
			}
			seg = s + i + 1;
			seglen = 0;
		} else {
			seglen++;
		}
	}
	return true;
}

bool config_valid_domain(const char *domain)
{
	if(!domain || strchr(domain, '/')) {
		return false;
	}
	return valid_dotted(domain, strlen(domain));
}

bool config_valid_key(const char *key)
{
	if(!key) {
		return false;
	}
	return valid_dotted(key, strlen(key));
}

/* ---- value memory -------------------------------------------------- */

static void value_free(config_value_t *v)
{
	if(!v) {
		return;
	}
	switch(v->type) {
	case CONFIG_TYPE_STRING:
		free((void *)v->v.string);
		break;
	case CONFIG_TYPE_ARRAY:
		if(v->v.array.items) {
			size_t i;

			for(i = 0; i < v->v.array.count; i++) {
				value_free(&v->v.array.items[i]);
			}
			free(v->v.array.items);
		}
		break;
	default:
		break;
	}
	v->type = CONFIG_TYPE_STRING;
	v->v.string = NULL;
}

/* deep copy; dst must be uninitialized or already freed */
static config_err_t value_copy(config_value_t *dst, const config_value_t *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->type = src->type;
	switch(src->type) {
	case CONFIG_TYPE_STRING:
		if(src->v.string) {
			dst->v.string = strdup(src->v.string);
			if(!dst->v.string) {
				dst->v.string = NULL;
				return CONFIG_ERR_NOMEM;
			}
		}
		break;
	case CONFIG_TYPE_ARRAY: {
		size_t i;

		if(src->v.array.count) {
			dst->v.array.items = calloc(src->v.array.count,
						      sizeof(config_value_t));
			if(!dst->v.array.items) {
				return CONFIG_ERR_NOMEM;
			}
		}
		dst->v.array.count = src->v.array.count;
		for(i = 0; i < src->v.array.count; i++) {
			config_err_t e = value_copy(&dst->v.array.items[i],
						    &src->v.array.items[i]);

			if(e) {
				dst->v.array.count = i;
				value_free(dst);
				return e;
			}
		}
		break;
	}
	default:
		dst->v = src->v;	/* scalars: plain copy */
		break;
	}
	return CONFIG_OK;
}

void config_value_free(config_value_t *value)
{
	value_free(value);
}

/* ---- scalar inference (docs §10: int -> float -> bool -> string) --- */

static bool token_is_int(const char *s, int64_t *out)
{
	const char *p = s;
	int sign = 1;
	long long v;
	char *end;

	if(*p == '+' || *p == '-') {
		if(*p == '-') {
			sign = -1;
		}
		p++;
	}
	if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		if(!isxdigit((unsigned char)*p)) {
			return false;
		}
		errno = 0;
		v = strtoll(p, &end, 16);
	} else {
		if(!isdigit((unsigned char)*p)) {
			return false;
		}
		errno = 0;
		v = strtoll(p, &end, 10);
	}
	if(errno || *end) {
		return false;
	}
	*out = (int64_t)sign * v;
	return true;
}

static bool token_is_float(const char *s, double *out)
{
	/* reject the non-grammar spellings strtod would otherwise eat */
	const char *t = s;
	char *end;
	double d;

	if(*t == '+' || *t == '-') {
		t++;
	}
	if(!strcasecmp(t, "inf") || !strcasecmp(t, "infinity") ||
	   !strcasecmp(t, "nan")) {
		return false;
	}
	errno = 0;
	d = strtod(s, &end);
	if(errno || *end) {
		return false;
	}
	*out = d;
	return true;
}

/* infer one bare (unquoted) scalar token */
static config_err_t infer_scalar(const char *tok, config_value_t *out)
{
	int64_t i;
	double d;

	memset(out, 0, sizeof(*out));
	if(token_is_int(tok, &i)) {
		out->type = CONFIG_TYPE_INT;
		out->v.integer = i;
		return CONFIG_OK;
	}
	if(token_is_float(tok, &d)) {
		out->type = CONFIG_TYPE_FLOAT;
		out->v.floating = d;
		return CONFIG_OK;
	}
	if(!strcmp(tok, "true") || !strcmp(tok, "false")) {
		out->type = CONFIG_TYPE_BOOL;
		out->v.boolean = !strcmp(tok, "true");
		return CONFIG_OK;
	}
	out->type = CONFIG_TYPE_STRING;
	out->v.string = strdup(tok);
	if(!out->v.string) {
		return CONFIG_ERR_NOMEM;
	}
	return CONFIG_OK;
}

/* ---- the .conf parser ---------------------------------------------- */

static void entries_free(struct entry *list);

/* parse a quoted string at *pp; on success *pp points past the closing
 * quote and *out holds a malloc'd string. */
static config_err_t parse_quoted(const char **pp, char **out)
{
	const char *p = *pp;
	char buf[CONF_MAX_LINE];
	size_t n = 0;

	p++;	/* opening quote */
	while(*p && *p != '"') {
		if(*p == '\\') {
			p++;
			switch(*p) {
			case '"':	buf[n++] = '"'; p++; break;
			case '\\':	buf[n++] = '\\'; p++; break;
			case 'n':	buf[n++] = '\n'; p++; break;
			case 't':	buf[n++] = '\t'; p++; break;
			default:
				return CONFIG_ERR_PARSE;
			}
		} else {
			if(n + 1 >= sizeof(buf)) {
				return CONFIG_ERR_PARSE;
			}
			buf[n++] = *p++;
		}
	}
	if(*p != '"') {
		return CONFIG_ERR_PARSE;	/* unterminated */
	}
	buf[n] = '\0';
	*pp = p + 1;
	*out = strdup(buf);
	return *out ? CONFIG_OK : CONFIG_ERR_NOMEM;
}

/* parse one array element (quoted or bare); *pp advances past it */
static config_err_t parse_element(const char **pp, config_value_t *out)
{
	const char *p = *pp;

	while(*p == ' ' || *p == '\t') {
		p++;
	}
	if(*p == '"') {
		char *s;
		config_err_t e = parse_quoted(&p, &s);

		if(e) {
			return e;
		}
		memset(out, 0, sizeof(*out));
		out->type = CONFIG_TYPE_STRING;
		out->v.string = s;
		*pp = p;
		return CONFIG_OK;
	}
	{
		char tok[CONF_MAX_LINE];
		size_t n = 0;
		config_err_t e;

		while(*p && *p != ',' && *p != ' ' && *p != '\t' &&
		      *p != '"') {
			if(n + 1 < sizeof(tok)) {
				tok[n++] = *p;
			} else {
				return CONFIG_ERR_PARSE;
			}
			p++;
		}
		if(*p == '"' || n == 0) {
			return CONFIG_ERR_PARSE;
		}
		tok[n] = '\0';
		e = infer_scalar(tok, out);
		if(e) {
			return e;
		}
		*pp = p;
		return CONFIG_OK;
	}
}

/* does the text have a comma outside any quoted region? */
static bool has_top_level_comma(const char *p)
{
	bool inq = false;

	for(; *p; p++) {
		if(inq) {
			if(*p == '\\') {
				p++;
			} else if(*p == '"') {
				inq = false;
			}
		} else if(*p == '"') {
			inq = true;
		} else if(*p == ',') {
			return true;
		}
	}
	return false;
}

/* parse an array whose first element is already parsed (first != NULL)
 * or starts at *pp; consumes the whole rest of the value text. */
static config_err_t parse_array_body(const char **pp, config_value_t *first,
				     config_value_t *out)
{
	const char *q = *pp;

	memset(out, 0, sizeof(*out));
	out->type = CONFIG_TYPE_ARRAY;
	out->v.array.count = 0;
	out->v.array.items = NULL;
	while(1) {
		config_value_t el;
		config_err_t e;
		config_value_t *ni;

		if(first) {
			el = *first;
			first = NULL;
		} else {
			e = parse_element(&q, &el);
			if(e) {
				value_free(out);
				return e;
			}
		}
		ni = realloc(out->v.array.items,
			     (out->v.array.count + 1) * sizeof(config_value_t));
		if(!ni) {
			value_free(&el);
			value_free(out);
			return CONFIG_ERR_NOMEM;
		}
		out->v.array.items = ni;
		out->v.array.items[out->v.array.count++] = el;
		while(*q == ' ' || *q == '\t') {
			q++;
		}
		if(*q == ',') {
			q++;
			continue;
		}
		if(*q == '\0') {
			*pp = q;
			return CONFIG_OK;
		}
		value_free(out);
		return CONFIG_ERR_PARSE;
	}
}

/* Parse the text after '=' into a value. Whole-value rules:
 *  - a leading quote starts a quoted string; a comma right after it
 *    makes the value an array whose first element is that string;
 *  - otherwise a top-level comma makes it an array;
 *  - otherwise the token must contain no whitespace (nor any of
 *    # = " mid-token) and is inferred (int -> float -> bool); an empty
 *    value is an empty string. */
static config_err_t parse_value(const char *text, config_value_t *out)
{
	const char *p = text;
	while(*p == ' ' || *p == '\t') {
		p++;
	}
	if(*p == '"') {
		char *s;
		const char *q;
		config_value_t first;
		config_err_t e = parse_quoted(&p, &s);

		if(e) {
			return e;
		}
		q = p;
		while(*q == ' ' || *q == '\t') {
			q++;
		}
		if(*q == '\0') {
			memset(out, 0, sizeof(*out));
			out->type = CONFIG_TYPE_STRING;
			out->v.string = s;
			return CONFIG_OK;
		}
		if(*q == ',') {
			/* an array beginning with the quoted element */
			first.type = CONFIG_TYPE_STRING;
			first.v.string = s;
			return parse_array_body(&q, &first, out);
		}
		free(s);
		return CONFIG_ERR_PARSE;	/* junk after the string */
	}
	if(!*p) {
		memset(out, 0, sizeof(*out));	/* empty value = empty string */
		out->type = CONFIG_TYPE_STRING;
		out->v.string = strdup("");
		return out->v.string ? CONFIG_OK : CONFIG_ERR_NOMEM;
	}
	if(has_top_level_comma(p)) {
		return parse_array_body(&p, NULL, out);
	}
	{
		/* bare scalar: no whitespace and none of # = " */
		const char *q = p;

		while(*q && *q != ' ' && *q != '\t' && *q != '#' &&
		      *q != '=' && *q != '"') {
			q++;
		}
		if(*q) {
			return CONFIG_ERR_PARSE;	/* must be quoted */
		}
		return infer_scalar(p, out);
	}
}

/* ---- group-record containers (docs §3.1) --------------------------- */

/* One open container while parsing. The top level is a container with
 * prefix "" that never pops. `records` = the names of the blocks opened
 * directly in this container (for duplicate-record-name detection). */
struct pctx {
	char *prefix;
	struct pctx *up;
	char **records;
	int nrecords, crecords;
};

/* free a heap container (not the stack-allocated top level) */
static void pctx_destroy(struct pctx *c)
{
	int i;

	for(i = 0; i < c->nrecords; i++) {
		free(c->records[i]);
	}
	free(c->records);
	free(c->prefix);
	free(c);
}

static void pctx_free_chain(struct pctx *c)
{
	while(c) {
		struct pctx *up = c->up;

		pctx_destroy(c);
		c = up;
	}
}

/* Bind a block (record) name in container c. Returns
 * CONFIG_ERR_PARSE on a duplicate record name (docs §3.1). */
static config_err_t pctx_bind_record(struct pctx *c, const char *name)
{
	int i;

	for(i = 0; i < c->nrecords; i++) {
		if(!strcmp(c->records[i], name)) {
			return CONFIG_ERR_PARSE;	/* duplicate record */
		}
	}
	if(c->nrecords == c->crecords) {
		int cap = c->crecords ? c->crecords * 2 : 4;
		char **nr = realloc(c->records, cap * sizeof(char *));

		if(!nr) {
			return CONFIG_ERR_NOMEM;
		}
		c->records = nr;
		c->crecords = cap;
	}
	c->records[c->nrecords++] = strdup(name);
	return c->records[c->nrecords - 1] ? CONFIG_OK : CONFIG_ERR_NOMEM;
}

static void blocks_free(char **blocks, int n)
{
	int i;

	if(!blocks) {
		return;
	}
	for(i = 0; i < n; i++) {
		free(blocks[i]);
	}
	free(blocks);
}

/* Parse whole-file text into an entry list (duplicate keys: last
 * occurrence wins, keeping the first occurrence's position). When
 * blocks/nblocks (when provided) receive the names of the top-level
 * explicit blocks (record domains) in the file, which the canonical
 * writer uses to choose nested spelling. */
static config_err_t parse_conf(const char *text, size_t len,
			       struct entry **list, char ***blocks,
			       int *nblocks)
{
	size_t off = 0;
	struct entry *head = NULL, *tail = NULL;
	struct pctx *top;
	struct pctx *cur;
	char **explicit = NULL;

	top = calloc(1, sizeof(*top));
	if(!top) {
		return CONFIG_ERR_NOMEM;
	}
	cur = top;
	int nexp = 0, cexp = 0;
	config_err_t rc = CONFIG_OK;

	*list = NULL;
	if(blocks) {
		*blocks = NULL;
	}
	if(nblocks) {
		*nblocks = 0;
	}
	if(len >= 3 && (unsigned char)text[0] == 0xEF &&
	   (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
		off = 3;	/* skip a UTF-8 BOM */
	}
	while(off < len && !rc) {
		char line[CONF_MAX_LINE + 1];
		size_t n = 0;
		char *p;

		while(off < len && text[off] != '\n' && n < CONF_MAX_LINE) {
			line[n++] = text[off++];
		}
		if(off < len && text[off] == '\n') {
			off++;	/* consume the newline */
		} else if(n == CONF_MAX_LINE && off < len) {
			rc = CONFIG_ERR_PARSE;	/* line too long */
			break;
		}
		line[n] = '\0';
		/* strip a trailing CR (CRLF tolerance) + trailing WS */
		while(n && (line[n - 1] == '\r' || line[n - 1] == ' ' ||
			    line[n - 1] == '\t')) {
			line[--n] = '\0';
		}
		p = line;
		while(*p == ' ' || *p == '\t') {
			p++;
		}
		if(!*p || *p == '#') {
			continue;	/* empty or full-line comment */
		}
		if(*p == '}') {
			/* close a group (only whitespace may follow) */
			char *q = p + 1;

			while(*q == ' ' || *q == '\t') {
				q++;
			}
			if(*q || cur == top) {
				rc = CONFIG_ERR_PARSE;
				break;
			}
			{
				struct pctx *pop = cur;

				cur = pop->up;
				pop->up = NULL;
				pctx_destroy(pop);
			}
			continue;
		}
		{
			char *eq = strchr(p, '=');
			char *keyend;
			char *val;
			char *rel;
			config_err_t e;

			if(!eq) {
				rc = CONFIG_ERR_PARSE;	/* no '=' */
				break;
			}
			keyend = eq;
			while(keyend > p && (keyend[-1] == ' ' ||
					     keyend[-1] == '\t')) {
				keyend--;
			}
			if(keyend == p) {
				rc = CONFIG_ERR_PARSE;
				break;
			}
			*keyend = '\0';
			rel = p;
			if(!config_valid_key(rel)) {
				rc = CONFIG_ERR_PARSE;
				break;
			}
			val = eq + 1;
			while(*val == ' ' || *val == '\t') {
				val++;
			}
			if(*val == '{') {
				/* group open. The record name is a single
				 * segment; "key = {" opens a multi-line
				 * group, "key = {}" is an empty group. */
				char *q = val + 1;
				int empty = 0;

				if(!valid_segment(rel, strlen(rel))) {
					rc = CONFIG_ERR_PARSE;
					break;
				}
				while(*q == ' ' || *q == '\t') {
					q++;
				}
				if(*q == '}') {
					empty = 1;
					q++;
					while(*q == ' ' || *q == '\t') {
						q++;
					}
					if(*q) {
						rc = CONFIG_ERR_PARSE;
						break;
					}
				} else if(*q) {
					/* a bare value may not begin with
					 * '{' (docs §3.1) */
					rc = CONFIG_ERR_PARSE;
					break;
				}
				e = pctx_bind_record(cur, rel);
				if(e) {
					rc = e;
					break;
				}
				if(cur == top) {
					/* remember top-level explicit
					 * blocks for the writer */
					char **nx;
					char *dup;

					if(nexp == cexp) {
						int cap = cexp ? cexp * 2 : 4;

						nx = realloc(explicit,
							      cap * sizeof(char *));
						if(!nx) {
							rc = CONFIG_ERR_NOMEM;
							break;
						}
						explicit = nx;
						cexp = cap;
					}
					dup = strdup(rel);
					if(!dup) {
						rc = CONFIG_ERR_NOMEM;
						break;
					}
					explicit[nexp++] = dup;
				}
				if(!empty) {
					/* push the new container */
					struct pctx *pc = calloc(1,
								 sizeof(*pc));
					size_t pl = cur->prefix ?
						    strlen(cur->prefix) : 0;
					size_t rl = strlen(rel);

					if(!pc) {
						rc = CONFIG_ERR_NOMEM;
						break;
					}
					pc->prefix = malloc(pl + rl + 2);
					if(!pc->prefix) {
						free(pc);
						rc = CONFIG_ERR_NOMEM;
						break;
					}
					if(pl) {
						memcpy(pc->prefix, cur->prefix,
						       pl);
						pc->prefix[pl] = '.';
						memcpy(pc->prefix + pl + 1,
						       rel, rl + 1);
					} else {
						memcpy(pc->prefix, rel, rl + 1);
					}
					pc->up = cur;
					cur = pc;
				}
				continue;
			}
			{
				/* a plain assignment: full key = the
				 * container prefix + the relative key */
				size_t pl = cur->prefix ?
					    strlen(cur->prefix) : 0;
				size_t rl = strlen(rel);
				char *full;
				config_value_t v;
				struct entry *cur2, *en;

				if(pl + rl + 2 > CONF_MAX_KEY + 1) {
					rc = CONFIG_ERR_PARSE;
					break;
				}
				full = malloc(pl + rl + 2);
				if(!full) {
					rc = CONFIG_ERR_NOMEM;
					break;
				}
				if(pl) {
					memcpy(full, cur->prefix, pl);
					full[pl] = '.';
					memcpy(full + pl + 1, rel, rl + 1);
				} else {
					memcpy(full, rel, rl + 1);
				}
				if(!config_valid_key(full)) {
					free(full);
					rc = CONFIG_ERR_PARSE;
					break;
				}
				e = parse_value(val, &v);
				if(e) {
					free(full);
					rc = e;
					break;
				}
				/* A name may not be both a stored scalar
				 * and a container (docs §3.1: records are
				 * identity-bearing). Exact duplicates are
				 * last-wins. */
				for(cur2 = head; cur2; cur2 = cur2->next) {
					size_t fl = strlen(full);
					size_t el = strlen(cur2->key);

					if(!strcmp(cur2->key, full)) {
						value_free(&cur2->val);
						cur2->val = v;
						free(full);
						goto next_line;
					}
					if(el > fl &&
					   !strncmp(cur2->key, full, fl) &&
					   cur2->key[fl] == '.') {
						/* a container already exists
						 * under this leaf key */
						value_free(&v);
						free(full);
						rc = CONFIG_ERR_PARSE;
						break;
					}
					if(el < fl &&
					   !strncmp(full, cur2->key, el) &&
					   full[el] == '.') {
						/* this key needs an existing
						 * leaf as a container */
						value_free(&v);
						free(full);
						rc = CONFIG_ERR_PARSE;
						break;
					}
				}
				if(rc) {
					break;
				}
				en = malloc(sizeof(struct entry));
				if(!en) {
					value_free(&v);
					free(full);
					rc = CONFIG_ERR_NOMEM;
					break;
				}
				en->key = full;
				en->val = v;
				en->next = NULL;
				if(tail) {
					tail->next = en;
				} else {
					head = en;
				}
				tail = en;
			}
next_line:
			;
		}
	}
	if(!rc && cur != top) {
		rc = CONFIG_ERR_PARSE;	/* unbalanced '{' */
	}
	/* hand ownership out on success, clean up on error. The chain of
	 * still-open containers (always including the heap `top`) is freed
	 * uniformly; containers closed during the parse were already
	 * destroyed at their '}' */
	if(!rc) {
		*list = head;
		if(blocks) {
			*blocks = explicit;
		}
		if(nblocks) {
			*nblocks = nexp;
		}
		pctx_free_chain(cur);
		return CONFIG_OK;
	}
	entries_free(head);
	pctx_free_chain(cur);
	blocks_free(explicit, nexp);
	return rc;
}
static void entries_free(struct entry *list)
{
	while(list) {
		struct entry *n = list->next;

		free(list->key);
		value_free(&list->val);
		free(list);
		list = n;
	}
}

/* Load + parse a scope's domain file. *found is 1 when the file
 * existed (even if empty). blocks/nblocks (either may be NULL) return
 * the top-level explicit-block names for the canonical writer. */
static config_err_t load_entries(config_scope_t scope, const char *domain,
				 struct entry **list, int *found,
				 char ***blocks, int *nblocks)
{
	char path[PATH_MAX];
	char *text;
	struct stat st;
	int fd, n;
	config_err_t e;
	config_err_t rc;

	*found = 0;
	*list = NULL;
	if(blocks) {
		*blocks = NULL;
	}
	if(nblocks) {
		*nblocks = 0;
	}
	rc = domain_path(scope, domain, path, sizeof(path));
	if(rc) {
		return rc;
	}
	fd = open(path, O_RDONLY);
	if(fd < 0) {
		if(errno == ENOENT) {
			return CONFIG_ERR_NOT_FOUND;
		}
		if(errno == EACCES) {
			return CONFIG_ERR_ACCESS;
		}
		return CONFIG_ERR_IO;
	}
	if(fstat(fd, &st)) {
		close(fd);
		return CONFIG_ERR_IO;
	}
	if(st.st_size > CONF_MAX_FILE) {
		close(fd);
		return CONFIG_ERR_PARSE;
	}
	*found = 1;
	text = malloc(st.st_size + 1);
	if(!text) {
		close(fd);
		return CONFIG_ERR_NOMEM;
	}
	n = 0;
	while(n < st.st_size) {
		ssize_t r = read(fd, text + n, st.st_size - n);

		if(r <= 0) {
			close(fd);
			free(text);
			return CONFIG_ERR_IO;
		}
		n += r;
	}
	close(fd);
	text[n] = '\0';
	e = parse_conf(text, n, list, blocks, nblocks);
	free(text);
	return e;
}

static struct entry *entry_find(struct entry *list, const char *key)
{
	for(; list; list = list->next) {
		if(!strcmp(list->key, key)) {
			return list;
		}
	}
	return NULL;
}

/* ---- reading ------------------------------------------------------- */

static config_err_t read_scope_internal(config_scope_t scope,
					const char *domain, const char *key,
					config_value_t *out)
{
	struct entry *list;
	int found;
	config_err_t e = load_entries(scope, domain, &list, &found,
				      NULL, NULL);
	struct entry *en;

	if(e) {
		return e;
	}
	en = entry_find(list, key);
	if(!en) {
		entries_free(list);
		return CONFIG_ERR_NOT_FOUND;
	}
	e = value_copy(out, &en->val);
	entries_free(list);
	return e;
}

config_err_t config_read_scope(config_scope_t scope, const char *domain,
			       const char *key, config_value_t *out)
{
	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !key || !out || !config_valid_domain(domain) ||
	   !config_valid_key(key)) {
		return CONFIG_ERR_INVALID;
	}
	return read_scope_internal(scope, domain, key, out);
}

/* resolution order: system -> user -> shared (docs plan D4; the enum
 * values are USER=0 < SHARED=1 < SYSTEM=2, so it is not a simple walk) */
static const config_scope_t scope_order[] = {
	CONFIG_SCOPE_SYSTEM,
	CONFIG_SCOPE_USER,
	CONFIG_SCOPE_SHARED,
};

config_err_t config_read(const char *domain, const char *key,
			 config_scope_t *found_scope, config_value_t *out)
{
	size_t k;
	config_scope_t s;

	if(!domain || !key || !out || !config_valid_domain(domain) ||
	   !config_valid_key(key)) {
		return CONFIG_ERR_INVALID;
	}
	/* full precedence: system -> user -> shared (docs plan D4) */
	for(k = 0; k < 3; k++) {
		s = scope_order[k];
		config_err_t e = read_scope_internal(s, domain, key, out);

		if(e == CONFIG_ERR_NOT_FOUND) {
			continue;
		}
		if(e) {
			return e;	/* PARSE/IO/ACCESS abort resolution */
		}
		if(found_scope) {
			*found_scope = s;
		}
		return CONFIG_OK;
	}
	return CONFIG_ERR_NOT_FOUND;
}

config_err_t config_resolve(const char *domain, const char *key,
			    config_scope_t *scope)
{
	config_value_t v;
	config_err_t e;

	if(!scope) {
		return CONFIG_ERR_INVALID;
	}
	e = config_read(domain, key, scope, &v);
	if(!e) {
		value_free(&v);
	}
	return e;
}

/* ---- typed getters (values live in a static slot until the next
 * value-returning call) ---------------------------------------------- */

static config_value_t lc_last;

static config_err_t typed_get(const char *domain, const char *key,
			      config_type_t want, config_value_t **out)
{
	config_err_t e;

	value_free(&lc_last);	/* drop the previous call's value */
	e = config_read(domain, key, NULL, &lc_last);
	if(e) {
		return e;
	}
	if(lc_last.type != want) {
		value_free(&lc_last);
		return CONFIG_ERR_TYPE;
	}
	*out = &lc_last;
	return CONFIG_OK;
}

config_err_t config_get_string(const char *domain, const char *key,
			       const char **out)
{
	config_value_t *v;
	config_err_t e;

	if(!out) {
		return CONFIG_ERR_INVALID;
	}
	e = typed_get(domain, key, CONFIG_TYPE_STRING, &v);
	if(e) {
		return e;
	}
	*out = v->v.string;
	return CONFIG_OK;
}

config_err_t config_get_bool(const char *domain, const char *key,
			     bool *out)
{
	config_value_t *v;
	config_err_t e;

	if(!out) {
		return CONFIG_ERR_INVALID;
	}
	e = typed_get(domain, key, CONFIG_TYPE_BOOL, &v);
	if(e) {
		return e;
	}
	*out = v->v.boolean;
	return CONFIG_OK;
}

config_err_t config_get_int(const char *domain, const char *key,
			    int64_t *out)
{
	config_value_t *v;
	config_err_t e;

	if(!out) {
		return CONFIG_ERR_INVALID;
	}
	e = typed_get(domain, key, CONFIG_TYPE_INT, &v);
	if(e) {
		return e;
	}
	*out = v->v.integer;
	return CONFIG_OK;
}

config_err_t config_get_float(const char *domain, const char *key,
			      double *out)
{
	config_value_t *v;
	config_err_t e;

	if(!out) {
		return CONFIG_ERR_INVALID;
	}
	e = typed_get(domain, key, CONFIG_TYPE_FLOAT, &v);
	if(e) {
		return e;
	}
	*out = v->v.floating;
	return CONFIG_OK;
}

config_err_t config_get_array(const char *domain, const char *key,
			      config_value_t **items, size_t *count)
{
	config_value_t *v;
	config_err_t e;

	if(!items || !count) {
		return CONFIG_ERR_INVALID;
	}
	e = typed_get(domain, key, CONFIG_TYPE_ARRAY, &v);
	if(e) {
		return e;
	}
	*items = v->v.array.items;
	*count = v->v.array.count;
	return CONFIG_OK;
}

/* ---- prefix read (config_get_all) ---------------------------------- */

config_err_t config_get_all(const char *domain, const char *prefix,
			    char ***keys, config_value_t **values,
			    size_t *count)
{
	struct entry *lists[3] = { NULL, NULL, NULL };
	char **ks = NULL;
	config_value_t *vs = NULL;
	size_t n = 0, cap = 0, plen, k;
	int i, s;

	if(!domain || !keys || !values || !count ||
	   !config_valid_domain(domain) ||
	   (prefix && *prefix && !config_valid_key(prefix))) {
		return CONFIG_ERR_INVALID;
	}
	plen = prefix ? strlen(prefix) : 0;
	for(k = 0; k < 3; k++) {
		s = scope_order[k];
		int found;
		struct entry *en;
		config_err_t e = load_entries((config_scope_t)s, domain,
					      &lists[s], &found, NULL, NULL);

		if(e == CONFIG_ERR_NOT_FOUND) {
			continue;
		}
		if(e) {
			goto fail;
		}
		for(en = lists[s]; en; en = en->next) {
			size_t klen = strlen(en->key);
			size_t j;
			int dup = 0;

			if(plen) {
				if(klen <= plen || strncmp(en->key, prefix,
							    plen) ||
				   en->key[plen] != '.') {
					continue;
				}
			}
			for(j = 0; j < n; j++) {
				if(!strcmp(ks[j], en->key)) {
					dup = 1;
					break;
				}
			}
			if(dup) {
				continue;
			}
			if(n == cap) {
				char **nk;
				config_value_t *nv;

				cap = cap ? cap * 2 : 8;
				nk = realloc(ks, cap * sizeof(char *));
				if(!nk) {
					goto fail;
				}
				ks = nk;
				nv = realloc(vs, cap * sizeof(config_value_t));
				if(!nv) {
					goto fail;
				}
				vs = nv;
			}
			ks[n] = strdup(en->key);
			if(!ks[n]) {
				goto fail;
			}
			memset(&vs[n], 0, sizeof(vs[n]));
			n++;
		}
	}
	/* resolve each key with full precedence */
	for(i = 0; i < (int)n; i++) {
		config_scope_t fs;
		config_err_t e = config_read(domain, ks[i], &fs, &vs[i]);

		if(e) {
			goto fail;
		}
	}
	for(s = CONFIG_SCOPE_USER; s <= CONFIG_SCOPE_SYSTEM; s++) {
		entries_free(lists[s]);
	}
	*keys = ks;
	*values = vs;
	*count = n;
	return CONFIG_OK;
fail:
	for(i = 0; i < (int)n; i++) {
		free(ks ? ks[i] : NULL);
		if(vs) {
			value_free(&vs[i]);
		}
	}
	free(ks);
	free(vs);
	for(s = CONFIG_SCOPE_USER; s <= CONFIG_SCOPE_SYSTEM; s++) {
		entries_free(lists[s]);
	}
	return CONFIG_ERR_NOMEM;
}

void config_free_keys(char **keys, config_value_t *values, size_t count)
{
	size_t i;

	if(keys) {
		for(i = 0; i < count; i++) {
			free(keys[i]);
		}
		free(keys);
	}
	if(values) {
		for(i = 0; i < count; i++) {
			value_free(&values[i]);
		}
		free(values);
	}
}

/* ---- record enumeration (group records, docs §3) ------------------- */

/* Enumerate the records of `group` in one scope's domain file, in
 * source order. A record is an immediate child of the group that is
 * itself a container (has descendant keys); group "" means the top
 * level. `prev` is the previously returned record (NULL = first):
 * config_record_first()/config_record_next() are thin wrappers. Returns
 * CONFIG_ERR_NOT_FOUND when there is no (further) record; a `prev` that
 * is not a record of the group is CONFIG_ERR_INVALID. */
static config_err_t record_walk(config_scope_t scope, const char *domain,
				const char *group, const char *prev,
				char **name)
{
	struct entry *list = NULL;
	char **cands = NULL;
	size_t nc = 0, cap = 0;
	int found;
	size_t plen = group ? strlen(group) : 0;
	struct entry *en;
	size_t i;
	config_err_t e = CONFIG_OK;

	*name = NULL;
	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !name || !config_valid_domain(domain) ||
	   (group && *group && !config_valid_key(group))) {
		return CONFIG_ERR_INVALID;
	}
	e = load_entries(scope, domain, &list, &found, NULL, NULL);
	if(e) {
		return e;	/* NOT_FOUND when the domain file is absent */
	}
	for(en = list; en; en = en->next) {
		const char *k = en->key;
		const char *seg;
		size_t slen;
		size_t j;

		if(plen) {
			if(strncmp(k, group, plen) || k[plen] != '.') {
				continue;
			}
			seg = k + plen + 1;
		} else {
			seg = k;
		}
		slen = strcspn(seg, ".");
		if(!seg[slen]) {
			continue;	/* the child itself is a leaf */
		}
		for(j = 0; j < nc; j++) {
			if(!strncmp(cands[j], seg, slen) && !cands[j][slen]) {
				break;
			}
		}
		if(j < nc) {
			continue;
		}
		if(nc == cap) {
			char **nn;

			cap = cap ? cap * 2 : 8;
			nn = realloc(cands, cap * sizeof(char *));
			if(!nn) {
				e = CONFIG_ERR_NOMEM;
				goto out;
			}
			cands = nn;
		}
		cands[nc] = strndup(seg, slen);
		if(!cands[nc]) {
			e = CONFIG_ERR_NOMEM;
			goto out;
		}
		nc++;
	}
	if(prev) {
		for(i = 0; i < nc; i++) {
			if(!strcmp(cands[i], prev)) {
				break;
			}
		}
		if(i == nc) {
			e = CONFIG_ERR_INVALID;	/* prev is not a record */
			goto out;
		}
		i++;
		if(i == nc) {
			e = CONFIG_ERR_NOT_FOUND;
			goto out;
		}
	} else {
		if(!nc) {
			e = CONFIG_ERR_NOT_FOUND;
			goto out;
		}
		i = 0;
	}
	*name = strdup(cands[i]);
	if(!*name) {
		e = CONFIG_ERR_NOMEM;
	}
out:
	if(e && *name) {
		free(*name);
		*name = NULL;
	}
	for(i = 0; i < nc; i++) {
		free(cands[i]);
	}
	free(cands);
	entries_free(list);
	return e;
}

config_err_t config_record_first(config_scope_t scope, const char *domain,
				 const char *group, char **name)
{
	return record_walk(scope, domain, group, NULL, name);
}

config_err_t config_record_next(config_scope_t scope, const char *domain,
				const char *group, const char *prev,
				char **name)
{
	if(!prev) {
		return CONFIG_ERR_INVALID;
	}
	return record_walk(scope, domain, group, prev, name);
}

/* ---- value serialization (docs §10 write canonicalization) --------- */

static bool string_needs_quotes(const char *s)
{
	if(!*s) {
		return true;
	}
	for(; *s; s++) {
		if(*s == ' ' || *s == '\t' || *s == '#' || *s == '=' ||
		   *s == ',' || *s == '"' || *s == '\n') {
			return true;
		}
	}
	return false;
}

static void string_to_text(const char *s, char *out, size_t outsz)
{
	const char *p;
	size_t n = 0;

	if(!string_needs_quotes(s)) {
		snprintf(out, outsz, "%s", s);
		return;
	}
	n = snprintf(out, outsz, "\"");
	for(p = s; *p && n + 4 < outsz; p++) {
		switch(*p) {
		case '"':	n += snprintf(out + n, outsz - n, "\\\""); break;
		case '\\':	n += snprintf(out + n, outsz - n, "\\\\"); break;
		case '\n':	n += snprintf(out + n, outsz - n, "\\n"); break;
		case '\t':	n += snprintf(out + n, outsz - n, "\\t"); break;
		default:
			out[n++] = *p;
			break;
		}
	}
	out[n] = '\0';
	if(n + 2 < outsz) {
		snprintf(out + n, outsz - n, "\"");
	}
}

/* shortest decimal that round-trips through strtod() */
static void float_to_text(double d, char *out, size_t outsz)
{
	int prec;

	for(prec = 1; prec <= 17; prec++) {
		char buf[64];

		snprintf(buf, sizeof(buf), "%.*g", prec, d);
		if(strtod(buf, NULL) == d) {
			snprintf(out, outsz, "%s", buf);
			return;
		}
	}
	snprintf(out, outsz, "%.17g", d);
}

/* Serialize a value into *text (malloc'd). */
static config_err_t value_to_text(const config_value_t *v, char **text)
{
	char buf[CONF_MAX_KEY + CONF_MAX_LINE + 64];

	switch(v->type) {
	case CONFIG_TYPE_STRING:
		string_to_text(v->v.string ? v->v.string : "", buf,
			       sizeof(buf));
		break;
	case CONFIG_TYPE_BOOL:
		snprintf(buf, sizeof(buf), "%s",
			 v->v.boolean ? "true" : "false");
		break;
	case CONFIG_TYPE_INT:
		snprintf(buf, sizeof(buf), "%lld", (long long)v->v.integer);
		break;
	case CONFIG_TYPE_FLOAT:
		float_to_text(v->v.floating, buf, sizeof(buf));
		break;
	case CONFIG_TYPE_ARRAY: {
		size_t i, n = 0;

		buf[0] = '\0';
		for(i = 0; i < v->v.array.count; i++) {
			char *el = NULL;
			config_err_t e = value_to_text(&v->v.array.items[i],
						       &el);

			if(e) {
				return e;
			}
			n += snprintf(buf + n, sizeof(buf) - n, "%s%s",
				      i ? ", " : "", el);
			free(el);
			if(n >= sizeof(buf)) {
				return CONFIG_ERR_PARSE;
			}
		}
		break;
	}
	default:
		return CONFIG_ERR_INVALID;
	}
	*text = strdup(buf);
	return *text ? CONFIG_OK : CONFIG_ERR_NOMEM;
}

/* ---- writing (atomic: temp + fsync + rename) ------------------------ */

static config_err_t mkdir_p(const char *dir)
{
	char tmp[PATH_MAX];
	size_t i;

	snprintf(tmp, sizeof(tmp), "%s", dir);
	if(!tmp[0]) {
		return CONFIG_OK;
	}
	for(i = 1; tmp[i]; i++) {
		if(tmp[i] == '/') {
			tmp[i] = '\0';
			if(mkdir(tmp, 0755) && errno != EEXIST) {
				return errno == EACCES ? CONFIG_ERR_ACCESS :
							CONFIG_ERR_IO;
			}
			tmp[i] = '/';
		}
	}
	if(mkdir(tmp, 0755) && errno != EEXIST) {
		return errno == EACCES ? CONFIG_ERR_ACCESS : CONFIG_ERR_IO;
	}
	return CONFIG_OK;
}

/* ---- canonical writer (group records, docs §3.1) ------------------- */

static void indent_of(int depth, char *out, size_t outsz)
{
	size_t i, n = (size_t)depth * 4;

	if(n >= outsz) {
		n = outsz ? outsz - 1 : 0;
	}
	for(i = 0; i < n; i++) {
		out[i] = ' ';
	}
	out[n] = '\0';
}

static config_err_t write_indented(int fd, const char *indent,
				   const char *relkey, const char *rhs)
{
	char line[CONF_MAX_KEY + CONF_MAX_LINE + 384];
	size_t len;

	len = snprintf(line, sizeof(line), "%s%s = %s\n", indent, relkey,
		       rhs);
	if(len >= sizeof(line)) {
		return CONFIG_ERR_PARSE;
	}
	if(write(fd, line, len) != (ssize_t)len) {
		return CONFIG_ERR_IO;
	}
	return CONFIG_OK;
}

/* Emit the children of container `prefix` ("" = the top level) in
 * first-seen order: leaf children as "name = value", container children
 * as nested "name = { ... }" blocks (4-space indentation per level). */
static config_err_t emit_children(int fd, struct entry *list,
				  const char *prefix, int depth)
{
	char ind[384];
	char **kids = NULL;
	size_t n = 0, cap = 0;
	size_t plen = prefix ? strlen(prefix) : 0;
	struct entry *en;
	size_t i;
	config_err_t e = CONFIG_OK;

	indent_of(depth, ind, sizeof(ind));
	for(en = list; en && !e; en = en->next) {
		const char *k = en->key;
		const char *seg;
		size_t slen;
		char **nk;
		int dup = 0;

		if(plen) {
			if(strncmp(k, prefix, plen) || k[plen] != '.') {
				continue;
			}
			seg = k + plen + 1;
		} else {
			seg = k;
		}
		slen = strcspn(seg, ".");
		for(i = 0; i < n; i++) {
			if(!strncmp(kids[i], seg, slen) && !kids[i][slen]) {
				dup = 1;
				break;
			}
		}
		if(dup) {
			continue;
		}
		if(n == cap) {
			cap = cap ? cap * 2 : 8;
			nk = realloc(kids, cap * sizeof(char *));
			if(!nk) {
				e = CONFIG_ERR_NOMEM;
				break;
			}
			kids = nk;
		}
		kids[n] = strndup(seg, slen);
		if(!kids[n]) {
			e = CONFIG_ERR_NOMEM;
			break;
		}
		n++;
	}
	for(i = 0; i < n && !e; i++) {
		size_t fl = (plen ? plen + 1 : 0) + strlen(kids[i]);
		char *full = malloc(fl + 1);
		struct entry *leaf;
		char *tv = NULL;

		if(!full) {
			e = CONFIG_ERR_NOMEM;
			break;
		}
		if(plen) {
			memcpy(full, prefix, plen);
			full[plen] = '.';
			strcpy(full + plen + 1, kids[i]);
		} else {
			strcpy(full, kids[i]);
		}
		leaf = entry_find(list, full);
		if(leaf) {
			/* a leaf child: "name = value" */
			e = value_to_text(&leaf->val, &tv);
			if(!e) {
				e = write_indented(fd, ind, kids[i], tv);
			}
			free(tv);
		} else {
			/* a container child: nested block */
			e = write_indented(fd, ind, kids[i], "{");
			if(!e) {
				e = emit_children(fd, list, full, depth + 1);
			}
			if(!e) {
				char close[384];
				size_t cl;

				cl = snprintf(close, sizeof(close), "%s}\n",
					      ind);
				if(cl >= sizeof(close)) {
					e = CONFIG_ERR_PARSE;
				} else if(write(fd, close, cl) != (ssize_t)cl) {
					e = CONFIG_ERR_IO;
				}
			}
		}
		free(full);
	}
	for(i = 0; i < n; i++) {
		free(kids[i]);
	}
	free(kids);
	return e;
}

/* Record-domain (nested) emission used when the file has at least one
 * top-level explicit block. */
static config_err_t emit_grouped(int fd, struct entry *list,
				 char **blocks, int nblocks)
{
	char **tops = NULL;
	size_t n = 0, cap = 0;
	struct entry *en;
	size_t i;
	config_err_t e = CONFIG_OK;

	/* top-level names in first-seen order */
	for(en = list; en && !e; en = en->next) {
		size_t slen = strcspn(en->key, ".");
		char **nt;
		size_t k;

		for(k = 0; k < n; k++) {
			if(!strncmp(tops[k], en->key, slen) &&
			   !tops[k][slen]) {
				break;
			}
		}
		if(k < n) {
			continue;
		}
		if(n == cap) {
			cap = cap ? cap * 2 : 8;
			nt = realloc(tops, cap * sizeof(char *));
			if(!nt) {
				e = CONFIG_ERR_NOMEM;
				break;
			}
			tops = nt;
		}
		tops[n] = strndup(en->key, slen);
		if(!tops[n]) {
			e = CONFIG_ERR_NOMEM;
			break;
		}
		n++;
	}
	for(i = 0; i < n && !e; i++) {
		int expl = 0;
		int b;

		for(b = 0; b < nblocks; b++) {
			if(!strcmp(blocks[b], tops[i])) {
				expl = 1;
				break;
			}
		}
		if(entry_find(list, tops[i])) {
			/* a top-level leaf */
			struct entry *leaf = entry_find(list, tops[i]);
			char *tv = NULL;

			e = value_to_text(&leaf->val, &tv);
			if(!e) {
				e = write_indented(fd, "", tops[i], tv);
			}
			free(tv);
		} else if(expl) {
			/* an explicit top-level block: nested spelling */
			e = write_indented(fd, "", tops[i], "{");
			if(!e) {
				e = emit_children(fd, list, tops[i], 1);
			}
			if(!e) {
				static const char close[] = "}\n";

				if(write(fd, close, 2) != 2) {
					e = CONFIG_ERR_IO;
				}
			}
		} else {
			/* a plain dotted domain under this name: keep the
			 * flat "full.key = value" spelling (unchanged) */
			for(en = list; en && !e; en = en->next) {
				size_t tl = strlen(tops[i]);

				if(strncmp(en->key, tops[i], tl) ||
				   en->key[tl] != '.') {
					continue;
				}
				{
					char *tv = NULL;

					e = value_to_text(&en->val, &tv);
					if(!e) {
						e = write_indented(fd, "",
								   en->key, tv);
					}
					free(tv);
				}
			}
		}
	}
	for(i = 0; i < n; i++) {
		free(tops[i]);
	}
	free(tops);
	return e;
}

/* Serialize an entry list to the domain file atomically. When
 * remove_if_empty and the list has no entries, the file is deleted.
 * blocks/nblocks carry the top-level explicit-block names from the
 * parse: when present, the file is written in nested record spelling;
 * otherwise (a pure flat domain) exactly as before. */
static config_err_t write_entries(config_scope_t scope, const char *domain,
				  struct entry *list, bool remove_if_empty,
				  char **blocks, int nblocks)
{
	char path[PATH_MAX];
	char tmp[PATH_MAX + 32];
	struct entry *en;
	config_err_t e;
	int fd;
	size_t nlines = 0;

	if(domain_path(scope, domain, path, sizeof(path))) {
		return CONFIG_ERR_INVALID;
	}
	if(remove_if_empty) {
		for(en = list; en; en = en->next) {
			nlines++;
		}
		if(!nlines) {
			if(!unlink(path) || errno == ENOENT) {
				return CONFIG_OK;
			}
			return errno == EACCES ? CONFIG_ERR_ACCESS :
						CONFIG_ERR_IO;
		}
	}
	/* create the file's directory chain */
	{
		char *slash = strrchr(path, '/');

		if(slash) {
			*slash = '\0';
			e = mkdir_p(path);
			*slash = '/';
			if(e) {
				return e;
			}
		}
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if(fd < 0) {
		return errno == EACCES ? CONFIG_ERR_ACCESS : CONFIG_ERR_IO;
	}
	if(blocks && nblocks > 0) {
		e = emit_grouped(fd, list, blocks, nblocks);
	} else {
		for(en = list; en; en = en->next) {
			char *tv = NULL;
			char line[CONF_MAX_KEY + CONF_MAX_LINE + 64];
			size_t len;

			e = value_to_text(&en->val, &tv);
			if(e) {
				break;
			}
			len = snprintf(line, sizeof(line), "%s = %s\n",
				       en->key, tv);
			free(tv);
			if(len >= sizeof(line)) {
				e = CONFIG_ERR_PARSE;
				break;
			}
			if(write(fd, line, len) != (ssize_t)len) {
				e = CONFIG_ERR_IO;
				break;
			}
		}
	}
	if(e) {
		close(fd);
		unlink(tmp);
		return e;
	}
	if(fsync(fd)) {
		close(fd);
		unlink(tmp);
		return CONFIG_ERR_IO;
	}
	close(fd);
	if(rename(tmp, path)) {
		unlink(tmp);
		return CONFIG_ERR_IO;
	}
	return CONFIG_OK;
}

/* Generic single-key write: load, replace/append, write back. */
static config_err_t set_key_replace(struct entry **listp, const char *key,
				    const config_value_t *v);
static config_err_t set_key(config_scope_t scope, const char *domain,
			    const char *key, const config_value_t *v)
{
	struct entry *list = NULL;
	int found;
	config_err_t e;

	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !key || !config_valid_domain(domain) ||
	   !config_valid_key(key)) {
		return CONFIG_ERR_INVALID;
	}
	{
		char **blocks = NULL;
		int nblocks = 0;
		config_err_t e2;

		e = load_entries(scope, domain, &list, &found, &blocks,
				 &nblocks);
		if(e && e != CONFIG_ERR_NOT_FOUND) {
			return e;
		}
		e2 = set_key_replace(&list, key, v);
		if(!e2) {
			e2 = write_entries(scope, domain, list, false,
					   blocks, nblocks);
		}
		blocks_free(blocks, nblocks);
		entries_free(list);
		return e2;
	}
}

/* helper shared by config_set_* and friends (appends when the key is
 * new; *listp is updated on first insertion) */
static config_err_t set_key_replace(struct entry **listp, const char *key,
				    const config_value_t *v)
{
	struct entry *list = *listp;
	struct entry *en, *t;

	en = entry_find(list, key);
	if(en) {
		value_free(&en->val);
	} else {
		t = list;
		en = malloc(sizeof(struct entry));
		if(!en) {
			return CONFIG_ERR_NOMEM;
		}
		en->key = strdup(key);
		if(!en->key) {
			free(en);
			return CONFIG_ERR_NOMEM;
		}
		en->next = NULL;
		if(!list) {
			*listp = en;
		} else {
			while(t->next) {
				t = t->next;
			}
			t->next = en;
		}
	}
	return value_copy(&en->val, v);
}
config_err_t config_set_string(config_scope_t scope, const char *domain,
			       const char *key, const char *value)
{
	config_value_t v;

	if(!value) {
		return CONFIG_ERR_INVALID;
	}
	v.type = CONFIG_TYPE_STRING;
	v.v.string = value;
	return set_key(scope, domain, key, &v);
}

config_err_t config_set_bool(config_scope_t scope, const char *domain,
			     const char *key, bool value)
{
	config_value_t v;

	v.type = CONFIG_TYPE_BOOL;
	v.v.boolean = value;
	return set_key(scope, domain, key, &v);
}

config_err_t config_set_int(config_scope_t scope, const char *domain,
			    const char *key, int64_t value)
{
	config_value_t v;

	v.type = CONFIG_TYPE_INT;
	v.v.integer = value;
	return set_key(scope, domain, key, &v);
}

config_err_t config_set_float(config_scope_t scope, const char *domain,
			      const char *key, double value)
{
	config_value_t v;

	v.type = CONFIG_TYPE_FLOAT;
	v.v.floating = value;
	return set_key(scope, domain, key, &v);
}

config_err_t config_set_array(config_scope_t scope, const char *domain,
			      const char *key, const config_value_t *items,
			      size_t count)
{
	config_value_t v;

	if(count && !items) {
		return CONFIG_ERR_INVALID;
	}
	v.type = CONFIG_TYPE_ARRAY;
	v.v.array.items = (config_value_t *)items;
	v.v.array.count = count;
	return set_key(scope, domain, key, &v);
}

config_err_t config_unset(config_scope_t scope, const char *domain,
			  const char *key)
{
	struct entry *list = NULL, *prev = NULL, *en;
	int found;
	config_err_t e;

	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !key || !config_valid_domain(domain) ||
	   !config_valid_key(key)) {
		return CONFIG_ERR_INVALID;
	}
	{
		char **blocks = NULL;
		int nblocks = 0;

		e = load_entries(scope, domain, &list, &found, &blocks,
				 &nblocks);
		if(e == CONFIG_ERR_NOT_FOUND) {
			return CONFIG_OK;	/* nothing to unset */
		}
		if(e) {
			return e;
		}
		for(en = list; en; prev = en, en = en->next) {
			if(!strcmp(en->key, key)) {
				if(prev) {
					prev->next = en->next;
				} else {
					list = en->next;
				}
				free(en->key);
				value_free(&en->val);
				free(en);
				e = write_entries(scope, domain, list, true,
						  blocks, nblocks);
				blocks_free(blocks, nblocks);
				entries_free(list);
				return e;
			}
		}
		blocks_free(blocks, nblocks);
		entries_free(list);
		return CONFIG_OK;	/* key absent: idempotent */
	}
}

config_err_t config_remove_domain(config_scope_t scope, const char *domain)
{
	char path[PATH_MAX];

	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !config_valid_domain(domain)) {
		return CONFIG_ERR_INVALID;
	}
	if(domain_path(scope, domain, path, sizeof(path))) {
		return CONFIG_ERR_INVALID;
	}
	if(!unlink(path) || errno == ENOENT) {
		return CONFIG_OK;
	}
	return errno == EACCES ? CONFIG_ERR_ACCESS : CONFIG_ERR_IO;
}

/* ---- domains -------------------------------------------------------- */

config_err_t config_list_domains(config_scope_t scope, char ***domains,
				 size_t *count)
{
	char dir[PATH_MAX];
	char **out = NULL;
	size_t n = 0, cap = 0;
	DIR *d;
	struct dirent *de;

	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domains || !count) {
		return CONFIG_ERR_INVALID;
	}
	scope_dir_path(scope, dir, sizeof(dir));
	d = opendir(dir);
	if(!d) {
		if(errno == ENOENT) {
			*domains = NULL;
			*count = 0;
			return CONFIG_OK;
		}
		return errno == EACCES ? CONFIG_ERR_ACCESS : CONFIG_ERR_IO;
	}
	while((de = readdir(d))) {
		size_t len = strlen(de->d_name);
		char *name;
		char **nn;

		if(len <= 5 || strcmp(de->d_name + len - 5, ".conf")) {
			continue;
		}
		name = strndup(de->d_name, len - 5);
		if(!name) {
			closedir(d);
			goto fail;
		}
		if(n == cap) {
			cap = cap ? cap * 2 : 8;
			nn = realloc(out, cap * sizeof(char *));
			if(!nn) {
				free(name);
				closedir(d);
				goto fail;
			}
			out = nn;
		}
		out[n++] = name;
	}
	closedir(d);
	/* insertion sort for deterministic output */
	{
		size_t i, j;

		for(i = 1; i < n; i++) {
			char *t = out[i];

			for(j = i; j > 0 && strcmp(out[j - 1], t) > 0; j--) {
				out[j] = out[j - 1];
			}
			out[j] = t;
		}
	}
	*domains = out;
	*count = n;
	return CONFIG_OK;
fail:
	while(n) {
		free(out[--n]);
	}
	free(out);
	return CONFIG_ERR_NOMEM;
}

void config_free_domains(char **domains, size_t count)
{
	size_t i;

	if(!domains) {
		return;
	}
	for(i = 0; i < count; i++) {
		free(domains[i]);
	}
	free(domains);
}

/* ---- utilities ------------------------------------------------------ */

config_err_t config_path(config_scope_t scope, const char *domain,
			 char *buf, size_t buflen)
{
	if(!buf || !buflen) {
		return CONFIG_ERR_INVALID;
	}
	return domain_path(scope, domain, buf, buflen);
}

const char *config_strerror(config_err_t err)
{
	switch(err) {
	case CONFIG_OK:			return "no error";
	case CONFIG_ERR_NOT_FOUND:	return "domain or key not found";
	case CONFIG_ERR_TYPE:		return "value has a different type";
	case CONFIG_ERR_PARSE:		return "malformed .conf file";
	case CONFIG_ERR_IO:		return "I/O error";
	case CONFIG_ERR_INVALID:	return "invalid argument";
	case CONFIG_ERR_ACCESS:		return "permission denied";
	case CONFIG_ERR_NOMEM:		return "out of memory";
	}
	return "unknown error";
}

const char *config_scope_name(config_scope_t scope)
{
	switch(scope) {
	case CONFIG_SCOPE_USER:		return "user";
	case CONFIG_SCOPE_SHARED:	return "shared";
	case CONFIG_SCOPE_SYSTEM:	return "system";
	}
	return "?";
}
