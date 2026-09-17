/*
 * libconfig_plist.c — the plist spelling of a config file.
 *
 * docs/design/plist-config-plan.md, stage P3b. A .conf file is read in EITHER
 * spelling — the legacy "key = value" text (still parsed in libconfig.c) or an
 * XML plist — and both parse into the same flat, ordered entry list
 * (libconfig_internal.h). The writer emits plists, unconditionally: that is the
 * user's recorded decision, and it is why the reader had to accept both
 * spellings before the writer changed.
 *
 * WHY THE TREE IS FLAT. The config model is a dotted-key store: `window.width`
 * is one entry, and a record is SYNTHESIZED from a key's prefix children
 * (docs/design/config-design.md §10.1). So a nested <dict> here is not a
 * separate value type — it is the plist spelling of `window = { … }`, and it
 * flattens to `window.width`. Nothing downstream sees a difference, which is
 * the property the whole conversion rests on.
 *
 * WHAT A PLIST CAN SAY THAT A .conf CANNOT: <date> and <data>. The config model
 * has no such types, so they are REFUSED with a message rather than guessed
 * into strings — a reader that invents a type is a reader that loses one.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libconfig.h>
#include <plist.h>		/* the shared core: -Iinclude */

#include "libconfig_internal.h"

/* Deep enough for any hand-written config; a cycle or a hostile file stops
 * here rather than at the stack guard. */
#define PLIST_CONF_MAX_DEPTH	32

/* ---- prose ----------------------------------------------------------- */

config_err_t config_comment_append(char **blob, const char *line)
{
	const char *body = line;
	size_t blen, used, add;
	int first;
	char *fresh;

	/* One line of prose, normalized for the plist spelling: a leading and a
	 * trailing space, so `<!-- fstype = proc -->` reads the way the legacy
	 * `# fstype = proc` did. A line with nothing on it stays EMPTY
	 * (`<!---->`): the blank line was in the file, and its presence is what
	 * separated two paragraphs. */
	while(*body == ' ' || *body == '\t') {
		body++;
	}
	blen = strlen(body);
	while(blen && (body[blen - 1] == ' ' || body[blen - 1] == '\t' ||
		       body[blen - 1] == '\r')) {
		blen--;
	}
	/* A LINE BOUNDARY FOR EVERY LINE, empty or not. The first cut made the
	 * separator depend on there being text to separate, so an empty prose line
	 * — a paragraph break — vanished silently. The probe caught it
	 * (`prose-line-count`), and that is precisely the quiet loss this stage
	 * exists to prevent. */
	first = (*blob == NULL);
	used = first ? 0 : strlen(*blob);
	add = blen ? blen + 2 : 0;
	fresh = realloc(*blob, used + (first ? 0 : 1) + add + 1);
	if(!fresh) {
		return CONFIG_ERR_NOMEM;
	}
	*blob = fresh;
	if(!first) {
		fresh[used++] = '\n';
	}
	if(add) {
		fresh[used++] = ' ';
		memcpy(fresh + used, body, blen);
		used += blen;
		fresh[used++] = ' ';
	}
	fresh[used] = '\0';
	return CONFIG_OK;
}

/* Emit a prose blob as comment items of `container`: one item per line, in
 * place, which is what the core writes back verbatim. */
static config_err_t prose_emit(plist_value_t *container, const char *blob)
{
	const char *p = blob;

	if(!blob) {
		return CONFIG_OK;
	}
	for(;;) {
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		char *line = malloc(n + 1);

		if(!line) {
			return CONFIG_ERR_NOMEM;
		}
		memcpy(line, p, n);
		line[n] = '\0';
		if(plist_comment_append(container, line)) {
			free(line);
			return CONFIG_ERR_NOMEM;
		}
		free(line);
		if(!nl) {
			break;
		}
		p = nl + 1;
	}
	return CONFIG_OK;
}

/* ---- detecting the spelling ------------------------------------------ */

