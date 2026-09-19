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

/*
 * §7.26/§7.63's `<…>` list — the same syntax whether it is a *type's* argument
 * list (`Array<String>`) or a *class's* parameter list (`class Box<T>`). One
 * implementation, because the two callers had copies of it and those copies had
 * already diverged in purpose: parse_type recorded the names while parse_class
 * only counted them, behind a comment claiming the two were duplicates.
 *
 * The counting is **character-wise, not token-wise**. §7.72's longest-run rule
 * makes `>>` a single token and `?>` a single token too, since `?` sits in the
 * same operator set — so testing a token's *first* character missed `Int32?>`'s
 * closing bracket and reported the list unclosed.
 *
 * Only the **outermost** name per position is recorded: §7.26's rules are about
 * what an argument *is*, and `Array<Box<String>>`'s argument is `Box` either way.
 *
 * The outputs are initialised **here** rather than at the call sites, because a
 * caller passing an uninitialised struct and having its garbage count read is a
 * segfault with no message — which is exactly what happened once already.
 *
 * Returns 1 on success, with *count left at 0 when there is no list at all.
 */
static int
parse_generic_list(st_parser *p, st_name **out, size_t *count)
{
	int depth = 1;
	int want_name = 1;
	size_t cap = 0;

	*out = NULL;
	*count = 0;
	if (p->tok.kind != ST_OPERATOR || p->tok.len != 1 ||
	    p->tok.start[0] != '<') {
		return 1;		/* no list, which is not an error */
	}
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
			/* Past the first `<` we are inside a nested argument. */
			want_name = 0;
		} else if (depth == 1 && want_name && p->tok.kind == ST_IDENT) {
			st_name name;

			if (!take_name(p, &name)) {
				return 0;
			}
			if (*count == cap) {
				size_t want = cap == 0 ? 4 : cap * 2;
				st_name *grown =
					st_arena_alloc(want * sizeof(st_name));

				if (grown == NULL) {
					return fail(p, "out of memory");
				}
				memcpy(grown, *out, *count * sizeof(st_name));
				*out = grown;
				cap = want;
			}
			(*out)[(*count)++] = name;
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
	return 1;
}

static int
parse_type(st_parser *p, st_type *out)
{
	/*
	 * Callers pass an uninitialised `st_type` — true since long before the
	 * argument list existed, and harmless while this struct was only ever
	 * *written*. `arguments` and `argument_count` are therefore initialised by
	 * parse_generic_list, which owns them and is the only thing that appends:
	 * a caller handing a garbage count across to be *read* is a segfault with
	 * no message, which is the worst way for a compiler to fail.
	 */
	out->kind = ST_TYPE_NAMED;
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
	 * A nested type is written dotted — `Foo.Bar` — and that name IS the type:
	 * the qualification is the Sterling spelling, and the emitter mangles the
	 * dot for ObjC. So the name is assembled here and nothing downstream has
	 * to know a type was nested.
	 *
	 * The dot is ST_PUNCT here (the lexer keeps a lone `.` out of §7.72's
	 * operator set), which is why this is a parse-level continuation rather
	 * than an operator.
	 */
	while (at_punct(p, '.')) {
		st_name part;

		bump(p);
		if (!take_name(p, &part)) {
			return 0;
		}
		{
			const char *outer = out->name.text;
			size_t olen = strlen(outer);
			size_t plen = strlen(part.text);
			char *joined = st_arena_alloc(olen + plen + 2);

			if (joined == NULL) {
				return fail(p, "out of memory");
			}
			memcpy(joined, outer, olen);
			joined[olen] = '.';
			memcpy(joined + olen + 1, part.text, plen + 1);
			out->name.text = joined;
		}
	}
	/*
	 * §7.26/§7.63's argument list — and what decides whether this declaration
	 * erases or is instantiated, which is the emitter's business. The scan
	 * itself is parse_generic_list's, shared with a class's parameter list;
	 * see its comment for the character-wise counting and for why it owns the
	 * initialisation of these two fields.
	 */
	if (!parse_generic_list(p, &out->arguments, &out->argument_count)) {
		return 0;
	}
	/*
	 * §4/§7.62: `?` makes the type nullable — a class becomes a nullable
	 * pointer, and a scalar or struct takes §7.62's pair-struct.
	 *
	 * RECORDED now, not consumed and dropped. Dropping it was not neutral:
	 * the generated header is wrapped in `_Pragma("clang assume_nonnull
	 * begin")`, so `String?` emitted `NSString *` *inside* that region —
	 * which asserts non-null, the opposite of what was written.
	 */
	if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
	    p->tok.start[0] == '?') {
		out->nullable = 1;
		bump(p);
	}
	return 1;
}

