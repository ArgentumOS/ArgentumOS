/*
 * fnx/kernel/kconf.c
 *
 * FNX kernel.conf subset parser, docs/design/kernel-conf-plan.md (M1) and
 * docs/design/config-design.md §12.
 *
 * A lean, read-only parser for the kernel's kernel.conf, accepting BOTH
 * spellings of an FNX configuration.
 *
 * The line grammar (what a hand-written file may still use), matching the
 * canonical output of the userland libconfig parser (userland/libconfig.c
 * parse_conf): line-based `key = value` settings with
 *   - `#` full-line comments and blank lines,
 *   - dot-nested keys kept as one flat key (console, system.kernel.root),
 *   - bare or "quoted" string values, `true`/`false`, integers incl. `0x`,
 *   - `key =` (empty value) accepted,
 *   - duplicate keys: last wins (each key/value pair is emitted in file
 *     order; the caller applies the last one),
 *   - no scopes/arrays (kernel subset; `{`/`}` lines are rejected and
 *     skipped with a warning, never a halt).
 *
 * The plist spelling (P3e, the one every other FNX configuration uses): an XML
 * plist whose root dict holds <key> elements with one scalar <string>,
 * <integer>, <true/> or <false/> value, its prose kept as <!-- ... --> comments.
 * The kernel cannot share the userland plist core — it has no libc, and the core
 * allocates — so the arm below reads the subset itself and emits the SAME
 * key/value stream; a <dict>, <array>, <real>, <date> or <data> value is refused
 * exactly like a `{` line. Which spelling a file uses is decided by CONTENT (the
 * first non-blank character is '<'), never by its name, so both spellings stay
 * readable side by side.
 *
 * The parser has no kernel dependencies so the same object can be built
 * on the host for the conformance corpus (tools/kconf_corpus.sh): both
 * this parser and the userland parser must produce the same key/value
 * sequence from the same file, in either spelling.
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

/* ---- the plist spelling (P3e) ------------------------------------------ */

/*
 * A kernel.conf may be written as an XML plist — the spelling every other FNX
 * configuration uses (docs/design/plist-config-plan.md). The kernel has no libc
 * and the shared plist core allocates, so the walk below is its own freestanding
 * reader; it produces the SAME struct kconf_kv stream from a plist that the line
 * scanner produces from `key = value` text, so kernel_conf_apply() and
 * everything under it is blind to which spelling the file uses.
 *
 * The subset stays scalar, exactly as the line side does: a <dict> or <array>
 * VALUE has no flat-key meaning in the kernel and is refused (KCONF_KV_ERR, the
 * same posture as a '{' or '}' line), and so are <real>, <date> and <data> — the
 * subset is strings, booleans and integers. Dot-nested keys are KEYS that
 * contain dots (`system.kernel.root`), the plist spelling of the legacy
 * `system.kernel.root = ...` line. Comments — every converted file carries its
 * prose as <!-- ... --> — are skipped wherever they stand.
 */

#define KCONF_TAG_MAX	16

/*
 * Does the buffer begin as a plist? The same content rule the userland reader
 * uses (userland/libconfig_plist.c config_text_is_plist): skip a UTF-8 BOM and
 * whitespace, then look for '<'. The line grammar cannot begin that way — a
 * setting needs a key, an '=' and a value first — so the test cannot mistake
 * one spelling for the other.
 */
