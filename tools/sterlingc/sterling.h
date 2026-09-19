/*
 * sterling.h — the Sterling compiler's shared definitions.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * K1 scope: this front end is sized to compile the specimen in
 * docs/design/sterling-syntax.md §1, and nothing beyond it yet. The rules
 * implemented here are the ones that § claim, quoted beside each one.
 */
#ifndef STERLING_H
#define STERLING_H

#include <stddef.h>
#include <stdint.h>

/* ---- tokens ------------------------------------------------------------ */

typedef enum {
	ST_EOF = 0,
	ST_ERROR,
	ST_IDENT,		/* also covers `nil`, `self`, `super` */
	ST_KEYWORD,
	ST_INT,
	ST_FLOAT,
	ST_STRING,
	ST_OPERATOR,		/* §7.72: a 1-3 character run of the operator set */
	ST_SUBSCRIPT,		/* the bare `[]` operator token, declarations only */
	ST_PUNCT,		/* ( ) { } , ; : . @ and the brackets outside a decl */
} st_token_kind;

typedef struct {
	st_token_kind kind;
	const char *start;	/* first character in the source */
	size_t len;		/* source characters consumed */
	int line;		/* 1-based, for diagnostics */
	int column;		/* 1-based */
	const char *text;	/* for ST_ERROR: a message rather than source */
} st_token;

/* The keywords of the surface. §3's map gives the emissions; this list is
 * the reserved set, and it is deliberately short: only words the grammar
 * cannot treat as an identifier appear here. */
extern const char *const st_keywords[];
extern const size_t st_keyword_count;

/* ---- the lexer --------------------------------------------------------- */

typedef struct {
	const char *src;	/* NUL-terminated source */
	size_t pos;
	int line;
	int column;
	const char *error;	/* set when a token could not be formed */
} st_lexer;

void st_lexer_init(st_lexer *lx, const char *src);

/*
 * Produce the next token. Returns ST_EOF at the end, and ST_ERROR exactly
 * once on a malformed token (the lexer is then exhausted). The returned
 * token points into `src` and stays valid for as long as the source does.
 */
st_token st_lexer_next(st_lexer *lx);

/* True when `word` is a reserved keyword. */
int st_is_keyword(const char *word, size_t len);

/* ---- helpers used by the tests and the driver -------------------------- */

const char *st_token_kind_name(st_token_kind kind);

/* A human-readable dump of the whole source, one token per line. */
void st_dump(const char *src);

#endif /* STERLING_H */
