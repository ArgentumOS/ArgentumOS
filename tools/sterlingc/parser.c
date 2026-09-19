/*
 * parser.c — a recursive-descent parser for the §1 specimen.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * K1 scope, stated plainly: this parses the specimen in
 * docs/design/sterling-syntax.md §1 and rejects everything it does not know,
 * with a message naming what it wanted. It is not a general Sterling parser
 * yet — it is the smallest thing that can feed the emitter for §2's output.
 *
 * The one rule worth pointing at: §7.1 makes a method's *first* piece the
 * method name with the first parameter's external name attached, so the
 * parser keeps both names on every parameter rather than only the label.
 */
#include "ast.h"
#include "sterling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the arena --------------------------------------------------------- */

#define ST_ARENA_BLOCK (64 * 1024)

typedef struct st_block {
	struct st_block *next;
	size_t used;
	size_t size;
	char bytes[1];
} st_block;

static st_block *g_blocks;

void *
st_arena_alloc(size_t size)
{
	st_block *b = g_blocks;
	size_t aligned = (size + 7u) & ~(size_t)7u;

	if (b == NULL || b->used + aligned > b->size) {
		size_t want = ST_ARENA_BLOCK;
		st_block *fresh;

		if (aligned > want) {
			want = aligned;
		}
		fresh = malloc(sizeof(st_block) + want);
		if (fresh == NULL) {
			return NULL;
		}
		fresh->next = g_blocks;
		fresh->used = 0;
		fresh->size = want;
		g_blocks = fresh;
		b = fresh;
	}
	{
		void *p = b->bytes + b->used;
		b->used += aligned;
		memset(p, 0, aligned);
		return p;
	}
}

char *
st_arena_strdup(const char *src, size_t len)
{
	char *copy = st_arena_alloc(len + 1);

	if (copy == NULL) {
		return NULL;
	}
	memcpy(copy, src, len);
	copy[len] = '\0';
	return copy;
}

void
st_arena_free(void)
{
	st_block *b = g_blocks;

	while (b != NULL) {
		st_block *next = b->next;
		free(b);
		b = next;
	}
	g_blocks = NULL;
}

/* ---- parser state ------------------------------------------------------ */

typedef struct {
	st_lexer lx;
	st_token tok;
	const char *error;
	/*
	 * The line of the token before the current one. §7.75 ends a statement at
	 * its line, so an expression must not reach across a line break — which
	 * matters most for a custom operator at §7.72's loosest precedence.
	 * Recording it in bump() makes the rule a comparison rather than
	 * line-sensitivity inside the lexer.
	 */
	int prev_line;
	/*
	 * §5 (2026-09): a protocol carries requirements and no implementations, so a
	 * body written inside one is an error rather than something ignored. Set
	 * while a protocol's body is being read, and consulted by parse_member_body,
	 * which every member body passes through.
	 */
	int in_protocol;
} st_parser;

static void
bump(st_parser *p)
{
	p->prev_line = p->tok.line;
	p->tok = st_lexer_next(&p->lx);
}

static int
fail(st_parser *p, const char *message)
{
	/*
	 * Every diagnostic carries its position and the token that produced it.
	 * Without them a failure reads "expected punctuation" and says nothing
	 * about where — which cost a second command to locate the frontier each
	 * time the parser learned something new.
	 *
	 * The buffer is static because st_parse's callers hold the message after
	 * the arena has been freed; the parser is single-threaded, so one buffer
	 * is enough.
	 */
	static char buf[256];

	if (p->error == NULL) {
		if (p->tok.kind == ST_EOF) {
			snprintf(buf, sizeof(buf), "%s at %d:%d (end of file)",
				 message, p->tok.line, p->tok.column);
		} else {
			snprintf(buf, sizeof(buf), "%s at %d:%d near `%.*s`",
				 message, p->tok.line, p->tok.column,
				 (int)p->tok.len, p->tok.start);
		}
		p->error = buf;
	}
	return 0;
}

static int
at_keyword(st_parser *p, const char *word)
{
	return p->tok.kind == ST_KEYWORD && strlen(word) == p->tok.len &&
	       memcmp(p->tok.start, word, p->tok.len) == 0;
}

static int
at_punct(st_parser *p, char c)
{
	return p->tok.kind == ST_PUNCT && p->tok.len == 1 && p->tok.start[0] == c;
}

static int
at_ident(st_parser *p)
{
	return p->tok.kind == ST_IDENT;
}

static int
expect_punct(st_parser *p, char c)
{
	if (!at_punct(p, c)) {
		return fail(p, "expected punctuation");
	}
	bump(p);
	return 1;
}

static int
expect_keyword(st_parser *p, const char *word)
{
	if (!at_keyword(p, word)) {
		return fail(p, "expected keyword");
	}
	bump(p);
	return 1;
}

/* A name: an identifier, or one of the words the grammar also uses as data. */
static int
take_name(st_parser *p, st_name *out)
{
	if (p->tok.kind != ST_IDENT && p->tok.kind != ST_KEYWORD) {
		return fail(p, "expected a name");
	}
	out->text = st_arena_strdup(p->tok.start, p->tok.len);
	if (out->text == NULL) {
		return fail(p, "out of memory");
	}
	bump(p);
	return 1;
}

static void
name_clear(st_name *n)
{
	n->text = NULL;
}

/* ---- types ------------------------------------------------------------- */

static int
parse_type(st_parser *p, st_type *out)
{
	/*
	 * Every field this function may append to is initialised here, because
	 * callers pass an uninitialised `st_type`. That has been true since long
	 * before the argument list existed and was harmless while the struct was
	 * only ever *written* — but the generic scan now *reads* `argument_count`,
	 * and a garbage count with a garbage pointer is a segfault with no message,
	 * which is the worst way for a compiler to fail. Owning the initialisation
	 * here rather than at every call site is what keeps that from recurring.
	 */
	out->kind = ST_TYPE_NAMED;
	out->arguments = NULL;
	out->argument_count = 0;
	/*
	 * §3's block type — `(Int32) -> Int32`, and the empty form
	 * `() -> Void`. Scanned to the closing parenthesis and, when `->`
	 * follows, through the result type. The AST has no function-type node,
	 * so the result is a named type carrying the placeholder name "Block";
	 * spelling it is the emitter's step.
	 */
	if (at_punct(p, '(')) {
		int depth = 1;

		bump(p);
		while (p->tok.kind != ST_EOF && depth > 0) {
			if (at_punct(p, '(')) {
				depth++;
			} else if (at_punct(p, ')')) {
				depth--;
			}
			bump(p);
		}
		if (depth != 0) {
			return fail(p, "unclosed `(` in a type");
		}
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 2 &&
		    memcmp(p->tok.start, "->", 2) == 0) {
			st_type result;

			bump(p);
			if (!parse_type(p, &result)) {
				return 0;
			}
		}
		out->name.text = st_arena_strdup("Block", 5);
		if (out->name.text == NULL) {
			return fail(p, "out of memory");
		}
		return 1;
	}
	if (!take_name(p, &out->name)) {
		return 0;
	}
	/*
	 * §7.26/§7.63: a type may carry lightweight-generic arguments —
	 * `Array<Int32>`, `Box<String>`, and nested forms. Which argument is
	 * present is what decides whether the declaration erases or is
	 * instantiated, so the emitter acts on it; here it is balanced-scanned
	 * and dropped.
	 *
	 * `>>` closes two levels at once, because §7.72's longest-run rule
	 * makes it a single two-character operator token.
	 */
	if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
	    p->tok.start[0] == '<') {
		int depth = 1;
		int want_name = 1;
		size_t cap = 0;

		bump(p);
		while (p->tok.kind != ST_EOF && depth > 0) {
			/*
			 * Count every `<` and `>` *character* in the token, not just
			 * its first. §7.72's longest-run rule makes `>>` a single
			 * token, and — the bug this replaces — `?>`, because `?` sits
			 * in the same operator set. Testing start[0] alone therefore
			 * missed `Int32?>`'s closing bracket and reported the type
			 * argument unclosed.
			 */
			if (p->tok.kind == ST_OPERATOR) {
				size_t k;

				for (k = 0; k < p->tok.len; k++) {
					if (p->tok.start[k] == '<') {
						depth++;
					} else if (p->tok.start[k] == '>') {
						depth--;
					}
				}
				/* Past the first `<` we are inside a nested argument,
				 * and only the outermost name is recorded. */
				want_name = 0;
			} else if (depth == 1 && want_name &&
				   p->tok.kind == ST_IDENT) {
				/*
				 * §7.26's two rules are about what an argument *is* —
				 * an object type, never a C type — so one name per
				 * argument answers them. `Array<Box<String>>` records
				 * `Box`, which is the type the argument names.
				 */
				st_name argument;

				if (!take_name(p, &argument)) {
					return 0;
				}
				if (out->argument_count == cap) {
					size_t want = cap == 0 ? 4 : cap * 2;
					st_name *grown =
						st_arena_alloc(want * sizeof(st_name));

					if (grown == NULL) {
						return fail(p, "out of memory");
					}
					memcpy(grown, out->arguments,
					       out->argument_count *
						       sizeof(st_name));
					out->arguments = grown;
					cap = want;
				}
				out->arguments[out->argument_count++] = argument;
				want_name = 0;
				continue;	/* take_name bumped already */
			}
			/* At the outer level a comma begins the next argument. */
			if (depth == 1 && at_punct(p, ',')) {
				want_name = 1;
			}
			bump(p);
		}
		if (depth > 0) {
			return fail(p, "unclosed `<` in a type argument");
		}
	}
	/*
	 * §4/§7.62: `?` makes the type nullable — a class becomes a nullable
	 * pointer, and a scalar or struct takes §7.62's pair-struct. Accepted
	 * here and not yet recorded; the AST field the emitter needs belongs
	 * to the same later step as `throws`.
	 */
	if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
	    p->tok.start[0] == '?') {
		bump(p);
	}
	return 1;
}

