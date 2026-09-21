/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * plist.c — the XML property-list core. See include/plist.h for the contract.
 *
 * Written for a config file's real audience: a human editing it, and two readers
 * (a C one and an Objective-C one) that must agree on the result. So the parser
 * fails LOUDLY on anything it does not understand — an unknown element is an
 * error, not a silently dropped node — and the serialiser writes Apple's shape,
 * because "it looks like a plist" is what keeps plutil and friends usable on
 * these files.
 */

#include "plist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Seconds between 1970-01-01 and 2001-01-01: the reference date Apple's plists
 * and Foundation's NSDate both count from. */
#define PLIST_EPOCH_SHIFT 978307200.0

/* ---- small helpers ------------------------------------------------------- */

static void *plist_xmalloc(size_t size)
{
	return malloc(size == 0 ? 1 : size);
}

static char *plist_strdup(const char *text)
{
	size_t length = strlen(text);
	char *copy = (char *)plist_xmalloc(length + 1);

	if (copy != NULL) {
		memcpy(copy, text, length + 1);
	}
	return copy;
}

/* ---- dates: fixed arithmetic, so the core needs no libc date functions ---- */

/* Days since 1970-01-01 for a proleptic Gregorian date. */
static long long plist_days_from_civil(long long year, unsigned month, unsigned day)
{
	long long era;
	long long year_of_era, day_of_era, day_of_year;

	year -= month <= 2;
	era = (year >= 0 ? year : year - 399) / 400;
	year_of_era = year - era * 400;
	day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
	return era * 146097 + day_of_era - 719468;
}

static void plist_civil_from_days(long long days, long long *year, unsigned *month, unsigned *day)
{
	long long era, day_of_era, year_of_era, day_of_year;
	long long month_prime;

	days += 719468;
	era = (days >= 0 ? days : days - 146096) / 146097;
	day_of_era = days - era * 146097;
	year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524
		       - day_of_era / 146096) / 365;
	day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
	month_prime = (5 * day_of_year + 2) / 153;
	*day = (unsigned)(day_of_year - (153 * month_prime + 2) / 5 + 1);
	*month = (unsigned)(month_prime + (month_prime < 10 ? 3 : -9));
	*year = year_of_era + era * 400 + (*month <= 2);
}

/* ---- base64 -------------------------------------------------------------- */

static const char plist_base64_alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int plist_base64_value(int character)
{
	if (character >= 'A' && character <= 'Z') {
		return character - 'A';
	}
	if (character >= 'a' && character <= 'z') {
		return character - 'a' + 26;
	}
	if (character >= '0' && character <= '9') {
		return character - '0' + 52;
	}
	if (character == '+') {
		return 62;
	}
	if (character == '/') {
		return 63;
	}
	return -1;
}

static unsigned char *plist_base64_decode(const char *text, size_t length, size_t *out_length)
{
	unsigned char *bytes = (unsigned char *)plist_xmalloc(length / 4 * 3 + 4);
	size_t written = 0;
	unsigned int accumulator = 0;
	int bits = 0;
	size_t i;

	if (bytes == NULL) {
		return NULL;
	}
	for (i = 0; i < length; i++) {
		int value;

		if (text[i] == '=') {
			break;
		}
		value = plist_base64_value((unsigned char)text[i]);
		if (value < 0) {
			continue;	/* whitespace and line breaks are not data */
		}
		accumulator = (accumulator << 6) | (unsigned int)value;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			bytes[written++] = (unsigned char)((accumulator >> bits) & 0xFF);
		}
	}
	*out_length = written;
	return bytes;
}

/* A growable text buffer: the serialiser builds into one of these. */
static void plist_buffer_append(char **buffer, size_t *length, size_t *capacity,
				const char *text, size_t count)
{
	if (*length + count + 1 > *capacity) {
		size_t grown = (*capacity == 0) ? 256 : *capacity;
		char *fresh;

		while (*length + count + 1 > grown) {
			grown *= 2;
		}
		fresh = (char *)realloc(*buffer, grown);
		if (fresh == NULL) {
			return;
		}
		*buffer = fresh;
		*capacity = grown;
	}
	memcpy(*buffer + *length, text, count);
	*length += count;
	(*buffer)[*length] = '\0';
}

static void plist_buffer_append_base64(char **buffer, size_t *length, size_t *capacity,
				       const unsigned char *bytes, size_t count)
{
	size_t i;

	for (i = 0; i < count; i += 3) {
		size_t remaining = count - i;
		unsigned int block = (unsigned int)bytes[i] << 16;
		char out[4];

		if (remaining > 1) {
			block |= (unsigned int)bytes[i + 1] << 8;
		}
		if (remaining > 2) {
			block |= bytes[i + 2];
		}
		out[0] = plist_base64_alphabet[(block >> 18) & 0x3F];
		out[1] = plist_base64_alphabet[(block >> 12) & 0x3F];
		out[2] = (remaining > 1) ? plist_base64_alphabet[(block >> 6) & 0x3F] : '=';
		out[3] = (remaining > 2) ? plist_base64_alphabet[block & 0x3F] : '=';
		plist_buffer_append(buffer, length, capacity, out, 4);
	}
}