static int kconf_is_plist(const char *data, unsigned int size)
{
	unsigned int i = 0;

	if(size >= 3 && (unsigned char)data[0] == 0xEF &&
	   (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
		i = 3;		/* a UTF-8 BOM is not content */
	}
	while(i < size && (data[i] == ' ' || data[i] == '\t' ||
			   data[i] == '\r' || data[i] == '\n')) {
		i++;
	}
	return i < size && data[i] == '<';
}

/* Bounded literal match at 'pos': 1 when data[pos..] starts with 'lit'. */
static int kconf_plist_at(const char *data, unsigned int size, unsigned int pos,
			  const char *lit)
{
	unsigned int i = 0;

	while(lit[i]) {
		if(pos + i >= size || data[pos + i] != lit[i]) {
			return 0;
		}
		i++;
	}
	return 1;
}

/*
 * Skip whitespace and comments, leaving *pos on the next real character.
 * Returns 0 at the end of the buffer (an unterminated comment counts as
 * the end).
 */
static int kconf_plist_skip(const char *data, unsigned int size, unsigned int *pos)
{
	unsigned int p = *pos;

	for(;;) {
		while(p < size && (data[p] == ' ' || data[p] == '\t' ||
				   data[p] == '\r' || data[p] == '\n')) {
			p++;
		}
		if(!kconf_plist_at(data, size, p, "<!--")) {
			*pos = p;
			return p < size;
		}
		p += 4;
		while(p < size && !kconf_plist_at(data, size, p, "-->")) {
			p++;
		}
		if(p >= size) {
			*pos = size;
			return 0;	/* unterminated comment */
		}
		p += 3;
	}
}

/*
 * Read the tag at *pos (data[*pos] must be '<'), advancing *pos past its '>'.
 * Fills name (bounded, and empty for the `<?...?>` and `<!...>` forms) and
 * reports whether it closes an element and whether it closes itself. Returns 0
 * when the buffer ends inside the tag, leaving *pos at the end.
 */
static int kconf_plist_tag(const char *data, unsigned int size, unsigned int *pos,
			   char *name, int *is_close, int *is_empty)
{
	unsigned int p = *pos, n = 0;

	name[0] = 0;
	*is_close = 0;
	*is_empty = 0;
	if(p >= size || data[p] != '<') {
		return 0;
	}
	p++;
	if(p < size && data[p] == '/') {
		*is_close = 1;
		p++;
	}
	if(*is_close || (p < size && data[p] != '?' && data[p] != '!')) {
		while(p < size) {
			char c = data[p];

			if(c == '>' || c == '/' || c == ' ' || c == '\t' ||
			   c == '\r' || c == '\n') {
				break;
			}
			if(n < KCONF_TAG_MAX - 1) {
				name[n++] = c;
			}
			p++;
		}
	}
	name[n] = 0;
	while(p < size && data[p] != '>') {
		p++;
	}
	if(p >= size) {
		*pos = size;
		return 0;
	}
	*is_empty = p > *pos && data[p - 1] == '/';
	*pos = p + 1;
	return 1;
}

/*
 * The entity standing at 'pos' (just after '&') and running to the ';' before
 * 'end': the five named forms the serialiser emits, plus numeric ones that name
 * an ASCII character (a wider code point would need UTF-8 encoding, and the
 * kernel's settings are paths and numbers). Returns the character and its
 * length in *span, or -1.
 */
static int kconf_plist_entity(const char *data, unsigned int size,
			      unsigned int pos, unsigned int end,
			      unsigned int *span)
{
	unsigned int e = pos;

	while(e < end && data[e] != ';') {
		e++;
	}
	if(e >= end) {
		return -1;
	}
	*span = e - pos;
	if(*span == 3 && kconf_plist_at(data, size, pos, "amp")) {
		return '&';
	}
	if(*span == 2 && kconf_plist_at(data, size, pos, "lt")) {
		return '<';
	}
	if(*span == 2 && kconf_plist_at(data, size, pos, "gt")) {
		return '>';
	}
	if(*span == 4 && kconf_plist_at(data, size, pos, "quot")) {
		return '"';
	}
	if(*span == 4 && kconf_plist_at(data, size, pos, "apos")) {
		return '\'';
	}
	if(*span >= 2 && data[pos] == '#') {
		unsigned int stop = pos + *span;	/* the ';' */
		unsigned int i = pos + 1;
		int base = 10, value = 0, digits = 0;

		if(i < stop && (data[i] == 'x' || data[i] == 'X')) {
			base = 16;
			i++;
		}
		for(; i < stop; i++) {
			int d = kconf_hexval(data[i]);

			if(d < 0 || d >= base) {
				return -1;
			}
			value = value * base + d;
			digits++;
		}
		if(digits > 0 && value > 0 && value < 0x80) {
			return value;
		}
	}
	return -1;
}

/*
 * Decode the text [start,end) into out (bounded, NUL-terminated), resolving
 * entities. Returns 0 on an entity it cannot resolve — the caller reports that
 * setting as malformed, the same posture as an unterminated quote in the line
 * grammar.
 */
static int kconf_plist_text(const char *data, unsigned int size,
			    unsigned int start, unsigned int end,
			    char *out, unsigned int outsz)
{
	unsigned int i = start, n = 0;

	while(i < end) {
		if(data[i] == '&') {
			unsigned int span = 0;
			int c = kconf_plist_entity(data, size, i + 1, end, &span);

			if(c < 0) {
				return 0;
			}
			if(n < outsz - 1) {
				out[n++] = (char)c;
			}
			i += span + 2;		/* '&' + name + ';' */
			continue;
		}
		if(n < outsz - 1) {
			out[n++] = data[i];
		}
		i++;
	}
	out[n] = 0;
	return 1;
}

/*
 * Advance *pos past the element whose open tag was just consumed, counting
 * same-name nesting so a refused <dict> holding another one is skipped whole.
 * One setting the subset cannot use must not derail the rest of the file — the
 * line side's "skipped with a warning, never a halt".
 */
static void kconf_plist_skip_element(const char *data, unsigned int size,
				     unsigned int *pos, const char *name,
				     int is_empty)
{
	unsigned int p = *pos;
	int depth = is_empty ? 0 : 1;

	while(depth > 0) {
		char tag[KCONF_TAG_MAX];
		int is_close, tag_empty;
		unsigned int q;

		while(p < size && data[p] != '<') {
			p++;
		}
		if(p >= size) {
			break;
		}
		q = p;
		if(!kconf_plist_tag(data, size, &q, tag, &is_close, &tag_empty)) {
			p = size;
			break;
		}
		if(!strcmp(tag, name)) {
			if(is_close) {
				depth--;
			} else if(!tag_empty) {
				depth++;
			}
		}
		p = q;
	}
	*pos = p;
}

/*
 * One setting from a plist: the <key> element that comes next and its scalar
 * value element, emitted as the same struct kconf_kv the line scanner emits.
 * Returns 1 on a setting (KCONF_KV_ERR for one the subset refuses or cannot
 * read), 0 at the end of the file — the same contract as kconf_next().
 */
static int kconf_plist_next(const char *data, unsigned int size, unsigned int *off,
			    struct kconf_kv *out)
{
	unsigned int pos = *off;
	char tag[KCONF_TAG_MAX];
	int is_close, is_empty;

	/* 1. the next <key>. Every other tag — the root <dict>, the header's
	 * <?xml?> and <!DOCTYPE ...>, a value stray from its key — is not a
	 * setting of its own. */
	for(;;) {
		unsigned int q;

		if(!kconf_plist_skip(data, size, &pos)) {
			*off = size;
			return 0;	/* end of file */
		}
		if(data[pos] != '<') {
			pos++;		/* text outside any element */
			continue;
		}
		q = pos;
		if(!kconf_plist_tag(data, size, &q, tag, &is_close, &is_empty)) {
			*off = size;
			return 0;	/* unterminated tag: end of file */
		}
		pos = q;
		if(!is_close && !strcmp(tag, "key")) {
			break;
		}
	}

	/* 2. the key's text, up to </key> */
	{
		unsigned int e = pos;

		for(;;) {
			while(e < size && data[e] != '<') {
				e++;
			}
			if(!kconf_plist_at(data, size, e, "</key>")) {
				if(e >= size) {
					*off = size;
					return 0;	/* no </key>: end of file */
				}
				e++;
				continue;
			}
			break;
		}
		if(e - pos >= sizeof(out->key)) {
			*off = e + 6;
			return 1;		/* key too long */
		}
		if(!kconf_plist_text(data, size, pos, e, out->key, sizeof(out->key))) {
			*off = e + 6;
			return 1;		/* malformed entity in the key */
		}
		pos = e + 6;
	}

	/* 3. the value element that follows the key */
	if(!kconf_plist_skip(data, size, &pos)) {
		*off = size;
		return 0;
	}
	if(pos >= size || data[pos] != '<') {
		*off = pos;
		return 1;			/* no value at all: malformed */
	}
	{
		unsigned int q = pos;

		if(!kconf_plist_tag(data, size, &q, tag, &is_close, &is_empty)) {
			*off = size;
			return 0;
		}
		pos = q;
	}
	if(is_close) {
		*off = pos;
		return 1;			/* a close tag where a value belongs */
	}

	if(!strcmp(tag, "string") || !strcmp(tag, "integer")) {
		const char *close = !strcmp(tag, "integer") ? "</integer>" : "</string>";
		unsigned int adv = !strcmp(tag, "integer") ? 10 : 9;
		unsigned int e = pos;

		for(;;) {
			while(e < size && data[e] != '<') {
				e++;
			}
			if(!kconf_plist_at(data, size, e, close)) {
				if(e >= size) {
					*off = size;
					return 1;	/* unterminated value */
				}
				e++;
				continue;
			}
			break;
		}
		if(!kconf_plist_text(data, size, pos, e, out->value,
				     sizeof(out->value))) {
			*off = e + adv;
			return 1;		/* malformed entity in the value */
		}
		out->kind = !strcmp(tag, "integer") ? KCONF_KV_INT : KCONF_KV_STRING;
		out->is_true = 0;
		*off = e + adv;
		return 1;
	}
	if(!strcmp(tag, "true") || !strcmp(tag, "false")) {
		out->kind = KCONF_KV_BOOL;
		out->is_true = !strcmp(tag, "true");
		strncpy(out->value, out->is_true ? "true" : "false",
			sizeof(out->value));
		*off = pos;
		return 1;
	}

	/* Anything else — <real>, <date>, <data>, a nested <dict>/<array> — has
	 * no meaning in the kernel subset. Skip the whole element and report the
	 * setting as malformed, exactly as a '{' line is reported. */
	kconf_plist_skip_element(data, size, &pos, tag, is_empty);
	*off = pos;
	return 1;
}

/* the line grammar, defined below */
static int kconf_legacy_next(const char *data, unsigned int size, unsigned int *off,
			     struct kconf_kv *out);

/*
 * kconf_next() - parse the next setting from 'data' (size-bounded, may
 * also be NUL-terminated) starting at *off, from either spelling: an XML plist
 * (P3e) or the `key = value` line grammar. Returns 1 and fills *out on a
 * setting (kind KCONF_KV_ERR for a skipped malformed line), 0 at EOF.
 */
int kconf_next(const char *data, unsigned int size, unsigned int *off,
	       struct kconf_kv *out)
{
	out->kind = KCONF_KV_ERR;
	if(kconf_is_plist(data, size)) {
		return kconf_plist_next(data, size, off, out);
	}
	return kconf_legacy_next(data, size, off, out);
}

/*
 * kconf_legacy_next() - the line grammar: `key = value` settings with '#'
 * comments and blank lines.
 */
static int kconf_legacy_next(const char *data, unsigned int size, unsigned int *off,
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
