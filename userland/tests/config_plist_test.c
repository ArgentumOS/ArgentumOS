/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * config_plist_test — P3b's acceptance: a .conf reads the SAME in both
 * spellings (docs/design/plist-config-plan.md).
 *
 * The stage's claim is narrow, so the test is narrow: "every existing .conf
 * parses identically through both readers". This probe takes a real .conf, reads
 * every key through the PUBLIC API, forces the file to be REWRITTEN — the writer
 * emits plists now, so the rewrite IS the conversion — and reads every key
 * again. Then it asserts:
 *
 *   same keys, in the same order  |  same type and value for each  (both ways)
 *   the rewrite IS a plist        |  (the writer's promise)
 *   the prose survived            |  one XML comment per legacy '#' line, and
 *                                 |  the first '#' line's text still present
 *   the spelling is STABLE        |  rewriting the converted file changes not
 *                                 |  one byte: the reader is faithful
 *
 * It uses only userland/libconfig.h, and it works on a COPY in a scratch tree
 * under /tmp, because a test must not touch the system's own files. Format
 * detection, reader and writer are therefore all the real ones — not a private
 * path that could pass while the library is broken.
 *
 * Usage: config_plist_test [file.conf ...]
 *   no arguments: every .conf in <root>/System/Configuration (where the guest
 *   keeps them) is converted in the scratch tree and checked.
 *
 * Prints CONFIG-PLIST-TEST <name> ok|FAIL lines and a RESULT tally; exits
 * non-zero if any check failed.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libconfig.h>

static int ok_count = 0;
static int fail_count = 0;

static void check(const char *name, int condition, const char *detail)
{
	if(condition) {
		ok_count++;
		printf("CONFIG-PLIST-TEST %s ok\n", name);
	} else {
		fail_count++;
		printf("CONFIG-PLIST-TEST %s FAIL %s\n", name,
		       detail != NULL ? detail : "");
	}
}

/* ---- small file helpers ---------------------------------------------- */

static char *slurp(const char *path)
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

static int write_all(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	size_t n = strlen(text);
	int rc;

	if(!f) {
		return -1;
	}
	if(n && fwrite(text, 1, n, f) != n) {
		fclose(f);
		return -1;
	}
	rc = fclose(f);
	return rc;
}

static int ensure_dir(const char *path)
{
	char buf[PATH_MAX];
	size_t i;

	if(strlen(path) >= sizeof(buf)) {
		return -1;
	}
	strcpy(buf, path);
	for(i = 1; buf[i]; i++) {
		if(buf[i] == '/') {
			buf[i] = '\0';
			mkdir(buf, 0755);
			buf[i] = '/';
		}
	}
	return (mkdir(buf, 0755) && errno != EEXIST) ? -1 : 0;
}

/* ---- the property under test ----------------------------------------- */

static long count_prose_lines(const char *text)
{
	long n = 0;
	const char *p = text;

	for(;;) {
		const char *eol = strchr(p, '\n');
		const char *q = p;

		while(*q == ' ' || *q == '\t') {
			q++;
		}
		if(*q == '#') {
			n++;
		}
		if(!eol) {
			break;
		}
		p = eol + 1;
	}
	return n;
}

static long count_occurrences(const char *hay, const char *needle)
{
	long n = 0;
	size_t nl = strlen(needle);
	const char *p = hay;

	while((p = strstr(p, needle)) != NULL) {
		n++;
		p += nl;
	}
	return n;
}

/* The prose of the first comment line, trimmed. */
static int first_prose(const char *text, char *out, size_t outsz)
{
	const char *p = text;

	for(;;) {
		const char *eol = strchr(p, '\n');
		const char *q = p;
		size_t n;

		while(*q == ' ' || *q == '\t') {
			q++;
		}
		if(*q == '#') {
			q++;
			while(*q == ' ' || *q == '\t') {
				q++;
			}
			n = eol ? (size_t)(eol - q) : strlen(q);
			while(n && (q[n - 1] == ' ' || q[n - 1] == '\t' ||
				    q[n - 1] == '\r')) {
				n--;
			}
			if(n && n + 1 < outsz) {
				memcpy(out, q, n);
				out[n] = '\0';
				return 0;
			}
		}
		if(!eol) {
			break;
		}
		p = eol + 1;
	}
	return -1;
}

static int value_equal(const config_value_t *a, const config_value_t *b)
{
	size_t i;

	if(a->type != b->type) {
		return 0;
	}
	switch(a->type) {
	case CONFIG_TYPE_STRING:
		return a->v.string && b->v.string &&
		       !strcmp(a->v.string, b->v.string);
	case CONFIG_TYPE_BOOL:
		return a->v.boolean == b->v.boolean;
	case CONFIG_TYPE_INT:
		return a->v.integer == b->v.integer;
	case CONFIG_TYPE_FLOAT:
		return a->v.floating == b->v.floating;
	case CONFIG_TYPE_ARRAY:
		if(a->v.array.count != b->v.array.count) {
			return 0;
		}
		for(i = 0; i < a->v.array.count; i++) {
			if(!value_equal(&a->v.array.items[i],
					&b->v.array.items[i])) {
				return 0;
			}
		}
		return 1;
	case CONFIG_TYPE_RECORD:
		if(a->v.record.count != b->v.record.count) {
			return 0;
		}
		for(i = 0; i < a->v.record.count; i++) {
			if(strcmp(a->v.record.fields[i].name,
				  b->v.record.fields[i].name)) {
				return 0;
			}
			if(!value_equal(&a->v.record.fields[i].value,
					&b->v.record.fields[i].value)) {
				return 0;
			}
		}
		return 1;
	}
	return 0;
}

/* Which key to rewrite: a scalar if there is one (the common case), otherwise
 * an array. -1 when the domain holds nothing settable. */
static long pick_settable(const config_value_t *vals, size_t n)
{
	size_t i;

	for(i = 0; i < n; i++) {
		if(vals[i].type == CONFIG_TYPE_STRING ||
		   vals[i].type == CONFIG_TYPE_BOOL ||
		   vals[i].type == CONFIG_TYPE_INT ||
		   vals[i].type == CONFIG_TYPE_FLOAT) {
			return (long)i;
		}
	}
	for(i = 0; i < n; i++) {
		if(vals[i].type == CONFIG_TYPE_ARRAY) {
			return (long)i;
		}
	}
	return -1;
}

static config_err_t set_like(const char *domain, const char *key,
			     const config_value_t *v)
{
	switch(v->type) {
	case CONFIG_TYPE_STRING:
		return config_set_string(CONFIG_SCOPE_SYSTEM, domain, key,
					 v->v.string ? v->v.string : "");
	case CONFIG_TYPE_BOOL:
		return config_set_bool(CONFIG_SCOPE_SYSTEM, domain, key,
				       v->v.boolean);
	case CONFIG_TYPE_INT:
		return config_set_int(CONFIG_SCOPE_SYSTEM, domain, key,
				      v->v.integer);
	case CONFIG_TYPE_FLOAT:
		return config_set_float(CONFIG_SCOPE_SYSTEM, domain, key,
					v->v.floating);
	case CONFIG_TYPE_ARRAY:
		return config_set_array(CONFIG_SCOPE_SYSTEM, domain, key,
					v->v.array.items, v->v.array.count);
	default:
		return CONFIG_ERR_TYPE;
	}
}

static void check_domain(const char *src, const char *domain, const char *root)
{
	char name[320];
	char dir[PATH_MAX];
	char dst[PATH_MAX];
	char prose[256];
	char *original;
	char *converted = NULL;
	char *twice = NULL;
	char **keys = NULL;
	char **keys_again = NULL;
	config_value_t *vals = NULL;
	config_value_t *vals_again = NULL;
	size_t n = 0, n_again = 0, i;
	long pick;
	config_err_t e;
	int same_keys = 1, same_values = 1;

	original = slurp(src);
	if(!original) {
		check(domain, 0, "cannot read the source file");
		return;
	}
	/* Explicit bounds, so gcc's truncation analysis has nothing to warn about:
	 * a scratch root under /tmp and a domain taken from a FILENAME (<= 255
	 * bytes) cannot reach them, and a path that did would be refused loudly
	 * rather than silently cut. */
	snprintf(dir, sizeof(dir), "%.3000s/System/Configuration", root);
	snprintf(dst, sizeof(dst), "%.3600s/%.200s.conf", dir, domain);
	if(ensure_dir(dir) || write_all(dst, original)) {
		check(domain, 0, "cannot stage the file into the scratch tree");
		free(original);
		return;
	}
	/* NOT EVERY .conf IS A LIBCONFIG DOMAIN. fonts.conf is fontconfig's own XML
	 * (libfontconfig reads it), and an XML document with no <plist> in it is
	 * somebody else's file: reading it is not this library's business, so it is
	 * reported and skipped rather than failed. */
	{
		const char *q = original;

		while(*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
			q++;
		}
		if(*q == '<' && strstr(original, "<plist") == NULL) {
			printf("CONFIG-PLIST-TEST %s skipped (XML, but not a property list: not a libconfig domain)\n",
			       domain);
			free(original);
			return;
		}
	}

	/* 1. every key, in the spelling the file is IN */
	e = config_get_all(domain, "", &keys, &vals, &n);
	if(e || !n) {
		snprintf(name, sizeof(name), "%s:readable", domain);
		check(name, 0, e ? config_strerror(e) : "no keys found");
		goto out;
	}
	pick = pick_settable(vals, n);
	snprintf(name, sizeof(name), "%s:has-a-settable-key", domain);
	if(pick < 0) {
		check(name, 0, "nothing settable in this domain");
		goto out;
	}
	check(name, 1, NULL);

	/* 2. THE REWRITE IS THE CONVERSION: a `config set` of a key to its own
	 * value, which the writer carries out as an XML plist. */
	e = set_like(domain, keys[pick], &vals[pick]);
	snprintf(name, sizeof(name), "%s:rewrite-succeeds", domain);
	if(e) {
		check(name, 0, config_strerror(e));
		goto out;
	}
	check(name, 1, NULL);

	converted = slurp(dst);
	snprintf(name, sizeof(name), "%s:writer-emits-a-plist", domain);
	check(name, converted != NULL &&
	      strstr(converted, "<plist version=\"1.0\">") != NULL,
	      "the rewritten file is not an XML plist");

	/* 3. THE BOTH-WAYS COMPARISON */
	e = config_get_all(domain, "", &keys_again, &vals_again, &n_again);
	snprintf(name, sizeof(name), "%s:rereads-through-the-plist", domain);
	if(e) {
		check(name, 0, config_strerror(e));
		goto out;
	}
	check(name, 1, NULL);
	for(i = 0; i < n && i < n_again; i++) {
		if(strcmp(keys[i], keys_again[i])) {
			same_keys = 0;
		}
		if(!value_equal(&vals[i], &vals_again[i])) {
			same_values = 0;
		}
	}
	snprintf(name, sizeof(name), "%s:same-keys-in-the-same-order", domain);
	check(name, same_keys && n == n_again,
	      "the plist spelling changed the key set or its order");
	snprintf(name, sizeof(name), "%s:same-types-and-values", domain);
	check(name, same_values && n == n_again,
	      "the plist spelling changed a value");

	/* 4. THE PROSE, in WHICHEVER SPELLING the file arrived in: one legacy '#'
	 * line, or one `<!-- … -->` item, per comment the rewrite emits — and the
	 * first comment's text still there. After P3c the shipped files ARE plists,
	 * so this has to hold for a plist SOURCE too: that is the evidence the
	 * conversion kept the prose rather than merely the values. */
	if(converted) {
		int plist_source = strstr(original, "<plist") != NULL;
		long was = plist_source ? count_occurrences(original, "<!--")
					: count_prose_lines(original);
		long now = count_occurrences(converted, "<!--");
		int have_prose = 0;

		if(plist_source) {
			const char *open = strstr(original, "<!--");
			const char *close = open ? strstr(open + 4, "-->") : NULL;

			if(close) {
				const char *s = open + 4;
				size_t sn = (size_t)(close - s);

				while(sn && (*s == ' ' || *s == '\t')) {
					s++;
					sn--;
				}
				while(sn && (s[sn - 1] == ' ' || s[sn - 1] == '\t')) {
					sn--;
				}
				if(sn && sn + 1 < sizeof(prose)) {
					memcpy(prose, s, sn);
					prose[sn] = '\0';
					have_prose = 1;
				}
			}
		} else {
			have_prose = first_prose(original, prose,
						 sizeof(prose)) == 0;
		}
		snprintf(name, sizeof(name), "%s:prose-line-count", domain);
		check(name, was == now,
		      "a comment line was dropped or invented");
		if(have_prose) {
			snprintf(name, sizeof(name),
				 "%s:first-comment-text-present", domain);
			check(name, strstr(converted, prose) != NULL, prose);
		}
	}

	/* 5. STABILITY: rewriting the converted file changes nothing at all —
	 * which holds only if the plist READER produced the very entries the
	 * writer wrote. */
	e = set_like(domain, keys[pick], &vals[pick]);
	if(!e) {
		twice = slurp(dst);
	}
	snprintf(name, sizeof(name), "%s:stable-across-a-rewrite", domain);
	check(name, twice && converted && !strcmp(twice, converted),
	      "the plist spelling is not idempotent");

out:
	config_free_keys(keys, vals, n);
	config_free_keys(keys_again, vals_again, n_again);
	free(converted);
	free(twice);
	free(original);
}

/* "system.mounts.conf" -> "system.mounts"; -1 when it is not a .conf name. */
static int conf_domain(const char *file, char *domain, size_t domain_size)
{
	const char *base = strrchr(file, '/');
	size_t n;

	base = base ? base + 1 : file;
	n = strlen(base);
	if(n < 6 || strcmp(base + n - 5, ".conf")) {
		return -1;
	}
	snprintf(domain, domain_size, "%.*s", (int)(n - 5), base);
	return 0;
}

int main(int argc, char **argv)
{
	char tmpl[] = "/tmp/config_plistXXXXXX";
	char domain[256];
	const char *root;
	int files = 0;
	int i;

	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("CONFIG-PLIST-TEST start\n");
	root = mkdtemp(tmpl);
	if(!root) {
		printf("CONFIG-PLIST-TEST FAIL cannot create a scratch root\n");
		return 1;
	}
	/* Every scope resolves under this root, so the copies are what gets read
	 * and written: the system's own configuration is never touched. */
	setenv("FNX_CONFIG_ROOT", root, 1);

	if(argc > 1) {
		for(i = 1; i < argc; i++) {
			if(conf_domain(argv[i], domain, sizeof(domain))) {
				continue;
			}
			if(config_is_pinned(domain)) {
				printf("CONFIG-PLIST-TEST %s ok (pinned domain: skipped)\n",
				       domain);
				ok_count++;
				continue;
			}
			check_domain(argv[i], domain, root);
			files++;
		}
	} else {
		char dir[PATH_MAX];
		struct dirent *de;
		DIR *d;

		snprintf(dir, sizeof(dir), "/System/Configuration");
		d = opendir(dir);
		if(!d) {
			printf("CONFIG-PLIST-TEST FAIL cannot list %s\n", dir);
			return 1;
		}
		while((de = readdir(d)) != NULL) {
			char path[PATH_MAX];

			if(conf_domain(de->d_name, domain, sizeof(domain))) {
				continue;
			}
			if(config_is_pinned(domain)) {
				continue;
			}
			snprintf(path, sizeof(path), "%.3800s/%.200s", dir,
				 de->d_name);
			check_domain(path, domain, root);
			files++;
		}
		closedir(d);
	}
	printf("CONFIG-PLIST-TEST RESULT files=%d ok=%d fail=%d\n", files,
	       ok_count, fail_count);
	printf("CONFIG-PLIST-TEST DONE\n");
	return fail_count == 0 ? 0 : 1;
}
