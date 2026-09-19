/*
 * lexer.c — the Sterling lexer.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * The rules that matter here, and where they come from:
 *
 *   §7.72  An operator is a one-to-three character run drawn from
 *          ! @ # $ % ^ & * - = + : < > ? / \ . — and the lexer takes the
 *          longest such run, up to three, then stops.
 *   §7.72  `[]` is the one exception: it is the subscript *operator* in a
 *          declaration and punchuation everywhere else, so this lexer only
 *          forms it when the previous token was the keyword `operator`.
 *   §7.65  `...` and `..<` are range forms, and both are runs of the same
 *          character set, so they are recognised before the general run.
 *   §7.72  `.` alone is member access, and `:` alone is a label separator,
 *          so neither is an operator on its own. Every other single
 *          character from the set is.
 *   §6     `//` and block comments are whitespace, as are all C spaces.
 *
 * Note the overlap the document flags: `.` and `:` are both in the operator
 * set and both have a single-character meaning that is not an operator. The
 * resolution is length: one character is punctuation, two or three is an
 * operator.
 */
#include "sterling.h"

#include <stdio.h>
#include <string.h>

const char *const st_keywords[] = {
	"import", "class", "struct", "enum", "protocol", "extension",
	"category", "method", "func", "init", "property", "readonly",
	"weak", "unowned", "required", "optional", "public", "private",
	"internal", "let", "var", "if", "else", "guard", "for", "while",
	"switch", "case", "default", "where", "defer", "with", "operator",
	"return", "throws", "self", "super", "nil", "true", "false",
	"strong", "assign", "copy",
	/*
	 * `break` and `continue` are statements — §7.71 names them among the
	 * ways a scope can be left — and leaving them out made them lex as
	 * identifiers, so `break` parsed as an *expression statement* and gave
	 * the appearance of working. `in` belongs to §7.66's `for` and §7.33's
	 * closure form; `as` to §7.46.
	 */
	"break", "continue", "in", "as",
};

const size_t st_keyword_count = sizeof(st_keywords) / sizeof(st_keywords[0]);

int
st_is_keyword(const char *word, size_t len)
{
	size_t i;

	for (i = 0; i < st_keyword_count; i++) {
		if (strlen(st_keywords[i]) == len &&
		    memcmp(st_keywords[i], word, len) == 0) {
			return 1;
		}
	}
	return 0;
}

const char *
st_token_kind_name(st_token_kind kind)
{
	switch (kind) {
	case ST_EOF:		return "EOF";
	case ST_ERROR:		return "ERROR";
	case ST_IDENT:		return "IDENT";
	case ST_KEYWORD:	return "KEYWORD";
	case ST_INT:		return "INT";
	case ST_FLOAT:		return "FLOAT";
	case ST_STRING:		return "STRING";
	case ST_OPERATOR:	return "OPERATOR";
	case ST_SUBSCRIPT:	return "SUBSCRIPT";
	case ST_PUNCT:		return "PUNCT";
	}
	return "?";
}

/* ---- character classes ------------------------------------------------- */

