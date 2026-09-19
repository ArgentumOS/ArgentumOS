/*
 * emit.c — the Sterling emitter, targeting docs/design/sterling-syntax.md §2.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * K1 emitted §1's specimen and nothing else: the body writer handled `return`
 * and one expression shape, and *skipped* every statement it could not write.
 * A skipped statement is accepted input that silently loses meaning — the same
 * failure as a scanned declaration — so the emitter is a real tree walk now:
 * emit_expr and emit_stmt cover the forms it can write, and anything else
 * REFUSES BY NAME (see `refuse`) instead of vanishing.
 *
 * §2 is still the authority. Where this file and §2 disagree, §2 wins.
 *
 * The mappings:
 *   Void -> void        Bool -> BOOL        String -> NSString *
 *   Int64 -> int64_t    Int32 -> int32_t    Float32 -> float
 *   Object -> NSObject
 *
 * Three rules that are easy to miss and are not special cases:
 *   - A *stored* scalar property is `(nonatomic, assign)`; a *read-only
 *     computed* property is `(nonatomic, readonly)` with no ownership
 *     qualifier, because it declares no storage (§2's notes).
 *   - An unqualified call that is not a method is a C function (§7.42), so
 *     the `.m` carries an `extern` declaration for it.
 *   - §5's Local puts `const` in a place that depends on the type: a scalar is
 *     `const int32_t n`, a class type is `NSString * const s`. The naïve
 *     `const` + type is the wrong constness and clang says so.
 */
#include "ast.h"
#include "sterling.h"

#include <stdio.h>
#include <string.h>

/*
 * The output stem: `label`'s basename with its extension removed. The driver
 * uses it for the `.h`/`.m` file names and the emitter for the banner that names
 * them, so it is ONE implementation — the two disagreeing would put a header
 * called `X.h` under a banner saying `Y.h`.
 */
void
st_source_stem(const char *label, char *buf, size_t size)
{
	const char *base;
	const char *dot;
	size_t len;

	if (size == 0) {
		return;
	}
	if (label == NULL) {
		buf[0] = '\0';
		return;
	}
	base = strrchr(label, '/');
	base = (base != NULL) ? base + 1 : label;
	dot = strrchr(base, '.');
	len = (dot != NULL && dot != base) ? (size_t)(dot - base)
					   : strlen(base);
	if (len >= size) {
		len = size - 1;
	}
	memcpy(buf, base, len);
	buf[len] = '\0';
}

/*
 * The refusal message. One program is emitted at a time by a single-threaded
 * driver, so the buffer is a static and the caller reads it once before
 * exiting — see st_emit_header's contract in ast.h.
 */
static char refusal[256];

static int
refuse(const char *what, const char **error)
{
	snprintf(refusal, sizeof(refusal),
		 "sterlingc: `%s` has no emission yet", what);
	if (error != NULL) {
		*error = refusal;
	}
	return 0;
}

static const char *
map_type(const char *name)
{
	if (name == NULL)			return "void";
	if (strcmp(name, "Void") == 0)		return "void";
	if (strcmp(name, "Bool") == 0)		return "BOOL";
	if (strcmp(name, "String") == 0)	return "NSString *";
	/*
	 * `NSObject *`, not `NSObject`: this is the TYPE table, and a class type
	 * is written with its pointer. The superclass position is the one place
	 * the bare name is wanted (`@interface X : NSObject`), and that is
	 * map_superclass's, which is why the two are separate functions.
	 */
	if (strcmp(name, "Object") == 0)	return "NSObject *";
	if (strcmp(name, "AnyObject") == 0)	return "id";
	if (strcmp(name, "Int") == 0)		return "NSInteger";
	if (strcmp(name, "UInt") == 0)		return "NSUInteger";
	if (strcmp(name, "Int8") == 0)		return "int8_t";
	if (strcmp(name, "Int16") == 0)		return "int16_t";
	if (strcmp(name, "Int32") == 0)		return "int32_t";
	if (strcmp(name, "Int64") == 0)		return "int64_t";
	if (strcmp(name, "UInt8") == 0)		return "uint8_t";
	if (strcmp(name, "UInt16") == 0)	return "uint16_t";
	if (strcmp(name, "UInt32") == 0)	return "uint32_t";
	if (strcmp(name, "UInt64") == 0)	return "uint64_t";
	if (strcmp(name, "Float32") == 0)	return "float";
	if (strcmp(name, "Float64") == 0)	return "double";
	if (strcmp(name, "Float") == 0)		return "float";
	if (strcmp(name, "Double") == 0)	return "double";
	if (strcmp(name, "Character") == 0)	return "char";
	if (strcmp(name, "CString") == 0)	return "const char *";
	if (strcmp(name, "MutableCString") == 0) return "char *";
	if (strcmp(name, "UnsafeMutablePointer") == 0) return "void *";
	if (strcmp(name, "UnsafePointer") == 0)	return "void const *";
	/*
	 * §4's prelude rows are NOT a table: "every class in the prelude drops
	 * Cocoa's NS in Sterling" generates `Array -> NSArray *`, `Error ->
	 * NSError *` and the rest from the class list, and that list comes from
	 * the headers. Reading them is §9.5's header importer (K3), so the only
	 * prelude class here is the one K1's specimen needs and a name that is
	 * neither a scalar above nor a known class falls through to itself —
	 * which is right for an author's own type and wrong for an unstated
	 * prelude one. Named rather than guessed.
	 */
	return name;
}