/* ---- construction -------------------------------------------------------- */

static plist_value_t *plist_new(plist_type_t type)
{
	plist_value_t *value = (plist_value_t *)plist_xmalloc(sizeof(plist_value_t));

	if (value != NULL) {
		memset(value, 0, sizeof(plist_value_t));
		value->type = type;
	}
	return value;
}

plist_value_t *plist_new_string(const char *utf8)
{
	plist_value_t *value = plist_new(PLIST_STRING);

	if (value != NULL) {
		value->u.string = plist_strdup(utf8 == NULL ? "" : utf8);
	}
	return value;
}

plist_value_t *plist_new_integer(long long integer)
{
	plist_value_t *value = plist_new(PLIST_INTEGER);

	if (value != NULL) {
		value->u.integer = integer;
	}
	return value;
}

plist_value_t *plist_new_real(double real)
{
	plist_value_t *value = plist_new(PLIST_REAL);

	if (value != NULL) {
		value->u.real = real;
	}
	return value;
}

plist_value_t *plist_new_boolean(int boolean)
{
	plist_value_t *value = plist_new(PLIST_BOOLEAN);

	if (value != NULL) {
		value->u.boolean = boolean ? 1 : 0;
	}
	return value;
}

plist_value_t *plist_new_date(double seconds_since_2001)
{
	plist_value_t *value = plist_new(PLIST_DATE);

	if (value != NULL) {
		value->u.date = seconds_since_2001;
	}
	return value;
}

plist_value_t *plist_new_data(const unsigned char *bytes, size_t length)
{
	plist_value_t *value = plist_new(PLIST_DATA);

	if (value != NULL) {
		value->u.data.bytes = (unsigned char *)plist_xmalloc(length == 0 ? 1 : length);
		value->u.data.length = length;
		if (value->u.data.bytes != NULL && length > 0) {
			memcpy(value->u.data.bytes, bytes, length);
		}
	}
	return value;
}

plist_value_t *plist_new_array(void)
{
	return plist_new(PLIST_ARRAY);
}

plist_value_t *plist_new_dictionary(void)
{
	return plist_new(PLIST_DICTIONARY);
}

/* A comment: the prose of a config file, kept verbatim in u.string — which is
 * what lets it be freed and written with no new machinery of its own. */
plist_value_t *plist_new_comment(const char *text)
{
	plist_value_t *value = plist_new(PLIST_COMMENT);

	if (value != NULL) {
		value->u.string = plist_strdup(text == NULL ? "" : text);
	}
	return value;
}

int plist_array_append(plist_value_t *array, plist_value_t *item)
{
	plist_value_t **fresh;

	if (array == NULL || array->type != PLIST_ARRAY) {
		return -1;
	}
	fresh = (plist_value_t **)realloc(array->u.array.items,
					  (array->u.array.count + 1) * sizeof(plist_value_t *));
	if (fresh == NULL) {
		return -1;
	}
	array->u.array.items = fresh;
	array->u.array.items[array->u.array.count++] = item;
	return 0;
}

/*
 * The append/replace core. `key` MAY BE NULL here, and that is the COMMENT
 * SLOT — the one legitimate NULL, which is why this is separate from the public
 * setter: a caller who passes a NULL key to plist_dictionary_set is a bug, and
 * gets -1, while the parser's comment path must be able to write one.
 */
static int plist_dictionary_put(plist_value_t *dictionary, const char *key,
				plist_value_t *value)
{
	size_t i;

	if (dictionary == NULL || dictionary->type != PLIST_DICTIONARY) {
		return -1;
	}
	if (key != NULL) {
		for (i = 0; i < dictionary->u.dictionary.count; i++) {
			if (dictionary->u.dictionary.keys[i] != NULL &&
			    strcmp(dictionary->u.dictionary.keys[i], key) == 0) {
				/* Replace IN PLACE: the key keeps its position, so a
				 * config file does not reshuffle because a field was
				 * set twice. */
				dictionary->u.dictionary.values[i] = value;
				return 0;
			}
		}
	}
	if (dictionary->u.dictionary.count + 1 > dictionary->u.dictionary.capacity) {
		size_t grown = (dictionary->u.dictionary.capacity == 0)
			? 8 : dictionary->u.dictionary.capacity * 2;
		char **keys = (char **)realloc(dictionary->u.dictionary.keys, grown * sizeof(char *));
		plist_value_t **values;

		if (keys == NULL) {
			return -1;
		}
		dictionary->u.dictionary.keys = keys;
		values = (plist_value_t **)realloc(dictionary->u.dictionary.values,
						   grown * sizeof(plist_value_t *));
		if (values == NULL) {
			return -1;
		}
		dictionary->u.dictionary.values = values;
		dictionary->u.dictionary.capacity = grown;
	}
	dictionary->u.dictionary.values[dictionary->u.dictionary.count] = value;
	dictionary->u.dictionary.keys[dictionary->u.dictionary.count] =
		(key == NULL) ? NULL : plist_strdup(key);
	dictionary->u.dictionary.count++;
	return 0;
}

int plist_dictionary_set(plist_value_t *dictionary, const char *key, plist_value_t *value)
{
	if (key == NULL) {
		return -1;
	}
	return plist_dictionary_put(dictionary, key, value);
}