/* ---- expressions ------------------------------------------------------- */

static int parse_expr(st_parser *p, st_expr **out);

/*
 * A call argument is `label: value`. The internal name follows the label
 * today — the two-name form arrives with the specimen that needs it.
 */
static int
parse_args(st_parser *p, st_expr *call)
{
	size_t capacity = 4;

	call->args = st_arena_alloc(capacity * sizeof(st_arg));
	if (call->args == NULL) {
		return fail(p, "out of memory");
	}
	while (!at_punct(p, ')')) {
		st_arg *arg;
		st_name label;

		name_clear(&label);
		/*
		 * An argument is `label: value`, or — for §7.38's conversion calls
		 * — a bare expression: `Float32(Int16(3))` carries no label
		 * anywhere. One token of lookahead decides which, and the lexer
		 * state and current token are both plain values, so they can be
		 * saved and put back without disturbing anything.
		 */
		if (at_ident(p) || p->tok.kind == ST_KEYWORD) {
			st_lexer saved_lexer = p->lx;
			st_token saved_token = p->tok;

			if (!take_name(p, &label)) {
				return 0;
			}
			if (at_punct(p, ':')) {
				bump(p);		/* consume the colon */
			} else {
				/* Positional: rewind and parse the whole thing. */
				p->lx = saved_lexer;
				p->tok = saved_token;
				name_clear(&label);
			}
		}
		if (call->arg_count == capacity) {
			st_arg *grown = st_arena_alloc(capacity * 2 *
						       sizeof(st_arg));
			if (grown == NULL) {
				return fail(p, "out of memory");
			}
			memcpy(grown, call->args,
			       call->arg_count * sizeof(st_arg));
			call->args = grown;
			capacity *= 2;
		}
		arg = &call->args[call->arg_count++];
		arg->external = label;
		arg->internal = label;
		if (!parse_expr(p, &arg->value)) {
			return 0;
		}
		if (at_punct(p, ',')) {
			bump(p);
			continue;
		}
		break;
	}
	return expect_punct(p, ')');
}

/*
 * parse_stmt is defined below, and a closure literal — which parse_primary
 * handles — contains a statement list. The forward declaration lives here at
 * file scope: a block-scope one would conflict with the `static` definition
 * further down.
 */
static int parse_stmt(st_parser *p, st_stmt **out);