/*
 * §4's types that are NOT references: the scalars, the two void pointers, and
 * `char`. A name outside this set and outside the class set is one §4's table
 * does not cover — §9.5's header importer is what would say which it is — and
 * §5's reference/value rule cannot be applied to it.
 */
static int
type_is_known_scalar(const char *mapped)
{
	static const char *const scalars[] = {
		"void", "BOOL", "char", "int8_t", "int16_t", "int32_t",
		"int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
		"float", "double", "NSInteger", "NSUInteger",
		"void *", "void const *", "const char *", "char *",
	};
	size_t i;

	for (i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++) {
		if (strcmp(mapped, scalars[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

/*
 * §4's `T?` cannot be emitted yet, and refusing it is not the cautious choice —
 * it is the only correct one. The header is wrapped in
 * `_Pragma("clang assume_nonnull begin")`, so emitting `String?` as
 * `NSString *` inside that region declares it NON-null: the generated code
 * would assert the opposite of the source.
 */
static int
type_is_emittable(const st_type *t, const char **error)
{
	if (t != NULL && t->nullable) {
		return refuse("a nullable type (§4's `T?`) — the header's "
			      "`assume_nonnull` region would assert the "
			      "opposite", error);
	}
	return 1;
}

/* §5's Local: which constness the type takes, and whether a float literal
 * under it is a `float` (with the `f` suffix) or a `double` (§2 shows `0.1f`). */
static int
type_is_class(const char *mapped)
{
	size_t len = strlen(mapped);

	if (strcmp(mapped, "NSObject") == 0 || strcmp(mapped, "id") == 0) {
		return 1;
	}
	return len > 0 && mapped[len - 1] == '*';
}

static const char *
map_superclass(const char *name)
{
	if (name != NULL && strcmp(name, "Object") == 0) {
		return "NSObject";
	}
	return name;
}

/*
 * The selector pieces of a method (§7.1). The first parameter contributes the
 * piece that carries the method name, so `baz(arg1: Bool, arg2: String)` emits
 * `baz:arg2:` — §2's own output — and not `bazarg1:arg2:`.
 *
 * §3's table agrees for a CALL SITE (`o.foobar(argname: 1, arg2: true)` ->
 * `[o foobar:1 arg2:YES]`, so the first piece is the method name there too),
 * and §5 reads the other way: it gives `class method foo(bar baz: Type)` as
 * emitting `+ (void)fooBar:(Type)baz`. §2 and §5 cannot both hold. §2 is the
 * golden and it is the specimen's own declaration this function has to match,
 * so §2 wins and the disagreement is recorded rather than papered over.
 */
static int
emit_signature(FILE *out, const st_decl *d, const char *terminator,
	       const char **error)
{
	size_t i;

	if (!type_is_emittable(&d->type, error)) {
		return 0;
	}
	for (i = 0; i < d->param_count; i++) {
		if (!type_is_emittable(&d->params[i].type, error)) {
			return 0;
		}
	}
	fprintf(out, "%s (%s)%s", d->is_class_method ? "+" : "-",
		map_type(d->type.name.text), d->name.text);
	for (i = 0; i < d->param_count; i++) {
		const st_param *p = &d->params[i];

		/*
		 * A distinct label (`label name:`) is the piece; otherwise the
		 * piece is empty and the name is the parameter's own, which is
		 * §7.50's `_` case as well.
		 */
		/* Selector pieces are space-separated: `baz:(BOOL)a arg2:B`. */
		if (i > 0) {
			fprintf(out, " ");
		}
		if (p->external.text != NULL && p->internal.text != NULL &&
		    strcmp(p->external.text, p->internal.text) != 0) {
			fprintf(out, "%s:(%s)%s", p->external.text,
				map_type(p->type.name.text), p->internal.text);
		} else if (i > 0) {
			fprintf(out, "%s:(%s)%s", p->internal.text,
				map_type(p->type.name.text), p->internal.text);
		} else {
			fprintf(out, ":(%s)%s", map_type(p->type.name.text),
				p->internal.text);
		}
	}
	fprintf(out, "%s\n", terminator);
	return 1;
}

/* ---- expressions ------------------------------------------------------- */

static int emit_expr(FILE *out, const st_expr *e, const char *expected,
		     const char **error);

/*
 * A sub-expression standing where C binds TIGHTER than any binary operator: a
 * send's receiver, a member's base, a call's callee, a unary's operand. Only a
 * binary or an assignment needs parentheses there — `(a + b).c` and `-(a + b)`
 * are the cases this exists for.
 */
static int
emit_tight(FILE *out, const st_expr *e, const char **error)
{
	int wrap = (e->kind == ST_EXPR_BINARY || e->kind == ST_EXPR_ASSIGN);
	int ok;

	if (wrap) {
		fprintf(out, "(");
	}
	ok = emit_expr(out, e, NULL, error);
	if (wrap) {
		fprintf(out, ")");
	}
	return ok;
}

/*
 * One operand of a binary expression. §7.22's table decides the parentheses,
 * and it is the SAME table the parser used (st_operator_precedence) — a second
 * copy is how `(a + b) * c` comes back as `a + b * c`.
 *
 * A child of strictly lower precedence needs them, and one of EQUAL precedence
 * needs them on the right, because C and §7.22 are both left-associative.
 */
static int
emit_binary_operand(FILE *out, const st_expr *e, int parent_precedence,
		    int is_right, const char **error)
{
	int wrap = 0;
	int ok;

	if (e->kind == ST_EXPR_BINARY) {
		int precedence = st_operator_precedence(e->text.text,
						       strlen(e->text.text));

		wrap = precedence < parent_precedence ||
		       (precedence == parent_precedence && is_right);
	} else if (e->kind == ST_EXPR_ASSIGN) {
		/* §7.21's assignment is looser than every operator. */
		wrap = 1;
	}
	if (wrap) {
		fprintf(out, "(");
	}
	ok = emit_expr(out, e, NULL, error);
	if (wrap) {
		fprintf(out, ")");
	}
	return ok;
}

static int
emit_call_args(FILE *out, const st_expr *e, int with_labels,
	       const char **error)
{
	size_t i;

	for (i = 0; i < e->arg_count; i++) {
		const st_arg *a = &e->args[i];

		if (i > 0) {
			fprintf(out, ", ");
		}
		if (with_labels) {
			fprintf(out, "%s: ",
				a->external.text != NULL ? a->external.text : "_");
		}
		if (!emit_expr(out, a->value, NULL, error)) {
			return 0;
		}
	}
	return 1;
}

static int
emit_expr(FILE *out, const st_expr *e, const char *expected,
	  const char **error)
{
	size_t i;

	switch (e->kind) {
	case ST_EXPR_INT:
		fprintf(out, "%s", e->text.text);
		return 1;
	case ST_EXPR_FLOAT:
		/*
		 * §4: a literal takes the EXPECTED type wherever there is one, and
		 * the default only where there is not — a float literal is a
		 * Float64 (`double`) on its own, and `float` under a Float32. §2's
		 * getter is the second case and shows the `0.1f`; emitting the
		 * suffix unconditionally made `let d: Float64 = 0.1` a float.
		 */
		fprintf(out, "%s%s", e->text.text,
			(expected != NULL && strcmp(expected, "float") == 0)
				? "f" : "");
		return 1;
	case ST_EXPR_STRING:
		/* §3: `"…"` emits as an NSString literal — one `@`, no conversion. */
		fprintf(out, "@%s", e->text.text);
		return 1;
	case ST_EXPR_TRUE:	fprintf(out, "YES");	return 1;
	case ST_EXPR_FALSE:	fprintf(out, "NO");	return 1;
	case ST_EXPR_NIL:	fprintf(out, "nil");	return 1;
	case ST_EXPR_SELF:	fprintf(out, "self");	return 1;
	case ST_EXPR_IDENT:
		fprintf(out, "%s", e->text.text);
		return 1;
	case ST_EXPR_MEMBER:
		if (!emit_tight(out, e->base, error)) {
			return 0;
		}
		fprintf(out, ".%s", e->text.text);
		return 1;
	case ST_EXPR_CALL:
		/*
		 * §7.42: an unqualified call is a C function when no method
		 * matches, and §5's Function says a C function has no selector —
		 * so the labels are checked at the declaration and DROPPED here.
		 * That is what makes §2's `callSomeFunc(arg: arg1)` emit the
		 * positional `callSomeFunc(arg1)`.
		 */
		if (!emit_tight(out, e->base, error)) {
			return 0;
		}
		fprintf(out, "(");
		if (!emit_call_args(out, e, 0, error)) {
			return 0;
		}
		fprintf(out, ")");
		return 1;
	case ST_EXPR_SEND:
		/*
		 * §6: `receiver.sel(label: arg, …)` -> `[receiver sel:arg …]`.
		 * The bracket form is never written in Sterling, so the send is
		 * the only thing that produces one.
		 */
		fprintf(out, "[");
		if (!emit_tight(out, e->base, error)) {
			return 0;
		}
		if (e->arg_count == 0) {
			/* §6: a no-argument message still writes `()` in the
			 * source (the omission rule was withdrawn), and `[o foo]`
			 * is what that produces. */
			fprintf(out, " %s]", e->text.text);
			return 1;
		}
		for (i = 0; i < e->arg_count; i++) {
			const st_arg *a = &e->args[i];
			/*
			 * §2/§3: the first piece is the METHOD NAME, later pieces
			 * are the author's labels. See emit_signature for the §5
			 * disagreement this follows §2 over.
			 */
			const char *piece = (i == 0) ? e->text.text
						     : a->external.text;

			fprintf(out, " %s:", piece != NULL ? piece : "");
			if (!emit_expr(out, a->value, NULL, error)) {
				return 0;
			}
		}
		fprintf(out, "]");
		return 1;
	case ST_EXPR_BINARY: {
		int precedence = st_operator_precedence(e->text.text,
						       strlen(e->text.text));

		if (!emit_binary_operand(out, e->base, precedence, 0, error)) {
			return 0;
		}
		fprintf(out, " %s ", e->text.text);
		return emit_binary_operand(out, e->args[0].value, precedence, 1,
					   error);
	}
	case ST_EXPR_UNARY:
		fprintf(out, "%s", e->text.text);
		return emit_tight(out, e->base, error);
	case ST_EXPR_ASSIGN:
		if (!emit_expr(out, e->base, NULL, error)) {
			return 0;
		}
		fprintf(out, " = ");
		return emit_expr(out, e->args[0].value, NULL, error);
	case ST_EXPR_UNSUPPORTED:
		return refuse(e->text.text != NULL ? e->text.text : "expression",
			      error);
	}
	return refuse("expression", error);
}

/* ---- statements -------------------------------------------------------- */

static int emit_stmt_list(FILE *out, const st_stmt *list, int depth,
			  const char *expected, const char **error);
static int emit_if(FILE *out, const st_stmt *s, int depth,
		   const char *expected, const char **error);

static void
emit_indent(FILE *out, int depth)
{
	int i;

	for (i = 0; i < depth; i++) {
		fprintf(out, "\t");
	}
}

/*
 * §4: "a declaration's type may come from its initializer", and an unsuffixed
 * literal has a type of its own — an integer literal is `Int` (`NSInteger`) and
 * a float literal `Float64` (`double`).
 *
 * Only literals are covered. `let p = Point(x: 0, y: 0)` needs the type of
 * `Point`, which is the header importer's (K3), so it is refused rather than
 * guessed at — the difference between a compiler that stops and one that emits
 * something plausible.
 */
static const char *
infer_local_type(const st_expr *init)
{
	switch (init->kind) {
	case ST_EXPR_INT:	return "NSInteger";
	case ST_EXPR_FLOAT:	return "double";
	case ST_EXPR_STRING:	return "NSString *";
	case ST_EXPR_TRUE:
	case ST_EXPR_FALSE:	return "BOOL";
	default:		return NULL;
	}
}

static int
emit_local(FILE *out, const st_stmt *s, int depth, const char **error)
{
	const char *mapped = NULL;
	const char *initial_expected = NULL;
	int is_let = (s->kind == ST_STMT_LET);

	if (s->type.name.text != NULL) {
		if (!type_is_emittable(&s->type, error)) {
			return 0;
		}
		mapped = map_type(s->type.name.text);
	} else if (s->value != NULL) {
		mapped = infer_local_type(s->value);
		if (mapped == NULL) {
			return refuse("a local whose type comes from its "
				      "initializer", error);
		}
	} else {
		return refuse("a local with neither a type nor an initializer",
			      error);
	}
	if (is_let && s->value == NULL) {
		/* `const T name;` is not C — a const needs its value. */
		return refuse("a `let` with no initializer", error);
	}
	initial_expected = mapped;

	emit_indent(out, depth);
	/*
	 * §5's three cases, in one test: a class type is `T * const name` and
	 * everything else is `const T name`. `mapped` already carries the `*`, so
	 * the difference is only which side of it the `const` goes.
	 */
	if (is_let) {
		if (type_is_class(mapped)) {
			fprintf(out, "%s const %s", mapped, s->name.text);
		} else {
			fprintf(out, "const %s %s", mapped, s->name.text);
		}
	} else {
		fprintf(out, "%s %s", mapped, s->name.text);
	}
	if (s->value != NULL) {
		fprintf(out, " = ");
		if (!emit_expr(out, s->value, initial_expected, error)) {
			return 0;
		}
	}
	fprintf(out, ";\n");
	return 1;
}

/*
 * An `if`, with the line ALREADY positioned by the caller — emit_stmt writes the
 * indent, and the `else if` path below continues the same line, which is why the
 * body is its own function.
 *
 * `} else \tif (…)` is what indenting the nested `if` produced: valid C, visibly
 * wrong, and exactly the kind of thing a golden file exists to catch.
 */
static int
emit_if(FILE *out, const st_stmt *s, int depth, const char *expected,
	const char **error)
{
	fprintf(out, "if (");
	if (!emit_expr(out, s->value, NULL, error)) {
		return 0;
	}
	fprintf(out, ") {\n");
	if (!emit_stmt_list(out, s->body, depth + 1, expected, error)) {
		return 0;
	}
	emit_indent(out, depth);
	fprintf(out, "}");
	if (s->has_else) {
		/*
		 * `else if` is one statement in the else branch, so the chain
		 * comes back out as a chain. C is the same either way; the
		 * difference is that a reader sees what they wrote.
		 */
		if (s->else_body != NULL && s->else_body->next == NULL &&
		    s->else_body->kind == ST_STMT_IF) {
			fprintf(out, " else ");
			return emit_if(out, s->else_body, depth, expected, error);
		}
		fprintf(out, " else {\n");
		if (!emit_stmt_list(out, s->else_body, depth + 1, expected,
				    error)) {
			return 0;
		}
		emit_indent(out, depth);
		fprintf(out, "}");
	}
	fprintf(out, "\n");
	return 1;
}

static int
emit_stmt(FILE *out, const st_stmt *s, int depth, const char *expected,
	  const char **error)
{
	switch (s->kind) {
	case ST_STMT_RETURN:
		emit_indent(out, depth);
		fprintf(out, "return");
		if (s->value != NULL) {
			fprintf(out, " ");
			if (!emit_expr(out, s->value, expected, error)) {
				return 0;
			}
		}
		/* §7.75: the surface may omit it, C may not. */
		fprintf(out, ";\n");
		return 1;
	case ST_STMT_EXPR:
		emit_indent(out, depth);
		if (s->value == NULL) {
			/* Not a surface form: a node the parser left empty. */
			return refuse("an empty statement", error);
		}
		if (!emit_expr(out, s->value, NULL, error)) {
			return 0;
		}
		fprintf(out, ";\n");
		return 1;
	case ST_STMT_LET:
	case ST_STMT_VAR:
		return emit_local(out, s, depth, error);
	case ST_STMT_IF:
		emit_indent(out, depth);
		return emit_if(out, s, depth, expected, error);
	case ST_STMT_WHILE:
		emit_indent(out, depth);
		fprintf(out, "while (");
		if (!emit_expr(out, s->value, NULL, error)) {
			return 0;
		}
		fprintf(out, ") {\n");
		if (!emit_stmt_list(out, s->body, depth + 1, expected, error)) {
			return 0;
		}
		emit_indent(out, depth);
		fprintf(out, "}\n");
		return 1;
	case ST_STMT_UNSUPPORTED:
		return refuse(s->text.text != NULL ? s->text.text : "statement",
			      error);
	}
	return refuse("statement", error);
}

static int
emit_stmt_list(FILE *out, const st_stmt *list, int depth, const char *expected,
	       const char **error)
{
	const st_stmt *s;

	for (s = list; s != NULL; s = s->next) {
		if (!emit_stmt(out, s, depth, expected, error)) {
			return 0;
		}
	}
	return 1;
}

/* ---- the .h ------------------------------------------------------------ */

/*
 * §7.45's forward declarations — one line per distinct referenced protocol
 * name, so the list is linear in the names a program *mentions* rather than in
 * the types it declares.
 */
#define EMIT_MAX_FORWARDS 32

/*
 * The "have I written this name already" test for the forward declarations. It
 * keeps the FIRST occurrence's order, which is what makes the emitted block
 * stable across runs — a hash set would not.
 */
static int
name_seen(const char *seen[], size_t *count, const char *name)
{
	size_t i;

	if (name == NULL) {
		return 1;
	}
	for (i = 0; i < *count; i++) {
		if (strcmp(seen[i], name) == 0) {
			return 1;
		}
	}
	if (*count < EMIT_MAX_FORWARDS) {
		seen[(*count)++] = name;
	}
	return 0;
}

/*
 * §7.52: one ownership attribute per property, and *which* one is either written
 * or inferred — §5's rule is a class type is `strong` and a scalar or struct is
 * `assign`. §2's note settles the third case: a COMPUTED property declares no
 * storage, so it carries no ownership qualifier at all.
 *
 * The predecessor emitted `(nonatomic, assign)` for every non-readonly property,
 * so `property x: Foo` — a class type — came out `assign`. That compiles and is
 * the wrong ownership rule, which is the kind of wrong answer this emitter is
 * not allowed to produce quietly.
 */
static int
emit_property_line(FILE *out, const st_decl *d, int stored, const char **error)
{
	const char *mapped = map_type(d->type.name.text);
	const char *own = NULL;

	if (!type_is_emittable(&d->type, error)) {
		return 0;
	}
	if (d->has_initial) {
		/*
		 * §9.16: a stored property's default is emitted as a synthesised
		 * *defaults* method, because neither an ivar nor a C struct member
		 * may carry an initializer (measured). Refused, so the value is
		 * never silently lost — which is what happened while this parsed
		 * into a variable called `discard`.
		 */
		return refuse("a stored property's default (§9.16)", error);
	}
	if (d->ownership == ST_OWN_UNOWNED) {
		return refuse("`unowned` (§7.53)", error);
	}
	fprintf(out, "@property (nonatomic");
	if (d->is_readonly) {
		fprintf(out, ", readonly");
	}
	if (stored) {
		switch (d->ownership) {
		case ST_OWN_STRONG:	own = "strong";	break;
		case ST_OWN_WEAK:	own = "weak";	break;
		case ST_OWN_COPY:	own = "copy";	break;
		case ST_OWN_ASSIGN:	own = "assign";	break;
		default:
			own = type_is_class(mapped) ? "strong" : "assign";
			break;
		}
		/*
		 * §7.52: `weak` (and `copy`) require a CLASS type — a scalar has
		 * nothing to weaken or to copy. It is a language rule rather than
		 * an emitter detail because clang only *warns* (`__weak int` is
		 * -Wignored-attributes with exit 0), so a weak scalar would emit
		 * and compile.
		 */
		if (strcmp(own, "assign") != 0 && !type_is_class(mapped)) {
			if (type_is_known_scalar(mapped)) {
				/* §7.52: a scalar has nothing to weaken or to
				 * copy, and clang only warns about it. */
				return refuse("`weak`/`copy`/`strong` on a scalar "
					      "(§7.52)", error);
			}
			/*
			 * A name §4's table does not cover could be a class or a
			 * struct, and only the class takes these — §9.5's header
			 * importer is what would say which. Refused rather than
			 * guessed, and the message says which of the two
			 * situation it is, because "not a class" would be a
			 * claim this compiler cannot make.
			 */
			return refuse("an ownership attribute that needs a class "
				      "type, on a name §4's type table does not "
				      "cover (§7.52)", error);
		}
		fprintf(out, ", %s", own);
	}
	/*
	 * §3's map attaches the pointer to the NAME in a declaration —
	 * `@property (nonatomic) Foo *x;` — while a cast-shaped position keeps it
	 * on the type (`(NSString *)name`). `mapped` already ends in ` *`, so the
	 * space before the name is what has to go.
	 */
	{
		size_t len = strlen(mapped);

		if (len >= 2 && mapped[len - 1] == '*' && mapped[len - 2] == ' ') {
			fprintf(out, ") %.*s*%s;\n", (int)(len - 1), mapped,
				d->name.text);
		} else {
			fprintf(out, ") %s %s;\n", mapped, d->name.text);
		}
	}
	return 1;
}

/*
 * §7.45: `protocol C: A, B { … }` emits ObjC's other bracket — `@protocol C <A,
 * B>` — and the colon is the surface's while the angle brackets are the
 * emission's, so nothing new is emitted.
 *
 * §7.48: `@required` and `@optional` are *sections*, not per-member markers, and
 * required is the default. So the members are sorted into runs — a protocol
 * declares no layout, so reordering is free — and an all-required protocol, most
 * of them, gets no marker at all.
 *
 * The sort is a run of two passes rather than an array: the list is short and the
 * pass number is the only state either branch needs.
 */
static int
emit_protocol(FILE *out, const st_protocol *prot, const char **error)
{
	const st_decl *d;
	int has_optional = 0;
	int pass;

	for (d = prot->requirements; d != NULL; d = d->next) {
		if (d->is_optional) {
			has_optional = 1;
		}
	}
	fprintf(out, "@protocol %s", prot->name.text);
	if (prot->inherit_count > 0) {
		size_t i;

		fprintf(out, " <");
		for (i = 0; i < prot->inherit_count; i++) {
			fprintf(out, "%s%s", i > 0 ? ", " : "",
				prot->inherits[i].text);
		}
		fprintf(out, ">");
	}
	fprintf(out, "\n");
	for (pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			if (!has_optional) {
				break;
			}
			fprintf(out, "@optional\n");
		}
		for (d = prot->requirements; d != NULL; d = d->next) {
			if ((d->is_optional != 0) != (pass == 1)) {
				continue;
			}
			if (d->kind == ST_DECL_METHOD) {
				if (!emit_signature(out, d, ";", error)) {
					return 0;
				}
				continue;
			}
			/*
			 * A protocol property is a REQUIREMENT, so it declares no
			 * storage — but ObjC still wants an ownership attribute on
			 * the declaration, and clang warns when it has none
			 * ("no 'assign', 'retain', or 'copy' attribute is
			 * specified — 'assign' is assumed", which is the
			 * dangerous default for an object type). So it takes the
			 * inferred or written one like any other property.
			 */
			if (!emit_property_line(out, d, d->body == NULL, error)) {
				return 0;
			}
		}
	}
	fprintf(out, "@end\n\n");
	return 1;
}

int
st_emit_header(FILE *out, const st_program *program, const char *source_label,
	       const char **error)
{
	size_t i;
	char stem[256];

	/*
	 * §7.4's categories and extensions are parsed and this emitter has no
	 * second `@interface X (Name)`. Refused rather than dropped: the block is
	 * a whole declaration list, and the parser used to read it and keep
	 * nothing at all.
	 */
	if (program->extension_count > 0) {
		return refuse("a category or extension (§7.4)", error);
	}
	/*
	 * A file may declare several classes — ordinary Sterling, and ONE
	 * translation unit — so they are all emitted into this pair. §7.9's
	 * per-class/per-module/per-program question is about *modules*, which do
	 * not exist yet; inside one file there is no choice to make.
	 *
	 * What is refused is a program with no class at all: the pair is named
	 * after one, and there is nothing to name it after.
	 */
	if (program->class_count == 0) {
		return refuse("a program with no class (the header's name comes "
			      "from one)", error);
	}

	if (source_label != NULL) {
		st_source_stem(source_label, stem, sizeof(stem));
		fprintf(out, "/* %s.h — generated by sterlingc from %s. Do not edit. */\n",
			stem, source_label);
	} else {
		/*
		 * The built-in specimen has no file, so §2's own pairing names
		 * it: `MyClass.h` from `MyClass.ag`.
		 */
		fprintf(out, "/* %s.h — generated by sterlingc from %s.ag. Do not edit. */\n",
			program->classes[0]->name.text,
			program->classes[0]->name.text);
	}
	fprintf(out, "#import <Foundation/Foundation.h>\n\n");
	fprintf(out, "_Pragma(\"clang assume_nonnull begin\")\n\n");

	/*
	 * §7.45: a protocol name is a TYPE, and one may be named before clang has
	 * seen its declaration — an imported protocol, or an inheritance between
	 * the program's own protocols written in either order (`protocol P: Q`
	 * does not require `Q` to come first in the source). §5's forward
	 * declaration rule covers it the way §3's `@class X;` covers a class, so
	 * every REFERENCED name gets `@protocol Name;` ahead of the definitions,
	 * and the source order stops mattering.
	 *
	 * A name that is declared and referenced gets one too. That is not noise
	 * to be optimised away: `@protocol P;` followed by `@protocol P … @end`
	 * is legal, and knowing which references come *before* the definition
	 * would mean ordering the definitions, which is a different feature.
	 */
	{
		const char *seen[EMIT_MAX_FORWARDS];
		size_t seen_count = 0;
		size_t k;

		for (i = 0; i < program->protocol_count; i++) {
			const st_protocol *prot = program->protocols[i];

			for (k = 0; k < prot->inherit_count; k++) {
				if (!name_seen(seen, &seen_count,
					       prot->inherits[k].text)) {
					fprintf(out, "@protocol %s;\n",
						prot->inherits[k].text);
				}
			}
		}
		for (i = 0; i < program->class_count; i++) {
			const st_class *c = program->classes[i];

			for (k = 0; k < c->conformance_count; k++) {
				if (!name_seen(seen, &seen_count,
					       c->conformances[k].text)) {
					fprintf(out, "@protocol %s;\n",
						c->conformances[k].text);
				}
			}
		}
		if (seen_count > 0) {
			fprintf(out, "\n");
		}
	}

	/*
	 * §7.45: the protocol DECLARATIONS come before the class, because a
	 * conformance list naming a protocol clang has not seen yet is an error
	 * rather than a forward reference.
	 */
	for (i = 0; i < program->protocol_count; i++) {
		if (!emit_protocol(out, program->protocols[i], error)) {
			return 0;
		}
	}

	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		const st_decl *d;

		if (c->parameter_count > 0) {
			return refuse("generic parameters", error);
		}
		fprintf(out, "@interface %s : %s", c->name.text,
			map_superclass(c->superclass.text));
		if (c->conformance_count > 0) {
			size_t j;

			fprintf(out, " <");
			for (j = 0; j < c->conformance_count; j++) {
				fprintf(out, "%s%s", j > 0 ? ", " : "",
					c->conformances[j].text);
			}
			fprintf(out, ">");
		}
		fprintf(out, "\n\n");

		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind != ST_DECL_PROPERTY) {
				continue;
			}
			if (!emit_property_line(out, d, d->body == NULL, error)) {
				return 0;
			}
		}
		/*
		 * The blank line separates the properties from the methods, so a
		 * class with no methods must not get one — that produced two
		 * consecutive blank lines before `@end`, which §2's specimen
		 * cannot show because it has both.
		 */
		{
			const st_decl *m;
			int any_method = 0;

			for (m = c->decls; m != NULL; m = m->next) {
				if (m->kind == ST_DECL_METHOD) {
					any_method = 1;
					break;
				}
			}
			if (any_method) {
				fprintf(out, "\n");
			}
		}
		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind == ST_DECL_METHOD) {
				if (!emit_signature(out, d, ";", error)) {
					return 0;
				}
			}
		}
		fprintf(out, "\n@end\n\n");
	}
	/*
	 * §3.12's assumed-non-null region is the FILE's, not each class's: it is
	 * `_Pragma("clang assume_nonnull begin")` near the top and the matching
	 * `end` once, after everything. Emitting the `end` inside the loop closed
	 * the region before the second class and left it open again.
	 */
	fprintf(out, "_Pragma(\"clang assume_nonnull end\")\n");
	return 1;
}