bool config_text_is_plist(const char *text, size_t len)
{
	size_t i = 0;

	if(len >= 3 && (unsigned char)text[0] == 0xEF &&
	   (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
		i = 3;		/* a UTF-8 BOM is not content */
	}
	while(i < len && (text[i] == ' ' || text[i] == '\t' ||
			  text[i] == '\r' || text[i] == '\n')) {
		i++;
	}
	/* The legacy grammar can never begin a file with '<': a line must have a
	 * key and an '=' before any value, so this test cannot mistake one for
	 * the other. */
	return i < len && text[i] == '<';
}

/* ---- the reader: a plist tree -> the entry model --------------------- */

static config_err_t plist_conf_fail(char *err, size_t errsz, const char *fmt, ...)
{
	if(err && errsz) {
		va_list ap;

		va_start(ap, fmt);
		vsnprintf(err, errsz, fmt, ap);
		va_end(ap);
	}
	return CONFIG_ERR_PARSE;
}

static int is_comment_item(const plist_value_t *node)
{
	return node == NULL || plist_type_of(node) == PLIST_COMMENT;
}

/* One plist node -> one config value. A nested dictionary becomes a RECORD
 * (the analogue of a `{ … }` value inside an array or a record), a nested
 * array an ARRAY. */
static config_err_t plist_to_conf(const plist_value_t *node, config_value_t *out,
				  char *err, size_t errsz, int depth)
{
	config_err_t e;

	if(depth > PLIST_CONF_MAX_DEPTH) {
		return plist_conf_fail(err, errsz,
				       "plist: nested more than %d deep",
				       PLIST_CONF_MAX_DEPTH);
	}
	memset(out, 0, sizeof(*out));
	switch(plist_type_of(node)) {
	case PLIST_STRING:
		out->type = CONFIG_TYPE_STRING;
		out->v.string = strdup(node->u.string ? node->u.string : "");
		return out->v.string ? CONFIG_OK : CONFIG_ERR_NOMEM;
	case PLIST_INTEGER:
		out->type = CONFIG_TYPE_INT;
		out->v.integer = node->u.integer;
		return CONFIG_OK;
	case PLIST_REAL:
		out->type = CONFIG_TYPE_FLOAT;
		out->v.floating = node->u.real;
		return CONFIG_OK;
	case PLIST_BOOLEAN:
		out->type = CONFIG_TYPE_BOOL;
		out->v.boolean = node->u.boolean ? true : false;
		return CONFIG_OK;
	case PLIST_ARRAY: {
		size_t i, n = 0;

		for(i = 0; i < node->u.array.count; i++) {
			if(!is_comment_item(node->u.array.items[i])) {
				n++;
			}
		}
		out->type = CONFIG_TYPE_ARRAY;
		if(n) {
			out->v.array.items = calloc(n, sizeof(config_value_t));
			if(!out->v.array.items) {
				return CONFIG_ERR_NOMEM;
			}
		}
		for(i = 0; i < node->u.array.count; i++) {
			if(is_comment_item(node->u.array.items[i])) {
				/* A comment inside an ARRAY has no home in the
				 * config model: an array has no prose, only
				 * elements. Dropped, and said so in the header. */
				continue;
			}
			e = plist_to_conf(node->u.array.items[i],
					  &out->v.array.items[out->v.array.count],
					  err, errsz, depth + 1);
			if(e) {
				config_value_free(out);
				return e;
			}
			out->v.array.count++;
		}
		return CONFIG_OK;
	}
	case PLIST_DICTIONARY: {
		size_t i, n = 0;

		for(i = 0; i < node->u.dictionary.count; i++) {
			if(node->u.dictionary.keys[i] != NULL) {
				n++;
			}
		}
		out->type = CONFIG_TYPE_RECORD;
		if(n) {
			out->v.record.fields = calloc(n,
						      sizeof(config_record_field_t));
			if(!out->v.record.fields) {
				return CONFIG_ERR_NOMEM;
			}
		}
		for(i = 0; i < node->u.dictionary.count; i++) {
			config_record_field_t *f;

			if(node->u.dictionary.keys[i] == NULL) {
				continue;	/* a comment slot: not a field */
			}
			f = &out->v.record.fields[out->v.record.count];
			f->name = strdup(node->u.dictionary.keys[i]);
			if(!f->name) {
				config_value_free(out);
				return CONFIG_ERR_NOMEM;
			}
			out->v.record.count++;
			e = plist_to_conf(node->u.dictionary.values[i], &f->value,
					  err, errsz, depth + 1);
			if(e) {
				/* f->value is uninitialized on failure: record
				 * only what succeeded */
				out->v.record.count--;
				free(f->name);
				f->name = NULL;
				config_value_free(out);
				return e;
			}
		}
		return CONFIG_OK;
	}
	default:
		/* PLIST_DATE, PLIST_DATA, PLIST_NONE, PLIST_COMMENT */
		return plist_conf_fail(err, errsz,
				       "plist: a .conf has no such type — the config model "
				       "has strings, numbers, booleans, arrays and records");
	}
}

static struct entry *find_entry(struct entry *list, const char *key)
{
	for(; list; list = list->next) {
		if(!strcmp(list->key, key)) {
			return list;
		}
	}
	return NULL;
}

static void conf_entries_free(struct entry *list)
{
	while(list) {
		struct entry *n = list->next;

		free(list->key);
		free(list->comment);
		config_value_free(&list->val);
		free(list);
		list = n;
	}
}

/* Append an entry, taking ownership of `key`, `val` and the pending prose
 * (which is cleared, so a comment belongs to exactly one entry). */
static config_err_t entry_append(struct entry **head, struct entry **tail,
				 char *key, config_value_t *val, char **pending)
{
	struct entry *en = malloc(sizeof(*en));

	if(!en) {
		return CONFIG_ERR_NOMEM;
	}
	en->key = key;
	en->comment = *pending;
	en->val = *val;
	en->next = NULL;
	*pending = NULL;
	if(*tail) {
		(*tail)->next = en;
	} else {
		*head = en;
	}
	*tail = en;
	return CONFIG_OK;
}

static config_err_t block_add(char ***blocks, int *nblocks, const char *name)
{
	char **nb;
	char *dup;

	if(!blocks || !nblocks) {
		return CONFIG_OK;	/* the caller does not want them */
	}
	nb = realloc(*blocks, (size_t)(*nblocks + 1) * sizeof(char *));
	if(!nb) {
		return CONFIG_ERR_NOMEM;
	}
	*blocks = nb;
	dup = strdup(name);
	if(!dup) {
		return CONFIG_ERR_NOMEM;
	}
	nb[*nblocks] = dup;
	(*nblocks)++;
	return CONFIG_OK;
}

static void blocks_free_local(char **blocks, int n)
{
	int i;

	for(i = 0; i < n; i++) {
		free(blocks[i]);
	}
	free(blocks);
}

/*
 * A dictionary inside the tree -> entries, recursively. `top` is true only for
 * the root's immediate children, whose names become the writer's explicit-block
 * list (deeper dictionaries are implied by the dotted keys, exactly as the
 * legacy writer treats them).
 */
static config_err_t dict_to_entries(const plist_value_t *dict, const char *prefix,
				    struct entry **head, struct entry **tail,
				    char **pending, char ***blocks, int *nblocks,
				    int top, char *err, size_t errsz, int depth)
{
	size_t i, plen = strlen(prefix);

	if(depth > PLIST_CONF_MAX_DEPTH) {
		return plist_conf_fail(err, errsz,
				       "plist: nested more than %d deep",
				       PLIST_CONF_MAX_DEPTH);
	}
	for(i = 0; i < dict->u.dictionary.count; i++) {
		const char *key = dict->u.dictionary.keys[i];
		const plist_value_t *node = dict->u.dictionary.values[i];
		char full[CONF_MAX_KEY + 1];
		config_value_t v;
		config_err_t e;

		if(key == NULL) {
			/* A COMMENT SLOT: the prose of the entry that follows.
			 * This is the piece that makes a rewrite lossless. */
			const char *body = (node != NULL &&
					    node->type == PLIST_COMMENT &&
					    node->u.string != NULL)
				? node->u.string : "";

			e = config_comment_append(pending, body);
			if(e) {
				return e;
			}
			continue;
		}
		if(plen + 1 + strlen(key) > CONF_MAX_KEY) {
			return plist_conf_fail(err, errsz,
					       "plist: key too long: %s", key);
		}
		if(plen) {
			snprintf(full, sizeof(full), "%s.%s", prefix, key);
		} else {
			snprintf(full, sizeof(full), "%s", key);
		}
		if(!config_valid_key(full)) {
			return plist_conf_fail(err, errsz,
					       "plist: \"%s\" is not a valid config key",
					       full);
		}
		if(plist_type_of(node) == PLIST_DICTIONARY) {
			/* the plist spelling of `key = { … }` */
			if(top && !strchr(key, '.')) {
				e = block_add(blocks, nblocks, full);
				if(e) {
					return e;
				}
			}
			e = dict_to_entries(node, full, head, tail, pending,
					    blocks, nblocks, 0, err, errsz,
					    depth + 1);
			if(e) {
				return e;
			}
			continue;
		}
		e = plist_to_conf(node, &v, err, errsz, depth);
		if(e) {
			return e;
		}
		e = entry_append(head, tail, strdup(full), &v, pending);
		if(e) {
			config_value_free(&v);
			return e;
		}
	}
	return CONFIG_OK;
}

config_err_t config_plist_parse(const char *text, size_t len, struct entry **list,
				char ***blocks, int *nblocks, char **trailer,
				char *err, size_t errsz)
{
	plist_value_t *root;
	struct entry *head = NULL, *tail = NULL;
	char *pending = NULL;
	char message[256];
	config_err_t e;

	*list = NULL;
	if(blocks) {
		*blocks = NULL;
	}
	if(nblocks) {
		*nblocks = 0;
	}
	if(trailer) {
		*trailer = NULL;
	}
	if(err && errsz) {
		err[0] = '\0';
	}
	root = plist_parse(text, len, message, sizeof(message));
	if(!root) {
		return plist_conf_fail(err, errsz, "%s",
				       message[0] ? message : "plist: malformed property list");
	}
	if(plist_type_of(root) != PLIST_DICTIONARY) {
		plist_free(root);
		return plist_conf_fail(err, errsz,
				       "plist: a .conf must have a <dict> at the top");
	}
	e = dict_to_entries(root, "", &head, &tail, &pending, blocks, nblocks, 1,
			    err, errsz, 0);
	if(!e) {
		/* Prose after the last entry is the file's closing words — kept,
		 * for the same reason the leading ones are. */
		if(pending) {
			if(trailer) {
				*trailer = pending;
				pending = NULL;
			}
		}
		*list = head;
		head = NULL;
	} else if(blocks && *blocks) {
		/* hand back nothing half-built on failure */
		blocks_free_local(*blocks, *nblocks);
		*blocks = NULL;
		*nblocks = 0;
	}
	free(pending);
	conf_entries_free(head);
	plist_free(root);
	return e;
}

/* ---- the writer: the entry model -> a plist tree --------------------- */

static plist_value_t *value_to_plist(const config_value_t *v, int depth);

static plist_value_t *array_to_plist(const config_value_t *v, int depth)
{
	plist_value_t *array = plist_new_array();
	size_t i;

	if(!array) {
		return NULL;
	}
	for(i = 0; i < v->v.array.count; i++) {
		plist_value_t *item = value_to_plist(&v->v.array.items[i], depth + 1);

		if(!item || plist_array_append(array, item)) {
			plist_free(item);
			plist_free(array);
			return NULL;
		}
	}
	return array;
}

static plist_value_t *record_to_plist(const config_value_t *v, int depth)
{
	plist_value_t *dict = plist_new_dictionary();
	size_t i;

	if(!dict) {
		return NULL;
	}
	for(i = 0; i < v->v.record.count; i++) {
		const config_record_field_t *f = &v->v.record.fields[i];
		plist_value_t *item = value_to_plist(&f->value, depth + 1);

		if(!item) {
			plist_free(dict);
			return NULL;
		}
		if(plist_dictionary_set(dict, f->name, item)) {
			plist_free(item);
			plist_free(dict);
			return NULL;
		}
	}
	return dict;
}

/* ---- the prose that stands AFTER the last entry ---------------------- */

static char *slurp_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *text;
	long size;

	if(!f) {
		return NULL;
	}
	if(fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 ||
	   fseek(f, 0, SEEK_SET)) {
		fclose(f);
		return NULL;
	}
	text = malloc((size_t)size + 1);
	if(!text) {
		fclose(f);
		return NULL;
	}
	if(size && fread(text, 1, (size_t)size, f) != (size_t)size) {
		free(text);
		fclose(f);
		return NULL;
	}
	fclose(f);
	text[size] = '\0';
	return text;
}