static int
parse_primary(st_parser *p, st_expr **out)
{
	st_expr *e = st_arena_alloc(sizeof(st_expr));

	if (e == NULL) {
		return fail(p, "out of memory");
	}

	/*
	 * A prefix operator: `-1`, `!flag`, and §7.72's *custom* prefix form such
	 * as `@p`. The token alone cannot say whether a custom symbol is prefix
	 * or postfix — that follows from which parameter name its declaration
	 * used — and the parser has no declaration table yet. Treating an unknown
	 * symbol as prefix is enough for the corpus; the ambiguity is recorded
	 * rather than hidden.
	 *
	 * `?`, `:`, `.`, `=` and `??` are excluded here: they are structural or
	 * coalescing, and are handled elsewhere.
	 */
	if (p->tok.kind == ST_OPERATOR) {
		int prefix = 0;

		switch (p->tok.start[0]) {
		case '-': case '+': case '!':
			prefix = 1;
			break;
		default:
			prefix = !(p->tok.len == 2 &&
				   memcmp(p->tok.start, "??", 2) == 0) &&
				 p->tok.start[0] != '?' &&
				 p->tok.start[0] != ':' &&
				 p->tok.start[0] != '.' &&
				 p->tok.start[0] != '=';
			break;
		}
		if (prefix) {
			st_expr *operand = NULL;

			bump(p);
			if (!parse_primary(p, &operand)) {
				return 0;
			}
			e->kind = ST_EXPR_IDENT;
			e->base = operand;
			*out = e;
			return 1;
		}
	}

	/*
	 * A closure literal (§7.33). The one shape is an optional parameter list,
	 * an optional capture list, then a body. In *expression* position a `{`
	 * can only be a closure — a braced block appears after `if`, `guard`,
	 * `defer` or a method's `->`, never where a value is wanted — so no
	 * lookahead is needed after all, contrary to what I assumed when this
	 * work was queued.
	 *
	 * ST_EXPR_NIL stands in as the node, like the other scanned forms: the
	 * emitter would print `nil` for a closure, which is wrong but inert until
	 * the emitter's step closes it. Recorded rather than hidden.
	 */
	if (at_punct(p, '{')) {
		e->kind = ST_EXPR_NIL;
		bump(p);

		if (at_punct(p, '(')) {
			bump(p);
			while (!at_punct(p, ')')) {
				st_name param_name;
				st_type param_type;

				if (!take_name(p, &param_name)) {
					return 0;
				}
				if (at_punct(p, ':')) {
					bump(p);
					if (!parse_type(p, &param_type)) {
						return 0;
					}
				}
				if (at_punct(p, ',')) {
					bump(p);
					continue;
				}
				break;
			}
			if (!expect_punct(p, ')')) {
				return 0;
			}
		}
		/* §7.51's capture list, scanned to its matching bracket. */
		if (at_punct(p, '[')) {
			int depth = 1;

			bump(p);
			while (p->tok.kind != ST_EOF && depth > 0) {
				if (at_punct(p, '[')) {
					depth++;
				} else if (at_punct(p, ']')) {
					depth--;
				}
				bump(p);
			}
		}
		if (at_keyword(p, "in")) {
			bump(p);
		}
		while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
			st_stmt *inner;

			if (!parse_stmt(p, &inner)) {
				return 0;
			}
		}
		if (!expect_punct(p, '}')) {
			return 0;
		}
		*out = e;
		return 1;
	}

	switch (p->tok.kind) {
	case ST_INT:
		e->kind = ST_EXPR_INT;
		e->text.text = st_arena_strdup(p->tok.start, p->tok.len);
		bump(p);
		break;
	case ST_FLOAT:
		e->kind = ST_EXPR_FLOAT;
		e->text.text = st_arena_strdup(p->tok.start, p->tok.len);
		bump(p);
		break;
	case ST_STRING:
		e->kind = ST_EXPR_STRING;
		e->text.text = st_arena_strdup(p->tok.start, p->tok.len);
		bump(p);
		break;
	case ST_IDENT:
	case ST_KEYWORD:
		if (at_keyword(p, "nil")) {
			e->kind = ST_EXPR_NIL;
			bump(p);
			break;
		}
		if (at_keyword(p, "true")) {
			e->kind = ST_EXPR_TRUE;
			bump(p);
			break;
		}
		if (at_keyword(p, "false")) {
			e->kind = ST_EXPR_FALSE;
			bump(p);
			break;
		}
		if (at_keyword(p, "self")) {
			e->kind = ST_EXPR_SELF;
			bump(p);
			break;
		}
		if (p->tok.kind != ST_IDENT) {
			return fail(p, "expected an expression");
		}
		e->kind = ST_EXPR_IDENT;
		e->text.text = st_arena_strdup(p->tok.start, p->tok.len);
		bump(p);
		/*
		 * `name(` is a call. §7.42 resolves an unqualified call at
		 * emission time — a method here, a C function otherwise — so
		 * the parser only records the shape.
		 */
		/*
		 * Postfix continuations on a name, looped so chains build:
		 *   `.member` — member access (§6). The dot is ST_PUNCT, because
		 *               the lexer keeps a lone one out of §7.72's set.
		 *   `(args)`  — a call. §7.42 decides whether it is a method or a
		 *               C function at emission time, so the parser only
		 *               records the shape.
		 *
		 * The member name itself is dropped: the AST has no member node
		 * yet, so `self.reset` parses as `self` with the dot consumed.
		 * That is a real gap of the same kind as the scanned initialiser,
		 * and closing it belongs to the emitter's step.
		 */
		for (;;) {
			/*
			 * There is deliberately **no** `?.` here. §9.6 confirms that sending
			 * to an optional is allowed and yields a `T?` — ObjC's nil-receiver
			 * rule already makes `[nil foo]` safe — so `x.foo` on a `Foo?` is
			 * the spelling, and a chain marker would be a second way to write
			 * the same thing. A branch accepting one used to live here; it was
			 * added to satisfy a corpus file carrying Swift's spelling, which is
			 * exactly the divergence this parser exists to make visible.
			 */
			if (at_punct(p, '.')) {
				st_name member;

				bump(p);
				if (!take_name(p, &member)) {
					return 0;
				}
				continue;
			}
			/*
			 * §4's first way out of an optional: `x!`. It is a **postfix** form,
			 * not a prefix one — `!` is also logical-not — and position is what
			 * tells them apart: this loop only runs once a complete primary has
			 * been consumed, which is the force-unwrap's position. Its twin
			 * lives in the loop after this function's switch.
			 */
			if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
			    p->tok.start[0] == '!') {
				bump(p);
				continue;
			}
			/*
			 * §7.46: `x as? T` and `x as! T` — the only conversions, since
			 * `T(x)` *constructs* rather than converts. `as` is a keyword and
			 * the `?` or `!` after it decides whether a failure is a nil or a
			 * trap. It is the one postfix form followed by a **type** rather
			 * than an expression, which is why it is not a variant of the
			 * branches beside it.
			 */
			if (at_keyword(p, "as")) {
				st_type target;

				bump(p);
				if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
				    (p->tok.start[0] == '?' ||
				     p->tok.start[0] == '!')) {
					bump(p);
				} else {
					return fail(p,
						    "expected `?` or `!` after `as`");
				}
				if (!parse_type(p, &target)) {
					return 0;
				}
				continue;
			}
			/*
			 * §7.72's `[]` in a *read* position. This branch was missing here
			 * while the twin loop after the switch had it, so `name[i]` — a
			 * subscript on an identifier rather than on `self` — did not parse.
			 * The corpus never wrote one, which is why comparing the two copies
			 * found it and running them did not.
			 */
			if (at_punct(p, '[')) {
				st_expr *index;

				bump(p);
				if (!parse_expr(p, &index)) {
					return 0;
				}
				if (!expect_punct(p, ']')) {
					return 0;
				}
				continue;
			}
			/*
			 * §7.33's closure notation in trailing position:
			 * `items.filter (item: String) { … }`. The parameter list *is*
			 * the call's parentheses, so a `[` or `{` after them continues
			 * the closure rather than starting something new. The earlier
			 * note here said a trailing closure was deliberately refused;
			 * that was written when a bare `{` was thought to begin one, and
			 * §7.33 has since settled that the parameter list is part of the
			 * notation — so a bare brace is never a closure and the `for`'s
			 * body can no longer be swallowed.
			 *
			 * The body is braces-scanned for now. Factoring the closure
			 * parser into a helper this and the inline case both call is
			 * queued work, not an assumption that it is done.
			 */
			if (at_punct(p, '(')) {
				st_expr *callee = st_arena_alloc(sizeof(st_expr));
				if (callee == NULL) {
					return fail(p, "out of memory");
				}
				*callee = *e;
				e->kind = ST_EXPR_CALL;
				e->base = callee;
				bump(p);
				if (!parse_args(p, e)) {
					return 0;
				}
				if (at_punct(p, '[')) {
					int depth = 1;

					bump(p);
					while (p->tok.kind != ST_EOF && depth > 0) {
						if (at_punct(p, '[')) {
							depth++;
						} else if (at_punct(p, ']')) {
							depth--;
						}
						bump(p);
					}
				}
				if (at_punct(p, '{')) {
					int depth = 1;

					bump(p);
					while (p->tok.kind != ST_EOF && depth > 0) {
						if (at_punct(p, '{')) {
							depth++;
						} else if (at_punct(p, '}')) {
							depth--;
						}
						bump(p);
					}
					if (depth != 0) {
						return fail(p, "unclosed closure body");
					}
				}
				continue;
			}
			break;
		}
		break;
	case ST_PUNCT:
		/*
		 * §7.64's declaration initialiser arrives parenthesised:
		 * `= ({1, 2, 3})`. A brace list has no expression form — §6 has
		 * none — so the contents are scanned to the matching close and
		 * dropped; the emitter is what gives them meaning. A plain
		 * `(expr)` is not yet parsed as a grouping.
		 *
		 * ST_EXPR_NIL stands in as the placeholder node. That is a real
		 * AST gap: a scanned initialiser wants its own kind, and it is
		 * recorded here rather than hidden.
		 */
		if (at_punct(p, '.')) {
			/*
			 * §7.35's enum-case spelling: `.idle` and
			 * `.circle(radius: 1.0)`. A leading dot names a case of the
			 * type in hand rather than a member of an expression, so the
			 * name becomes the node's text and any argument list is
			 * parsed as an ordinary call.
			 */
			st_name case_name;

			bump(p);
			if (!take_name(p, &case_name)) {
				return 0;
			}
			e->kind = ST_EXPR_IDENT;
			e->text = case_name;
			if (at_punct(p, '(')) {
				st_expr *callee = st_arena_alloc(sizeof(st_expr));

				if (callee == NULL) {
					return fail(p, "out of memory");
				}
				*callee = *e;
				e->kind = ST_EXPR_CALL;
				e->base = callee;
				bump(p);
				if (!parse_args(p, e)) {
					return 0;
				}
			}
			*out = e;
			return 1;
		}
		if (at_punct(p, '(')) {
			int depth = 1;

			bump(p);
			while (p->tok.kind != ST_EOF && depth > 0) {
				if (at_punct(p, '(') || at_punct(p, '{')) {
					depth++;
				} else if (at_punct(p, ')') || at_punct(p, '}')) {
					depth--;
				}
				bump(p);
			}
			if (depth > 0) {
				return fail(p, "unclosed `(` in an initialiser");
			}
			e->kind = ST_EXPR_NIL;
			break;
		}
		return fail(p, "expected an expression");
	default:
		return fail(p, "expected an expression");
	}

	/*
	 * The same postfix loop again, here rather than only inside
	 * `case ST_IDENT`. `self` and the literals reach their nodes through
	 * switch branches that `break` straight out, so a loop in the
	 * identifier case never sees `self.reset()` — which is exactly what
	 * the corpus reported. Running it here too costs one redundant pass
	 * over a name's continuations and is correct for every primary;
	 * folding the two into one is tidy-up rather than a fix.
	 */
	for (;;) {
		if (at_punct(p, '.')) {
			st_name member;

			bump(p);
			if (!take_name(p, &member)) {
				return 0;
			}
			continue;
		}
		/*
		 * There is deliberately no `?.` in this loop either — §9.6 makes `x.foo`
		 * on a `Foo?` an ordinary send. The twin comment above has the
		 * reasoning; both branches went together, since they were added
		 * together.
		 */
		/*
		 * §4's `x!`, the force-unwrap — postfix, and the twin of the branch in
		 * the loop above. It reaches this copy whenever the receiver is `self`
		 * or a literal, since those leave the switch early.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '!') {
			bump(p);
			continue;
		}
		/*
		 * §7.72's `[]` in a *read* position — `self.slots[i]`. The
		 * declaration form belongs to parse_decl; this is the use. It cannot
		 * be confused with the closure continuation inside the `(` branch
		 * below, which follows a call's parentheses rather than a complete
		 * primary.
		 */
		if (at_punct(p, '[')) {
			st_expr *index;

			bump(p);
			if (!parse_expr(p, &index)) {
				return 0;
			}
			if (!expect_punct(p, ']')) {
				return 0;
			}
			continue;
		}
		/*
		 * §7.46's conversion form, the twin of the branch in the loop above.
		 * `as` is a keyword, and what follows is a *type* — the only postfix
		 * form here that is not followed by an expression.
		 */
		if (at_keyword(p, "as")) {
			st_type target;

			bump(p);
			if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
			    (p->tok.start[0] == '?' || p->tok.start[0] == '!')) {
				bump(p);
			} else {
				return fail(p, "expected `?` or `!` after `as`");
			}
			if (!parse_type(p, &target)) {
				return 0;
			}
			continue;
		}
		if (at_punct(p, '(')) {
			st_expr *callee = st_arena_alloc(sizeof(st_expr));

			if (callee == NULL) {
				return fail(p, "out of memory");
			}
			*callee = *e;
			e->kind = ST_EXPR_CALL;
			e->base = callee;
			bump(p);
			if (!parse_args(p, e)) {
				return 0;
			}
			/*
			 * §7.33 in trailing position — the same continuation the loop in
			 * case ST_IDENT carries. Two copies of one loop meant that fixing
			 * one of them fixed half the sends: `self.run (…) { … }` reaches
			 * *this* copy, because `self` is a keyword. Folding the two into
			 * one remains the tidy-up; until then they must agree.
			 */
			if (at_punct(p, '[')) {
				int depth = 1;

				bump(p);
				while (p->tok.kind != ST_EOF && depth > 0) {
					if (at_punct(p, '[')) {
						depth++;
					} else if (at_punct(p, ']')) {
						depth--;
					}
					bump(p);
				}
			}
			if (at_punct(p, '{')) {
				int depth = 1;

				bump(p);
				while (p->tok.kind != ST_EOF && depth > 0) {
					if (at_punct(p, '{')) {
						depth++;
					} else if (at_punct(p, '}')) {
						depth--;
					}
					bump(p);
				}
				if (depth != 0) {
					return fail(p, "unclosed closure body");
				}
			}
			continue;
		}
		break;
	}

	*out = e;
	return 1;
}