/* ---- the .m ------------------------------------------------------------ */

/*
 * §7.42: an unqualified call resolves to a method when one matches and to a
 * C function otherwise. Every such call that is NOT a method of the class needs
 * an `extern` in the `.m`, and every one of them gets one.
 *
 * The predecessor emitted only the FIRST call it found, by searching the class
 * for one — so a body calling two C functions declared one of them. Collecting
 * all of them is what makes the tree walk pay for itself.
 */
#define EMIT_MAX_EXTERNS 64

typedef struct {
	const st_expr *calls[EMIT_MAX_EXTERNS];
	size_t count;
	int saw_method_call;	/* §7.42: an unqualified call naming a method */
} extern_set;

static int
name_is_method(const st_class *c, const char *name)
{
	const st_decl *d;

	for (d = c->decls; d != NULL; d = d->next) {
		if (d->kind == ST_DECL_METHOD && d->name.text != NULL &&
		    strcmp(d->name.text, name) == 0) {
			return 1;
		}
	}
	return 0;
}

static void
collect_calls(const st_expr *e, const st_class *c, extern_set *set)
{
	size_t i;

	if (e == NULL) {
		return;
	}
	if (e->kind == ST_EXPR_CALL && e->base != NULL &&
	    e->base->kind == ST_EXPR_IDENT && e->base->text.text != NULL) {
		if (name_is_method(c, e->base->text.text)) {
			/*
			 * §7.42: an unqualified call resolves to a method when one
			 * matches. That lowering is a message to `self`, and it is
			 * part of the class surface — emitting it as a C call would
			 * produce a call to an undeclared function. Recorded, so the
			 * class stops before anything is written.
			 */
			set->saw_method_call = 1;
			return;
		}
		for (i = 0; i < set->count; i++) {
			/* One `extern` per callee, however many times it is used. */
			if (strcmp(set->calls[i]->base->text.text,
				   e->base->text.text) == 0) {
				return;
			}
		}
		if (set->count < EMIT_MAX_EXTERNS) {
			set->calls[set->count++] = e;
		}
	}
	collect_calls(e->base, c, set);
	for (i = 0; i < e->arg_count; i++) {
		collect_calls(e->args[i].value, c, set);
	}
}