/* ---- expressions ------------------------------------------------------- */

static int parse_expr(st_parser *p, st_expr **out);

/*
 * §7.33's closure body and §7.24's case body are both *statement lists*, and
 * both are parsed from functions defined above this one — a closure inside a
 * primary, a case inside parse_stmt. The declaration belongs here rather than
 * the definition being moved: both callers are earlier in the file for good
 * reason, since a closure *is* a primary and a case *is* a statement.
 */
static int parse_stmt_block(st_parser *p, st_stmt **out);

/*
 * A type declared inside a class and the parse of a class are mutually
 * recursive: parse_class's body loop hands a nested `class` to
 * parse_nested_type, which parses it with parse_class again, qualified by the
 * enclosing name.
 */
static int parse_class(st_parser *p, st_class **out, const char *prefix);
static int parse_nested_type(st_parser *p, st_class *outer);

/*
 * The two type kinds a nested declaration can also be, and that parse_nested_type
 * only CONSUMES: neither has an emission anywhere yet, so the declaration is read
 * — nothing goes unchecked — and counted, for the emitter to refuse by name.
 */
static int parse_struct(st_parser *p);
static int parse_enum(st_parser *p);

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

/*
 * Every postfix continuation a primary may carry. **One** implementation, called
 * by both loops in parse_primary — the loop under `case ST_IDENT` and the loop
 * after that function's switch — because two copies of this have diverged three
 * times: once for `[`, once for the closure body, and once when a conversion
 * dropped the `at_punct(p, '{')` guard that makes a *trailing* closure optional.
 * Each divergence was invisible until a corpus file exercised the copy nobody
 * had edited, which is the whole argument for there being one copy.
 *
 * Returns 1 when a continuation was consumed (the caller calls again), 0 when
 * none applies (the caller breaks), and sets *ok to 0 on a malformed form so the
 * caller can fail with the message fail() has already stored.
 */