/*
 * §7.72's precedence rule, as a table: a symbol that already exists as a C
 * operator takes that operator's precedence, and any other symbol is new and
 * binds at the loosest level — which is what the fall-through to 1 says.
 *
 * Associativity is left to right, as in C for everything listed.
 */
static int
binary_precedence(const st_token *tok)
{
	if (tok->kind != ST_OPERATOR) {
		return 0;
	}
	/*
	 * §7.72's longest-run rule means a token may be one to three characters,
	 * and that length is what separates a structural character from a custom
	 * operator. A lone `.` is member access and a lone `?` an optional
	 * suffix — never binary — while `.:.` is an operator an author declared
	 * and binds at the loosest level.
	 *
	 * The same distinction separates `??` from `?` and `==` from `=`, so the
	 * two-character forms are named before the fall-through. Reading only
	 * start[0] was wrong for every one of them: `.:.` came back structural,
	 * and `==` and `!=` came back as `=` and `!` — not binary at all.
	 */
	if (tok->len == 1) {
		switch (tok->start[0]) {
		case '=': case '?': case ':': case '.': case '!':
			return 0;	/* structural, never binary */
		}
	}
	if (tok->len == 2) {
		if (memcmp(tok->start, "==", 2) == 0 ||
		    memcmp(tok->start, "!=", 2) == 0) {
			return 6;
		}
		if (memcmp(tok->start, "<=", 2) == 0 ||
		    memcmp(tok->start, ">=", 2) == 0) {
			return 7;
		}
		if (memcmp(tok->start, "<<", 2) == 0 ||
		    memcmp(tok->start, ">>", 2) == 0) {
			return 8;
		}
		if (memcmp(tok->start, "&&", 2) == 0) {
			return 2;
		}
		if (memcmp(tok->start, "||", 2) == 0 ||
		    memcmp(tok->start, "??", 2) == 0) {
			return 1;
		}
	}
	switch (tok->start[0]) {
	case '*': case '/': case '%':	return 10;
	case '+': case '-':		return 9;
	case '<': case '>':		return 7;
	case '&':			return 5;
	case '^':			return 4;
	case '|':			return 3;
	default:
		return 1;		/* a new symbol binds loosest */
	}
}

static int
parse_binary(st_parser *p, st_expr **out, int min_precedence)
{
	st_expr *left;
	st_expr *right;

	if (!parse_primary(p, &left)) {
		return 0;
	}
	for (;;) {
		int precedence = binary_precedence(&p->tok);
		int operator_line;
		st_expr *node;

		if (precedence == 0 || precedence < min_precedence) {
			break;
		}
		/*
		 * §7.75: a statement ends at its line, so both the operator and the
		 * expression it operates on must sit on the line the expression has
		 * reached. The first check stops an operator that *begins* a line from
		 * continuing the previous one; the second stops one that ends a line
		 * from reaching forward for an operand.
		 *
		 * The second is what a postfix custom operator needs: `p#` is declared
		 * with one argument, but at its use site `#` is an ordinary OPERATOR
		 * token that binary_precedence gives the loosest level, so without
		 * this the next line's first name becomes its operand.
		 */
		if (p->tok.line != p->prev_line) {
			break;
		}
		operator_line = p->tok.line;
		bump(p);			/* the operator */
		if (p->tok.line != operator_line) {
			break;
		}
		if (!parse_binary(p, &right, precedence + 1)) {
			return 0;
		}
		/*
		 * There is no operator node in the AST, so the symbol rides on a
		 * call-shaped node holding both operands — the same placeholder
		 * convention as the scanned initialiser, and the emitter's step is
		 * what gives it meaning.
		 */
		node = st_arena_alloc(sizeof(st_expr));
		if (node == NULL) {
			return fail(p, "out of memory");
		}
		node->kind = ST_EXPR_CALL;
		node->base = left;
		node->args = st_arena_alloc(sizeof(st_arg));
		if (node->args == NULL) {
			return fail(p, "out of memory");
		}
		node->args[0].value = right;
		node->arg_count = 1;
		left = node;
	}
	*out = left;
	return 1;
}

static int
parse_expr(st_parser *p, st_expr **out)
{
	return parse_binary(p, out, 1);
}

/* ---- statements -------------------------------------------------------- */

static int parse_stmt(st_parser *p, st_stmt **out);

/*
 * A condition list, as both `if` (§7.67) and `guard` (§7.68) take it. An item
 * is either a binding — §7.10's `let name = expr`, optionally with a type — or
 * a plain expression, and items are comma-separated. Scanned rather than
 * recorded: the emitter needs the conditions, and that is the same later
 * field as the other placeholders.
 */
static int
parse_cond_list(st_parser *p)
{
	for (;;) {
		if (at_keyword(p, "let") || at_keyword(p, "var")) {
			st_name bound;
			st_type bound_type;
			st_expr *value;

			bump(p);
			if (!take_name(p, &bound)) {
				return 0;
			}
			if (at_punct(p, ':')) {
				bump(p);
				if (!parse_type(p, &bound_type)) {
					return 0;
				}
			}
			if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
			    p->tok.start[0] == '=') {
				bump(p);
				if (!parse_expr(p, &value)) {
					return 0;
				}
			}
		} else {
			st_expr *condition;

			if (!parse_expr(p, &condition)) {
				return 0;
			}
		}
		if (at_punct(p, ',')) {
			bump(p);
			continue;
		}
		return 1;
	}
}

/* A braced statement body, which `if`, `guard` and `defer` all take. */
static int
parse_stmt_block(st_parser *p)
{
	if (!expect_punct(p, '{')) {
		return 0;
	}
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_stmt *inner;

		if (!parse_stmt(p, &inner)) {
			return 0;
		}
	}
	return expect_punct(p, '}');
}