/* Put a comment INTO a container: an ordinary item when the container is an
 * array, a slot with a NULL key when it is a dictionary. This is the parser's
 * only way in, and it returns -1 rather than half-inserting. */
static int plist_append_comment(plist_value_t *container, const char *text)
{
	plist_value_t *comment;

	if (container == NULL) {
		return -1;
	}
	comment = plist_new_comment(text);
	if (comment == NULL) {
		return -1;
	}
	if (container->type == PLIST_ARRAY) {
		if (plist_array_append(container, comment) != 0) {
			plist_free(comment);
			return -1;
		}
		return 0;
	}
	if (container->type == PLIST_DICTIONARY) {
		if (plist_dictionary_put(container, NULL, comment) != 0) {
			plist_free(comment);
			return -1;
		}
		return 0;
	}
	plist_free(comment);
	return -1;
}

/*
 * The PUBLIC spelling of the above. The parser is no longer the only creator of
 * a comment slot: libconfig REWRITES config files, and it has to be able to
 * build a commented tree or the first `config set` would delete the prose that
 * explains a setting — the whole reason comments are kept at all (P3a). One
 * implementation, two names, so there is no second code path to drift.
 */
int plist_comment_append(plist_value_t *container, const char *text)
{
	return plist_append_comment(container, text);
}

/* ---- lifetime ------------------------------------------------------------ */

void plist_free(plist_value_t *value)
{
	size_t i;

	if (value == NULL) {
		return;
	}
	switch (value->type) {
	case PLIST_STRING:
	case PLIST_COMMENT:		/* both carry the text in u.string */
		free(value->u.string);
		break;
	case PLIST_DATA:
		free(value->u.data.bytes);
		break;
	case PLIST_ARRAY:
		for (i = 0; i < value->u.array.count; i++) {
			plist_free(value->u.array.items[i]);
		}
		free(value->u.array.items);
		break;
	case PLIST_DICTIONARY:
		for (i = 0; i < value->u.dictionary.count; i++) {
			/* A COMMENT SLOT has a NULL key, and free(NULL) is the
			 * correct disposal of one. */
			free(value->u.dictionary.keys[i]);
			plist_free(value->u.dictionary.values[i]);
		}
		free(value->u.dictionary.keys);
		free(value->u.dictionary.values);
		break;
	default:
		break;
	}
	free(value);
}

/* ---- accessors ----------------------------------------------------------- */

plist_type_t plist_type_of(const plist_value_t *value)
{
	return value == NULL ? PLIST_NONE : value->type;
}

plist_value_t *plist_dictionary_get(const plist_value_t *dictionary, const char *key)
{
	size_t i;

	if (dictionary == NULL || dictionary->type != PLIST_DICTIONARY || key == NULL) {
		return NULL;
	}
	for (i = 0; i < dictionary->u.dictionary.count; i++) {
		/* A NULL KEY IS A COMMENT SLOT: it can never match a caller's key,
		 * and strcmp would fault on it. */
		if (dictionary->u.dictionary.keys[i] != NULL &&
		    strcmp(dictionary->u.dictionary.keys[i], key) == 0) {
			return dictionary->u.dictionary.values[i];
		}
	}
	return NULL;
}

/* Comments are items of the tree but not of the SEQUENCE: the count and the
 * index accessor both step over them, so an index never lands on prose. */
static int plist_array_skips(const plist_value_t *item)
{
	return item == NULL || item->type == PLIST_COMMENT;
}

size_t plist_array_count(const plist_value_t *array)
{
	size_t i, count = 0;

	if (array == NULL || array->type != PLIST_ARRAY) {
		return 0;
	}
	for (i = 0; i < array->u.array.count; i++) {
		if (!plist_array_skips(array->u.array.items[i])) {
			count++;
		}
	}
	return count;
}

plist_value_t *plist_array_get(const plist_value_t *array, size_t index)
{
	size_t i, seen = 0;

	if (array == NULL || array->type != PLIST_ARRAY) {
		return NULL;
	}
	for (i = 0; i < array->u.array.count; i++) {
		plist_value_t *item = array->u.array.items[i];

		if (plist_array_skips(item)) {
			continue;
		}
		if (seen == index) {
			return item;
		}
		seen++;
	}
	return NULL;
}

/* ---- the parser ---------------------------------------------------------- */

typedef struct {
	const char *text;
	size_t length;
	size_t position;
	char *error;
	size_t error_size;
} plist_parser_t;

/* The FIRST failure is the one reported: a cascade of derived errors helps
 * nobody, and the position of the first is the only useful one. */
static void plist_fail(plist_parser_t *parser, const char *message, const char *detail)
{
	if (parser->error != NULL && parser->error_size > 0 && parser->error[0] == '\0') {
		if (detail != NULL) {
			snprintf(parser->error, parser->error_size,
				 "plist: %s at offset %lu (near \"%.24s\")",
				 message, (unsigned long)parser->position, detail);
		} else {
			snprintf(parser->error, parser->error_size, "plist: %s at offset %lu",
				 message, (unsigned long)parser->position);
		}
	}
}