/*
 * The prose that FOLLOWED the last entry of `path`, or -1 when there is none.
 *
 * It is read from the FILE being replaced rather than carried around in the
 * entry list: a set or an unset cannot change words that stand after the last
 * entry, and the read paths — which never write — have no use for it.
 *
 * A plist is parsed with the core, so the trailing comments of its root
 * dictionary come back exactly where they stood. The LEGACY spelling's closing
 * prose is taken as the trailing run of '#' lines: the legacy parser (in
 * libconfig.c) can report it as well, but it is static there, and that grammar
 * is deleted outright in P3f — so this stays a scan of lines rather than
 * plumbing a parameter through every read path for a format with a scheduled
 * end.
 */
int config_trailer_prose(const char *path, char **out)
{
	char *text;
	const char *line, *tail_from = NULL, *p;
	config_err_t e;
	char **blocks = NULL;
	int nblocks = 0;
	char message[256];
	struct entry *list = NULL;

	if(out) {
		*out = NULL;
	}
	text = slurp_file(path);
	if(!text) {
		return -1;		/* no file, nothing to carry */
	}
	if(config_text_is_plist(text, strlen(text))) {
		e = config_plist_parse(text, strlen(text), &list, &blocks,
				       &nblocks, out, message, sizeof(message));
		conf_entries_free(list);
		blocks_free_local(blocks, nblocks);
		free(text);
		return (e == CONFIG_OK && out && *out) ? 0 : -1;
	}
	/* THE LEGACY SPELLING: its closing prose is the trailing RUN of '#' lines —
	 * the run that reaches the end of the file, blank lines and all, ended by
	 * the last real line. Taking only the run's LAST line loses the paragraph
	 * above it, which is exactly what the probe caught. */
	for(line = text;;) {
		const char *eol = strchr(line, '\n');
		const char *q = line;
		int blank = 1;

		while(q < (eol ? eol : line + strlen(line)) &&
		      (*q == ' ' || *q == '\t' || *q == '\r')) {
			q++;
		}
		if(q < (eol ? eol : line + strlen(line))) {
			blank = 0;
		}
		if(!blank) {
			if(*q != '#') {
				tail_from = NULL;	/* a real line ends a run */
			} else if(tail_from == NULL) {
				tail_from = line;	/* the run starts here */
			}
		}
		if(!eol) {
			break;
		}
		line = eol + 1;
	}
	if(tail_from) {
		for(p = tail_from; *p;) {
			const char *eol = strchr(p, '\n');
			size_t n = eol ? (size_t)(eol - p) : strlen(p);
			const char *s = p;
			size_t sn = n;
			char *one;

			while(sn && (*s == ' ' || *s == '\t')) {
				s++;
				sn--;
			}
			if(sn && *s == '#') {
				one = malloc(sn + 1);
				if(!one) {
					free(text);
					return -1;
				}
				memcpy(one, s, sn);
				one[sn] = '\0';
				/* one + 1: the prose is what followed the '#',
				 * exactly as the legacy parser hands it over */
				if(config_comment_append(out, one + 1) != CONFIG_OK) {
					free(one);
					free(text);
					return -1;
				}
				free(one);
			}
			if(!eol) {
				break;
			}
			p = eol + 1;
		}
	}
	free(text);
	return (out && *out) ? 0 : -1;
}