static int
parse_stmt(st_parser *p, st_stmt **out)
{
	st_stmt *s = st_arena_alloc(sizeof(st_stmt));

	if (s == NULL) {
		return fail(p, "out of memory");
	}
	if (at_keyword(p, "if")) {
		/*
		 * §7.67: braces mandatory, parentheses not, `else if` written as
		 * two words. The chain is scanned rather than recorded — the
		 * emitter needs the conditions, which is the same later field.
		 */
		s->kind = ST_STMT_EXPR;
		bump(p);
		if (!parse_cond_list(p)) {
			return 0;
		}
		if (!parse_stmt_block(p)) {
			return 0;
		}
		while (at_keyword(p, "else")) {
			bump(p);
			if (at_keyword(p, "if")) {
				bump(p);
				if (!parse_cond_list(p)) {
					return 0;
				}
			}
			if (!parse_stmt_block(p)) {
				return 0;
			}
		}
	} else if (at_keyword(p, "guard")) {
		/*
		 * §7.68: the same condition list, then a mandatory `else` and a
		 * block. The language requires that block to `return` on every
		 * path — §6 states the rule, and it is a diagnostic rather than an
		 * emission rule, so nothing is enforced here yet.
		 */
		s->kind = ST_STMT_EXPR;
		bump(p);
		if (!parse_cond_list(p)) {
			return 0;
		}
		if (!expect_keyword(p, "else")) {
			return 0;
		}
		if (!parse_stmt_block(p)) {
			return 0;
		}
	} else if (at_keyword(p, "while")) {
		/*
		 * §7.69: the same condition list and block as `if`, with no `else`
		 * and no `do while`. `true` is a Bool, so a loop that must run its
		 * body at least once is written `while true` with a `break`.
		 */
		s->kind = ST_STMT_EXPR;
		bump(p);
		if (!parse_cond_list(p)) {
			return 0;
		}
		if (!parse_stmt_block(p)) {
			return 0;
		}
	} else if (at_keyword(p, "break") || at_keyword(p, "continue")) {
		/*
		 * §7.71 lists these among the ways a scope can be left. Scanned and
		 * dropped: which loop they leave is the emitter's concern, since the
		 * surface gives them no label.
		 */
		s->kind = ST_STMT_EXPR;
		bump(p);
	} else if (at_keyword(p, "for")) {
		/*
		 * §7.66: the only `for` form — `for [var|let] name in collection
		 * [where condition] { }` — with §7.73's optional filter. Nothing is
		 * recorded: the binding, the collection and the filter are fields the
		 * emitter owns, as with the other scanned statements.
		 */
		s->kind = ST_STMT_EXPR;
		bump(p);
		if (at_keyword(p, "var") || at_keyword(p, "let")) {
			bump(p);		/* the binding keyword */
		}
		bump(p);			/* the bound name */
		if (!expect_keyword(p, "in")) {
			return 0;
		}
		{
			st_expr *collection;

			if (!parse_expr(p, &collection)) {
				return 0;
			}
		}
		if (at_keyword(p, "where")) {
			st_expr *filter;

			bump(p);
			if (!parse_expr(p, &filter)) {
				return 0;
			}
		}
		if (!parse_stmt_block(p)) {
			return 0;
		}
	} else if (at_keyword(p, "return")) {
		s->kind = ST_STMT_RETURN;
		bump(p);
		/*
		 * §7.75 again: `;` separates rather than terminates, so a `return`
		 * with no value ends at the `}` that closes its block — `else {
		 * return }` is exactly that shape. The same-line rule remains
		 * unenforced; what matters here is not demanding a separator the
		 * surface does not require.
		 */
		if (!at_punct(p, ';') && !at_punct(p, '}')) {
			if (!parse_expr(p, &s->value)) {
				return 0;
			}
		}
	} else if (at_keyword(p, "switch")) {
		/*
		 * §7.24/§7.65/§7.73: `switch subject { case pattern [where cond] { … }
		 * … default { … } }`. Scanned rather than recorded — there is no case
		 * or pattern node, so a pattern is a token run terminated by `where`
		 * or the block, and the block is brace-scanned. That is the same
		 * staging as `defer` and `guard` above: what the corpus measures now
		 * is the parse, and emitting this form is a later step.
		 *
		 * `value` is deliberately left NULL so emit_body skips it rather than
		 * writing the subject out as a stray expression statement.
		 */
		st_expr *subject;
		int depth;

		s->kind = ST_STMT_EXPR;
		bump(p);
		if (!parse_expr(p, &subject)) {
			return 0;
		}
		if (!expect_punct(p, '{')) {
			return 0;
		}
		while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
			if (at_keyword(p, "case")) {
				bump(p);
				while (!at_punct(p, '{') && !at_keyword(p, "where") &&
				       p->tok.kind != ST_EOF) {
					bump(p);
				}
			} else if (at_keyword(p, "default")) {
				bump(p);
			} else {
				return fail(p, "expected `case` or `default`");
			}
			if (at_keyword(p, "where")) {
				st_expr *condition;

				bump(p);
				if (!parse_expr(p, &condition)) {
					return 0;
				}
			}
			if (!at_punct(p, '{')) {
				return fail(p, "expected a case body");
			}
			depth = 1;
			bump(p);
			while (p->tok.kind != ST_EOF && depth > 0) {
				if (at_punct(p, '{')) {
					depth++;
				} else if (at_punct(p, '}')) {
					depth--;
				}
				bump(p);
			}
			if (depth != 0) {
				return fail(p, "unclosed case body");
			}
		}
		if (!expect_punct(p, '}')) {
			return 0;
		}
	} else if (at_keyword(p, "with")) {
		/*
		 * §7.70: `with target { … }` — a member rewrite, admissible because the
		 * target is static. Same staging as a `switch` case: the body is
		 * brace-scanned and `value` stays NULL so emit_body skips it.
		 */
		st_expr *target;
		int depth;

		s->kind = ST_STMT_EXPR;
		bump(p);
		if (!parse_expr(p, &target)) {
			return 0;
		}
		if (!at_punct(p, '{')) {
			return fail(p, "expected a `with` body");
		}
		depth = 1;
		bump(p);
		while (p->tok.kind != ST_EOF && depth > 0) {
			if (at_punct(p, '{')) {
				depth++;
			} else if (at_punct(p, '}')) {
				depth--;
			}
			bump(p);
		}
		if (depth != 0) {
			return fail(p, "unclosed `with` body");
		}
	} else if (at_keyword(p, "var") || at_keyword(p, "let")) {
		/*
		 * §5's local: `var name: T = expr`, or the form with the type
		 * omitted so §9.15's inference supplies it. Scanned rather than
		 * recorded — the emitter needs the name, the type and the value,
		 * and those AST fields are a later step. What the corpus measures
		 * right now is the parse.
		 */
		st_name local;
		st_type local_type;
		st_expr *initial;

		s->kind = ST_STMT_EXPR;
		bump(p);
		if (!take_name(p, &local)) {
			return 0;
		}
		if (at_punct(p, ':')) {
			bump(p);
			if (!parse_type(p, &local_type)) {
				return 0;
			}
		}
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			bump(p);
			if (!parse_expr(p, &initial)) {
				return 0;
			}
		}
	} else if (at_keyword(p, "defer")) {
		/*
		 * §7.71: a deferred block belongs to its own block and runs at that
		 * block's exit. The body is a plain braced statement list, parsed
		 * here rather than through parse_body so that no forward
		 * declaration is needed — defer is a statement, and statements
		 * nest.
		 */
		s->kind = ST_STMT_EXPR;
		bump(p);
		if (!expect_punct(p, '{')) {
			return 0;
		}
		while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
			st_stmt *inner;

			if (!parse_stmt(p, &inner)) {
				return 0;
			}
		}
		if (!expect_punct(p, '}')) {
			return 0;
		}
	} else {
		s->kind = ST_STMT_EXPR;
		if (!parse_expr(p, &s->value)) {
			return 0;
		}
		/*
		 * §7.21 makes assignment an expression, and `target = value` is
		 * what that usually looks like at statement level. Parsed as a
		 * continuation of the target: the AST records the target, and the
		 * assigned value is a field the emitter's step adds. The `=` is an
		 * OPERATOR rather than punctuation, being in §7.72's set.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			st_expr *assigned;

			bump(p);
			if (!parse_expr(p, &assigned)) {
				return 0;
			}
		}
	}
	if (at_punct(p, ';')) {
		/*
		 * §7.75: `;` separates statements that share a line rather than
		 * terminating one, so a trailing separator is consumed and one
		 * is not required. The converse check — that two statements on
		 * one line *must* be separated — needs the previous token's
		 * line and is the next step.
		 */
		bump(p);
	}
	*out = s;
	return 1;
}

/* ---- declarations ------------------------------------------------------ */

static int
parse_body(st_parser *p, st_stmt **out)
{
	st_stmt *head = NULL;
	st_stmt **tail = &head;

	if (!expect_punct(p, '{')) {
		return 0;
	}
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_stmt *s = NULL;

		if (!parse_stmt(p, &s)) {
			return 0;
		}
		*tail = s;
		tail = &s->next;
	}
	*out = head;
	return expect_punct(p, '}');
}

/*
 * §5 (2026-09): a protocol member is a *signature* — a body inside a protocol is
 * an error, not something ignored. That is the same reason §5's `case` is
 * required in an enum: a rule that is not enforced cannot be measured, and a
 * parser that quietly accepted a body would let the corpus look conformant while
 * carrying code that could never run. Everywhere else a method's body is
 * required, exactly as before.
 */
static int
parse_member_body(st_parser *p, st_stmt **out)
{
	if (p->in_protocol) {
		if (at_punct(p, '{')) {
			return fail(p,
				    "a protocol member is a requirement and takes no body");
		}
		return 1;
	}
	return parse_body(p, out);
}

/*
 * Parameters, keeping both names. `label name: T` gives two; `name: T` gives
 * the label and uses it as the internal name too, which is the specimen's
 * form. `_` in the label position is §7.50's suppression and stays spelled `_`
 * until the emitter needs to act on it.
 */
