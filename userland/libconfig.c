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

/* Builds "<root>/<scope>/Application Support" into out; the caller
 * appends "/<domain>". Behaviour material, not settings: the same scope
 * tree and the same domain key as Configuration/, a different payload
 * kind (docs/design/config-design.md §0). */
static void app_support_dir_path(config_scope_t scope, char *out, size_t outsz)
{
	switch(scope) {
	case CONFIG_SCOPE_USER:
		snprintf(out, outsz, "%s/Users/%s/Application Support",
			config_root(), user_name());
		break;
	case CONFIG_SCOPE_SHARED:
		snprintf(out, outsz, "%s/Shared/Application Support",
			config_root());
		break;
	default:
		snprintf(out, outsz, "%s/System/Application Support",
			config_root());
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

/*
 * v2 key addressing (docs §10.2): `key` may carry `ident[i]` segments
 * (and bare all-digit segments, which address an array element) mixed
 * with plain idents, e.g. `rules[1].edits[0].value` and
 * `rules[0].tests.1` (≡ `rules[0].tests[1]`). Plain dotted keys stay
 * valid; array indexes are 0-based. Records are addressed by name only
 * (no implicit ordinal keys).
 */
bool config_valid_address(const char *key)
{
	size_t i, len;

	if(!key) {
		return false;
	}
	len = strlen(key);
	if(!len || len > CONF_MAX_KEY || key[0] == '.' ||
	   key[len - 1] == '.') {
		return false;
	}
	for(i = 0; i < len;) {
		size_t start = i;
		int digit_seg = 1;

		/* one segment: ident, ident[index], or digits */
		while(i < len && key[i] != '.') {
			if(!(isalnum((unsigned char)key[i]) ||
			     key[i] == '_' || key[i] == '-' ||
			     key[i] == '[' || key[i] == ']')) {
				return false;
			}
			if(!isdigit((unsigned char)key[i]) &&
			   key[i] != '[' && key[i] != ']') {
				digit_seg = 0;
			}
			i++;
		}
		if(i == start) {
			return false;
		}
		if(digit_seg) {
			/* bare digits: array index only */
			size_t j;

			for(j = start; j < i; j++) {
				if(!isdigit((unsigned char)key[j])) {
					return false;
				}
			}
		} else {
			/* ident or ident[...]: parse the segment */
			const char *seg = key + start;
			size_t slen = i - start;
			size_t b = 0;

			while(b < slen && seg[b] != '[') {
				b++;
			}
			if(!valid_segment(seg, b)) {
				return false;
			}
			/* optional [digits] suffix; no trailing junk */
			if(b < slen) {
				size_t j = b + 1;
				int saw_digit = 0;

				if(seg[slen - 1] != ']') {
					return false;
				}
				for(; j < slen - 1; j++) {
					if(!isdigit((unsigned char)seg[j])) {
						return false;
					}
					saw_digit = 1;
				}
				if(!saw_digit) {
					return false;	/* empty index */
				}
			}
		}
		if(i < len) {
			i++;	/* '.' */
		}
	}
	return true;
}

/*
 * Addressing steps (docs §10.2). Each '.'-separated piece becomes one
 * step: a plain ident is a record field (or a dotted-key segment), an
 * `ident[i]` / bare `i` piece is an array index. Names + indexes may
 * mix: `rules[0].tests.1` ≡ `rules[0].tests[1]`.
 */
#define ADDR_MAX_STEPS	((CONF_MAX_KEY + 1) / 2)

enum addr_kind {
	ADDR_NAME,
	ADDR_INDEX,
};

struct addr_step {
	enum addr_kind kind;
	char name[CONF_MAX_SEGMENT + 1];	/* ADDR_NAME */
	size_t index;			/* ADDR_INDEX */
};

/* Parse an addressing key into steps. Returns false on malformed input
 * (caller should have validated with config_valid_address first). */
static bool address_split(const char *key, struct addr_step *steps,
			  size_t *nsteps)
{
	size_t n = 0;
	const char *p = key;

	while(*p) {
		const char *e = strchr(p, '.');
		size_t slen = e ? (size_t)(e - p) : strlen(p);
		size_t b = 0;
		int all_digits = 1;
		size_t j;

		if(n >= ADDR_MAX_STEPS) {
			return false;
		}
		for(j = 0; j < slen; j++) {
			if(!isdigit((unsigned char)p[j])) {
				all_digits = 0;
				break;
			}
		}
		if(all_digits && slen) {
			steps[n].kind = ADDR_INDEX;
			steps[n].index = (size_t)strtoul(p, NULL, 10);
			n++;
			p = e ? e + 1 : p + slen;
			continue;
		}
		while(b < slen && p[b] != '[') {
			b++;
		}
		/* ident part */
		if(b > CONF_MAX_SEGMENT) {
			return false;
		}
		memcpy(steps[n].name, p, b);
		steps[n].name[b] = '\0';
		if(b == 0 || steps[n].name[0] == ']' ||
		   steps[n].name[0] == '[') {
			return false;
		}
		steps[n].kind = ADDR_NAME;
		n++;
		if(b < slen) {	/* ident[i]: index step follows */
			char idx[32];
			size_t k = b + 1;
			size_t m = 0;

			if(slen == 0 || p[slen - 1] != ']' ||
			   k >= slen - 1) {
				return false;
			}
			while(k < slen - 1) {
				if(!isdigit((unsigned char)p[k]) ||
				   m + 1 >= sizeof(idx)) {
					return false;
				}
				idx[m++] = p[k++];
			}
			idx[m] = '\0';
			if(n >= ADDR_MAX_STEPS) {
				return false;
			}
			steps[n].kind = ADDR_INDEX;
			steps[n].index = (size_t)strtoul(idx, NULL, 10);
			n++;
		}
		p = e ? e + 1 : p + slen;
	}
	*nsteps = n;
	return n != 0;
}

/* Index (array position) of the first ADDR_INDEX step, or nsteps. */
static size_t first_index_step(const struct addr_step *steps, size_t nsteps)
{
	size_t i;

	for(i = 0; i < nsteps; i++) {
		if(steps[i].kind == ADDR_INDEX) {
			return i;
		}
	}
	return nsteps;
}

/* Build the dotted key from the leading run of ADDR_NAME steps
 * [0,last); caller guarantees last >= 1 when there are names. */
static void name_prefix_key(const struct addr_step *steps, size_t last,
			    char *out, size_t outsz)
{
	size_t i, n = 0;

	out[0] = '\0';
	for(i = 0; i < last; i++) {
		int r = snprintf(out + n, outsz - n, "%s%s",
				 i ? "." : "", steps[i].name);

		if(r < 0 || (size_t)r >= outsz - n) {
			break;
		}
		n += (size_t)r;
	}
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
	case CONFIG_TYPE_RECORD:
		if(v->v.record.fields) {
			size_t i;

			for(i = 0; i < v->v.record.count; i++) {
				free(v->v.record.fields[i].name);
				value_free(&v->v.record.fields[i].value);
			}
			free(v->v.record.fields);
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
	case CONFIG_TYPE_RECORD: {
		size_t i;

		if(src->v.record.count) {
			dst->v.record.fields = calloc(src->v.record.count,
						      sizeof(config_record_field_t));
			if(!dst->v.record.fields) {
				return CONFIG_ERR_NOMEM;
			}
		}
		dst->v.record.count = src->v.record.count;
		for(i = 0; i < src->v.record.count; i++) {
			const config_record_field_t *sf =
				&src->v.record.fields[i];
			config_record_field_t *df = &dst->v.record.fields[i];
			config_err_t e;

			df->name = strdup(sf->name);
			if(!df->name) {
				dst->v.record.count = i;
				value_free(dst);
				return CONFIG_ERR_NOMEM;
			}
			e = value_copy(&df->value, &sf->value);
			if(e) {
				dst->v.record.count = i + 1;
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

/* ---- v2 values: bracketed arrays + records (docs §10.1) ------------ */
/*
 * v2 grammar is a strict superset of v1. Two new value shapes arrive:
 *  - a bracketed literal `[ … ]`: an explicit array whose elements may
 *    be scalars (as v1) or records; items separated by commas, spanning
 *    lines (the strict §10 grammar has one item or close per line).
 *  - a record value `{ name = value … }`: anonymous (never flattened),
 *    built into a CONFIG_TYPE_RECORD; records appear as array elements
 *    and as values nested inside other records.
 * Named blocks `key = { … }` keep their v1 flattening (prefix store), so
 * record-domain consumers (pwconf/mounts) are untouched; only records
 * spelled INSIDE a bracketed literal are tree values. A record element
 * is line-structured: the first field may share the `{` line, later
 * fields each start on their own line (blank/comment lines allowed),
 * and `}` closes on its own line.
 *
 * The value parser consumes whole lines from the same (text,len,&off)
 * cursor parse_conf uses, so a multi-line bracketed array cannot be
 * handled by the one-line parse_value(); parse_conf calls v2_parse_value
 * when the value begins with '['.
 */
struct v2src {
	const char *text;
	size_t len;
	size_t *off;		/* next unread byte (a line start) */
	char line[CONF_MAX_LINE + 1];	/* last line pulled */
	const char *p;		/* scan cursor within line */
	int depth;		/* container nesting (see CONFIG_MAX_NEST) */
};

/* SECURITY (audit 2026-09): how deep a value may nest.  The v2 parser is
 * mutually recursive — value -> record/array -> value — so without a cap a
 * config file of a few thousand '[' or '{' bytes overflows the C stack and
 * crashes whatever parses it.  That includes root-run tools that resolve a
 * USER-scope file (the toolkit and the WM read system -> user -> shared),
 * so a user could take down a privileged process with a file of their own.
 * The deepest shipped config is a record inside an array inside a record
 * (3); the addressing grammar's `rules[0].edits[1]` needs no more. */
#define CONFIG_MAX_NEST	32

/* Pull the next physical line into s->line (trimmed: CR + trailing WS
 * removed) and point s->p at it. Returns 1 on a new line, 0 at EOF,
 * -1 if a line is overlong. Blank/comment lines are NOT skipped here;
 * callers decide (parse_record skips them between fields). */
static int v2_pull(struct v2src *s)
{
	size_t n = 0;

	if(*s->off >= s->len) {
		s->p = NULL;
		return 0;
	}
	while(*s->off < s->len && s->text[*s->off] != '\n' &&
	      n < CONF_MAX_LINE) {
		s->line[n++] = s->text[(*s->off)++];
	}
	if(*s->off < s->len && s->text[*s->off] == '\n') {
		(*s->off)++;
	} else if(n == CONF_MAX_LINE && *s->off < s->len) {
		s->p = NULL;
		return -1;
	}
	while(n && (s->line[n - 1] == '\r' || s->line[n - 1] == ' ' ||
		    s->line[n - 1] == '\t')) {
		n--;
	}
	s->line[n] = '\0';
	s->p = s->line;
	return 1;
}

/* Skip spaces/tabs. If the scan reaches end-of-line, pull the next
 * non-blank, non-comment line and continue. Returns 1 when a
 * non-ws char is at *p, 0 at EOF, -1 on an overlong line. */
static int v2_skip_ws(struct v2src *s)
{
	for(;;) {
		while(*s->p == ' ' || *s->p == '\t') {
			s->p++;
		}
		if(*s->p) {
			return 1;
		}
		for(;;) {	/* pull the next non-empty line */
			int r = v2_pull(s);

			if(r <= 0) {
				return r;
			}
			{
				const char *q = s->p;

				while(*q == ' ' || *q == '\t') {
					q++;
				}
				if(*q && *q != '#') {
					s->p = q;
					return 1;
				}
				/* blank or comment: keep pulling */
			}
		}
	}
}

static config_err_t v2_parse_value(struct v2src *s, config_value_t *out);
static config_err_t v2_parse_record(struct v2src *s, config_value_t *out);
static config_err_t v2_parse_array(struct v2src *s, config_value_t *out);
static config_err_t v2_parse_record_inner(struct v2src *s,
					  config_value_t *out);
static config_err_t v2_parse_array_inner(struct v2src *s,
					 config_value_t *out);

/* Parse one scalar array element (quoted or bare) at *s->p; advances
 * past it. Element chars exclude WS, ',', ']', '"', '{', '}'. */
static config_err_t v2_parse_element(struct v2src *s, config_value_t *out)
{
	const char *p = s->p;
	char tok[CONF_MAX_LINE];
	size_t n = 0;
	config_err_t e;

	if(*p == '"') {
		char *str;
		const char *q;

		e = parse_quoted(&p, &str);
		if(e) {
			return e;
		}
		memset(out, 0, sizeof(*out));
		out->type = CONFIG_TYPE_STRING;
		out->v.string = str;
		s->p = p;
		/* must be followed by ws, ',' or ']' */
		q = p;
		while(*q == ' ' || *q == '\t') {
			q++;
		}
		if(*q && *q != ',' && *q != ']') {
			value_free(out);
			return CONFIG_ERR_PARSE;
		}
		return CONFIG_OK;
	}
	while(*p && *p != ',' && *p != ' ' && *p != '\t' && *p != ']' &&
	      *p != '"' && *p != '{' && *p != '}' && *p != '[') {
		if(n + 1 < sizeof(tok)) {
			tok[n++] = *p;
		} else {
			return CONFIG_ERR_PARSE;
		}
		p++;
	}
	if(n == 0 || (*p && *p != ',' && *p != ' ' && *p != '\t' &&
		     *p != ']' && *p != '{' && *p != '}' && *p != '[')) {
		return CONFIG_ERR_PARSE;	/* empty or bad element */
	}
	tok[n] = '\0';
	e = infer_scalar(tok, out);
	if(e) {
		return e;
	}
	s->p = p;
	return CONFIG_OK;
}

/* ---- record field helpers ------------------------------------------ */

static config_err_t v2_field_set(config_value_t *rec, const char *name,
				 const config_value_t *val)
{
	size_t i;

	for(i = 0; i < rec->v.record.count; i++) {
		if(!strcmp(rec->v.record.fields[i].name, name)) {
			config_value_t *dst = &rec->v.record.fields[i].value;

			/* records are identity-bearing: a field may not
			 * be both a leaf and a record, and duplicate
			 * record values are parse errors (v2 §10.1) */
			if(dst->type == CONFIG_TYPE_RECORD ||
			   val->type == CONFIG_TYPE_RECORD) {
				if(dst->type == CONFIG_TYPE_RECORD &&
				   val->type == CONFIG_TYPE_RECORD) {
					return CONFIG_ERR_PARSE; /* dup */
				}
				return CONFIG_ERR_PARSE; /* leaf vs record */
			}
			value_free(dst);
			return value_copy(dst, val);
		}
	}
	{
		config_record_field_t *nf;
		config_err_t e;

		nf = realloc(rec->v.record.fields,
			     (rec->v.record.count + 1) *
			     sizeof(*nf));
		if(!nf) {
			return CONFIG_ERR_NOMEM;
		}
		rec->v.record.fields = nf;
		nf[rec->v.record.count].name = strdup(name);
		if(!nf[rec->v.record.count].name) {
			return CONFIG_ERR_NOMEM;
		}
		memset(&nf[rec->v.record.count].value, 0,
		       sizeof(nf[rec->v.record.count].value));
		e = value_copy(&nf[rec->v.record.count].value, val);
		if(e) {
			free(nf[rec->v.record.count].name);
			return e;
		}
		rec->v.record.count++;
		return CONFIG_OK;
	}
}

/* Parse one record field assignment; s->p is at the field name, the
 * field must occupy the remainder of its line (nested [ … ] / { … }
 * values may span lines and close on their own line). On success s->p
 * is at the end of the value's closing line. */
static config_err_t v2_parse_field(struct v2src *s, config_value_t *rec)
{
	const char *p0 = s->p;
	const char *eq = strchr(p0, '=');
	const char *ke;
	char name[CONF_MAX_SEGMENT + 1];
	size_t nl;
	config_value_t v;
	config_err_t e;
	const char *valp;

	while(*p0 == ' ' || *p0 == '\t') {
		p0++;
	}
	if(!*p0 || *p0 == '#') {
		return CONFIG_OK;	/* blank/comment */
	}
	if(!eq) {
		return CONFIG_ERR_PARSE;
	}
	ke = eq;
	while(ke > p0 && (ke[-1] == ' ' || ke[-1] == '\t')) {
		ke--;
	}
	nl = (size_t)(ke - p0);
	if(!nl || nl > CONF_MAX_SEGMENT || !valid_segment(p0, nl)) {
		return CONFIG_ERR_PARSE;
	}
	memcpy(name, p0, nl);
	name[nl] = '\0';
	valp = eq + 1;
	while(*valp == ' ' || *valp == '\t') {
		valp++;
	}
	memset(&v, 0, sizeof(v));
	if(*valp == '[') {
		s->p = valp;
		e = v2_parse_array(s, &v);
	} else if(*valp == '{') {
		s->p = valp;
		e = v2_parse_record(s, &v);
	} else {
		e = parse_value(valp, &v);
		if(!e) {
			s->p = valp + strlen(valp);
		}
	}
	if(e) {
		return e;
	}
	/* value must end at the line end */
	{
		const char *t = s->p;

		while(*t == ' ' || *t == '\t') {
			t++;
		}
		if(*t) {
			value_free(&v);
			return CONFIG_ERR_PARSE;
		}
	}
	e = v2_field_set(rec, name, &v);
	value_free(&v);
	return e;
}

/* Parse a record value. s->p is at '{'. The first field may follow '{'
 * on the same line; subsequent fields each start on their own line;
 * '}' (alone on its line) closes. */
/* CONFIG_MAX_NEST is enforced here, around the two functions that can
 * recurse (a record's field may be a record or an array; an array's
 * element may be either — v2_parse_field dispatches straight to these,
 * which is why the bound cannot live in the value dispatcher). */
static config_err_t v2_parse_record(struct v2src *s, config_value_t *out)
{
	config_err_t rc;

	if(s->depth >= CONFIG_MAX_NEST) {
		return CONFIG_ERR_PARSE;
	}
	s->depth++;
	rc = v2_parse_record_inner(s, out);
	s->depth--;
	return rc;
}

static config_err_t v2_parse_array(struct v2src *s, config_value_t *out)
{
	config_err_t rc;

	if(s->depth >= CONFIG_MAX_NEST) {
		return CONFIG_ERR_PARSE;
	}
	s->depth++;
	rc = v2_parse_array_inner(s, out);
	s->depth--;
	return rc;
}

static config_err_t v2_parse_record_inner(struct v2src *s, config_value_t *out)
{
	config_err_t rc = CONFIG_OK;
	const char *q;

	memset(out, 0, sizeof(*out));
	out->type = CONFIG_TYPE_RECORD;
	out->v.record.fields = NULL;
	out->v.record.count = 0;
	s->p++;		/* '{' */
	q = s->p;
	while(*q == ' ' || *q == '\t') {
		q++;
	}
	if(*q == '}') {
		/* empty record {} — leave any tail (',', ']') to the
		 * caller (array loop) */
		s->p = q + 1;
		return CONFIG_OK;
	}
	if(*q) {
		/* first field shares the '{' line */
		s->p = q;
		rc = v2_parse_field(s, out);
		if(rc) {
			goto fail;
		}
	}
	for(;;) {
		int r;
		const char *p0;

		r = v2_pull(s);
		if(r <= 0) {
			rc = CONFIG_ERR_PARSE;	/* unterminated record */
			break;
		}
		p0 = s->p;
		while(*p0 == ' ' || *p0 == '\t') {
			p0++;
		}
		if(!*p0 || *p0 == '#') {
			continue;	/* blank/comment */
		}
		if(*p0 == '}') {
			/* record closed: leave any tail (',', ']')
			 * to the caller (array loop) */
			s->p = p0 + 1;
			break;
		}
		s->p = p0;
		rc = v2_parse_field(s, out);
		if(rc) {
			break;
		}
	}
fail:
	if(rc) {
		value_free(out);
	}
	return rc;
}

/* Parse a bracketed array literal. s->p is at '['. Elements are scalars
 * or records separated by commas; whitespace/blank lines may surround
 * items; ']' closes (may be alone on a line). */
static config_err_t v2_parse_array_inner(struct v2src *s, config_value_t *out)
{
	config_err_t rc = CONFIG_OK;

	memset(out, 0, sizeof(*out));
	out->type = CONFIG_TYPE_ARRAY;
	out->v.array.items = NULL;
	out->v.array.count = 0;
	s->p++;		/* '[' */
	for(;;) {
		int r = v2_skip_ws(s);

		if(r <= 0) {
			rc = CONFIG_ERR_PARSE;	/* unterminated */
			break;
		}
		if(*s->p == ']') {
			s->p++;
			/* only whitespace may follow on this line */
			{
				const char *t = s->p;

				while(*t == ' ' || *t == '\t') {
					t++;
				}
				if(*t) {
					rc = CONFIG_ERR_PARSE;
					break;
				}
			}
			break;
		}
		if(*s->p == ',') {
			rc = CONFIG_ERR_PARSE;	/* empty element */
			break;
		}
		{
			config_value_t item;
			config_err_t e;
			config_value_t *ni;

			memset(&item, 0, sizeof(item));
			if(*s->p == '{') {
				e = v2_parse_record(s, &item);
			} else {
				e = v2_parse_element(s, &item);
			}
			if(e) {
				rc = e;
				break;
			}
			ni = realloc(out->v.array.items,
				     (out->v.array.count + 1) *
				     sizeof(config_value_t));
			if(!ni) {
				value_free(&item);
				rc = CONFIG_ERR_NOMEM;
				break;
			}
			out->v.array.items = ni;
			out->v.array.items[out->v.array.count++] = item;
		}
		/* after an element: comma or close */
		r = v2_skip_ws(s);
		if(r <= 0) {
			rc = CONFIG_ERR_PARSE;
			break;
		}
		if(*s->p == ',') {
			s->p++;
			continue;
		}
		if(*s->p == ']') {
			s->p++;
			{
				const char *t = s->p;

				while(*t == ' ' || *t == '\t') {
					t++;
				}
				if(*t) {
					rc = CONFIG_ERR_PARSE;
					break;
				}
			}
			break;
		}
		rc = CONFIG_ERR_PARSE;
		break;
	}
	if(rc) {
		value_free(out);
	}
	return rc;
}

/* v2 value parser entry: parse a value that starts at *s->p on the
 * current (caller) line. Scalars/comma arrays fall through to the v1
 * single-line parse_value; '['/'{' span lines via the recursive
 * parsers above. */
static config_err_t v2_parse_value(struct v2src *s, config_value_t *out)
{
	const char *p = s->p;

	while(*p == ' ' || *p == '\t') {
		p++;
	}
	if(*p == '[') {
		s->p = p;
		return v2_parse_array(s, out);
	}
	if(*p == '{') {
		s->p = p;
		return v2_parse_record(s, out);
	}
	return parse_value(p, out);
}

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
				if(*val == '[') {
					/* v2 bracketed literal: may span lines and hold
					 * records. Parse with the recursive v2 value
					 * parser, which consumes whole lines from the
					 * shared cursor (off). */
					struct v2src vs;

					vs.text = text;
					vs.len = len;
					vs.off = &off;
					vs.depth = 0;
					snprintf(vs.line, sizeof(vs.line), "%s", val);
					vs.p = vs.line;
					e = v2_parse_value(&vs, &v);
				} else {
					e = parse_value(val, &v);
				}
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
		} else {
			blocks_free(explicit, nexp);
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

/* Load + parse a .conf file at an explicit path (not a scope/domain
 * lookup). *found is 1 when the file existed (even if empty).
 * blocks/nblocks (either may be NULL) return the top-level explicit-
 * block names for the canonical writer. */
static config_err_t load_path(const char *path, struct entry **list,
			      int *found, char ***blocks, int *nblocks)
{
	char *text;
	struct stat st;
	int fd, n;
	config_err_t e;

	*found = 0;
	*list = NULL;
	if(blocks) {
		*blocks = NULL;
	}
	if(nblocks) {
		*nblocks = 0;
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

/* Load + parse a scope's domain file. *found is 1 when the file
 * existed (even if empty). blocks/nblocks (either may be NULL) return
 * the top-level explicit-block names for the canonical writer. */
static config_err_t load_entries(config_scope_t scope, const char *domain,
				 struct entry **list, int *found,
				 char ***blocks, int *nblocks)
{
	char path[PATH_MAX];
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
	return load_path(path, list, found, blocks, nblocks);
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

/*
 * Synthesize a CONFIG_TYPE_RECORD for a block name from its prefix
 * children in one scope's flat entry list (v2 §10.1 "reads never care
 * which spelling": `user = { … }` and `user.admin.uid = 0` both read
 * back whole via config_read("user")). Children keep file order; a
 * child is a leaf when an exact entry exists, else a nested record
 * built from its own prefix children. Returns CONFIG_ERR_NOT_FOUND when
 * `key` has no children in this list.
 */
static config_err_t record_from_prefix(struct entry *list, const char *key,
				       config_value_t *out)
{
	const char *pre;
	char seg[CONF_MAX_SEGMENT + 1];
	size_t plen, n = 0, cap = 0;
	config_record_field_t *fields = NULL;
	struct entry *en;
	config_err_t rc = CONFIG_ERR_NOT_FOUND;

	pre = key;
	plen = strlen(pre);
	memset(out, 0, sizeof(*out));
	for(en = list; en; en = en->next) {
		size_t el = strlen(en->key);
		size_t m;
		int have = 0;

		if(el <= plen || strncmp(en->key, pre, plen) ||
		   en->key[plen] != '.') {
			continue;
		}
		/* immediate child segment: up to the next '.' */
		m = plen + 1;
		while(m < el && en->key[m] != '.') {
			if(m - (plen + 1) >= CONF_MAX_SEGMENT) {
				break;
			}
			m++;
		}
		{
			size_t cl = m - (plen + 1);
			size_t j;

			if(cl == 0 || cl > CONF_MAX_SEGMENT) {
				continue;
			}
			memcpy(seg, en->key + plen + 1, cl);
			seg[cl] = '\0';
			for(j = 0; j < n; j++) {
				if(!strcmp(fields[j].name, seg)) {
					have = 1;
					break;
				}
			}
		}
		if(have) {
			continue;
		}
		/* new child `key.seg`: leaf or nested record? */
		{
			char full[CONF_MAX_KEY + 1];
			struct entry *exact;
			config_value_t v;
			config_err_t e;
			config_record_field_t *nf;

			snprintf(full, sizeof(full), "%s.%s", key, seg);
			exact = entry_find(list, full);
			memset(&v, 0, sizeof(v));
			if(exact) {
				e = value_copy(&v, &exact->val);
			} else {
				e = record_from_prefix(list, full, &v);
			}
			if(e) {
				rc = e;
				break;
			}
			if(n == cap) {
				cap = cap ? cap * 2 : 4;
				nf = realloc(fields,
					     cap * sizeof(*nf));
				if(!nf) {
					value_free(&v);
					rc = CONFIG_ERR_NOMEM;
					break;
				}
				fields = nf;
			}
			fields[n].name = strdup(seg);
			if(!fields[n].name) {
				value_free(&v);
				rc = CONFIG_ERR_NOMEM;
				break;
			}
			memset(&fields[n].value, 0, sizeof(fields[n].value));
			memcpy(&fields[n].value, &v, sizeof(v));
			n++;
			rc = CONFIG_OK;
		}
	}
	if(rc == CONFIG_OK) {
		out->type = CONFIG_TYPE_RECORD;
		out->v.record.fields = fields;
		out->v.record.count = n;
		return CONFIG_OK;
	}
	if(fields) {
		size_t j;

		for(j = 0; j < n; j++) {
			free(fields[j].name);
			value_free(&fields[j].value);
		}
		free(fields);
	}
	return rc;
}

/* ---- reading ------------------------------------------------------- */

/*
 * M1 addressing (docs §10.2): `key` may be a plain dotted key OR carry
 * array-index segments (`rules[1].edits[0].value`, bare-digit
 * `rules[0].tests.1`). Resolution happens in two phases:
 *   1. the leading run of ident segments forms a *dotted base key*
 *      resolved against the flat store exactly as a plain dotted read
 *      (exact entry, or a record synthesized from prefix children);
 *   2. remaining steps walk that resolved tree value: ADDR_INDEX
 *      selects an array element, ADDR_NAME selects a record field
 *      (inside an array-of-records / record value the fields are real
 *      record children).
 * A record value reached via a plain dotted key keeps its M0 read
 * (synthesis); an ADDR_NAME after the base only occurs inside tree
 * values whose fields are already stored (never flattened again).
 *
 * Returns CONFIG_ERR_TYPE when a step cannot apply (index on a
 * non-array, name on a non-record) and CONFIG_ERR_NOT_FOUND for an
 * out-of-range index / absent field / absent base.
 */
static config_err_t tree_descend(config_value_t *cur,
				 const struct addr_step *steps, size_t n,
				 size_t from, config_value_t *out)
{
	config_value_t node;
	config_err_t e;

	/* cur is owned by the caller; deep-copy it as the starting node
	 * so we can free intermediate nodes as we walk */
	memset(&node, 0, sizeof(node));
	e = value_copy(&node, cur);
	if(e) {
		return e;
	}
	while(from < n) {
		if(steps[from].kind == ADDR_INDEX) {
			if(node.type != CONFIG_TYPE_ARRAY) {
				value_free(&node);
				return CONFIG_ERR_TYPE;
			}
			if(steps[from].index >= node.v.array.count) {
				value_free(&node);
				return CONFIG_ERR_NOT_FOUND;
			}
			{
				config_value_t next;

				memset(&next, 0, sizeof(next));
				e = value_copy(&next,
					       &node.v.array.items[steps[from].index]);
				value_free(&node);
				if(e) {
					return e;
				}
				node = next;
			}
		} else {	/* ADDR_NAME: record field */
			config_value_t *field = NULL;

			if(node.type != CONFIG_TYPE_RECORD) {
				value_free(&node);
				return CONFIG_ERR_TYPE;
			}
			e = config_record_child(&node, steps[from].name,
						 &field);
			if(e) {
				value_free(&node);
				return e == CONFIG_ERR_NOT_FOUND ?
				       CONFIG_ERR_NOT_FOUND : e;
			}
			{
				config_value_t next;

				memset(&next, 0, sizeof(next));
				e = value_copy(&next, field);
				value_free(&node);
				if(e) {
					return e;
				}
				node = next;
			}
		}
		from++;
	}
	memcpy(out, &node, sizeof(node));
	return CONFIG_OK;
}

/* Resolve `key` against an already-loaded entry list (one scope's file
 * or a raw path). The list is consumed/freed on all paths. This is the
 * resolution half of read_scope_internal() shared with config_read_path. */
static config_err_t resolve_list(struct entry *list, const char *key,
				 config_value_t *out)
{
	struct entry *en;
	struct addr_step steps[ADDR_MAX_STEPS];
	size_t nsteps;
	config_err_t e;

	if(!address_split(key, steps, &nsteps)) {
		entries_free(list);
		return CONFIG_ERR_INVALID;
	}
	if(first_index_step(steps, nsteps) == nsteps) {
		/* fast path: plain dotted key (no index segments) */
		en = entry_find(list, key);
		if(!en) {
			/* v2: a block name reads back as a synthesized
			 * record from its prefix children (§10.1 "reads
			 * never care which spelling") */
			e = record_from_prefix(list, key, out);
			entries_free(list);
			return e;
		}
		e = value_copy(out, &en->val);
		entries_free(list);
		return e;
	}
	/* addressing key with index segments */
	{
		size_t base_idents = first_index_step(steps, nsteps);
		char base[CONF_MAX_KEY + 1];
		config_value_t baseval;

		if(base_idents == 0) {
			entries_free(list);
			return CONFIG_ERR_INVALID;	/* no leading name */
		}
		name_prefix_key(steps, base_idents, base, sizeof(base));
		en = entry_find(list, base);
		if(!en) {
			/* base may be a record synthesized from prefix
			 * children; inside a flat-spelled record only
			 * names may follow (records aren't arrays) */
			if(!record_from_prefix(list, base, &baseval)) {
				config_err_t e2;

				entries_free(list);
				e2 = tree_descend(&baseval, steps, nsteps,
						  base_idents, out);
				value_free(&baseval);
				return e2;
			}
			entries_free(list);
			return CONFIG_ERR_NOT_FOUND;
		}
		/* base is an exact entry: walk its value with the
		 * remaining steps */
		{
			config_err_t e2;

			memset(&baseval, 0, sizeof(baseval));
			e2 = value_copy(&baseval, &en->val);
			entries_free(list);
			if(e2) {
				return e2;
			}
			e2 = tree_descend(&baseval, steps, nsteps,
					  base_idents, out);
			value_free(&baseval);
			return e2;
		}
	}
}

static config_err_t read_scope_internal(config_scope_t scope,
					const char *domain, const char *key,
					config_value_t *out)
{
	struct entry *list;
	int found;
	config_err_t e = load_entries(scope, domain, &list, &found,
				      NULL, NULL);

	if(e) {
		return e;
	}
	return resolve_list(list, key, out);
}

config_err_t config_read_scope(config_scope_t scope, const char *domain,
			       const char *key, config_value_t *out)
{
	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !key || !out || !config_valid_domain(domain) ||
	   !config_valid_address(key)) {
		return CONFIG_ERR_INVALID;
	}
	return read_scope_internal(scope, domain, key, out);
}

/*
 * Raw-file read (v2): parse ONE .conf file at an absolute path and
 * resolve `key` in it. Unlike the domain reads there is no scope tree
 * and no system -> user -> shared merge — the file is the only source.
 * Used for data files that live outside the Configuration/ dirs (e.g.
 * Argentum theme files under /Shared/Themes/<theme>.conf). Key rules,
 * record synthesis and error codes match config_read_scope(). On
 * CONFIG_OK *out holds the value (free with config_value_free()).
 */
config_err_t config_read_file(const char *path, const char *key,
			      config_value_t *out)
{
	struct entry *list;
	int found;
	config_err_t e;

	if(!path || !*path || !key || !out ||
	   !config_valid_address(key)) {
		return CONFIG_ERR_INVALID;
	}
	e = load_path(path, &list, &found, NULL, NULL);
	if(e) {
		return e;
	}
	return resolve_list(list, key, out);
}

/* resolution order: system -> user -> shared (docs plan D4; the enum
 * values are USER=0 < SHARED=1 < SYSTEM=2, so it is not a simple walk) */
static const config_scope_t scope_order[] = {
	CONFIG_SCOPE_SYSTEM,
	CONFIG_SCOPE_USER,
	CONFIG_SCOPE_SHARED,
};

/* precedence rank: system highest */
static int scope_rank(config_scope_t s)
{
	switch(s) {
	case CONFIG_SCOPE_SYSTEM:	return 2;
	case CONFIG_SCOPE_SHARED:	return 0;
	default:			return 1;	/* USER */
	}
}

/*
 * Where does `domain`'s behaviour material live? Behaviour material is NOT
 * configuration (docs/design/config-design.md §0): each app's scripts and
 * app data live in an Application Support/ directory keyed by the app's
 * DOMAIN NAME, and the scopes resolve with the SAME precedence its settings
 * use - scope_rank() above: SYSTEM wins, then USER, then SHARED. (Walked in
 * rank order rather than through scope_order[], which exists for the merge
 * loop; the two must stay in step, so this reads the ranks directly.)
 *
 * *scope (optional) reports which scope won. CONFIG_ERR_NOT_FOUND when no
 * scope has a directory for the domain.
 */
config_err_t config_app_support(const char *domain, config_scope_t *scope,
				char *out, size_t outsz)
{
	static const config_scope_t by_rank[] = {
		CONFIG_SCOPE_SYSTEM,
		CONFIG_SCOPE_USER,
		CONFIG_SCOPE_SHARED,
	};
	size_t k;

	if(!domain || !out || outsz == 0 || !config_valid_domain(domain)) {
		return CONFIG_ERR_INVALID;
	}
	for(k = 0; k < sizeof(by_rank) / sizeof(by_rank[0]); k++) {
		char dir[PATH_MAX];
		char path[PATH_MAX];
		struct stat st;

		app_support_dir_path(by_rank[k], dir, sizeof(dir));
		if(snprintf(path, sizeof(path), "%s/%s", dir, domain) >=
		   (int) sizeof(path)) {
			return CONFIG_ERR_INVALID;
		}
		if(stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}
		if(snprintf(out, outsz, "%s", path) >= (int) outsz) {
			return CONFIG_ERR_INVALID;
		}
		if(scope) {
			*scope = by_rank[k];
		}
		return CONFIG_OK;
	}
	return CONFIG_ERR_NOT_FOUND;
}

config_err_t config_read(const char *domain, const char *key,
			 config_scope_t *found_scope, config_value_t *out)
{
	struct addr_step steps[ADDR_MAX_STEPS];
	size_t nsteps, base_idents, k;
	char base[CONF_MAX_KEY + 1];
	config_value_t merged;
	config_scope_t top_scope = CONFIG_SCOPE_USER;
	int have = 0;

	if(!domain || !key || !out || !config_valid_domain(domain) ||
	   !config_valid_address(key)) {
		return CONFIG_ERR_INVALID;
	}
	if(!address_split(key, steps, &nsteps)) {
		return CONFIG_ERR_INVALID;
	}
	base_idents = first_index_step(steps, nsteps);
	if(base_idents == 0) {
		return CONFIG_ERR_INVALID;
	}
	if(base_idents == nsteps) {
		snprintf(base, sizeof(base), "%s", key);
	} else {
		name_prefix_key(steps, base_idents, base, sizeof(base));
	}

	/* v2 (§5-v2): arrays are additive — the effective value of an
	 * array base key is the concatenation of every scope's list in
	 * precedence order. Scalars/records keep v1 wholesale (highest
	 * scope that defines the key wins). */
	memset(&merged, 0, sizeof(merged));
	for(k = 0; k < 3; k++) {
		config_scope_t s = scope_order[k];
		struct entry *list;
		int found;
		config_err_t e = load_entries(s, domain, &list, &found,
					      NULL, NULL);
		struct entry *en;

		if(e == CONFIG_ERR_NOT_FOUND) {
			continue;
		}
		if(e) {
			value_free(&merged);
			return e;	/* PARSE/IO/ACCESS abort */
		}
		en = entry_find(list, base);
		if(!en) {
			config_value_t v;

			if(record_from_prefix(list, base, &v)) {
				entries_free(list);
				continue;	/* base absent in scope */
			}
			/* base is a record tree in this scope */
			entries_free(list);
			if(!have || scope_rank(s) > scope_rank(top_scope)) {
				value_free(&merged);
				merged = v;
				top_scope = s;
				have = 1;
			} else {
				value_free(&v);
			}
			if(merged.type != CONFIG_TYPE_ARRAY) {
				/* records/scalars: wholesale, stop */
				break;
			}
			continue;
		}
		{
			config_value_t ev;

			memset(&ev, 0, sizeof(ev));
			e = value_copy(&ev, &en->val);
			entries_free(list);
			if(e) {
				value_free(&merged);
				return e;
			}
			if(ev.type == CONFIG_TYPE_ARRAY) {
				/* additive: append this scope's list */
				size_t i;

				if(!have) {
					merged.type = CONFIG_TYPE_ARRAY;
					top_scope = s;
				} else if(merged.type != CONFIG_TYPE_ARRAY) {
					/* higher scope had scalar/record */
					value_free(&merged);
					value_free(&ev);
					return CONFIG_ERR_TYPE;
				}
				for(i = 0; i < ev.v.array.count; i++) {
					config_value_t *ni, *el;

					ni = realloc(merged.v.array.items
						     ? merged.v.array.items
						     : NULL,
						     (merged.v.array.count + 1) *
						     sizeof(config_value_t));
					if(!ni) {
						value_free(&merged);
						value_free(&ev);
						return CONFIG_ERR_NOMEM;
					}
					merged.v.array.items = ni;
					el = &merged.v.array.items[merged.v.array.count];
					memset(el, 0, sizeof(*el));
					e = value_copy(el, &ev.v.array.items[i]);
					if(e) {
						value_free(&merged);
						value_free(&ev);
						return e;
					}
					merged.v.array.count++;
				}
				value_free(&ev);
				have = 1;
				continue;
			}
			/* wholesale scalar/record: highest defining scope
			 * wins, lower scopes cannot extend it (v1) */
			if(!have || scope_rank(s) > scope_rank(top_scope)) {
				value_free(&merged);
				merged = ev;
				top_scope = s;
				have = 1;
			} else {
				value_free(&ev);
			}
			break;	/* scalar/record never additive */
		}
	}
	if(!have) {
		return CONFIG_ERR_NOT_FOUND;
	}
	if(base_idents == nsteps) {
		/* whole-key read: merged value IS the answer */
		memcpy(out, &merged, sizeof(merged));
		if(found_scope) {
			*found_scope = top_scope;
		}
		return CONFIG_OK;
	}
	/* descend remaining index/name steps through the merged tree */
	{
		config_err_t e = tree_descend(&merged, steps, nsteps,
					      base_idents, out);

		value_free(&merged);
		if(!e && found_scope) {
			*found_scope = top_scope;
		}
		return e;
	}
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

config_err_t config_record_child(const config_value_t *record,
				 const char *name,
				 config_value_t **out)
{
	if(!record || !name || record->type != CONFIG_TYPE_RECORD) {
		return CONFIG_ERR_TYPE;
	}
	{
		size_t i;

		for(i = 0; i < record->v.record.count; i++) {
			if(!strcmp(record->v.record.fields[i].name, name)) {
				if(out) {
					*out = &record->v.record.fields[i].value;
				}
				return CONFIG_OK;
			}
		}
	}
	return CONFIG_ERR_NOT_FOUND;
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

static config_err_t emit_value_lines(int fd, const char *indent,
				     const char *name,
				     const config_value_t *v, char *scratch);
static void indent_of(int depth, char *out, size_t outsz);
static config_err_t write_indented(int fd, const char *indent,
				   const char *relkey, const char *rhs);
static bool value_has_records(const config_value_t *v);

/* canonical leaf emitter: one-line for scalars/scalar arrays (v1
 * spelling), nested v2 spelling for records and record-bearing arrays.
 * Replaces write_indented at leaf sites so v2-shaped domains round-trip
 * through their own grammar. */
static config_err_t emit_leaf(int fd, const char *indent, const char *name,
			      const config_value_t *v)
{
	if(v->type == CONFIG_TYPE_RECORD ||
	   (v->type == CONFIG_TYPE_ARRAY && value_has_records(v))) {
		return emit_value_lines(fd, indent, name, v, NULL);
	}
	{
		char *tv = NULL;
		config_err_t e = value_to_text(v, &tv);

		if(e) {
			return e;
		}
		e = write_indented(fd, indent, name, tv);
		free(tv);
		return e;
	}
}

/* ---- canonical writer (group records, docs §3.1) ------------------- */

static config_err_t emit_value_lines(int fd, const char *indent,
				     const char *name,
				     const config_value_t *v, char *scratch);

/* true when the value contains at least one CONFIG_TYPE_RECORD anywhere
 * (a record, or an array element chain reaching a record) */
static bool value_has_records(const config_value_t *v)
{
	switch(v->type) {
	case CONFIG_TYPE_RECORD:
		return true;
	case CONFIG_TYPE_ARRAY: {
		size_t i;

		for(i = 0; i < v->v.array.count; i++) {
			if(value_has_records(&v->v.array.items[i])) {
				return true;
			}
		}
		break;
	}
	default:
		break;
	}
	return false;
}

/* raw single-line write helper */
static config_err_t putl(int fd, const char *s, size_t n)
{
	if(write(fd, s, n) != (ssize_t)n) {
		return CONFIG_ERR_IO;
	}
	return CONFIG_OK;
}

/* one record value: its fields on lines one deeper than `indent`.
 * Canonical record body: first field may share the `{` line is allowed,
 * but the writer always opens `{` alone and closes `}` alone (the
 * strict §10.2 line structure). */
static config_err_t emit_record_body(int fd, const char *indent,
				     const config_value_t *rec)
{
	char inner[640];
	size_t i;
	config_err_t e = CONFIG_OK;

	indent_of(strlen(indent) / 4 + 1, inner, sizeof(inner));
	for(i = 0; i < rec->v.record.count && !e; i++) {
		const config_record_field_t *f = &rec->v.record.fields[i];

		e = emit_value_lines(fd, inner, f->name, &f->value, NULL);
	}
	return e;
}

/* An array that contains records: the canonical anonymous-array
 * spelling. Elements are one per line; a `,` on its own line separates
 * elements (the v2 line grammar has no inline ';' separators). */
static config_err_t emit_record_array(int fd, const char *indent,
				      const config_value_t *arr)
{
	char inner[640];
	size_t i;
	config_err_t e = CONFIG_OK;

	indent_of(strlen(indent) / 4 + 1, inner, sizeof(inner));
	for(i = 0; i < arr->v.array.count && !e; i++) {
		const config_value_t *el = &arr->v.array.items[i];
		char et[CONF_MAX_LINE + 256];
		char line[CONF_MAX_LINE + 640];
		size_t n;

		if(el->type == CONFIG_TYPE_RECORD) {
			n = snprintf(line, sizeof(line), "%s{\n", inner);
			if(n >= sizeof(line)) {
				return CONFIG_ERR_PARSE;
			}
			e = putl(fd, line, n);
			if(e) {
				break;
			}
			e = emit_record_body(fd, inner, el);
			if(e) {
				break;
			}
			n = snprintf(line, sizeof(line), "%s}", inner);
			if(n >= sizeof(line)) {
				return CONFIG_ERR_PARSE;
			}
			e = putl(fd, line, n);
			if(e) {
				break;
			}
		} else {
			char *tv = NULL;

			e = value_to_text(el, &tv);
			if(e) {
				return e;
			}
			n = snprintf(line, sizeof(line), "%s%s", inner, tv);
			free(tv);
			if(n >= sizeof(line)) {
				return CONFIG_ERR_PARSE;
			}
			e = putl(fd, line, n);
			if(e) {
				break;
			}
		}
		(void)et;
		if(i + 1 < arr->v.array.count) {
			n = snprintf(line, sizeof(line), ",\n");
			if(write(fd, line, 2) != 2) {
				e = CONFIG_ERR_IO;
				break;
			}
		} else {
			if(write(fd, "\n", 1) != 1) {
				e = CONFIG_ERR_IO;
				break;
			}
		}
	}
	if(e) {
		return e;
	}
	{
		char line[64];
		size_t n = snprintf(line, sizeof(line), "%s]\n", indent);

		if(n >= sizeof(line)) {
			return CONFIG_ERR_PARSE;
		}
		return putl(fd, line, n);
	}
}

/* Canonical single assignment: `indent name = value`. Scalars and
 * scalar-only arrays write one line (v1 comma spelling, byte-for-byte
 * flat round trip). Records and record-bearing arrays use the nested
 * v2 spelling spanning lines. */
static config_err_t emit_value_lines(int fd, const char *indent,
				     const char *name,
				     const config_value_t *v, char *scratch)
{
	char line[CONF_MAX_KEY + CONF_MAX_LINE + 640];
	size_t n;

	(void)scratch;
	if(v->type == CONFIG_TYPE_RECORD) {
		n = snprintf(line, sizeof(line), "%s%s = {\n", indent,
			     name);
		if(n >= sizeof(line)) {
			return CONFIG_ERR_PARSE;
		}
		if(write(fd, line, n) != (ssize_t)n) {
			return CONFIG_ERR_IO;
		}
		{
			config_err_t e = emit_record_body(fd, indent, v);

			if(e) {
				return e;
			}
		}
		n = snprintf(line, sizeof(line), "%s}\n", indent);
		if(n >= sizeof(line)) {
			return CONFIG_ERR_PARSE;
		}
		return putl(fd, line, n);
	}
	if(v->type == CONFIG_TYPE_ARRAY && v->v.array.count == 0) {
		/* an empty value would reload as an empty string, so
		 * write the bracket spelling */
		n = snprintf(line, sizeof(line), "%s%s = []\n", indent,
			     name);
		if(n >= sizeof(line)) {
			return CONFIG_ERR_PARSE;
		}
		return putl(fd, line, n);
	}
	if(v->type == CONFIG_TYPE_ARRAY && value_has_records(v)) {
		n = snprintf(line, sizeof(line), "%s%s = [\n", indent,
			     name);
		if(n >= sizeof(line)) {
			return CONFIG_ERR_PARSE;
		}
		if(write(fd, line, n) != (ssize_t)n) {
			return CONFIG_ERR_IO;
		}
		return emit_record_array(fd, indent, v);
	}
	{
		char *tv = NULL;
		config_err_t e = value_to_text(v, &tv);

		if(e) {
			return e;
		}
		n = snprintf(line, sizeof(line), "%s%s = %s\n", indent,
			     name, tv);
		free(tv);
		if(n >= sizeof(line)) {
			return CONFIG_ERR_PARSE;
		}
		return putl(fd, line, n);
	}
}

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
			/* a leaf child: "name = value" (record-bearing
			 * values take the nested v2 spelling) */
			e = emit_leaf(fd, ind, kids[i], &leaf->val);
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

			e = emit_leaf(fd, "", tops[i], &leaf->val);
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
				e = emit_leaf(fd, "", en->key, &en->val);
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
		for(en = list; en && !e; en = en->next) {
			e = emit_leaf(fd, "", en->key, &en->val);
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

/* true when an addressing key uses bare-digit array indexes (`a.1`,
 * `rules.0.tests.1`) rather than brackets. Reads accept both spellings
 * (§10.2); writes must not persist a bare-digit key (the file grammar
 * only accepts ident segments), so set/unset reject them. */
static bool address_has_bare_index(const char *key)
{
	const char *p = key;

	while(*p) {
		const char *e = strchr(p, '.');
		size_t slen = e ? (size_t)(e - p) : strlen(p);
		size_t j;
		int all_digits = slen != 0;

		for(j = 0; j < slen; j++) {
			if(!isdigit((unsigned char)p[j])) {
				all_digits = 0;
				break;
			}
		}
		if(all_digits) {
			return true;
		}
		p = e ? e + 1 : p + slen;
	}
	return false;
}

/* Generic single-key write: load, replace/append, write back. */
static config_err_t set_key_replace(struct entry **listp, const char *key,
				    const config_value_t *v);
static config_err_t set_address_value(config_value_t *cur,
				      const struct addr_step *steps,
				      size_t n, size_t from,
				      const config_value_t *v);
static config_err_t set_key(config_scope_t scope, const char *domain,
			    const char *key, const config_value_t *v)
{
	struct entry *list = NULL;
	int found;
	config_err_t e;

	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !key || !config_valid_domain(domain) ||
	   !config_valid_address(key) || address_has_bare_index(key)) {
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
		if(strchr(key, '[')) {
			/* M1: in-place bracket-path element write on an
			 * existing stored array value */
			struct addr_step steps[ADDR_MAX_STEPS];
			size_t nsteps, base_idents;
			char base[CONF_MAX_KEY + 1];
			struct entry *en;

			if(!address_split(key, steps, &nsteps) ||
			   (base_idents = first_index_step(steps, nsteps)) == 0) {
				e2 = CONFIG_ERR_INVALID;
				goto out;
			}
			name_prefix_key(steps, base_idents, base, sizeof(base));
			en = entry_find(list, base);
			if(!en) {
				e2 = CONFIG_ERR_NOT_FOUND;
				goto out;
			}
			if(en->val.type != CONFIG_TYPE_ARRAY) {
				e2 = CONFIG_ERR_TYPE;
				goto out;
			}
			e2 = set_address_value(&en->val, steps, nsteps,
					       base_idents, v);
			if(!e2) {
				e2 = write_entries(scope, domain, list, false,
						   blocks, nblocks);
			}
			goto out;
		}
		e2 = set_key_replace(&list, key, v);
		if(!e2) {
			e2 = write_entries(scope, domain, list, false,
					   blocks, nblocks);
		}
out:
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

/*
 * M1 bracket-path set (docs §10.2): mutate an existing element of a
 * stored array value addressed by `key` (`accept[1]`, `rules[0]`,
 * `rules[0].edits[1].value`). Only writes whose resulting domain still
 * serializes with the M0/M1 writer are accepted — i.e. arrays whose
 * elements are scalars. Domains holding CONFIG_TYPE_RECORD values (or
 * arrays containing records) still reject cleanly with
 * CONFIG_ERR_INVALID until M2's canonical writer.
 */
static config_err_t set_address_value(config_value_t *cur,
				      const struct addr_step *steps,
				      size_t n, size_t from,
				      const config_value_t *v)
{
	size_t k;

	for(k = from; k < n; k++) {
		if(steps[k].kind == ADDR_INDEX) {
			if(cur->type != CONFIG_TYPE_ARRAY) {
				return CONFIG_ERR_TYPE;
			}
			if(steps[k].index >= cur->v.array.count) {
				/* out-of-range: cannot grow a record
				 * array in M1 (writer can't serialize);
				 * scalar arrays grow by set_array. */
				return CONFIG_ERR_NOT_FOUND;
			}
			if(k == n - 1) {
				if(cur->v.array.items[steps[k].index].type ==
				   CONFIG_TYPE_RECORD) {
					return CONFIG_ERR_INVALID;
				}
				value_free(&cur->v.array.items[steps[k].index]);
				return value_copy(&cur->v.array.items[steps[k].index],
						  v);
			}
			cur = &cur->v.array.items[steps[k].index];
		} else {
			config_value_t *field = NULL;
			config_err_t e;

			if(cur->type != CONFIG_TYPE_RECORD) {
				return CONFIG_ERR_TYPE;
			}
			e = config_record_child(cur, steps[k].name, &field);
			if(e) {
				return e;
			}
			if(k == n - 1) {
				if(field->type == CONFIG_TYPE_RECORD) {
					return CONFIG_ERR_INVALID;
				}
				value_free(field);
				return value_copy(field, v);
			}
			cur = field;
		}
	}
	return CONFIG_ERR_INVALID;
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
	char **blocks = NULL;
	int nblocks = 0;

	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !key || !config_valid_domain(domain) ||
	   !config_valid_address(key) || address_has_bare_index(key)) {
		return CONFIG_ERR_INVALID;
	}
	{
		e = load_entries(scope, domain, &list, &found, &blocks,
				 &nblocks);
		if(e == CONFIG_ERR_NOT_FOUND) {
			return CONFIG_OK;	/* nothing to unset */
		}
		if(e) {
			return e;
		}
		if(strchr(key, '[')) {
			/* M1: remove one element of a stored scalar
			 * array by address (records reject until the M2
			 * canonical writer). Only a plain `base[i]`
			 * address is supported for unset. */
			struct addr_step steps[ADDR_MAX_STEPS];
			size_t nsteps, base_idents;
			char base[CONF_MAX_KEY + 1];

			if(!address_split(key, steps, &nsteps) ||
			   (base_idents = first_index_step(steps, nsteps)) == 0 ||
			   base_idents != nsteps - 1 ||
			   steps[base_idents].kind != ADDR_INDEX) {
				e = CONFIG_ERR_INVALID;
				goto unset_out;
			}
			name_prefix_key(steps, base_idents, base, sizeof(base));
			for(en = list; en; en = en->next) {
				struct entry *pv;

				if(strcmp(en->key, base)) {
					continue;
				}
				if(en->val.type != CONFIG_TYPE_ARRAY ||
				   steps[base_idents].index >=
					   en->val.v.array.count) {
					e = CONFIG_ERR_NOT_FOUND;
					goto unset_out;
				}
				if(en->val.v.array.items[steps[base_idents].index]
					   .type == CONFIG_TYPE_RECORD) {
					e = CONFIG_ERR_INVALID;
					goto unset_out;
				}
				{
					size_t i = steps[base_idents].index;

					value_free(&en->val.v.array.items[i]);
					memmove(&en->val.v.array.items[i],
						&en->val.v.array.items[i + 1],
						(en->val.v.array.count - i - 1) *
						sizeof(config_value_t));
					en->val.v.array.count--;
				}
				if(en->val.v.array.count == 0) {
					/* dropping the last element must
					 * remove the entry: an empty
					 * array serializes as "" and would
					 * reload as an empty string */
					for(pv = list; pv; pv = pv->next) {
						if(pv->next == en) {
							break;
						}
					}
					if(pv && pv->next == en) {
						pv->next = en->next;
					} else {
						list = en->next;
					}
					free(en->key);
					value_free(&en->val);
					free(en);
				}
				e = write_entries(scope, domain, list, true,
						  blocks, nblocks);
				goto unset_out;
			}
			e = CONFIG_OK;	/* array absent: idempotent */
			goto unset_out;
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
unset_out:
	blocks_free(blocks, nblocks);
	entries_free(list);
	return e;
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