static int
parse_postfix(st_parser *p, st_expr *e, int *ok)
{
	/*
	 * §6's member access. It is NOT a message send: §7.60 **withdrew** the
	 * parens-omission rule (2026-09), so `o.foo` is a member and only
	 * `o.foo()` is a send.
	 *
	 * The name used to be dropped — the AST had no member node — so
	 * `self.reset` parsed as `self` and the member was invisible to
	 * everything downstream.
	 */
	if (at_punct(p, '.')) {
		st_expr *receiver = st_arena_alloc(sizeof(st_expr));
		st_name member;

		if (receiver == NULL) {
			fail(p, "out of memory");
			*ok = 0;
			return 1;
		}
		*receiver = *e;
		bump(p);
		if (!take_name(p, &member)) {
			*ok = 0;
			return 1;
		}
		e->kind = ST_EXPR_MEMBER;
		e->base = receiver;
		e->text = member;
		e->arg_count = 0;
		return 1;
	}
	/*
	 * §4's `x!`. A **postfix** form, not a prefix one — `!` is also
	 * logical-not — and position is what tells them apart: this only runs once
	 * a complete primary has been consumed.
	 *
	 * §6's *trap* is what `!` means, and it belongs to the optional-flows
	 * slice. Emitting the operand alone would drop the trap and the result
	 * would still compile — a silent wrong answer — so the node is refused
	 * instead.
	 */
	if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
	    p->tok.start[0] == '!') {
		st_expr *operand = st_arena_alloc(sizeof(st_expr));

		if (operand == NULL) {
			fail(p, "out of memory");
			*ok = 0;
			return 1;
		}
		*operand = *e;
		bump(p);
		e->kind = ST_EXPR_UNSUPPORTED;
		e->base = operand;
		e->text.text = st_arena_strdup("x!", 2);
		return 1;
	}
	/*
	 * §7.46: `x as? T` and `x as! T` — the only conversions, since `T(x)`
	 * *constructs* rather than converts. `as` is a keyword and the `?` or `!`
	 * after it decides whether a failure is a nil or a trap, so both are
	 * written. It is the one postfix form followed by a **type** rather than an
	 * expression.
	 *
	 * A conversion is `isKindOfClass:` plus a cast (§7.44) and has no emission
	 * here yet; dropping it would emit the operand with the conversion gone,
	 * so it is refused.
	 */
	if (at_keyword(p, "as")) {
		st_type target;
		st_expr *operand = st_arena_alloc(sizeof(st_expr));

		if (operand == NULL) {
			fail(p, "out of memory");
			*ok = 0;
			return 1;
		}
		*operand = *e;
		bump(p);
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    (p->tok.start[0] == '?' || p->tok.start[0] == '!')) {
			bump(p);
		} else {
			fail(p, "expected `?` or `!` after `as`");
			*ok = 0;
			return 1;
		}
		if (!parse_type(p, &target)) {
			*ok = 0;
			return 1;
		}
		e->kind = ST_EXPR_UNSUPPORTED;
		e->base = operand;
		e->text.text = st_arena_strdup("x as", 4);
		return 1;
	}
	/*
	 * §7.72's `[]` in a *read* position — `self.slots[i]`, or `name[i]`. The
	 * declaration form belongs to parse_decl; this is the use. The subscript is
	 * the member-only operator pair `[]`/`[]=` and lowers to ObjC's
	 * `objectAtIndexedSubscript:`, which is its own piece of work — so the
	 * index is parsed (nothing goes unread) and the node refused.
	 */
	if (at_punct(p, '[')) {
		st_expr *index;
		st_expr *container = st_arena_alloc(sizeof(st_expr));

		if (container == NULL) {
			fail(p, "out of memory");
			*ok = 0;
			return 1;
		}
		*container = *e;
		bump(p);
		if (!parse_expr(p, &index)) {
			*ok = 0;
			return 1;
		}
		if (!expect_punct(p, ']')) {
			*ok = 0;
			return 1;
		}
		e->kind = ST_EXPR_UNSUPPORTED;
		e->base = container;
		e->text.text = st_arena_strdup("subscript", 9);
		return 1;
	}
	/*
	 * §6's message send, and §7.42's bare call. The distinction is exactly
	 * whether a `.member` was just read: `receiver.sel(label: arg)` emits
	 * `[receiver sel:arg]`, while a bare `name(...)` is a C function when no
	 * method matches and a message to `self` when one does — §2's specimen is
	 * the first, and it is why the two nodes are not one.
	 */
	if (at_punct(p, '(')) {
		if (e->kind == ST_EXPR_MEMBER) {
			/* `e` already holds the receiver and the method name. */
			e->kind = ST_EXPR_SEND;
			e->arg_count = 0;
			e->args = NULL;
		} else {
			st_expr *callee = st_arena_alloc(sizeof(st_expr));

			if (callee == NULL) {
				fail(p, "out of memory");
				*ok = 0;
				return 1;
			}
			*callee = *e;
			e->kind = ST_EXPR_CALL;
			e->base = callee;
			e->arg_count = 0;
			e->args = NULL;
		}
		bump(p);
		if (!parse_args(p, e)) {
			*ok = 0;
			return 1;
		}
		/*
		 * §7.51's capture list, brace-scanned: which bindings it names changes
		 * nothing the AST holds, so there is no node to fill.
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
		/*
		 * §7.33's trailing closure: the parameter list IS the call's
		 * parentheses, so a `{` here continues the same call. The body is a
		 * statement list, and the guard is **load-bearing**: this runs after
		 * every primary, so an unguarded call would demand a `{` after each
		 * one and break `self.g()`.
		 *
		 * Blocks are the closure slice's, and the node that used to stand in
		 * for one emitted `nil` — a wrong answer that compiled. It is refused
		 * by name now.
		 */
		if (at_punct(p, '{')) {
			st_expr *closure = st_arena_alloc(sizeof(st_expr));

			if (closure == NULL) {
				fail(p, "out of memory");
				*ok = 0;
				return 1;
			}
			*closure = *e;
			if (!parse_stmt_block(p, NULL)) {
				*ok = 0;
				return 1;
			}
			e->kind = ST_EXPR_UNSUPPORTED;
			e->base = closure;
			e->text.text = st_arena_strdup("closure", 7);
		}
		return 1;
	}
	return 0;
}

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
			st_token symbol = p->tok;

			bump(p);
			if (!parse_primary(p, &operand)) {
				return 0;
			}
			/*
			 * The symbol is recorded: `-x` and `!x` are different
			 * expressions, and the placeholder this replaces kept only
			 * the operand, which made every prefix form the same node.
			 */
			e->kind = ST_EXPR_UNARY;
			e->text.text = st_arena_strdup(symbol.start, symbol.len);
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
	 * The node used to be ST_EXPR_NIL, which the emitter printed as `nil` — a
	 * wrong answer that compiles. It is refused by name now: the closure slice
	 * owns the emission, and until it lands a program using a block stops.
	 */
	if (at_punct(p, '{')) {
		e->kind = ST_EXPR_UNSUPPORTED;
		e->text.text = st_arena_strdup("closure", 7);
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
		/*
		 * §3's map: `super` is a receiver like `self` — `super.foo()` emits
		 * `[super foo]`. It is a KEYWORD, not an identifier, so without
		 * this branch it fell through to "expected an expression" and the
		 * spelling simply did not exist.
		 */
		if (at_keyword(p, "super")) {
			e->kind = ST_EXPR_SUPER;
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
			int ok = 1;

			if (!parse_postfix(p, e, &ok)) {
				break;
			}
			if (!ok) {
				return 0;
			}
		}
		break;
	case ST_PUNCT:
		if (at_punct(p, '.')) {
			/*
			 * §7.35's enum-case spelling: `.idle` and
			 * `.circle(radius: 1.0)`. A leading dot names a case of the
			 * type in hand rather than a member of an expression, so the
			 * name becomes the node's text and any argument list is
			 * parsed as an ordinary call.
			 *
			 * Enums are not emitted at all yet — neither the plain kind
			 * nor the tagged union — and §5 prefixes the qualified form
			 * (`MyEnumType_valueOne`) in a way a bare name here cannot
			 * know. Emitting `idle` would be a wrong answer that
			 * compiles, so the node is refused.
			 */
			st_name case_name;
			st_expr *inner = st_arena_alloc(sizeof(st_expr));

			if (inner == NULL) {
				return fail(p, "out of memory");
			}
			bump(p);
			if (!take_name(p, &case_name)) {
				return 0;
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
			}
			*inner = *e;
			e->kind = ST_EXPR_UNSUPPORTED;
			e->base = inner;
			e->text.text = st_arena_strdup("enum case", 9);
			*out = e;
			return 1;
		}
		if (at_punct(p, '(')) {
			/*
			 * The `(` goes FIRST, before the `{` test below — checking
			 * for `{` while the current token is still `(` is what made
			 * `= ({1, 2, 3})` parse as a grouping around a closure and
			 * fail at the first comma.
			 */
			bump(p);
			/*
			 * §7.64's brace list arrives parenthesised — `= ({1, 2, 3})`
			 * — and a brace list has no expression form (§6 has none),
			 * so it is scanned to its matching close and the node
			 * refused rather than printed as `nil`.
			 */
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
				if (depth > 0) {
					return fail(p, "unclosed `{` in an initialiser");
				}
				if (!expect_punct(p, ')')) {
					return 0;
				}
				e->kind = ST_EXPR_UNSUPPORTED;
				e->text.text = st_arena_strdup("brace initialiser", 17);
				break;
			}
			/*
			 * Anything else in parentheses is a plain grouping, and a
			 * grouping needs NO node: §7.22's precedence is what puts
			 * the parentheses back, so `a * (b + c)` emits with them
			 * and `(a + b) * c` likewise. Replacing the node rather
			 * than wrapping it is the whole implementation.
			 */
			{
				st_expr *inner = NULL;

				if (!parse_expr(p, &inner)) {
					return 0;
				}
				if (!expect_punct(p, ')')) {
					return 0;
				}
				*e = *inner;
			}
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
		int ok = 1;

		if (!parse_postfix(p, e, &ok)) {
			break;
		}
		if (!ok) {
			return 0;
		}
	}

	*out = e;
	return 1;
}