static int
parse_params(st_parser *p, st_decl *decl)
{
	size_t capacity = 4;

	decl->params = st_arena_alloc(capacity * sizeof(st_param));
	if (decl->params == NULL) {
		return fail(p, "out of memory");
	}
	while (!at_punct(p, ')')) {
		st_param *param;
		st_name first;

		name_clear(&first);
		if (!take_name(p, &first)) {
			return 0;
		}
		if (decl->param_count == capacity) {
			st_param *grown = st_arena_alloc(capacity * 2 *
							 sizeof(st_param));
			if (grown == NULL) {
				return fail(p, "out of memory");
			}
			memcpy(grown, decl->params,
			       decl->param_count * sizeof(st_param));
			decl->params = grown;
			capacity *= 2;
		}
		param = &decl->params[decl->param_count++];
		param->external = first;
		if (at_punct(p, ':')) {
			param->internal = first;
		} else if (!take_name(p, &param->internal)) {
			return 0;
		}
		if (!expect_punct(p, ':')) {
			return 0;
		}
		if (!parse_type(p, &param->type)) {
			return 0;
		}
		/*
		 * §7.74: `= expression` gives the parameter a default. The test is
		 * deliberately not at_punct: `=` is in §7.72's operator set, so it
		 * lexes as an OPERATOR rather than punctuation — the same
		 * distinction the `?` suffix needed, and the reason this check
		 * silently never fired when written the other way.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			st_expr *discard;

			bump(p);
			if (!parse_expr(p, &discard)) {
				return 0;
			}
		}
		if (at_punct(p, ',')) {
			bump(p);
			continue;
		}
		break;
	}
	return expect_punct(p, ')');
}

static int
parse_decl(st_parser *p, st_decl **out)
{
	st_decl *d = st_arena_alloc(sizeof(st_decl));

	if (d == NULL) {
		return fail(p, "out of memory");
	}

	/*
	 * §7.57's visibility markers and §5's memory qualifiers are prefix words
	 * that may precede the declaration kind: `private property label`,
	 * `strong property held`. Consumed and dropped — what the emitter writes
	 * for each is its own step. `readonly` is merely the one that already had
	 * this treatment, and this loop generalises it.
	 *
	 * `optional` and `required` join the same list. §5 says `optional` is a
	 * prefix keyword on a protocol member, and §7.48 says `required` is the
	 * default written out loud — accepted and ignored, the tolerance §7.28 gives
	 * an explicit `let`.
	 *
	 * `optional` is the one member of this loop that is **recorded** rather than
	 * dropped, because §7.48's conformance check exempts it: a required member
	 * must be implemented by whatever claims conformance, and an optional one
	 * need not be. Everything else here is the emitter's business.
	 */
	while (at_keyword(p, "public") || at_keyword(p, "private") ||
	       at_keyword(p, "internal") || at_keyword(p, "strong") ||
	       at_keyword(p, "weak") || at_keyword(p, "unowned") ||
	       at_keyword(p, "assign") || at_keyword(p, "copy") ||
	       at_keyword(p, "optional") || at_keyword(p, "required")) {
		if (at_keyword(p, "optional")) {
			d->is_optional = 1;
		}
		bump(p);
	}

	if (at_keyword(p, "operator")) {
		/*
		 * §7.72's declaration form, inside a class. The symbol is either an
		 * operator token (`.:.`) or the lexer's bare `[]`; `[]=` is that
		 * token followed by `=`. The rest is a method's shape, the body
		 * always written in the corpus. Scanned rather than recorded —
		 * spelling the mangled name belongs to the emitter, and the
		 * placeholder name "operator" is there to say so.
		 */
		st_stmt *body;

		d->kind = ST_DECL_METHOD;
		bump(p);			/* the `operator` keyword */
		if (p->tok.kind == ST_SUBSCRIPT) {
			bump(p);
			if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
			    p->tok.start[0] == '=') {
				bump(p);		/* the `[]=` form */
			}
		} else if (p->tok.kind == ST_OPERATOR) {
			bump(p);
		} else {
			return fail(p, "expected an operator symbol");
		}
		d->name.text = st_arena_strdup("operator", 8);
		if (d->name.text == NULL) {
			return fail(p, "out of memory");
		}
		if (!expect_punct(p, '(')) {
			return 0;
		}
		if (!parse_params(p, d)) {
			return 0;
		}
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 2 &&
		    memcmp(p->tok.start, "->", 2) == 0) {
			bump(p);
			if (!parse_type(p, &d->type)) {
				return 0;
			}
		} else {
			d->type.kind = ST_TYPE_NAMED;
			d->type.name.text = st_arena_strdup("Void", 4);
			if (d->type.name.text == NULL) {
				return fail(p, "out of memory");
			}
		}
		if (at_punct(p, '{') && !parse_body(p, &body)) {
			return 0;
		}
		*out = d;
		return 1;
	}

	/*
	 * §5: a struct's state is written `var`, because a struct has no @property
	 * machinery to synthesise storage with — so the field *is* the storage, and
	 * a field declaration stands on its own. It reuses ST_DECL_PROPERTY because
	 * to an emitter a field and a synthesized property occupy the same slot;
	 * what §5 distinguishes is what gets *written* for each, which is the
	 * emitter's business rather than the parser's.
	 */
	if (at_keyword(p, "var")) {
		bump(p);
		if (!take_name(p, &d->name)) {
			return 0;
		}
		if (!expect_punct(p, ':')) {
			return 0;
		}
		if (!parse_type(p, &d->type)) {
			return 0;
		}
		/*
		 * §9.16: a stored property's default goes in the declaration, so a
		 * field may carry `= expression` — `var count: Int32 = 0`. Scanned
		 * rather than recorded: the emitter needs the initial value and the
		 * AST has no slot for it yet, which is the same staging as the other
		 * scanned forms.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			st_expr *initial;

			bump(p);
			if (!parse_expr(p, &initial)) {
				return 0;
			}
		}
		d->kind = ST_DECL_PROPERTY;
		*out = d;
		return 1;
	}

	if (at_keyword(p, "property") || at_keyword(p, "readonly")) {
		d->kind = ST_DECL_PROPERTY;
		if (at_keyword(p, "readonly")) {
			d->is_readonly = 1;
			bump(p);
		}
		if (!expect_keyword(p, "property")) {
			return 0;
		}
		if (!take_name(p, &d->name)) {
			return 0;
		}
		/*
		 * §7.64: `widths[4]` and `table[2][3]` — the brackets before the
		 * colon declare storage, and the declaration is where the size
		 * lives because a pointer cannot carry one. The lexer leaves `[`
		 * and `]` as punctuation outside a declaration, so this is
		 * ordinary parsing rather than a special case.
		 */
		while (at_punct(p, '[')) {
			st_expr *size;

			bump(p);
			if (!parse_expr(p, &size)) {
				return 0;
			}
			if (!expect_punct(p, ']')) {
				return 0;
			}
		}
		if (!expect_punct(p, ':')) {
			return 0;
		}
		if (!parse_type(p, &d->type)) {
			return 0;
		}
		/*
		 * §5's stored-property form is `property x: T = expression`. The
		 * test is not at_punct: `=` lexes as an OPERATOR (§7.72), not
		 * punctuation. Accepted and not yet recorded — the emitter is what
		 * acts on the value, via §9.16's defaults method.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			st_expr *discard;

			bump(p);
			if (!parse_expr(p, &discard)) {
				return 0;
			}
		}
		if (at_punct(p, '{')) {
			if (!parse_body(p, &d->body)) {
				return 0;
			}
		}
		/*
		 * §7.75: `;` separates statements sharing a line rather than
		 * terminating one, so a property declaration needs none and a
		 * trailing one is harmless.
		 */
		if (at_punct(p, ';')) {
			bump(p);
		}
		*out = d;
		return 1;
	}

	if (at_keyword(p, "init")) {
		/*
		 * §7.49: `init(arg: T)` — the `method` keyword and the `-> Self`
		 * return are both implied and not written, and the body writes no
		 * `return`. Parsed as a method named `init` returning Void, which is
		 * the shape the rest of the parser already handles. What makes an
		 * initializer *different* is the chained `self = Superclass()`, and
		 * that is an ordinary body statement.
		 */
		d->kind = ST_DECL_METHOD;
		d->name.text = st_arena_strdup("init", 4);
		if (d->name.text == NULL) {
			return fail(p, "out of memory");
		}
		bump(p);
		if (!expect_punct(p, '(')) {
			return 0;
		}
		if (!parse_params(p, d)) {
			return 0;
		}
		/* No `->` here: the return type is implied by the form. */
		d->type.kind = ST_TYPE_NAMED;
		d->type.name.text = st_arena_strdup("Void", 4);
		if (d->type.name.text == NULL) {
			return fail(p, "out of memory");
		}
		/* §5's protocol rule applies here too: a requirement takes no body. */
		if (!parse_member_body(p, &d->body)) {
			return 0;
		}
		*out = d;
		return 1;
	}

	if (at_keyword(p, "class") || at_keyword(p, "method")) {
		d->kind = ST_DECL_METHOD;
		if (at_keyword(p, "class")) {
			d->is_class_method = 1;
			bump(p);
			if (!expect_keyword(p, "method")) {
				return 0;
			}
		} else {
			bump(p);
		}
		if (!take_name(p, &d->name)) {
			return 0;
		}
		if (!expect_punct(p, '(')) {
			return 0;
		}
		if (!parse_params(p, d)) {
			return 0;
		}
		/*
		 * §7.6: `throws` sits between the parameter list and the return
		 * type. It is not an unwinding mechanism — the selector gains
		 * `…error:` and the caller reads the NSError — so the method's
		 * shape is unchanged. Accepted but not yet recorded in the AST;
		 * the field the emitter will need is a later step.
		 */
		if (at_keyword(p, "throws")) {
			bump(p);
		}
		/*
		 * `->` is a single operator token (§7.72), and it may be **omitted** —
		 * in which case the method returns `Void`. That is the rule the
		 * `operator` branch above already applies and the shape parse_func
		 * uses; requiring the arrow here contradicted both, and §5's own
		 * struct example writes `method moveTo(x: Float32, y: Float32) { … }`
		 * with no arrow at all.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 2 &&
		    memcmp(p->tok.start, "->", 2) == 0) {
			bump(p);
			if (!parse_type(p, &d->type)) {
				return 0;
			}
		} else {
			d->type.kind = ST_TYPE_NAMED;
			d->type.name.text = st_arena_strdup("Void", 4);
			if (d->type.name.text == NULL) {
				return fail(p, "out of memory");
			}
		}
		/* §5's protocol rule: a method in a protocol is a requirement and
		 * carries no body, so this is where a body there gets refused. */
		if (!parse_member_body(p, &d->body)) {
			return 0;
		}
		*out = d;
		return 1;
	}

	return fail(p, "expected a declaration");
}