static plist_value_t *value_to_plist(const config_value_t *v, int depth)
{
	if(depth > PLIST_CONF_MAX_DEPTH) {
		return NULL;
	}
	switch(v->type) {
	case CONFIG_TYPE_STRING:
		return plist_new_string(v->v.string ? v->v.string : "");
	case CONFIG_TYPE_BOOL:
		return plist_new_boolean(v->v.boolean ? 1 : 0);
	case CONFIG_TYPE_INT:
		return plist_new_integer(v->v.integer);
	case CONFIG_TYPE_FLOAT:
		return plist_new_real(v->v.floating);
	case CONFIG_TYPE_ARRAY:
		return array_to_plist(v, depth);
	case CONFIG_TYPE_RECORD:
		return record_to_plist(v, depth);
	default:
		return NULL;
	}
}

/* Set a child of `parent`, taking ownership of the value, or fail. */
static config_err_t set_child(plist_value_t *parent, const char *name,
			      plist_value_t *value)
{
	if(!value) {
		return CONFIG_ERR_NOMEM;
	}
	if(plist_dictionary_set(parent, name, value)) {
		plist_free(value);
		return CONFIG_ERR_NOMEM;
	}
	return CONFIG_OK;
}

/*
 * Emit the children of `prefix` into `parent`, in first-seen order: a leaf
 * child as its value, a container child as a nested dictionary. This is the
 * plist spelling of the writer's nested-record emission, and it is the same
 * arithmetic the legacy `emit_children` uses — an exact entry means a leaf,
 * anything else means a container.
 *
 * An entry's PROSE is emitted immediately before that entry's own value, at
 * whatever depth the entry lands. A container key therefore carries no comment
 * of its own: its first child's prose precedes that child, just inside.
 */