/*
 * §7.22's precedence rule, by token — the table itself is §7.72's symbol set's
 * and lives in lexer.c (st_operator_precedence) so the emitter reads the same
 * one. This wrapper is only the token-to-symbol step.
 */
static int
binary_precedence(const st_token *tok)
{
	if (tok->kind != ST_OPERATOR) {
		return 0;
	}
	return st_operator_precedence(tok->start, tok->len);
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
		st_token op;

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
		/*
		 * The symbol is captured BEFORE the bump, because it is what the node
		 * records: an operator node without its operator is not a node. The
		 * placeholder this replaces held both operands but dropped the
		 * symbol, so `a + b` and `a - b` were the *same tree* — and no
		 * emitter could have told them apart even with a node to walk.
		 */
		op = p->tok;
		bump(p);			/* the operator */
		if (p->tok.line != operator_line) {
			break;
		}
		if (!parse_binary(p, &right, precedence + 1)) {
			return 0;
		}
		node = st_arena_alloc(sizeof(st_expr));
		if (node == NULL) {
			return fail(p, "out of memory");
		}
		node->kind = ST_EXPR_BINARY;
		node->base = left;
		node->text.text = st_arena_strdup(op.start, op.len);
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
 * §7.67/§7.68's condition list: comma-separated items, each a `let`/`var`
 * binding (§6's nullable forms) or a plain expression.
 *
 * Returns the *shape*, because that is what the two callers need to know:
 *
 *   1  exactly one plain expression — the only shape this emitter can write
 *   2  a binding, or more than one item — the optional-flows slice's, since a
 *      binding needs §6's rename map and its numbered temps
 *   0  a parse error
 *
 * `*out` holds the single expression on 1 and is untouched on 2; `out` may be
 * NULL when only the parse matters.
 */
static int
parse_cond_list(st_parser *p, st_expr **out)
{
	int shape = 1;

	for (;;) {
		if (at_keyword(p, "let") || at_keyword(p, "var")) {
			st_name bound;
			st_type bound_type;
			st_expr *value;

			/* A binding: §6's `if let x = y` family. */
			shape = 2;
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
			st_expr *condition = NULL;

			if (!parse_expr(p, &condition)) {
				return 0;
			}
			if (out != NULL) {
				*out = condition;
			}
		}
		if (at_punct(p, ',')) {
			/* A second item is already one more than this emitter writes. */
			shape = 2;
			bump(p);
			continue;
		}
		return shape;
	}
}

/*
 * A braced statement body — what `if` (§7.67), `while` (§7.69) and a closure
 * (§7.33) all take. `out` receives the statement list; it may be NULL when only
 * the parse matters, because a construct this emitter refuses BY NAME still has
 * its body read on the way to the refusal.
 *
 * This absorbed parse_body, which was the same loop with a non-NULL `out`. Two
 * copies of one statement loop is not a tidiness question here: it is how the
 * closure body came to be brace-scanned while a method body was not.
 */
static int
parse_stmt_block(st_parser *p, st_stmt **out)
{
	st_stmt *head = NULL;
	st_stmt **tail = &head;

	if (!expect_punct(p, '{')) {
		return 0;
	}
	while (!at_punct(p, '}') && p->tok.kind != ST_EOF) {
		st_stmt *inner = NULL;

		if (!parse_stmt(p, &inner)) {
			return 0;
		}
		*tail = inner;
		tail = &inner->next;
	}
	if (out != NULL) {
		*out = head;
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
		 * two words.
		 *
		 * The condition must be ONE plain expression. §6's binding forms
		 * (`if let x = y`) are items of the same list, and they need the
		 * rename map and numbered temps the optional-flows slice owns — so
		 * a list that used one is refused BY NAME rather than
		 * half-emitted. The whole chain is still parsed either way.
		 */
		int shape;

		s->kind = ST_STMT_IF;
		bump(p);
		shape = parse_cond_list(p, &s->value);
		if (shape == 0) {
			return 0;
		}
		if (!parse_stmt_block(p, &s->body)) {
			return 0;
		}
		while (at_keyword(p, "else")) {
			bump(p);
			s->has_else = 1;
			if (at_keyword(p, "if")) {
				/*
				 * `else if` is two words, so the rest of the chain is
				 * parsed as the else branch. That is what makes an
				 * arbitrarily long chain one recursive call rather
				 * than a second loop that has to keep in step with
				 * the first.
				 */
				if (!parse_stmt(p, &s->else_body)) {
					return 0;
				}
				break;
			}
			if (!parse_stmt_block(p, &s->else_body)) {
				return 0;
			}
		}
		if (shape != 1) {
			s->kind = ST_STMT_UNSUPPORTED;
			s->text.text = st_arena_strdup("if-binding", 10);
		}
	} else if (at_keyword(p, "guard")) {
		/*
		 * §7.68: the same condition list, then a mandatory `else` and a
		 * block. The language requires that block to `return` on every
		 * path — §6 states the rule, and it is a diagnostic rather than an
		 * emission rule, so nothing is enforced here yet.
		 *
		 * Refused by name: every `guard` worth writing is a binding form
		 * (§6's `guard let x = y else { return }`), so there is no shape
		 * left to emit before the rename map exists.
		 */
		s->kind = ST_STMT_UNSUPPORTED;
		s->text.text = st_arena_strdup("guard", 5);
		bump(p);
		if (parse_cond_list(p, NULL) == 0) {
			return 0;
		}
		if (!expect_keyword(p, "else")) {
			return 0;
		}
		if (!parse_stmt_block(p, NULL)) {
			return 0;
		}
	} else if (at_keyword(p, "while")) {
		/*
		 * §7.69: the same condition list and block as `if`, with no `else`
		 * and no `do while`. `true` is a Bool, so a loop that must run its
		 * body at least once is written `while true` with a `break`.
		 */
		int shape;

		s->kind = ST_STMT_WHILE;
		bump(p);
		shape = parse_cond_list(p, &s->value);
		if (shape == 0) {
			return 0;
		}
		if (!parse_stmt_block(p, &s->body)) {
			return 0;
		}
		if (shape != 1) {
			s->kind = ST_STMT_UNSUPPORTED;
			s->text.text = st_arena_strdup("while-binding", 13);
		}
	} else if (at_keyword(p, "break") || at_keyword(p, "continue")) {
		/*
		 * §7.71 lists these among the ways a scope can be left. There is no
		 * node for a jump statement, and dropping one changes a loop's
		 * *meaning* with nothing in the output to show it, so it is refused
		 * by name — the loop surfaces that own `break` must bring the node
		 * with them.
		 */
		s->kind = ST_STMT_UNSUPPORTED;
		if (at_keyword(p, "break")) {
			s->text.text = st_arena_strdup("break", 5);
		} else {
			s->text.text = st_arena_strdup("continue", 8);
		}
		bump(p);
	} else if (at_keyword(p, "for")) {
		/*
		 * §7.66: the only `for` form — `for [var|let] name in collection
		 * [where condition] { }` — with §7.73's optional filter. A `for-in`
		 * is not a C statement: it lowers to a cursor or an index, and that
		 * lowering (with the element's type) is its own piece of work, so the
		 * statement is refused by name rather than approximated.
		 */
		s->kind = ST_STMT_UNSUPPORTED;
		s->text.text = st_arena_strdup("for-in", 6);
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
		if (!parse_stmt_block(p, NULL)) {
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
		 * … default { … } }`. A plain enum's case is a C `case` label, but a
		 * tagged union's is a tag test *with the payload hoisted into the
		 * braced body*, and §7.24's no-fallthrough makes the lowering a
		 * rewrite rather than a translation — its own piece of work, as is
		 * the enum it matches. Refused by name.
		 *
		 * The case bodies are statement lists, parsed rather than
		 * brace-scanned: a scan accepts any bytes at all and reports as
		 * passing, which is the one failure mode K1 spent its whole length
		 * removing.
		 */
		st_expr *subject;

		s->kind = ST_STMT_UNSUPPORTED;
		s->text.text = st_arena_strdup("switch", 6);
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
			if (!parse_stmt_block(p, NULL)) {
				return 0;
			}
		}
		if (!expect_punct(p, '}')) {
			return 0;
		}
	} else if (at_keyword(p, "with")) {
		/*
		 * §7.70: `with target { … }` — inside the block a member name means
		 * the target's member. It is admitted because the target's type is
		 * static, so the rewrite is purely syntactic; that rewrite is name
		 * resolution's, and this compiler has no name resolution pass yet.
		 * Refused by name; the body is still a parsed statement list.
		 */
		st_expr *target;

		s->kind = ST_STMT_UNSUPPORTED;
		s->text.text = st_arena_strdup("with", 4);
		bump(p);
		if (!parse_expr(p, &target)) {
			return 0;
		}
		if (!parse_stmt_block(p, NULL)) {
			return 0;
		}
	} else if (at_keyword(p, "var") || at_keyword(p, "let")) {
		/*
		 * §5's Local. `let` emits `const` and `var` does not, and *where* the
		 * `const` goes follows the type — a scalar is `const T name`, a class
		 * type is `T * const name` — so the emitter needs the type; §7.19
		 * lets it be omitted when the initializer carries it, which is why an
		 * absent type is recorded as absent rather than guessed here.
		 */
		s->kind = at_keyword(p, "let") ? ST_STMT_LET : ST_STMT_VAR;
		bump(p);
		if (!take_name(p, &s->name)) {
			return 0;
		}
		if (at_punct(p, ':')) {
			bump(p);
			if (!parse_type(p, &s->type)) {
				return 0;
			}
		}
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			bump(p);
			if (!parse_expr(p, &s->value)) {
				return 0;
			}
		}
	} else if (at_keyword(p, "defer")) {
		/*
		 * §7.71: a deferred block belongs to its own block and runs at that
		 * block's exit, so emitting it means carrying the enclosing scopes'
		 * deferred calls onto every exit path, innermost first. That is a
		 * real lowering, not a translation, and it is refused by name here —
		 * the body is still parsed, so a mistake inside it is still caught.
		 */
		s->kind = ST_STMT_UNSUPPORTED;
		s->text.text = st_arena_strdup("defer", 5);
		bump(p);
		if (!parse_stmt_block(p, NULL)) {
			return 0;
		}
	} else {
		s->kind = ST_STMT_EXPR;
		if (!parse_expr(p, &s->value)) {
			return 0;
		}
		/*
		 * §7.21 makes assignment an expression, and `target = value` is what
		 * that usually looks like at statement level. The `=` is an OPERATOR
		 * rather than punctuation, being in §7.72's set. The node is built
		 * rather than left as the bare target — which is what makes `x = 1`
		 * come out as `x = 1` and not as the statement `x;`, an assignment
		 * that used to be scanned away at exactly this line.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			st_expr *target = s->value;
			st_expr *assigned = NULL;
			st_expr *node = st_arena_alloc(sizeof(st_expr));

			if (node == NULL) {
				return fail(p, "out of memory");
			}
			bump(p);
			if (!parse_expr(p, &assigned)) {
				return 0;
			}
			node->kind = ST_EXPR_ASSIGN;
			node->base = target;
			node->args = st_arena_alloc(sizeof(st_arg));
			if (node->args == NULL) {
				return fail(p, "out of memory");
			}
			node->args[0].value = assigned;
			node->arg_count = 1;
			s->value = node;
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

/*
 * §5 (2026-09): a protocol member is a *signature* — a body inside a protocol is
 * an error, not something ignored. That is the same reason §5's `case` is
 * required in an enum: a rule that is not enforced cannot be measured, and a
 * parser that quietly accepted a body would let the corpus look conformant while
 * carrying code that could never run. Everywhere else a method's body is
 * required, exactly as before.
 *
 * A body is the same braced statement list `if`/`while`/a closure take, so this
 * calls parse_stmt_block. parse_body used to be its own copy of that loop; two
 * copies of one statement loop is a statement loop that gets fixed once.
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
	return parse_stmt_block(p, out);
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
		st_ownership written = ST_OWN_INFER;

		if (at_keyword(p, "optional")) {
			d->is_optional = 1;
		}
		/*
		 * §7.52's ownership attribute is now RECORDED, not consumed and
		 * dropped. It used to be dropped, which meant `weak property x:
		 * Foo` parsed and emitted `(nonatomic, assign)` — the `weak` gone,
		 * and the output still compiles, so nothing said so.
		 */
		if (at_keyword(p, "strong"))	written = ST_OWN_STRONG;
		if (at_keyword(p, "weak"))	written = ST_OWN_WEAK;
		if (at_keyword(p, "unowned"))	written = ST_OWN_UNOWNED;
		if (at_keyword(p, "assign"))	written = ST_OWN_ASSIGN;
		if (at_keyword(p, "copy"))	written = ST_OWN_COPY;
		if (written != ST_OWN_INFER) {
			/*
			 * §7.52: **exactly one** ownership attribute. Two written
			 * is an error rather than last-one-wins: `weak copy` is
			 * not a refinement, it is two different ownership rules
			 * at once.
			 */
			if (d->ownership != ST_OWN_INFER) {
				return fail(p, "exactly one ownership attribute "
						"per property (§7.52)");
			}
			d->ownership = written;
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
		if (at_punct(p, '{') && !parse_stmt_block(p, &body)) {
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
		 * field may carry `= expression` — `var count: Int32 = 0`.
		 * RECORDED rather than scanned: the emission is a synthesised
		 * *defaults* method (neither an ivar nor a C struct member may
		 * carry an initializer), which is not written yet — and the
		 * emitter refuses the form rather than losing the value.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			bump(p);
			if (!parse_expr(p, &d->initial)) {
				return 0;
			}
			d->has_initial = 1;
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
		 * punctuation.
		 *
		 * RECORDED rather than dropped — this parsed into a variable named
		 * `discard`, so `readonly property x: Int32 = 3` emitted no value
		 * at all and nothing said so. §9.16 puts the emission in a
		 * synthesised defaults method, so the emitter refuses the form
		 * until that lands.
		 */
		if (p->tok.kind == ST_OPERATOR && p->tok.len == 1 &&
		    p->tok.start[0] == '=') {
			bump(p);
			if (!parse_expr(p, &d->initial)) {
				return 0;
			}
			d->has_initial = 1;
		}
		if (at_punct(p, '{')) {
			if (!parse_stmt_block(p, &d->body)) {
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

/*
 * `class` begins a nested class (`class Inner { … }`) or a class METHOD
 * (`class method foo()`), and only the next word tells them apart. One token of
 * lookahead, saved and put back — the same mechanism `parse_args` uses to tell
 * `label: value` from a positional argument.
 */
static int
class_is_method(st_parser *p)
{
	st_lexer saved_lexer = p->lx;
	st_token saved_token = p->tok;
	int is_method;

	bump(p);
	is_method = at_keyword(p, "method");
	p->lx = saved_lexer;
	p->tok = saved_token;
	return is_method;
}

static int
parse_class(st_parser *p, st_class **out, const char *prefix)
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
	 * A nested class carries its QUALIFIED name: `Bar` inside `Foo` is the
	 * Sterling type `Foo.Bar`, and that is what a `.ag` file writes and what
	 * the emitter mangles. Building it here means the rest of the compiler
	 * never has to know a type was nested — `Foo.Bar` is a name, and the
	 * mangling is lexical.
	 */
	if (prefix != NULL) {
		size_t plen = strlen(prefix);
		size_t nlen = strlen(c->name.text);
		char *qualified = st_arena_alloc(plen + nlen + 2);

		if (qualified == NULL) {
			return fail(p, "out of memory");
		}
		memcpy(qualified, prefix, plen);
		qualified[plen] = '.';
		memcpy(qualified + plen + 1, c->name.text, nlen + 1);
		c->name.text = qualified;
	}
	/*
	 * §7.26/§7.63's parameter list, `class Box<T>`. The scan is
	 * parse_generic_list's, shared with a type's argument list. This comment
	 * used to read that the two scans "are duplicates; the shared helper is a
	 * tidy waiting for a session with room for it" — true of their *shape* and
	 * false of their *purpose*: this one only counted brackets while
	 * parse_type's recorded names. Both record now, from one implementation.
	 */
	if (!parse_generic_list(p, &c->parameters, &c->parameter_count)) {
		return 0;
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

		/*
		 * A type declared inside a class. It is a declaration the body loop
		 * would otherwise hand to parse_decl, which has no case for a nested
		 * `class`/`struct`/`enum` and would fail at the keyword — which is
		 * how `class Outer { struct Inner { … } }` came to be a parse error.
		 *
		 * `class` is ambiguous with a CLASS METHOD, and telling them apart
		 * needs the next word: `class Inner { … }` is a nested type and
		 * `class method foo()` is a declaration. Testing the keyword alone
		 * sent `class method baz` to parse_nested_type, which then failed at
		 * `baz` — a regression the golden's specimen caught on the first run.
		 */
		if (at_keyword(p, "struct") || at_keyword(p, "enum") ||
		    (at_keyword(p, "class") && !class_is_method(p))) {
			if (!parse_nested_type(p, c)) {
				return 0;
			}
			continue;
		}
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
 * A type declared inside another. `Bar` inside `Foo` is the type `Foo.Bar` —
 * the qualified name is what a Sterling author writes, and the ObjC name is
 * that name with the dot mangled to `_`, computed LEXICALLY at emission, so
 * nothing here has to be resolved through a symbol table.
 *
 * A nested CLASS is recorded and emitted like any other class. A nested struct
 * or enum is not emittable at all — neither kind has an emission anywhere yet —
 * so it is counted on the outer class and the emitter refuses BY NAME, which is
 * the same treatment a top-level one gets. Dropping it instead would be the bug
 * this compiler has spent its whole length removing.
 */
static int
parse_nested_type(st_parser *p, st_class *outer)
{
	if (at_keyword(p, "struct")) {
		if (!parse_struct(p)) {
			return 0;
		}
		outer->struct_count++;
		return 1;
	}
	if (at_keyword(p, "enum")) {
		if (!parse_enum(p)) {
			return 0;
		}
		outer->enum_count++;
		return 1;
	}
	{
		st_class *inner = NULL;
		st_class **grown;
		size_t want = outer->nested_count + 1;

		if (!parse_class(p, &inner, outer->name.text)) {
			return 0;
		}
		grown = st_arena_alloc(want * sizeof(st_class *));
		if (grown == NULL) {
			return fail(p, "out of memory");
		}
		if (outer->nested_count > 0) {
			memcpy(grown, outer->nested,
			       outer->nested_count * sizeof(st_class *));
		}
		outer->nested = grown;
		outer->nested[outer->nested_count++] = inner;
		return 1;
	}
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
	return parse_stmt_block(p, &body);
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
			/*
			 * COUNTED, not dropped: the tree holds no struct, so the
			 * emitter has to be told one was here or it writes a file
			 * missing it and says nothing.
			 */
			program->struct_count++;
			continue;
		}
		if (at_keyword(&p, "enum")) {
			if (!parse_enum(&p)) {
				*error = p.error != NULL ? p.error : "parse error";
				st_arena_free();
				return NULL;
			}
			/* Counted, for the same reason as `struct` above. */
			program->enum_count++;
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
			/*
			 * Counted, not just consumed. The block itself went nowhere —
			 * a whole declaration list, read and dropped, with the parse
			 * reporting success — and the emitter refuses on the count.
			 */
			program->extension_count++;
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
		if (!parse_class(&p, &c, NULL)) {
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
