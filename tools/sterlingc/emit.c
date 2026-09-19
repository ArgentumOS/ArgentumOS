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
	if (strcmp(name, "Object") == 0)	return "NSObject";
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
static void
emit_signature(FILE *out, const st_decl *d, const char *terminator)
{
	size_t i;

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

int
st_emit_header(FILE *out, const st_program *program, const char **error)
{
	size_t i;

	/*
	 * A protocol declaration and a class's conformance list are parsed,
	 * §7.48 already CHECKS them, and neither has an emission here. Printing a
	 * class without its `<Protocol>` list would quietly weaken the interface
	 * the source declares, so both refuse.
	 */
	if (program->protocol_count > 0) {
		return refuse("protocol", error);
	}
	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		const st_decl *d;

		if (c->conformance_count > 0) {
			return refuse("a conformance list", error);
		}
		if (c->parameter_count > 0) {
			return refuse("generic parameters", error);
		}
		fprintf(out, "/* %s.h — generated by sterlingc from %s.ag. Do not edit. */\n",
			c->name.text, c->name.text);
		fprintf(out, "#import <Foundation/Foundation.h>\n\n");
		fprintf(out, "_Pragma(\"clang assume_nonnull begin\")\n\n");
		fprintf(out, "@interface %s : %s\n\n", c->name.text,
			map_superclass(c->superclass.text));

		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind != ST_DECL_PROPERTY) {
				continue;
			}
			/*
			 * §2's notes: a computed read-only property declares no
			 * storage, so it carries no ownership qualifier; a stored
			 * scalar does, and it is `assign`.
			 */
			if (d->is_readonly) {
				fprintf(out, "@property (nonatomic, readonly) %s %s;\n",
					map_type(d->type.name.text), d->name.text);
			} else {
				fprintf(out, "@property (nonatomic, assign) %s %s;\n",
					map_type(d->type.name.text), d->name.text);
			}
		}
		fprintf(out, "\n");
		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind == ST_DECL_METHOD) {
				emit_signature(out, d, ";");
			}
		}
		fprintf(out, "\n@end\n\n");
		fprintf(out, "_Pragma(\"clang assume_nonnull end\")\n");
	}
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
		       const char **error)
{
	size_t i;

	if (program->protocol_count > 0) {
		return refuse("protocol", error);
	}
	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		const st_decl *d;
		extern_set set;

		if (c->conformance_count > 0) {
			return refuse("a conformance list", error);
		}
		if (c->parameter_count > 0) {
			return refuse("generic parameters", error);
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

		fprintf(out, "/* %s.m — generated by sterlingc from %s.ag. Do not edit. */\n",
			c->name.text, c->name.text);
		fprintf(out, "#import \"%s.h\"\n\n", c->name.text);
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
			emit_signature(out, d, "");
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