/* WHITESPACE, AND ONLY WHITESPACE. A comment used to be eaten here, and that is
 * exactly why the parser could not keep one: prose in a config file is now an
 * ITEM of the tree (P3a), captured where it stands by plist_capture_comments. */
static void plist_skip_space(plist_parser_t *parser)
{
	while (parser->position < parser->length) {
		char character = parser->text[parser->position];

		if (character == ' ' || character == '\t' ||
		    character == '\r' || character == '\n') {
			parser->position++;
			continue;
		}
		return;
	}
}

/* Is a comment standing at the cursor? */
static int plist_at_comment(const plist_parser_t *parser)
{
	return parser->position + 4 <= parser->length &&
	       strncmp(parser->text + parser->position, "<!--", 4) == 0;
}

/* Where the comment at the cursor ENDS — its "-->" — or NULL. The search is
 * BOUNDED BY THE TEXT'S LENGTH, because a plist need not be NUL-terminated and
 * strstr would read past the end of the buffer it was handed. */
static const char *plist_comment_end(const plist_parser_t *parser)
{
	const char *start = parser->text + parser->position + 4;
	size_t remaining = parser->length - (parser->position + 4);
	size_t i;

	for (i = 0; i + 3 <= remaining; i++) {
		if (start[i] == '-' && start[i + 1] == '-' && start[i + 2] == '>') {
			return start + i;
		}
	}
	return NULL;
}

/* Take every comment standing at the cursor, appending each to `container` as
 * an item of it — or CONSUMING AND DISCARDING them when container is NULL,
 * which is what a position with nowhere to keep one passes (see plist_parse:
 * a comment before the root value). Returns 0, or -1 when a comment is
 * unterminated or there is no memory for one. */
static int plist_capture_comments(plist_parser_t *parser, plist_value_t *container)
{
	for (;;) {
		const char *end;
		char *text;
		size_t length;

		plist_skip_space(parser);
		if (!plist_at_comment(parser)) {
			return 0;
		}
		end = plist_comment_end(parser);
		if (end == NULL) {
			plist_fail(parser, "unterminated comment", NULL);
			parser->position = parser->length;
			return -1;
		}
		/* The comment's text is what stood between <!-- and -->, verbatim:
		 * no escaping on the way in, none on the way out. */
		length = (size_t)(end - (parser->text + parser->position + 4));
		text = (char *)plist_xmalloc(length + 1);
		if (text == NULL) {
			plist_fail(parser, "out of memory", NULL);
			return -1;
		}
		memcpy(text, parser->text + parser->position + 4, length);
		text[length] = '\0';
		parser->position = (size_t)(end - parser->text) + 3;
		if (container != NULL && plist_append_comment(container, text) != 0) {
			free(text);
			plist_fail(parser, "out of memory", NULL);
			return -1;
		}
		free(text);
	}
}