static void
collect_stmt_calls(const st_stmt *list, const st_class *c, extern_set *set)
{
	const st_stmt *s;

	for (s = list; s != NULL; s = s->next) {
		collect_calls(s->value, c, set);
		collect_stmt_calls(s->body, c, set);
		collect_stmt_calls(s->else_body, c, set);
	}
}

static void
emit_extern(FILE *out, const st_expr *call)
{
	const char *fn = call->base->text.text;
	size_t i;

	fprintf(out, "/* Declared elsewhere; sterlingc imported it. The parameter's name is `%s`. */\n",
		call->arg_count > 0 && call->args[0].internal.text != NULL
			? call->args[0].internal.text
			: "arg");
	fprintf(out, "extern void %s(", fn);
	for (i = 0; i < call->arg_count; i++) {
		/*
		 * The argument names a local or literal of the enclosing
		 * method, so the type is not recoverable from the call alone;
		 * §2 uses BOOL, and a later milestone reads the real
		 * declaration (§9.5's header importer).
		 */
		fprintf(out, "BOOL %s",
			call->args[i].internal.text != NULL
				? call->args[i].internal.text
				: "arg");
		if (i + 1 < call->arg_count) {
			fprintf(out, ", ");
		}
	}
	if (call->arg_count == 0) {
		/* §7.56: `()` is an UNPROTOTYPED declaration in C, which would
		 * switch off clang's own argument checking. */
		fprintf(out, "void");
	}
	fprintf(out, ");\n\n");
}

