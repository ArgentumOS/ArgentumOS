/* libconfig.c — implementation of include/libconfig.h (FNX userland).
 *
 * One plain-text "key = value" file per reverse-DNS domain in a
 * Configuration/ directory at each of the three scopes; reads resolve
 * user -> shared -> system; writes go to an explicit scope atomically
 * (temp + fsync + rename). Grammar: docs/config-design.md §10.
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

/* Full on-disk path of a domain file in a scope. Returns 0 or
 * CONFIG_ERR_INVALID (bad scope/domain/truncation). */
static config_err_t domain_path(config_scope_t scope, const char *domain,
				char *out, size_t outsz)
{
	size_t base;

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

/* Parse whole-file text into an entry list (duplicate keys: last
 * occurrence wins, keeping the first occurrence's position). */
static config_err_t parse_conf(const char *text, size_t len,
			       struct entry **list)
{
	size_t off = 0;
	struct entry *head = NULL, *tail = NULL;

	if(len >= 3 && (unsigned char)text[0] == 0xEF &&
	   (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
		off = 3;	/* skip a UTF-8 BOM */
	}
	while(off < len) {
		char line[CONF_MAX_LINE + 1];
		size_t n = 0;
		char *p;

		while(off < len && text[off] != '\n' && n < CONF_MAX_LINE) {
			line[n++] = text[off++];
		}
		if(off < len && text[off] == '\n') {
			off++;	/* consume the newline */
		} else if(n == CONF_MAX_LINE && off < len) {
			return CONFIG_ERR_PARSE;	/* line too long */
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
		{
			char *eq = strchr(p, '=');
			char *keyend;
			char *val;
			char *key;
			config_value_t v;
			config_err_t e;
			struct entry *en, *cur;

			if(!eq) {
				return CONFIG_ERR_PARSE;	/* no '=' */
			}
			keyend = eq;
			while(keyend > p && (keyend[-1] == ' ' ||
					     keyend[-1] == '\t')) {
				keyend--;
			}
			if(keyend == p) {
				return CONFIG_ERR_PARSE;
			}
			*keyend = '\0';
			if(!config_valid_key(p)) {
				return CONFIG_ERR_PARSE;
			}
			val = eq + 1;
			while(*val == ' ' || *val == '\t') {
				val++;
			}
			key = strdup(p);
			if(!key) {
				return CONFIG_ERR_NOMEM;
			}
			e = parse_value(val, &v);
			if(e) {
				free(key);
				return e;
			}
			/* duplicate key: replace in place (last wins) */
			for(cur = head; cur; cur = cur->next) {
				if(!strcmp(cur->key, key)) {
					value_free(&cur->val);
					cur->val = v;
					free(key);
					goto next_line;
				}
			}
			en = malloc(sizeof(struct entry));
			if(!en) {
				value_free(&v);
				free(key);
				return CONFIG_ERR_NOMEM;
			}
			en->key = key;
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
	*list = head;
	return CONFIG_OK;
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
 * existed (even if empty). */
static config_err_t load_entries(config_scope_t scope, const char *domain,
				 struct entry **list, int *found)
{
	char path[PATH_MAX];
	char *text;
	struct stat st;
	int fd, n;
	config_err_t e;
	config_err_t rc;

	*found = 0;
	*list = NULL;
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
	e = parse_conf(text, n, list);
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
	config_err_t e = load_entries(scope, domain, &list, &found);
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

config_err_t config_read(const char *domain, const char *key,
			 config_scope_t *found_scope, config_value_t *out)
{
	config_scope_t s;

	if(!domain || !key || !out || !config_valid_domain(domain) ||
	   !config_valid_key(key)) {
		return CONFIG_ERR_INVALID;
	}
	for(s = CONFIG_SCOPE_USER; s <= CONFIG_SCOPE_SYSTEM; s++) {
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
	size_t n = 0, cap = 0, plen;
	int i, s;

	if(!domain || !keys || !values || !count ||
	   !config_valid_domain(domain) ||
	   (prefix && *prefix && !config_valid_key(prefix))) {
		return CONFIG_ERR_INVALID;
	}
	plen = prefix ? strlen(prefix) : 0;
	for(s = CONFIG_SCOPE_USER; s <= CONFIG_SCOPE_SYSTEM; s++) {
		int found;
		struct entry *en;
		config_err_t e = load_entries((config_scope_t)s, domain,
					      &lists[s], &found);

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

/* Serialize an entry list to the domain file atomically. When
 * remove_if_empty and the list has no entries, the file is deleted. */
static config_err_t write_entries(config_scope_t scope, const char *domain,
				  struct entry *list, bool remove_if_empty)
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
	for(en = list; en; en = en->next) {
		char *tv = NULL;
		char line[CONF_MAX_KEY + CONF_MAX_LINE + 64];
		size_t len;

		e = value_to_text(&en->val, &tv);
		if(e) {
			close(fd);
			unlink(tmp);
			return e;
		}
		len = snprintf(line, sizeof(line), "%s = %s\n", en->key, tv);
		free(tv);
		if(len >= sizeof(line)) {
			close(fd);
			unlink(tmp);
			return CONFIG_ERR_PARSE;
		}
		if(write(fd, line, len) != (ssize_t)len) {
			close(fd);
			unlink(tmp);
			return CONFIG_ERR_IO;
		}
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
static config_err_t set_key(config_scope_t scope, const char *domain,
			    const char *key, const config_value_t *v)
{
	struct entry *list = NULL, *en;
	int found;
	config_err_t e;

	if(scope < CONFIG_SCOPE_USER || scope > CONFIG_SCOPE_SYSTEM ||
	   !domain || !key || !config_valid_domain(domain) ||
	   !config_valid_key(key)) {
		return CONFIG_ERR_INVALID;
	}
	e = load_entries(scope, domain, &list, &found);
	if(e && e != CONFIG_ERR_NOT_FOUND) {
		return e;
	}
	en = entry_find(list, key);
	if(en) {
		value_free(&en->val);
	} else {
		struct entry *t = list;

		en = malloc(sizeof(struct entry));
		if(!en) {
			entries_free(list);
			return CONFIG_ERR_NOMEM;
		}
		en->key = strdup(key);
		if(!en->key) {
			free(en);
			entries_free(list);
			return CONFIG_ERR_NOMEM;
		}
		en->next = NULL;
		if(!list) {
			list = en;
		} else {
			while(t->next) {
				t = t->next;
			}
			t->next = en;
		}
	}
	if((e = value_copy(&en->val, v))) {
		entries_free(list);
		return e;
	}
	e = write_entries(scope, domain, list, false);
	entries_free(list);
	return e;
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
	e = load_entries(scope, domain, &list, &found);
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
			e = write_entries(scope, domain, list, true);
			entries_free(list);
			return e;
		}
	}
	entries_free(list);
	return CONFIG_OK;	/* key absent: idempotent */
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