/* ---- the file ---------------------------------------------------------- */

static int
parse_class(st_parser *p, st_class **out)
{
	st_class *c = st_arena_alloc(sizeof(st_class));
	st_decl *head = NULL;
	st_decl **tail = &head;

	if (c == NULL) {
		return fail(p, "out of memory");
	}
	if (!expect_keyword(p, "class")) {
		return 0;
	}
	if (!take_name(p, &c->name)) {
		return 0;
	}
	/*
	 * §7.26/§7.63: a class may declare lightweight-generic parameters —
	 * `class Box<T>: Object`. Scanned and dropped exactly as parse_type scans
	 * a type's arguments, including the character-wise count of `<` and `>`
	 * that §7.72's longest-run rule makes necessary (`T?>` is one token). The
	 * two scans are duplicates; the shared helper is a tidy waiting for a
	 * session with room for it.
	 */
	if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
	    p->tok.start[0] == '<') {
		int depth = 1;

		bump(p);
		while (p->tok.kind != ST_EOF && depth > 0) {
			if (p->tok.kind == ST_OPERATOR) {
				size_t k;

				for (k = 0; k < p->tok.len; k++) {
					if (p->tok.start[k] == '<') {
						depth++;
					} else if (p->tok.start[k] == '>') {
						depth--;
					}
				}
			}
			bump(p);
		}
		if (depth > 0) {
			return fail(p, "unclosed `<` in a type argument");
		}
	}
	if (!expect_punct(p, ':')) {
		return 0;
	}
	if (!take_name(p, &c->superclass)) {
		return 0;
	}
	/*
	 * §7.45: a conformance list follows the superclass, comma separated —
	 * `class Node: Object, Drawable`. **Recorded** rather than dropped: §7.48's
	 * rule that a claiming type must implement every required member is the
	 * check this list exists for, and that check is the first thing in this
	 * compiler which cannot be decided from syntax. The list grows the way every
	 * other one here does — a fresh arena block and a copy — because the count
	 * is not known until it ends.
	 */
	{
		size_t cap = 0;

		while (at_punct(p, ',')) {
			st_name conformance;

			bump(p);
			if (!take_name(p, &conformance)) {
				return 0;
			}
			if (c->conformance_count == cap) {
				size_t want = cap == 0 ? 4 : cap * 2;
				st_name *grown =
					st_arena_alloc(want * sizeof(st_name));

				if (grown == NULL) {
					return fail(p, "out of memory");
				}
				memcpy(grown, c->conformances,
				       c->conformance_count * sizeof(st_name));
				c->conformances = grown;
				cap = want;
			}
			c->conformances[c->conformance_count++] = conformance;
		}
	}
	if (!expect_punct(p, '{')) {
		return 0;
	}
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_decl *d = NULL;

		if (!parse_decl(p, &d)) {
			return 0;
		}
		*tail = d;
		tail = &d->next;
	}
	if (!expect_punct(p, '}')) {
		return 0;
	}
	c->decls = head;
	*out = c;
	return 1;
}

/*
 * §5's `struct Name: Base { … }`. The shape overlaps parse_class's heavily, and
 * the two differences are exactly the ones that matter to a parse: the
 * superclass is optional, and the body holds `var` fields rather than
 * `property` — §5 says a struct has no @property machinery to synthesise
 * storage with, so those fields *are* the storage.
 *
 * Written out rather than shared: a flag-parameterised parse_class would hide
 * all three differences to save forty lines.
 */
static int
parse_struct(st_parser *p)
{
	st_name name;
	st_name superclass;

	bump(p);				/* `struct` */
	if (!take_name(p, &name)) {
		return 0;
	}
	/*
	 * §5/§7.16: `struct Point3: Point` — the base is an anonymous member at
	 * offset 0, so this is the class colon, optionally absent.
	 */
	if (at_punct(p, ':')) {
		bump(p);
		if (!take_name(p, &superclass)) {
			return 0;
		}
		while (at_punct(p, ',')) {
			st_name conformance;

			bump(p);
			if (!take_name(p, &conformance)) {
				return 0;
			}
		}
	}
	if (!expect_punct(p, '{')) {
		return 0;
	}
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_decl *d = NULL;

		if (!parse_decl(p, &d)) {
			return 0;
		}
	}
	if (!expect_punct(p, '}')) {
		return 0;
	}
	return 1;
}

/*
 * §7.56: `func name(a: T) -> R { … }` — a bare function, callable and not bound
 * to a type. Its body is a real statement block, so this parses rather than
 * scans: what the corpus needs from the form is the parse, and a scanned body
 * would accept anything at all.
 */
static int
parse_func(st_parser *p)
{
	st_name name;
	st_type result;
	st_decl params;
	st_stmt *body;

	memset(&params, 0, sizeof(params));
	bump(p);				/* `func` */
	if (!take_name(p, &name)) {
		return 0;
	}
	if (!expect_punct(p, '(')) {
		return 0;
	}
	if (!parse_params(p, &params)) {
		return 0;
	}
	if (p->tok.kind == ST_OPERATOR && p->tok.len == 2 &&
	    memcmp(p->tok.start, "->", 2) == 0) {
		bump(p);
		if (!parse_type(p, &result)) {
			return 0;
		}
	}
	return parse_body(p, &body);
}

/*
 * §5's `enum Name: T { … }` — two kinds, and *which* kind you get is decided by
 * the members rather than by a keyword: no member carrying an associated value
 * makes a plain C enum, and one or more makes a tagged union. That distinction
 * is the emitter's; what the parser checks is the member grammar.
 *
 * Members are one per line with no separators — §5 says the line ends the
 * member exactly as it ends a statement — so each is a name, an optional
 * payload list, and an optional `= value`. No line test is needed: a payload
 * list or a value expression consumes exactly its own tokens, so the next token
 * to arrive is the next member's name.
 */
static int
parse_enum(st_parser *p)
{
	st_name name;
	st_type underlying;

	bump(p);				/* `enum` */
	if (!take_name(p, &name)) {
		return 0;
	}
	/*
	 * §5: the `: T` an enum gets in C++ and C23, so no `NS_ENUM` is needed.
	 * For a tagged union this is the *tag*'s width, not the union's.
	 */
	if (at_punct(p, ':')) {
		bump(p);
		if (!parse_type(p, &underlying)) {
			return 0;
		}
	}
	if (!expect_punct(p, '{')) {
		return 0;
	}
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_name member;

		/*
		 * §5: an enum may declare methods — "the same rule covers an enum's
		 * methods" — and 01's Shape carries one. The form is the same one a
		 * class uses, so parse_decl handles it and the member loop only has to
		 * notice it rather than read its name as a member.
		 */
		if (at_keyword(p, "method") || at_keyword(p, "class") ||
		    at_keyword(p, "init") || at_keyword(p, "property") ||
		    at_keyword(p, "readonly")) {
			st_decl *d = NULL;

			if (!parse_decl(p, &d)) {
				return 0;
			}
			continue;
		}
		/*
		 * §5 (amended 2026-09): a member is written `case name` — Swift's
		 * spelling — and the keyword is **required**. It is a marker that emits
		 * nothing, so requiring it costs nothing at emission and is what keeps
		 * the corpus honest: a bare name is now an error rather than an
		 * alternative spelling, so any file still using one reports itself.
		 */
		if (!at_keyword(p, "case")) {
			return fail(p, "expected `case` before an enum member");
		}
		bump(p);
		if (!take_name(p, &member)) {
			return 0;
		}
		/* The associated-value list of a tagged-union case: `.case(a: T, …)`.
		 * Scanned, because §5's emission is a payload struct per case and the
		 * AST has no case node to hold them. */
		if (at_punct(p, '(')) {
			int depth = 1;

			bump(p);
			while (p->tok.kind != ST_EOF && depth > 0) {
				if (at_punct(p, '(')) {
					depth++;
				} else if (at_punct(p, ')')) {
					depth--;
				}
				bump(p);
			}
			if (depth != 0) {
				return fail(p, "unclosed `(` in an associated value");
			}
		}
		/* §5: a member may pre-set its value, and C's seeding rule — an
		 * explicit value seeds the ones after it — is inherited whole. */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			st_expr *value;

			bump(p);
			if (!parse_expr(p, &value)) {
				return 0;
			}
		}
	}
	if (!expect_punct(p, '}')) {
		return 0;
	}
	return 1;
}