int
st_emit_implementation(FILE *out, const st_program *program,
		       const char *source_label, const char **error)
{
	size_t i;
	char stem[256];

	if (program->extension_count > 0) {
		return refuse("a category or extension (§7.4)", error);
	}
	if (program->class_count == 0) {
		return refuse("a program with no class (the file's name comes from "
			      "one)", error);
	}
	/*
	 * ONE banner and ONE import, because this is one file: `#import
	 * "Alpha.h"` already carries every class the header declares. Emitting
	 * them per class inside the loop produced an `.m` with a second
	 * "generated from Beta.ag" banner and an `#import "Beta.h"` that does not
	 * exist.
	 */
	if (source_label != NULL) {
		st_source_stem(source_label, stem, sizeof(stem));
		fprintf(out, "/* %s.m — generated by sterlingc from %s. Do not edit. */\n",
			stem, source_label);
	} else {
		snprintf(stem, sizeof(stem), "%s",
			 program->classes[0]->name.text);
		fprintf(out, "/* %s.m — generated by sterlingc from %s.ag. Do not edit. */\n",
			stem, stem);
	}
	fprintf(out, "#import \"%s.h\"\n\n", stem);

	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		const st_decl *d;
		extern_set set;

		if (c->parameter_count > 0) {
			return refuse("generic parameters", error);
		}
		if (i > 0) {
			/* One blank line between classes, and none after the
			 * last: §2's specimen is one class and its `.m` ends at
			 * `@end`. */
			fprintf(out, "\n");
		}

		set.count = 0;
		set.saw_method_call = 0;
		for (d = c->decls; d != NULL; d = d->next) {
			collect_stmt_calls(d->body, c, &set);
		}
		/*
		 * §7.42's method half. Refused BEFORE any output, for the same
		 * reason the other refusals are: the class is a supported program,
		 * and what it needs is a lowering this emitter does not have.
		 */
		if (set.saw_method_call) {
			return refuse("an unqualified call to a method (§7.42)",
				      error);
		}

		{
			/* `n`, not `i`: the class loop owns `i`. */
			size_t n;

			for (n = 0; n < set.count; n++) {
				emit_extern(out, set.calls[n]);
			}
		}

		fprintf(out, "@implementation %s\n\n", c->name.text);
		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind != ST_DECL_METHOD) {
				continue;
			}
			if (!emit_signature(out, d, "", error)) {
				return 0;
			}
			fprintf(out, "{\n");
			if (!emit_stmt_list(out, d->body, 1,
					    map_type(d->type.name.text), error)) {
				return 0;
			}
			fprintf(out, "}\n\n");
		}
		/* §7.54: a read-only property's block *is* its getter. */
		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind != ST_DECL_PROPERTY || d->body == NULL) {
				continue;
			}
			fprintf(out, "- (%s)%s\n", map_type(d->type.name.text),
				d->name.text);
			fprintf(out, "{\n");
			if (!emit_stmt_list(out, d->body, 1,
					    map_type(d->type.name.text), error)) {
				return 0;
			}
			fprintf(out, "}\n\n");
		}
		fprintf(out, "@end\n");
	}
	return 1;
}