/* Read up to the next '<', decoding entities as we go. */
static char *plist_read_text(plist_parser_t *parser)
{
	char *out = NULL;
	size_t length = 0;
	size_t capacity = 0;

	while (parser->position < parser->length && parser->text[parser->position] != '<') {
		char character = parser->text[parser->position];

		if (character != '&') {
			plist_buffer_append(&out, &length, &capacity, &character, 1);
			parser->position++;
			continue;
		}
		{
			const char *semi = memchr(parser->text + parser->position, ';',
						  parser->length - parser->position);
			size_t span;
			char entity[16];
			int decoded = -1;

			if (semi == NULL) {
				plist_fail(parser, "unterminated entity", NULL);
				break;
			}
			span = (size_t)(semi - (parser->text + parser->position)) - 1;
			if (span == 0 || span >= sizeof(entity)) {
				plist_fail(parser, "malformed entity", NULL);
				break;
			}
			memcpy(entity, parser->text + parser->position + 1, span);
			entity[span] = '\0';
			if (strcmp(entity, "amp") == 0) {
				decoded = '&';
			} else if (strcmp(entity, "lt") == 0) {
				decoded = '<';
			} else if (strcmp(entity, "gt") == 0) {
				decoded = '>';
			} else if (strcmp(entity, "quot") == 0) {
				decoded = '"';
			} else if (strcmp(entity, "apos") == 0) {
				decoded = '\'';
			}
			if (decoded >= 0) {
				char one = (char)decoded;

				plist_buffer_append(&out, &length, &capacity, &one, 1);
			} else if (entity[0] == '#') {
				/* &#NN; and &#xHH;: the numeric forms a writer may emit for a
				 * character it cannot spell in ASCII. */
				long code = (entity[1] == 'x' || entity[1] == 'X')
					? strtol(entity + 2, NULL, 16) : strtol(entity + 1, NULL, 10);
				unsigned int code_point = (unsigned int)code;
				char utf8[4];
				size_t count = 0;

				if (code_point < 0x80) {
					utf8[count++] = (char)code_point;
				} else if (code_point < 0x800) {
					utf8[count++] = (char)(0xC0 | (code_point >> 6));
					utf8[count++] = (char)(0x80 | (code_point & 0x3F));
				} else if (code_point < 0x10000) {
					utf8[count++] = (char)(0xE0 | (code_point >> 12));
					utf8[count++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
					utf8[count++] = (char)(0x80 | (code_point & 0x3F));
				} else {
					utf8[count++] = (char)(0xF0 | (code_point >> 18));
					utf8[count++] = (char)(0x80 | ((code_point >> 12) & 0x3F));
					utf8[count++] = (char)(0x80 | ((code_point >> 6) & 0x3F));
					utf8[count++] = (char)(0x80 | (code_point & 0x3F));
				}
				plist_buffer_append(&out, &length, &capacity, utf8, count);
			} else {
				plist_fail(parser, "unknown entity", entity);
				break;
			}
			parser->position = (size_t)(semi - parser->text) + 1;
		}
	}
	if (out == NULL) {
		out = plist_strdup("");
	}
	return out;
}

/* Read a tag name; the cursor sits just after '<'. */
static int plist_read_tag(plist_parser_t *parser, char *name, size_t name_size, int *self_closing)
{
	size_t used = 0;

	plist_skip_space(parser);
	if (parser->position >= parser->length) {
		plist_fail(parser, "expected a tag", NULL);
		return -1;
	}
	if (parser->text[parser->position] == '/') {
		plist_fail(parser, "unexpected closing tag", NULL);
		return -1;
	}
	while (parser->position < parser->length &&
	       parser->text[parser->position] != '>' &&
	       parser->text[parser->position] != '/' &&
	       parser->text[parser->position] != ' ' &&
	       parser->text[parser->position] != '\t' &&
	       parser->text[parser->position] != '\n' &&
	       parser->text[parser->position] != '\r') {
		if (used + 1 < name_size) {
			name[used++] = parser->text[parser->position];
		}
		parser->position++;
	}
	name[used] = '\0';
	/* Skip any attributes: Apple's writer emits none, but a hand-written file
	 * may carry some and refusing them would be gratuitous. */
	while (parser->position < parser->length && parser->text[parser->position] != '>') {
		if (parser->text[parser->position] == '/') {
			*self_closing = 1;
		}
		parser->position++;
	}
	if (parser->position >= parser->length) {
		plist_fail(parser, "unterminated tag", name);
		return -1;
	}
	parser->position++;		/* the '>' */
	return 0;
}

/* Consume "</name>". */
static int plist_expect_close(plist_parser_t *parser, const char *name)
{
	char tag[32];
	int self_closing = 0;

	plist_skip_space(parser);
	if (parser->position >= parser->length || parser->text[parser->position] != '<') {
		plist_fail(parser, "expected a closing tag", name);
		return -1;
	}
	parser->position++;
	plist_skip_space(parser);
	if (parser->position >= parser->length || parser->text[parser->position] != '/') {
		plist_fail(parser, "expected a closing tag", name);
		return -1;
	}
	parser->position++;
	if (plist_read_tag(parser, tag, sizeof(tag), &self_closing) != 0) {
		return -1;
	}
	if (strcmp(tag, name) != 0) {
		plist_fail(parser, "closing tag does not match", tag);
		return -1;
	}
	return 0;
}

static plist_value_t *plist_parse_value(plist_parser_t *parser);

static int plist_at_close(plist_parser_t *parser)
{
	return (parser->position + 1 < parser->length &&
		parser->text[parser->position] == '<' &&
		parser->text[parser->position + 1] == '/');
}

static plist_value_t *plist_parse_array(plist_parser_t *parser)
{
	plist_value_t *array = plist_new_array();

	for (;;) {
		plist_value_t *item;

		/* In an ARRAY a comment is an ordinary item, so it is captured INTO
		 * the array and written back out where it stood. */
		if (plist_capture_comments(parser, array) != 0) {
			plist_free(array);
			return NULL;
		}
		if (plist_at_close(parser)) {
			break;
		}
		if (parser->position >= parser->length) {
			plist_fail(parser, "unterminated <array>", NULL);
			plist_free(array);
			return NULL;
		}
		item = plist_parse_value(parser);
		if (item == NULL || plist_array_append(array, item) != 0) {
			plist_free(item);
			plist_free(array);
			return NULL;
		}
	}
	if (plist_expect_close(parser, "array") != 0) {
		plist_free(array);
		return NULL;
	}
	return array;
}

static plist_value_t *plist_parse_dictionary(plist_parser_t *parser)
{
	plist_value_t *dictionary = plist_new_dictionary();

	for (;;) {
		char tag[32];
		int self_closing = 0;
		char *key;
		plist_value_t *value;

		/* Comments are captured at EVERY position an entry may follow, so a
		 * commented table keeps its prose. plist_capture_comments takes the
		 * whitespace too, which is why there is no separate skip here. */
		if (plist_capture_comments(parser, dictionary) != 0) {
			plist_free(dictionary);
			return NULL;
		}
		if (plist_at_close(parser)) {
			break;
		}
		if (parser->position >= parser->length) {
			plist_fail(parser, "unterminated <dict>", NULL);
			plist_free(dictionary);
			return NULL;
		}
		parser->position++;	/* '<' */
		if (plist_read_tag(parser, tag, sizeof(tag), &self_closing) != 0) {
			plist_free(dictionary);
			return NULL;
		}
		if (strcmp(tag, "key") != 0) {
			plist_fail(parser, "expected <key> in a dictionary", tag);
			plist_free(dictionary);
			return NULL;
		}
		key = plist_read_text(parser);
		if (plist_expect_close(parser, "key") != 0) {
			free(key);
			plist_free(dictionary);
			return NULL;
		}
		/* The position BETWEEN a key and its value: a comment standing there
		 * belongs to the same entry, so it is captured BEFORE the pair is
		 * written into the dict and stays attached to it on the way out. */
		if (plist_capture_comments(parser, dictionary) != 0) {
			free(key);
			plist_free(dictionary);
			return NULL;
		}
		value = plist_parse_value(parser);
		if (value == NULL || plist_dictionary_set(dictionary, key, value) != 0) {
			plist_free(value);
			free(key);
			plist_free(dictionary);
			return NULL;
		}
		free(key);
	}
	if (plist_expect_close(parser, "dict") != 0) {
		plist_free(dictionary);
		return NULL;
	}
	return dictionary;
}

static plist_value_t *plist_parse_value(plist_parser_t *parser)
{
	char tag[32];
	int self_closing = 0;

	plist_skip_space(parser);
	if (parser->position >= parser->length || parser->text[parser->position] != '<') {
		plist_fail(parser, "expected a value", NULL);
		return NULL;
	}
	parser->position++;
	if (plist_read_tag(parser, tag, sizeof(tag), &self_closing) != 0) {
		return NULL;
	}

	/* A SELF-CLOSING COLLECTION IS EMPTY. Apple's writer emits <dict/> and
	 * <array/>, and a reader that cannot read its own writer's output is not a
	 * reader — the round-trip test caught exactly that here. */
	if (strcmp(tag, "dict") == 0) {
		return self_closing ? plist_new_dictionary() : plist_parse_dictionary(parser);
	}
	if (strcmp(tag, "array") == 0) {
		return self_closing ? plist_new_array() : plist_parse_array(parser);
	}
	if (strcmp(tag, "true") == 0 || strcmp(tag, "false") == 0) {
		int boolean = (tag[0] == 't');

		if (!self_closing && plist_expect_close(parser, tag) != 0) {
			return NULL;
		}
		return plist_new_boolean(boolean);
	}
	if (strcmp(tag, "string") == 0) {
		plist_value_t *value = plist_new(PLIST_STRING);

		if (value != NULL) {
			value->u.string = plist_read_text(parser);
		}
		if (value == NULL || plist_expect_close(parser, "string") != 0) {
			plist_free(value);
			return NULL;
		}
		return value;
	}
	if (strcmp(tag, "integer") == 0) {
		char *text = plist_read_text(parser);
		plist_value_t *value = plist_new_integer(strtoll(text, NULL, 10));

		free(text);
		if (value == NULL || plist_expect_close(parser, "integer") != 0) {
			plist_free(value);
			return NULL;
		}
		return value;
	}
	if (strcmp(tag, "real") == 0) {
		char *text = plist_read_text(parser);
		plist_value_t *value = plist_new_real(strtod(text, NULL));

		free(text);
		if (value == NULL || plist_expect_close(parser, "real") != 0) {
			plist_free(value);
			return NULL;
		}
		return value;
	}
	if (strcmp(tag, "data") == 0) {
		char *text = plist_read_text(parser);
		size_t byte_count = 0;
		unsigned char *bytes = plist_base64_decode(text, strlen(text), &byte_count);
		plist_value_t *value = plist_new(PLIST_DATA);

		free(text);
		if (value != NULL) {
			value->u.data.bytes = bytes;
			value->u.data.length = byte_count;
		}
		if (value == NULL || plist_expect_close(parser, "data") != 0) {
			plist_free(value);
			return NULL;
		}
		return value;
	}
	if (strcmp(tag, "date") == 0) {
		char *text = plist_read_text(parser);
		long long year = 0;
		unsigned month = 0, day = 0, hour = 0, minute = 0;
		double second = 0.0;
		/* %lf, so "30.5Z" and "30Z" BOTH parse - the fraction is optional in the text. */
		int fields = sscanf(text, "%lld-%u-%uT%u:%u:%lf", &year, &month, &day,
				    &hour, &minute, &second);
		plist_value_t *value = NULL;

		free(text);
		if (fields < 3) {
			plist_fail(parser, "malformed <date>", NULL);
			return NULL;
		}
		value = plist_new_date((double)plist_days_from_civil(year, month, day) * 86400.0
				       + (double)(hour * 3600 + minute * 60) + second
				       - PLIST_EPOCH_SHIFT);
		if (value == NULL || plist_expect_close(parser, "date") != 0) {
			plist_free(value);
			return NULL;
		}
		return value;
	}

	/* AN UNKNOWN ELEMENT IS AN ERROR. A parser that skips what it does not know
	 * is a parser that silently drops a config field — and this file is read by
	 * two different implementations, which must therefore refuse the same
	 * input. */
	plist_fail(parser, "unknown element", tag);
	return NULL;
}

plist_value_t *plist_parse(const char *text, size_t length, char *error, size_t error_size)
{
	plist_parser_t parser;
	plist_value_t *root = NULL;
	char tag[32];
	int self_closing = 0;
	const char *plist_tag;

	if (error != NULL && error_size > 0) {
		error[0] = '\0';
	}
	if (text == NULL) {
		return NULL;
	}
	parser.text = text;
	parser.length = length;
	parser.position = 0;
	parser.error = error;
	parser.error_size = error_size;

	/* Everything before <plist — the XML declaration and the DOCTYPE — is
	 * skipped by search, which is what makes a plist written by Apple's tools
	 * readable without an XML prolog parser. */
	plist_tag = strstr(text, "<plist");
	if (plist_tag == NULL) {
		plist_fail(&parser, "no <plist> element", NULL);
		return NULL;
	}
	parser.position = (size_t)(plist_tag - text) + 1;
	if (plist_read_tag(&parser, tag, sizeof(tag), &self_closing) != 0 ||
	    strcmp(tag, "plist") != 0) {
		plist_fail(&parser, "expected <plist>", tag);
		return NULL;
	}
	/* A comment between <plist …> and the root value is ACCEPTED AND THEN
	 * DISCARDED, rather than hoisted into the tree: a file's header prose
	 * belongs to the ROOT DICTIONARY, where it round-trips natively, and
	 * hoisting it here would mean re-shaping whichever type the root happens
	 * to be — a plist whose root is a <string> has no slot for a comment. */
	if (plist_capture_comments(&parser, NULL) != 0) {
		return NULL;		/* the first error is already recorded */
	}
	root = plist_parse_value(&parser);
	if (root == NULL) {
		return NULL;
	}
	if (plist_expect_close(&parser, "plist") != 0) {
		plist_free(root);
		return NULL;
	}
	return root;
}

/* ---- the serialiser ------------------------------------------------------ */

static void plist_append_indent(char **buffer, size_t *length, size_t *capacity, int depth)
{
	int i;

	for (i = 0; i < depth; i++) {
		plist_buffer_append(buffer, length, capacity, "\t", 1);
	}
}

static void plist_append_escaped(char **buffer, size_t *length, size_t *capacity, const char *text)
{
	const char *cursor;

	for (cursor = text; *cursor != '\0'; cursor++) {
		switch (*cursor) {
		case '&':
			plist_buffer_append(buffer, length, capacity, "&amp;", 5);
			break;
		case '<':
			plist_buffer_append(buffer, length, capacity, "&lt;", 4);
			break;
		case '>':
			plist_buffer_append(buffer, length, capacity, "&gt;", 4);
			break;
		default:
			plist_buffer_append(buffer, length, capacity, cursor, 1);
			break;
		}
	}
}

/* The shortest of %.15g .. %.17g that reads back as the same double, so a config
 * keeps 0.1 as 0.1 rather than 0.10000000000000001. */
static void plist_format_real(char *out, size_t out_size, double value)
{
	int precision;

	for (precision = 15; precision < 17; precision++) {
		snprintf(out, out_size, "%.*g", precision, value);
		if (strtod(out, NULL) == value) {
			return;
		}
	}
	snprintf(out, out_size, "%.17g", value);
}

static void plist_serialize_value(char **buffer, size_t *length, size_t *capacity,
				  const plist_value_t *value, int depth)
{
	char text[128];

	if (value == NULL) {
		plist_buffer_append(buffer, length, capacity, "<string></string>\n", 19);
		return;
	}
	switch (value->type) {
	case PLIST_COMMENT:
		/* The prose a config file carries, written back where it stood.
		 * The body is NOT escaped: a comment is not markup, and escaping
		 * it would hand a reader different bytes. */
		plist_buffer_append(buffer, length, capacity, "<!--", 4);
		plist_buffer_append(buffer, length, capacity, value->u.string,
				    strlen(value->u.string));
		plist_buffer_append(buffer, length, capacity, "-->\n", 4);
		break;
	case PLIST_STRING:
		plist_buffer_append(buffer, length, capacity, "<string>", 8);
		plist_append_escaped(buffer, length, capacity, value->u.string);
		plist_buffer_append(buffer, length, capacity, "</string>\n", 10);
		break;
	case PLIST_INTEGER:
		snprintf(text, sizeof(text), "<integer>%lld</integer>\n", value->u.integer);
		plist_buffer_append(buffer, length, capacity, text, strlen(text));
		break;
	case PLIST_REAL:
		{
			char number[64];

			plist_format_real(number, sizeof(number), value->u.real);
			snprintf(text, sizeof(text), "<real>%s</real>\n", number);
			plist_buffer_append(buffer, length, capacity, text, strlen(text));
		}
		break;
	case PLIST_BOOLEAN:
		if (value->u.boolean) {
			plist_buffer_append(buffer, length, capacity, "<true/>\n", 8);
		} else {
			plist_buffer_append(buffer, length, capacity, "<false/>\n", 9);
		}
		break;
	case PLIST_DATE:
		{
			double seconds = value->u.date + PLIST_EPOCH_SHIFT;
			long long whole = (long long)floor(seconds);
			long long days = whole / 86400;
			long long year;
			unsigned month, day, hour, minute, second;

			if (whole % 86400 < 0) {
				days--;
			}
			hour = (unsigned)((whole - days * 86400) / 3600);
			minute = (unsigned)((whole - days * 86400) % 3600 / 60);
			second = (unsigned)((whole - days * 86400) % 60);
			plist_civil_from_days(days, &year, &month, &day);
			/* THE FRACTION IS PART OF THE VALUE AND IS WRITTEN (§45-X). A date here is a double,
			 * and rounding it to whole seconds on the way out made an NSDate that went through a
			 * plist come back UNEQUAL: measured, 1234567890.5 was written as ...T23:31:30Z and read
			 * back as 1234567890. Few of 1..9 fractional digits are written - the fewest that reads
			 * back as the same value - and NONE are written for a whole second, so every date that
			 * has no fraction keeps the exact bytes it has always had. */
			{
				double frac = seconds - (double)whole;
				char frac_text[24];

				frac_text[0] = '\0';
				if (frac > 0.0) {
					int digits;

					for (digits = 1; digits <= 9; digits++) {
						snprintf(frac_text, sizeof(frac_text), "%.*f", digits, frac);
						if (strtod(frac_text, NULL) == frac) {
							break;
						}
					}
					/* drop the leading "0" and keep the dot: "0.5" -> ".5" */
					memmove(frac_text, frac_text + 1, strlen(frac_text));
				}
				snprintf(text, sizeof(text),
					 "<date>%04lld-%02u-%02uT%02u:%02u:%02u%sZ</date>\n",
					 year, month, day, hour, minute, second, frac_text);
			}
			plist_buffer_append(buffer, length, capacity, text, strlen(text));
		}
		break;
	case PLIST_DATA:
		{
			char *base64 = NULL;
			size_t base64_length = 0;
			size_t base64_capacity = 0;
			size_t i;

			plist_buffer_append(buffer, length, capacity, "<data>", 6);
			plist_buffer_append_base64(&base64, &base64_length, &base64_capacity,
						   value->u.data.bytes, value->u.data.length);
			/* Long payloads wrap at 76 columns with a newline and the current
			 * indent; a short one stays on a single line. */
			for (i = 0; i < base64_length; i += 76) {
				size_t span = (base64_length - i > 76) ? 76 : base64_length - i;

				if (i > 0 || base64_length > 76) {
					plist_buffer_append(buffer, length, capacity, "\n", 1);
					plist_append_indent(buffer, length, capacity, depth + 1);
				}
				plist_buffer_append(buffer, length, capacity, base64 + i, span);
			}
			free(base64);
			plist_buffer_append(buffer, length, capacity, "</data>\n", 8);
		}
		break;
	case PLIST_ARRAY:
		{
			size_t i;

			/* Apple writes an EMPTY collection self-closing, and matching their
			 * spelling is the whole point of writing their shape at all. */
			if (value->u.array.count == 0) {
				plist_buffer_append(buffer, length, capacity, "<array/>\n", 9);
				break;
			}
			plist_buffer_append(buffer, length, capacity, "<array>\n", 8);
			for (i = 0; i < value->u.array.count; i++) {
				plist_append_indent(buffer, length, capacity, depth + 1);
				plist_serialize_value(buffer, length, capacity,
						      value->u.array.items[i], depth + 1);
			}
			plist_append_indent(buffer, length, capacity, depth);
			plist_buffer_append(buffer, length, capacity, "</array>\n", 9);
		}
		break;
	case PLIST_DICTIONARY:
		{
			size_t i;

			if (value->u.dictionary.count == 0) {
				plist_buffer_append(buffer, length, capacity, "<dict/>\n", 8);
				break;
			}
			plist_buffer_append(buffer, length, capacity, "<dict>\n", 7);
			for (i = 0; i < value->u.dictionary.count; i++) {
				plist_append_indent(buffer, length, capacity, depth + 1);
				/* A NULL KEY IS A COMMENT SLOT: it gets no <key>, and
				 * the comment is indented where the entry it precedes
				 * stands — which is what makes a commented config file
				 * round-trip byte for byte. */
				if (value->u.dictionary.keys[i] != NULL) {
					plist_buffer_append(buffer, length, capacity, "<key>", 5);
					plist_append_escaped(buffer, length, capacity,
							     value->u.dictionary.keys[i]);
					plist_buffer_append(buffer, length, capacity, "</key>\n", 7);
					plist_append_indent(buffer, length, capacity, depth + 1);
				}
				plist_serialize_value(buffer, length, capacity,
						      value->u.dictionary.values[i], depth + 1);
			}
			plist_append_indent(buffer, length, capacity, depth);
			plist_buffer_append(buffer, length, capacity, "</dict>\n", 8);
		}
		break;
	default:
		plist_buffer_append(buffer, length, capacity, "<string></string>\n", 19);
		break;
	}
}

char *plist_serialize(const plist_value_t *value, size_t *length_out)
{
	const char *header =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
		"\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\">\n";
	char *buffer = NULL;
	size_t length = 0;
	size_t capacity = 0;

	plist_buffer_append(&buffer, &length, &capacity, header, strlen(header));
	plist_serialize_value(&buffer, &length, &capacity, value, 0);
	plist_buffer_append(&buffer, &length, &capacity, "</plist>\n", 9);
	if (length_out != NULL) {
		*length_out = length;
	}
	return buffer;
}