/*
 * §5's `protocol C: A, B { … }`. The colon list is the protocols it inherits —
 * the same syntax a class's conformance list uses — and the body holds
 * *requirements*, which is what in_protocol is for: parse_member_body refuses a
 * body inside one, so a member here is a signature by construction rather than
 * by convention.
 *
 * The name, the inherited list and the requirements are all **recorded** now,
 * because together they are the data §7.48's conformance check runs on: the
 * check resolves a class's conformance list against these. It compares
 * *selectors* rather than names, which is what lets an implementation inherited
 * from a superclass count — the selector is what the runtime dispatches on.
 */
static int
parse_protocol(st_parser *p, st_protocol **out)
{
	st_protocol *prot = st_arena_alloc(sizeof(st_protocol));
	st_decl *head = NULL;
	st_decl **tail = &head;

	if (prot == NULL) {
		return fail(p, "out of memory");
	}
	bump(p);				/* `protocol` */
	if (!take_name(p, &prot->name)) {
		return 0;
	}
	/*
	 * §7.26: **a protocol declaration takes no type parameters.** A *class* may
	 * — `class Box<T>` — but the parameter is a class feature, so `protocol
	 * P<T>` is refused here rather than parsed and ignored. This is one of the
	 * two sub-rules of §7.26 that survive its amendment, and it is *syntactic*:
	 * nothing else need be known to see that a `<` cannot go here.
	 */
	if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
	    p->tok.start[0] == '<') {
		return fail(p, "a protocol declaration takes no type parameters");
	}
	if (at_punct(p, ':')) {
		size_t cap = 0;

		bump(p);
		/*
		 * One inherited name, then another for each comma — spelled with an
		 * explicit `first` rather than folded into the loop condition, which is
		 * what this did and what made it hard to read.
		 */
		for (int first = 1; first || at_punct(p, ','); first = 0) {
			st_name inherited;

			if (!first) {
				bump(p);
			}
			if (!take_name(p, &inherited)) {
				return 0;
			}
			if (prot->inherit_count == cap) {
				size_t want = cap == 0 ? 4 : cap * 2;
				st_name *grown =
					st_arena_alloc(want * sizeof(st_name));

				if (grown == NULL) {
					return fail(p, "out of memory");
				}
				memcpy(grown, prot->inherits,
				       prot->inherit_count * sizeof(st_name));
				prot->inherits = grown;
				cap = want;
			}
			prot->inherits[prot->inherit_count++] = inherited;
		}
	}
	if (!expect_punct(p, '{')) {
		return 0;
	}
	p->in_protocol = 1;
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_decl *d = NULL;

		if (!parse_decl(p, &d)) {
			p->in_protocol = 0;
			return 0;
		}
		*tail = d;
		tail = &d->next;
	}
	p->in_protocol = 0;
	prot->requirements = head;
	if (!expect_punct(p, '}')) {
		return 0;
	}
	*out = prot;
	return 1;
}

/*
 * §5's `extension X { … }` / `extension X: P { … }` and `category X (Name)
 * { … }` / `category X (Name): P { … }` — §7.4's two forms. An extension is
 * ObjC's class extension (`@interface X ()`) and a category is
 * `@interface X (Name)`; either may carry a conformance list. The body holds
 * real declarations rather than requirements, so parse_decl reads it.
 *
 * The class name, the category name and the conformance list are read and
 * dropped for now: the emitter writes them, and recording the list is step one
 * of the conformance checker the plan carries as a milestone.
 */
static int
parse_extension(st_parser *p)
{
	st_name name;

	bump(p);				/* `extension` or `category` */
	if (!take_name(p, &name)) {
		return 0;
	}
	/* A category names itself: `category X (Name)`. */
	if (at_punct(p, '(')) {
		st_name category;

		bump(p);
		if (!take_name(p, &category)) {
			return 0;
		}
		if (!expect_punct(p, ')')) {
			return 0;
		}
	}
	if (at_punct(p, ':')) {
		bump(p);
		if (!take_name(p, &name)) {
			return 0;
		}
		while (at_punct(p, ',')) {
			bump(p);
			if (!take_name(p, &name)) {
				return 0;
			}
		}
	}
	if (!expect_punct(p, '{')) {
		return 0;
	}
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_decl *d = NULL;

		if (!parse_decl(p, &d)) {
			return 0;
		}
	}
	if (!expect_punct(p, '}')) {
		return 0;
	}
	return 1;
}

st_program *
st_parse(const char *src, const char **error)
{
	st_parser p;
	st_program *program;
	size_t capacity = 4;
	size_t proto_capacity = 4;

	memset(&p, 0, sizeof(p));
	st_lexer_init(&p.lx, src);
	bump(&p);
	if (p.tok.kind == ST_ERROR) {
		*error = p.tok.text;
		return NULL;
	}

	program = st_arena_alloc(sizeof(st_program));
	if (program == NULL) {
		*error = "out of memory";
		return NULL;
	}
	program->classes = st_arena_alloc(capacity * sizeof(st_class *));
	if (program->classes == NULL) {
		*error = "out of memory";
		return NULL;
	}
	/*
	 * §5's protocols are kept beside the classes because §7.48's conformance
	 * check resolves a class's conformance list against them, comparing
	 * *selectors* rather than names so that an implementation inherited from a
	 * superclass counts.
	 */
	program->protocols = st_arena_alloc(proto_capacity * sizeof(st_protocol *));
	if (program->protocols == NULL) {
		*error = "out of memory";
		return NULL;
	}

	while (p.tok.kind != ST_EOF) {
		st_class *c = NULL;

		if (at_keyword(&p, "import")) {
			/* `import Foundation` — recorded, not yet acted on. */
			bump(&p);
			while (p.tok.kind == ST_IDENT) {
				bump(&p);
			}
			continue;
		}
		/*
		 * Every top-level declaration kind in §5 is parsed here now, and there
		 * is no scanner left — this comment replaces one that listed these kinds
		 * as "scanned to the end of its braced body and dropped". That stopped
		 * being true one function at a time, and the comment was never corrected
		 * as it went, which is the hazard a stale comment always is.
		 *
		 * Each kind has its own function below, and each is paired with a
		 * `rejects/` case that puts nonsense inside it: a form that is *scanned*
		 * accepts any bytes at all and still reports as passing, which is how six
		 * of these once read as "9 of 9" while their contents went unread.
		 */
		if (at_keyword(&p, "func")) {
			if (!parse_func(&p)) {
				*error = p.error != NULL ? p.error : "parse error";
				st_arena_free();
				return NULL;
			}
			continue;
		}
		if (at_keyword(&p, "struct")) {
			if (!parse_struct(&p)) {
				*error = p.error != NULL ? p.error : "parse error";
				st_arena_free();
				return NULL;
			}
			continue;
		}
		if (at_keyword(&p, "enum")) {
			if (!parse_enum(&p)) {
				*error = p.error != NULL ? p.error : "parse error";
				st_arena_free();
				return NULL;
			}
			continue;
		}
		if (at_keyword(&p, "protocol")) {
			st_protocol *prot = NULL;

			if (!parse_protocol(&p, &prot)) {
				*error = p.error != NULL ? p.error : "parse error";
				st_arena_free();
				return NULL;
			}
			if (program->protocol_count == proto_capacity) {
				size_t want = proto_capacity * 2;
				st_protocol **grown =
					st_arena_alloc(want * sizeof(st_protocol *));

				if (grown == NULL) {
					*error = "out of memory";
					st_arena_free();
					return NULL;
				}
				memcpy(grown, program->protocols,
				       program->protocol_count *
					       sizeof(st_protocol *));
				program->protocols = grown;
				proto_capacity = want;
			}
			program->protocols[program->protocol_count++] = prot;
			continue;
		}
		if (at_keyword(&p, "extension") || at_keyword(&p, "category")) {
			if (!parse_extension(&p)) {
				*error = p.error != NULL ? p.error : "parse error";
				st_arena_free();
				return NULL;
			}
			continue;
		}
		/*
		 * §7.72's operator declaration at file scope — the same form parse_decl
		 * reads inside a class, and the last thing that was still being scanned.
		 * With this the scanner is gone: every top-level declaration kind in §5
		 * is parsed rather than swallowed.
		 */
		if (at_keyword(&p, "operator")) {
			st_decl *d = NULL;

			if (!parse_decl(&p, &d)) {
				*error = p.error != NULL ? p.error : "parse error";
				st_arena_free();
				return NULL;
			}
			continue;
		}
		if (!at_keyword(&p, "class")) {
			*error = "expected a declaration at file scope";
			st_arena_free();
			return NULL;
		}
		if (!parse_class(&p, &c)) {
			*error = p.error != NULL ? p.error : "parse error";
			st_arena_free();
			return NULL;
		}
		if (program->class_count == capacity) {
			st_class **grown = st_arena_alloc(capacity * 2 *
							  sizeof(st_class *));
			if (grown == NULL) {
				*error = "out of memory";
				st_arena_free();
				return NULL;
			}
			memcpy(grown, program->classes,
			       program->class_count * sizeof(st_class *));
			program->classes = grown;
			capacity *= 2;
		}
		program->classes[program->class_count++] = c;
	}

	return program;
}