static int
is_space(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int
is_digit(int c)
{
	return c >= '0' && c <= '9';
}

static int
is_ident_start(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident_body(int c)
{
	return is_ident_start(c) || is_digit(c);
}

/*
 * §7.72's closed operator set — AMENDED (2026-09). An author may define an
 * operator the language already has for a type that does not support it, and
 * may not invent one. There is no character run to munch any more; the lexer
 * matches the table in lex_operator and this predicate gates it.
 */
static int
is_operator_char(int c)
{
	return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
	       c == '<' || c == '>' || c == '=' || c == '!' ||
	       c == '&' || c == '|' || c == '^' || c == '~' || c == '?';
}

/* ---- the lexer --------------------------------------------------------- */

void
st_lexer_init(st_lexer *lx, const char *src)
{
	lx->src = src;
	lx->pos = 0;
	lx->line = 1;
	lx->column = 1;
	lx->error = NULL;
}

static int
peek(st_lexer *lx, size_t ahead)
{
	return (unsigned char)lx->src[lx->pos + ahead];
}

static int
at_end(st_lexer *lx)
{
	return lx->src[lx->pos] == '\0';
}

static int
advance(st_lexer *lx)
{
	int c;

	if (at_end(lx)) {
		return 0;
	}
	c = (unsigned char)lx->src[lx->pos++];
	if (c == '\n') {
		lx->line++;
		lx->column = 1;
	} else {
		lx->column++;
	}
	return c;
}

/*
 * Skip whitespace and comments. A block comment that never closes is an
 * error the caller reports; the lexer consumes to the end and says so.
 */
static void
skip_trivia(st_lexer *lx)
{
	for (;;) {
		if (is_space(peek(lx, 0))) {
			advance(lx);
			continue;
		}
		if (peek(lx, 0) == '/' && peek(lx, 1) == '/') {
			while (!at_end(lx) && peek(lx, 0) != '\n') {
				advance(lx);
			}
			continue;
		}
		if (peek(lx, 0) == '/' && peek(lx, 1) == '*') {
			advance(lx);
			advance(lx);
			while (!at_end(lx) &&
			       !(peek(lx, 0) == '*' && peek(lx, 1) == '/')) {
				advance(lx);
			}
			if (at_end(lx)) {
				lx->error = "unterminated block comment";
				return;
			}
			advance(lx);
			advance(lx);
			continue;
		}
		return;
	}
}

static st_token
make(st_lexer *lx, st_token_kind kind, size_t start, int line, int column,
     size_t len)
{
	st_token tok;

	tok.kind = kind;
	tok.start = lx->src + start;
	tok.len = len;
	tok.line = line;
	tok.column = column;
	tok.text = NULL;
	return tok;
}

static st_token
make_error(st_lexer *lx, const char *message, int line, int column)
{
	st_token tok;

	tok.kind = ST_ERROR;
	tok.start = NULL;
	tok.len = 0;
	tok.line = line;
	tok.column = column;
	tok.text = message;
	return tok;
}

static st_token
lex_ident(st_lexer *lx, size_t start, int line, int column)
{
	while (is_ident_body(peek(lx, 0))) {
		advance(lx);
	}
	if (st_is_keyword(lx->src + start, lx->pos - start)) {
		return make(lx, ST_KEYWORD, start, line, column, lx->pos - start);
	}
	return make(lx, ST_IDENT, start, line, column, lx->pos - start);
}

static st_token
lex_number(st_lexer *lx, size_t start, int line, int column)
{
	int is_float = 0;

	while (is_digit(peek(lx, 0))) {
		advance(lx);
	}
	/*
	 * A fractional part counts only when a digit follows the dot, so
	 * `1...45` stays an integer followed by a range rather than a number
	 * with a strange tail (§7.65).
	 */
	if (peek(lx, 0) == '.' && is_digit(peek(lx, 1))) {
		is_float = 1;
		advance(lx);
		while (is_digit(peek(lx, 0))) {
			advance(lx);
		}
	}
	if (peek(lx, 0) == 'e' || peek(lx, 0) == 'E') {
		size_t save = lx->pos;
		int save_line = lx->line;
		int save_col = lx->column;

		advance(lx);
		if (peek(lx, 0) == '+' || peek(lx, 0) == '-') {
			advance(lx);
		}
		if (is_digit(peek(lx, 0))) {
			is_float = 1;
			while (is_digit(peek(lx, 0))) {
				advance(lx);
			}
		} else {
			lx->pos = save;
			lx->line = save_line;
			lx->column = save_col;
		}
	}
	return make(lx, is_float ? ST_FLOAT : ST_INT, start, line, column,
		    lx->pos - start);
}

static st_token
lex_string(st_lexer *lx, size_t start, int line, int column)
{
	advance(lx);				/* the opening quote */
	while (!at_end(lx) && peek(lx, 0) != '"') {
		if (peek(lx, 0) == '\\' && peek(lx, 1) != '\0') {
			advance(lx);		/* the backslash */
		}
		if (peek(lx, 0) == '\n') {
			return make_error(lx, "unterminated string", line, column);
		}
		advance(lx);
	}
	if (at_end(lx)) {
		return make_error(lx, "unterminated string", line, column);
	}
	advance(lx);				/* the closing quote */
	return make(lx, ST_STRING, start, line, column, lx->pos - start);
}

/*
 * §7.22's binary operators, longest match first, plus the two range patterns
 * §7.65 lexes by shape. With §7.72's set closed there is nothing else to
 * recognise — which is what removes the old ambiguity: `>:` and `?>` can no
 * longer form, because neither is an operator.
 */
/*
 * The complete set of two-character operators. It was very nearly one short:
 * `->` is not in §7.22's precedence table because it is not a binary operator,
 * but it *is* a token, and leaving it out of a formerly general run-munch broke
 * every file that declares a function type. The corpus caught it in one run.
 */
static const char *const two_char_operators[] = {
	"->", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "??",
};

static st_token
lex_operator(st_lexer *lx, size_t start, int line, int column)
{
	size_t i;

	/* `...` and `..<`, the only three-character forms. */
	if (peek(lx, 0) == '.' && peek(lx, 1) == '.' &&
	    (peek(lx, 2) == '.' || peek(lx, 2) == '<')) {
		advance(lx); advance(lx); advance(lx);
		return make(lx, ST_OPERATOR, start, line, column, 3);
	}
	for (i = 0; i < sizeof(two_char_operators) / sizeof(two_char_operators[0]); i++) {
		if (peek(lx, 0) == two_char_operators[i][0] &&
		    peek(lx, 1) == two_char_operators[i][1]) {
			advance(lx);
			advance(lx);
			return make(lx, ST_OPERATOR, start, line, column, 2);
		}
	}
	advance(lx);
	return make(lx, ST_OPERATOR, start, line, column, 1);
}

/*
 * `[]` is an operator in a declaration and punctuation elsewhere. The
 * declaration position is the one after the keyword `operator`, which is the
 * only lookback this lexer needs — and it is the whole of §7.72's rule.
 */
static int
after_operator_keyword(const st_lexer *lx, size_t token_start)
{
	size_t i = token_start;

	while (i > 0 && is_space(lx->src[i - 1])) {
		i--;
	}
	/* "operator" is eight characters, not seven. */
	if (i < 8) {
		return 0;
	}
	return memcmp(lx->src + i - 8, "operator", 8) == 0 &&
	       (i == 8 || !is_ident_body((unsigned char)lx->src[i - 9]));
}

st_token
st_lexer_next(st_lexer *lx)
{
	size_t start;
	int line, column;
	int c;

	if (lx->error != NULL) {
		return make_error(lx, lx->error, lx->line, lx->column);
	}

	skip_trivia(lx);
	if (lx->error != NULL) {
		return make_error(lx, lx->error, lx->line, lx->column);
	}
	if (at_end(lx)) {
		return make(lx, ST_EOF, lx->pos, lx->line, lx->column, 0);
	}

	start = lx->pos;
	line = lx->line;
	column = lx->column;
	c = peek(lx, 0);

	if (is_ident_start(c)) {
		return lex_ident(lx, start, line, column);
	}
	if (is_digit(c)) {
		return lex_number(lx, start, line, column);
	}
	if (c == '"') {
		return lex_string(lx, start, line, column);
	}
	/*
	 * §9.19: `'a'` is a Character — a one-byte literal — while a multi-byte
	 * character literal is a String and `''` is a String holding only the
	 * terminating NUL. The scan is a string's with a different closing quote;
	 * telling the three cases apart is a later concern, which is why this
	 * reuses ST_STRING for now.
	 */
	if (c == '\'') {
		advance(lx);
		while (!at_end(lx) && peek(lx, 0) != '\'') {
			if (peek(lx, 0) == '\\' && peek(lx, 1) != '\0') {
				advance(lx);
			}
			if (peek(lx, 0) == '\n') {
				return make_error(lx, "unterminated character literal",
						  line, column);
			}
			advance(lx);
		}
		if (at_end(lx)) {
			return make_error(lx, "unterminated character literal",
					  line, column);
		}
		advance(lx);
		return make(lx, ST_STRING, start, line, column, lx->pos - start);
	}

	if (c == '[' && peek(lx, 1) == ']') {
		if (after_operator_keyword(lx, start)) {
			advance(lx);
			advance(lx);
			return make(lx, ST_SUBSCRIPT, start, line, column, 2);
		}
	}

	/*
	 * §7.65's range patterns begin with `.`, which is otherwise member access,
	 * so the two-dot lookahead is what routes them to lex_operator. `:` is a
	 * label separator and a lone `.` is member access; neither is an operator
	 * now that §7.72's set is closed.
	 */
	if (c == '.' && peek(lx, 1) == '.') {
		return lex_operator(lx, start, line, column);
	}
	if (is_operator_char(c)) {
		return lex_operator(lx, start, line, column);
	}
	if (c == '.' || c == ':') {
		advance(lx);
		return make(lx, ST_PUNCT, start, line, column, 1);
	}

	switch (c) {
	case '(': case ')': case '{': case '}': case '[': case ']':
	case ',': case ';': case '@':
		advance(lx);
		return make(lx, ST_PUNCT, start, line, column, 1);
	default:
		break;
	}

	advance(lx);
	return make_error(lx, "unexpected character", line, column);
}

/* ---- the dump used by the driver and the golden check ------------------ */

void
st_dump(const char *src)
{
	st_lexer lx;
	st_token tok;

	st_lexer_init(&lx, src);
	for (;;) {
		tok = st_lexer_next(&lx);
		if (tok.kind == ST_EOF) {
			printf("%s\n", st_token_kind_name(tok.kind));
			break;
		}
		if (tok.kind == ST_ERROR) {
			printf("ERROR %s at %d:%d\n", tok.text, tok.line, tok.column);
			break;
		}
		printf("%-9s %d:%-3d %.*s\n", st_token_kind_name(tok.kind),
		       tok.line, tok.column, (int)tok.len, tok.start);
	}
}
