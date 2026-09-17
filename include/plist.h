/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * plist.h — XML property lists (v1.0) in plain C.
 * docs/design/foundation-plan.md; the sharing decision is recorded there.
 *
 * THE ONE CORE, TWO SKINS. This is the whole plist implementation: no runtime,
 * no allocator of its own, no iostream, nothing but <stdlib.h>. libconfig will
 * consume it from C (a config file becomes an ordinary C tree), and Foundation
 * wraps it in -Objective-C- as NSPropertyListSerialization. One implementation
 * means the C and ObjC sides cannot drift apart on the same file, which matters
 * because they will read and write the SAME config files.
 *
 * WHY XML AND NOT OpenStep syntax: the format is TYPED. `key = 1;` cannot say
 * whether it meant the integer 1, the real 1.0 or the string "1", and a config
 * format that cannot distinguish those is a config format that guesses. The
 * OpenStep form is accepted by Apple's reader for compatibility; this one does
 * not read it, deliberately (see plist_parse's "unsupported" path).
 *
 * WHAT IS SUPPORTED (Apple's own vocabulary, and nothing invented):
 *   <dict> <key> <array> <string> <integer> <real> <true/> <false/>
 *   <data> (base64) <date> (ISO 8601, UTC)
 * plus the XML prolog, the plist DOCTYPE, comments and arbitrary whitespace.
 *
 * WHAT IS NOT: entities beyond the five XML predefined ones plus numeric
 * character references; DTD-internal subsets; and OpenStep/binary formats.
 */

#ifndef FNX_PLIST_H
#define FNX_PLIST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	PLIST_NONE = 0,
	PLIST_STRING,
	PLIST_INTEGER,
	PLIST_REAL,
	PLIST_BOOLEAN,
	PLIST_DATE,		/* u.date is SECONDS since 2001-01-01T00:00:00Z */
	PLIST_DATA,
	PLIST_ARRAY,
	PLIST_DICTIONARY
} plist_type_t;

typedef struct plist_value plist_value_t;

struct plist_value {
	plist_type_t type;
	union {
		char *string;				/* UTF-8, NUL-terminated */
		long long integer;
		double real;
		int boolean;				/* 0 or 1 */
		double date;
		struct {
			unsigned char *bytes;
			size_t length;
		} data;
		struct {
			plist_value_t **items;
			size_t count;
		} array;
		struct {
			char **keys;			/* insertion order is preserved */
			plist_value_t **values;
			size_t count;
			size_t capacity;
		} dictionary;
	} u;
};

/* ---- parsing ------------------------------------------------------------- */

/*
 * Parse an XML plist. Returns the root value, or NULL on failure with a
 * human-readable message in `error` (when error_size is non-zero). The input
 * need not be NUL-terminated; pass its length.
 */
plist_value_t *plist_parse(const char *text, size_t length,
			   char *error, size_t error_size);

/* ---- serialising ---------------------------------------------------------
 *
 * Serialise to XML v1.0 in Apple's shape: the XML declaration, the plist
 * DOCTYPE, <plist version="1.0">, and the value indented one TAB per depth.
 * Returns a NUL-terminated malloc'd string, or NULL on failure. *length_out
 * (when non-NULL) receives the byte length, EXCLUDING the terminator.
 */
char *plist_serialize(const plist_value_t *value, size_t *length_out);

/* ---- lifetime ------------------------------------------------------------ */

void plist_free(plist_value_t *value);

/* ---- accessors ----------------------------------------------------------- */

plist_value_t *plist_dictionary_get(const plist_value_t *dictionary, const char *key);
size_t plist_array_count(const plist_value_t *array);
plist_value_t *plist_array_get(const plist_value_t *array, size_t index);
plist_type_t plist_type_of(const plist_value_t *value);

/* ---- construction (what a writer of any kind needs) ---------------------- */

plist_value_t *plist_new_string(const char *utf8);
plist_value_t *plist_new_integer(long long value);
plist_value_t *plist_new_real(double value);
plist_value_t *plist_new_boolean(int value);
plist_value_t *plist_new_date(double seconds_since_2001);
plist_value_t *plist_new_data(const unsigned char *bytes, size_t length);
plist_value_t *plist_new_array(void);
plist_value_t *plist_new_dictionary(void);

/* Both return 0 on success, -1 on failure (no memory). Adding a duplicate key
 * REPLACES the value and keeps the key's original position. */
int plist_array_append(plist_value_t *array, plist_value_t *item);
int plist_dictionary_set(plist_value_t *dictionary, const char *key, plist_value_t *value);

#ifdef __cplusplus
}
#endif

#endif /* FNX_PLIST_H */