static config_err_t build_level(plist_value_t *parent, struct entry *list,
				const char *prefix, int depth)
{
	size_t plen = prefix ? strlen(prefix) : 0;
	struct entry *en;
	size_t i, n = 0, cap = 0;
	char **kids = NULL;
	config_err_t e = CONFIG_OK;

	if(depth > PLIST_CONF_MAX_DEPTH) {
		return CONFIG_ERR_PARSE;
	}
	for(en = list; en && !e; en = en->next) {
		const char *seg;
		size_t slen;

		if(plen) {
			if(strncmp(en->key, prefix, plen) || en->key[plen] != '.') {
				continue;
			}
			seg = en->key + plen + 1;
		} else {
			seg = en->key;
		}
		slen = strcspn(seg, ".");
		for(i = 0; i < n; i++) {
			if(!strncmp(kids[i], seg, slen) && !kids[i][slen]) {
				break;
			}
		}
		if(i < n) {
			continue;	/* already emitted with its group */
		}
		if(n == cap) {
			char **nk;

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
		char full[CONF_MAX_KEY + 1];
		struct entry *exact;

		if(plen) {
			snprintf(full, sizeof(full), "%s.%s", prefix, kids[i]);
		} else {
			snprintf(full, sizeof(full), "%s", kids[i]);
		}
		exact = find_entry(list, full);
		if(exact) {
			e = prose_emit(parent, exact->comment);
			if(!e) {
				e = set_child(parent, kids[i],
					      value_to_plist(&exact->val, depth));
			}
		} else {
			plist_value_t *child = plist_new_dictionary();

			if(!child) {
				e = CONFIG_ERR_NOMEM;
			} else {
				e = build_level(child, list, full, depth + 1);
				if(!e) {
					e = set_child(parent, kids[i], child);
				} else {
					plist_free(child);
				}
			}
		}
	}
	for(i = 0; i < n; i++) {
		free(kids[i]);
	}
	free(kids);
	return e;
}

static int block_listed(char **blocks, int nblocks, const char *name)
{
	int i;

	for(i = 0; i < nblocks; i++) {
		if(!strcmp(blocks[i], name)) {
			return 1;
		}
	}
	return 0;
}

/*
 * The root. With no explicit blocks the domain is PURE FLAT and every entry
 * keeps its dotted key, exactly as it was written. With blocks, the top-level
 * names keep their nested spelling: a top-level leaf, then an explicit block,
 * then a plain dotted group whose keys stay flat — the same three cases, in the
 * same order, as the legacy `emit_grouped`.
 */
static config_err_t build_root(plist_value_t *root, struct entry *list,
			       char **blocks, int nblocks)
{
	struct entry *en;
	size_t i, n = 0, cap = 0;
	char **tops = NULL;
	config_err_t e = CONFIG_OK;

	if(!blocks || nblocks <= 0) {
		for(en = list; en && !e; en = en->next) {
			e = prose_emit(root, en->comment);
			if(!e) {
				e = set_child(root, en->key,
					      value_to_plist(&en->val, 0));
			}
		}
		return e;
	}
	for(en = list; en && !e; en = en->next) {
		size_t slen = strcspn(en->key, ".");

		for(i = 0; i < n; i++) {
			if(!strncmp(tops[i], en->key, slen) && !tops[i][slen]) {
				break;
			}
		}
		if(i < n) {
			continue;
		}
		if(n == cap) {
			char **nt;

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
		struct entry *leaf = find_entry(list, tops[i]);

		if(leaf) {
			e = prose_emit(root, leaf->comment);
			if(!e) {
				e = set_child(root, tops[i],
					      value_to_plist(&leaf->val, 0));
			}
		} else if(block_listed(blocks, nblocks, tops[i])) {
			plist_value_t *child = plist_new_dictionary();

			if(!child) {
				e = CONFIG_ERR_NOMEM;
			} else {
				e = build_level(child, list, tops[i], 1);
				if(!e) {
					e = set_child(root, tops[i], child);
				} else {
					plist_free(child);
				}
			}
		} else {
			/* a plain dotted group under this name: keep the flat
			 * "full.key" spelling, unchanged */
			size_t tl = strlen(tops[i]);

			for(en = list; en && !e; en = en->next) {
				if(strncmp(en->key, tops[i], tl) ||
				   en->key[tl] != '.') {
					continue;
				}
				e = prose_emit(root, en->comment);
				if(!e) {
					e = set_child(root, en->key,
						      value_to_plist(&en->val, 0));
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

config_err_t config_plist_write(int fd, struct entry *list, char **blocks,
				int nblocks, const char *trailer)
{
	plist_value_t *root = plist_new_dictionary();
	char *text;
	size_t len = 0;
	config_err_t e;

	if(!root) {
		return CONFIG_ERR_NOMEM;
	}
	e = build_root(root, list, blocks, nblocks);
	if(!e) {
		e = prose_emit(root, trailer);
	}
	if(!e) {
		text = plist_serialize(root, &len);
		if(!text) {
			e = CONFIG_ERR_NOMEM;
		} else {
			if(len && write(fd, text, len) != (ssize_t)len) {
				e = CONFIG_ERR_IO;
			}
			free(text);
		}
	}
	plist_free(root);
	return e;
}
