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
 * The ObjC name of a Sterling TYPE. A nested type's Sterling name is qualified —
 * `Bar` inside `Foo` is the type `Foo.Bar` — and the dot is the whole of the
 * mangling: `Foo_Bar`.
 *
 * LEXICAL, deliberately: `map_type` stays a pure name-to-name function and no
 * symbol table is consulted, so a type reference resolves the same way whether
 * the type is in this file, another one, or imported.
 *
 * What that costs: a top-level class literally NAMED `Foo_Bar` collides with the
 * nested `Bar` of `Foo`. Stated rather than discovered later; the alternatives
 * (an escape scheme the reader cannot reverse) buy less than they cost.
 */
static void
mangle_name(const char *name, char *buf, size_t size)
{
	size_t i;

	if (size == 0) {
		return;
	}
	if (name == NULL) {
		buf[0] = '\0';
		return;
	}
	for (i = 0; i + 1 < size && name[i] != '\0'; i++) {
		buf[i] = (name[i] == '.') ? '_' : name[i];
	}
	buf[i] = '\0';
}

/*
 * The class names THIS FILE declares, mangled — nested ones included. Populated
 * before emission and consulted by `type_text` and the ownership inference,
 * because a locally declared class is a type §4's table knows nothing about
 * (that table's class rows are the prelude's) and its ObjC form needs its
 * pointer: `Outer.Inner` is `Outer_Inner *`.
 *
 * A static for the same reason `refusal` is one: the driver emits one program at
 * a time on one thread, and threading the program through every expression
 * function to be read at exactly two places is worse.
 *
 * Only LOCALLY declared names resolve this way. An imported name — §3's
 * `property x: Foo` — stays what it was, which emits a by-value `Foo item;` and
 * fails at clang; that is loud, and §9.5's header importer is what will make it
 * right rather than a guess here.
 */
#define EMIT_MAX_DECLARED 64

typedef struct {
	char names[EMIT_MAX_DECLARED][256];
	size_t count;
} name_set;

static name_set declared_classes;

static void
name_set_add(name_set *set, const char *name)
{
	size_t i;

	if (name == NULL) {
		return;
	}
	for (i = 0; i < set->count; i++) {
		if (strcmp(set->names[i], name) == 0) {
			return;
		}
	}
	if (set->count < EMIT_MAX_DECLARED) {
		snprintf(set->names[set->count++], 256, "%s", name);
	}
}

static int
name_set_has(const name_set *set, const char *name)
{
	size_t i;

	if (name == NULL) {
		return 0;
	}
	for (i = 0; i < set->count; i++) {
		if (strcmp(set->names[i], name) == 0) {
			return 1;
		}
	}
	return 0;
}

static void
collect_declared_classes(const st_class *c, name_set *set)
{
	char mangled[256];
	size_t i;

	mangle_name(c->name.text, mangled, sizeof(mangled));
	name_set_add(set, mangled);
	for (i = 0; i < c->nested_count; i++) {
		collect_declared_classes(c->nested[i], set);
	}
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
	if (strcmp(name, "Self") == 0)		return "instancetype";
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
static int type_is_class(const char *mapped);

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
 * §4's `T?` on a CLASS type is `_Nullable` after the pointer, and the header's
 * `assume_nonnull begin` region is exactly why it must be written: without it
 * the declaration asserts non-null, the opposite of the source.
 *
 * The other two cases still refuse, for different reasons. A scalar's `?` is
 * §7.62's pair-struct — a value type carrying a has-value flag — which is a
 * different mechanism and not a qualifier. And a name §4's table does not cover
 * cannot be classified at all, so which of the two it needs is unknown.
 */
static int
type_is_emittable(const st_type *t, const char **error)
{
	const char *mapped;

	if (t == NULL || !t->nullable) {
		return 1;
	}
	mapped = map_type(t->name.text);
	if (type_is_class(mapped)) {
		return 1;
	}
	if (type_is_known_scalar(mapped)) {
		return refuse("a nullable scalar (§7.62's pair-struct)", error);
	}
	return refuse("a nullable type on a name §4's table does not cover "
		      "(`T?` is `_Nullable` for a class and §7.62's pair-struct "
		      "for a scalar)", error);
}

/*
 * The type as written, `_Nullable` included. ONE implementation, because the
 * qualifier has to land after the pointer (`NSString * _Nullable`, not
 * `NSString _Nullable *`) and every position that prints a type would otherwise
 * have to know that.
 *
 * `fallback` is the type for a local whose declaration had none and whose
 * initializer supplied it — a literal, so never nullable — and it is why the
 * text is built separately from being printed: that position has to combine an
 * INFERRED type with the same `_Nullable` rule, and only one of the two sources
 * can ever carry the flag.
 */
static void
type_text(const st_type *t, const char *fallback, char *buf, size_t size)
{
	const char *name = (t != NULL && t->name.text != NULL) ? t->name.text
							       : fallback;
	char plain[256];
	const char *mapped;
	size_t len;

	if (name == NULL) {
		snprintf(buf, size, "void");
		return;
	}
	/*
	 * The dot mangling happens before `map_type`, so a nested type's qualified
	 * name (`Foo.Bar`) arrives at the table as the ObjC identifier it is
	 * (`Foo_Bar`) and falls through it unchanged — while every name the table
	 * DOES know has no dot and is untouched.
	 */
	mangle_name(name, plain, sizeof(plain));
	mapped = map_type(plain);
	if (mapped == NULL) {
		snprintf(buf, size, "void");
		return;
	}
	/*
	 * §4's reference/value rule for a class THIS FILE declares: §4's table's
	 * class rows are the prelude's, so nothing in it maps `Outer_Inner` to
	 * anything and the pointer has to be added here. Without it a nested
	 * class-typed property was emitted by VALUE — `Outer.Inner item;`, which
	 * does not compile.
	 */
	{
		char base[512];

		if (name_set_has(&declared_classes, plain) &&
		    !type_is_class(mapped)) {
			snprintf(base, sizeof(base), "%s *", mapped);
		} else {
			snprintf(base, sizeof(base), "%s", mapped);
		}
		/*
		 * `t == NULL` is the INFERRED local: its type came from the
		 * initializer, which is a literal, so there is no flag to consult
		 * and nothing to add.
		 */
		if (t == NULL || !t->nullable) {
			snprintf(buf, size, "%s", base);
			return;
		}
		len = strlen(base);
		if (len >= 2 && base[len - 1] == '*' && base[len - 2] == ' ') {
			snprintf(buf, size, "%.*s* _Nullable", (int)(len - 1),
				 base);
		} else {
			snprintf(buf, size, "%s _Nullable", base);
		}
	}
}

static void
emit_type(FILE *out, const st_type *t)
{
	char buf[256];

	type_text(t, "void", buf, sizeof(buf));
	fprintf(out, "%s", buf);
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

/*
 * True when a Sterling type NAME is a class: one this file declares, or one
 * §4's table maps to a pointer. The two are asked together everywhere the answer
 * changes the emission — §5's `const` placement and §7.52's ownership inference —
 * so this is one question rather than two call-site decisions.
 */
static int
type_name_is_class(const char *name)
{
	char plain[256];

	if (name == NULL) {
		return 0;
	}
	mangle_name(name, plain, sizeof(plain));
	if (name_set_has(&declared_classes, plain)) {
		return 1;
	}
	return type_is_class(map_type(plain));
}

/*
 * The superclass as ObjC names it: `Object` is `NSObject` (§4's prelude rename),
 * and a nested superclass is mangled like any other type reference.
 */
static void
superclass_text(const char *name, char *buf, size_t size)
{
	char plain[256];

	if (name != NULL && strcmp(name, "Object") == 0) {
		snprintf(buf, size, "NSObject");
		return;
	}
	mangle_name(name, plain, sizeof(plain));
	snprintf(buf, size, "%s", plain);
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
/*
 * §7.49: `init` is the whole convention for an initializer. The `method` keyword
 * and the `-> Self` return are implied, and §5 says each is "accepted and
 * ignored" when spelled out — so a METHOD named `init` is an initializer
 * whatever else its declaration says, and the name is the only test there is.
 */
static int
is_initializer(const st_decl *d)
{
	return d->kind == ST_DECL_METHOD && d->name.text != NULL &&
	       strcmp(d->name.text, "init") == 0;
}

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
	fprintf(out, "%s (", d->is_class_method ? "+" : "-");
	/*
	 * §7.49: an initializer's return is `instancetype` — the `-> Self` it is
	 * written without — and the override is UNCONDITIONAL, which is what
	 * "accepted and ignored" means for a spelled-out `-> Self`. Without this
	 * an initializer emitted `- (void)init(n:)` and the published
	 * `- (instancetype)init` — the one every `T(value: 3)` call is a send to
	 * — did not exist, so the call form could not compile against it.
	 */
	if (is_initializer(d)) {
		fprintf(out, "instancetype");
	} else {
		emit_type(out, &d->type);
	}
	fprintf(out, ")%s", d->name.text);
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
			fprintf(out, "%s:(", p->external.text);
			emit_type(out, &p->type);
			fprintf(out, ")%s", p->internal.text);
		} else if (i > 0) {
			fprintf(out, "%s:(", p->internal.text);
			emit_type(out, &p->type);
			fprintf(out, ")%s", p->internal.text);
		} else {
			fprintf(out, ":(");
			emit_type(out, &p->type);
			fprintf(out, ")%s", p->internal.text);
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
	case ST_EXPR_SUPER:	fprintf(out, "super");	return 1;
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
	case ST_EXPR_UNWRAP:
		/*
		 * §6: `x!` is the one postfix form that carries behaviour — "the raw
		 * value, trapped if it is nil" — and the macro is where that lives
		 * (see emit_trap_support; it is emitted into the header only when
		 * this case is reachable). The operand goes through emit_expr whole,
		 * so §6's "binding tighter than the message send, `x!.foo` is
		 * `[(x!) foo]`" falls out of the SEND's receiver emitter rather than
		 * being arranged here.
		 */
		fprintf(out, "STERLING_UNWRAP(");
		if (!emit_expr(out, e->base, NULL, error)) {
			return 0;
		}
		fprintf(out, ")");
		return 1;
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
	 * everything else is `const T name`. The type text already carries the
	 * `*` — and `_Nullable`, where it applies — so the difference is only
	 * which side of it the `const` goes.
	 *
	 * `type_text`, not `emit_type`: an INFERRED local has no `st_type` to
	 * read, its type is the `mapped` this function computed from the
	 * initializer.
	 */
	{
		char typebuf[256];

		type_text(s->type.name.text != NULL ? &s->type : NULL, mapped,
			  typebuf, sizeof(typebuf));
		if (is_let) {
			/*
			 * §5's class-type rule, asked of the NAME where there is one and
			 * of the inferred type where there is not — a locally declared
			 * class is a reference even though §4's table has never heard of
			 * it, so `let item: Outer.Inner = …` is `Outer_Inner * const`.
			 */
			if (s->type.name.text != NULL
				    ? type_name_is_class(s->type.name.text)
				    : type_is_class(mapped)) {
				fprintf(out, "%s const %s", typebuf,
					s->name.text);
			} else {
				fprintf(out, "const %s %s", typebuf,
					s->name.text);
			}
		} else {
			fprintf(out, "%s %s", typebuf, s->name.text);
		}
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
	/*
	 * ONE question, asked twice below: §7.52's inference needs to know whether
	 * the property is a reference, and so does the class-type requirement. A
	 * class this FILE declares counts — which is what makes
	 * `property item: Outer.Inner` infer `strong` rather than `assign`.
	 */
	int is_class = type_name_is_class(d->type.name.text);

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
			own = is_class ? "strong" : "assign";
			break;
		}
		/*
		 * §7.52: `weak` (and `copy`) require a CLASS type — a scalar has
		 * nothing to weaken or to copy. It is a language rule rather than
		 * an emitter detail because clang only *warns* (`__weak int` is
		 * -Wignored-attributes with exit 0), so a weak scalar would emit
		 * and compile.
		 */
		if (strcmp(own, "assign") != 0 && !is_class) {
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
	 *
	 * A NULLABLE type is the exception: `_Nullable` is a qualifier that has to
	 * sit between the `*` and the declarator, so the name cannot be glued.
	 */
	if (d->type.nullable) {
		fprintf(out, ") ");
		emit_type(out, &d->type);
		fprintf(out, " %s;\n", d->name.text);
	} else {
		/*
		 * `type_text`, not the raw `mapped`: this path prints the type
		 * itself, so it is the one place the nested-type mangling would be
		 * missed — `property item: Outer.Inner` came out as
		 * `Outer.Inner item;`, which does not compile.
		 */
		char typebuf[256];
		size_t len;

		type_text(&d->type, mapped, typebuf, sizeof(typebuf));
		len = strlen(typebuf);
		if (len >= 2 && typebuf[len - 1] == '*' && typebuf[len - 2] == ' ') {
			fprintf(out, ") %.*s*%s;\n", (int)(len - 1), typebuf,
				d->name.text);
		} else {
			fprintf(out, ") %s %s;\n", typebuf, d->name.text);
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

/*
 * §3: "`@class X;` forward declaration — automatic — the emitter manages it".
 * A class-typed property or parameter needs the name known before the
 * `@interface` that uses it, and source order does not have to make that true —
 * a nested type is emitted after the class it is nested in, and two classes may
 * name each other.
 *
 * Only REFERENCED classes get a line, which is why §2's specimen is unchanged: it
 * names no class type at all. `@class` is all a POINTER needs; a superclass needs
 * the whole `@interface`, so a subclass still has to follow its superclass in the
 * source — stated in the plan rather than pretended away.
 */
#define EMIT_MAX_REFS 64

typedef struct {
	char names[EMIT_MAX_REFS][256];
	size_t count;
} type_refs;

static void
add_type_ref(type_refs *refs, const st_type *t)
{
	char mangled[256];
	size_t i;

	if (t == NULL || t->name.text == NULL) {
		return;
	}
	mangle_name(t->name.text, mangled, sizeof(mangled));
	for (i = 0; i < refs->count; i++) {
		if (strcmp(refs->names[i], mangled) == 0) {
			return;
		}
	}
	if (refs->count < EMIT_MAX_REFS) {
		snprintf(refs->names[refs->count++], 256, "%s", mangled);
	}
}

static void
collect_class_type_refs(const st_class *c, type_refs *refs)
{
	const st_decl *d;
	size_t i;

	for (d = c->decls; d != NULL; d = d->next) {
		add_type_ref(refs, &d->type);
		for (i = 0; i < d->param_count; i++) {
			add_type_ref(refs, &d->params[i].type);
		}
	}
	for (i = 0; i < c->nested_count; i++) {
		collect_class_type_refs(c->nested[i], refs);
	}
}

/* True when `mangled` names a class the program declares, at any depth. */
static int
declares_name(const st_class *c, const char *mangled)
{
	char buf[256];
	size_t i;

	mangle_name(c->name.text, buf, sizeof(buf));
	if (strcmp(buf, mangled) == 0) {
		return 1;
	}
	for (i = 0; i < c->nested_count; i++) {
		if (declares_name(c->nested[i], mangled)) {
			return 1;
		}
	}
	return 0;
}

/*
 * The members of an `@interface` — properties, then methods — written once and
 * called from both a class's interface and §7.4's extension interfaces, so the
 * two cannot drift into two dialects.
 *
 * The blank line after the `@interface` is the CALLER's gap. The blank line
 * before the methods separates the two groups and is owed only when both exist:
 * emitting it unconditionally gave a properties-only class two blanks before
 * `@end`, and a methods-only one two after `@interface` — §2's specimen has
 * both, so neither shows there.
 */
static int
emit_member_decls(FILE *out, const st_decl *decls, const char **error)
{
	const st_decl *d;
	int any_property = 0;
	int any_method = 0;

	for (d = decls; d != NULL; d = d->next) {
		if (d->kind == ST_DECL_PROPERTY) {
			any_property = 1;
		} else if (d->kind == ST_DECL_METHOD) {
			any_method = 1;
		}
	}
	for (d = decls; d != NULL; d = d->next) {
		if (d->kind != ST_DECL_PROPERTY) {
			continue;
		}
		/*
		 * The third argument is `is_stored`: §7.54's computed form (a
		 * property carrying a block) is a getter, not an ivar, and that
		 * is what makes it legal in a category.
		 */
		if (!emit_property_line(out, d, d->body == NULL, error)) {
			return 0;
		}
	}
	if (any_property && any_method) {
		fprintf(out, "\n");
	}
	for (d = decls; d != NULL; d = d->next) {
		if (d->kind == ST_DECL_METHOD) {
			if (!emit_signature(out, d, ";", error)) {
				return 0;
			}
		}
	}
	return 1;
}

/*
 * One class's `@interface`, preceded by the types declared inside it. Nested
 * types come first so that a property OF a nested type is a complete type rather
 * than a forward-declared pointer, and the `@class` lines above cover the other
 * direction — a nested type naming the class it is nested in.
 */
static int
emit_interface(FILE *out, const st_class *c, const char **error)
{
	char namebuf[256];
	char superbuf[256];
	size_t i;

	if (c->parameter_count > 0) {
		return refuse("generic parameters", error);
	}
	if (c->struct_count > 0) {
		return refuse("a nested struct", error);
	}
	if (c->enum_count > 0) {
		return refuse("a nested enum", error);
	}
	for (i = 0; i < c->nested_count; i++) {
		if (!emit_interface(out, c->nested[i], error)) {
			return 0;
		}
	}

	mangle_name(c->name.text, namebuf, sizeof(namebuf));
	superclass_text(c->superclass.text, superbuf, sizeof(superbuf));
	fprintf(out, "@interface %s : %s", namebuf, superbuf);
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

	if (!emit_member_decls(out, c->decls, error)) {
		return 0;
	}
	fprintf(out, "\n@end\n\n");
	return 1;
}

/*
 * §7.4: one `extension X { … }` or `category X (Name) { … }` as an
 * `@interface`, emitted after every class. The whole difference between the two
 * forms in what is written here is the name — `@interface X ()` against
 * `@interface X (Name)` — and the rest is the rule that goes with it.
 *
 * Two refusals, both because the language knows something clang either cannot
 * say or says without checking:
 *
 *   - A STORED property in a CATEGORY. Measured: clang rejects the ivar outright
 *     ("instance variables may not be placed in categories"), and §7.4's whole
 *     argument for two words rather than one was that the storage rule becomes
 *     statable against a NAMED intent — so the language states it.
 *   - An EXTENSION for a class this unit does not define. §5 says `extension X`
 *     is legal "only where the unit also defines X", and — measured — a class
 *     extension carrying an ivar in a unit that does not implement the class
 *     compiles CLEAN in clang. Nothing checks it, and the consequence is an ABI
 *     one, because that unit's view of the class's layout is not the class's. A
 *     category on a class the unit does not own is the point of a category, and
 *     is emitted as one.
 */
static int
emit_extension_interface(FILE *out, const st_extension *e,
			 const st_program *program, const char **error)
{
	char targetbuf[256];
	const st_decl *d;
	size_t i;

	mangle_name(e->target.text, targetbuf, sizeof(targetbuf));
	if (!e->is_category) {
		int declared = 0;

		for (i = 0; i < program->class_count; i++) {
			if (declares_name(program->classes[i], targetbuf)) {
				declared = 1;
				break;
			}
		}
		if (!declared) {
			return refuse("an extension for a class this unit does "
				      "not define", error);
		}
	} else {
		/*
		 * §7.54's computed form — a property carrying a block — is a
		 * getter rather than an ivar, and is legal here; the stored one is
		 * what a category may not have.
		 */
		for (d = e->decls; d != NULL; d = d->next) {
			if (d->kind == ST_DECL_PROPERTY && d->body == NULL) {
				return refuse("a stored property in a category",
					      error);
			}
		}
	}

	if (e->is_category) {
		fprintf(out, "@interface %s (%s)", targetbuf, e->name.text);
	} else {
		fprintf(out, "@interface %s ()", targetbuf);
	}
	if (e->conformance_count > 0) {
		fprintf(out, " <");
		for (i = 0; i < e->conformance_count; i++) {
			fprintf(out, "%s%s", i > 0 ? ", " : "",
				e->conformances[i].text);
		}
		fprintf(out, ">");
	}
	fprintf(out, "\n\n");

	if (!emit_member_decls(out, e->decls, error)) {
		return 0;
	}
	fprintf(out, "\n@end\n\n");
	return 1;
}

/* ---- §6/§3.14: `x!` and its trap --------------------------------------- */

/*
 * §6, quoted because every decision here is already in it: "`x!` on a `T?` yields
 * the raw value and **traps if it is `nil`**. It is the first of the language's
 * three *trapping* operations … which are collectively §0's exception: ObjC has
 * no 'crash if null', so `x!` is compiled to a null check that aborts." And the
 * emitted shape is "a **header-only** macro rather than a library function, so the
 * plan's 'no runtime library to stage' still holds".
 *
 * Two things this settles, because the macro is where the language's only runtime
 * behaviour lives:
 *
 *   - The trap is **`__builtin_trap()`** — one instruction, unconditional, and it
 *     needs no header at all, which is what "header-only" has to mean when the
 *     generated header is the only thing a hand-written `.m` may include. §6
 *     forbids the tempting alternative by name: an `NDEBUG`-stripped assert
 *     "would silently make `!` non-trapping in an optimised build".
 *   - It is emitted **only when the unit unwraps something**. A macro block in
 *     every generated header would be dead text in the many units that never
 *     write `x!` — and §2's specimen is a byte-for-byte golden, so emitting it
 *     unconditionally would change the one output the language is specified by.
 *
 * The statement expression carrying `__typeof__(v)` is a clang/GNU extension and
 * is used deliberately: it evaluates the operand ONCE, which a macro that pasted
 * `(v)` into a null test and then returned it could not promise.
 */
static int
expr_uses_unwrap(const st_expr *e)
{
	size_t i;

	if (e == NULL) {
		return 0;
	}
	if (e->kind == ST_EXPR_UNWRAP) {
		return 1;
	}
	if (expr_uses_unwrap(e->base)) {
		return 1;
	}
	for (i = 0; i < e->arg_count; i++) {
		if (expr_uses_unwrap(e->args[i].value)) {
			return 1;
		}
	}
	return 0;
}

static int
stmts_use_unwrap(const st_stmt *list)
{
	const st_stmt *s;

	for (s = list; s != NULL; s = s->next) {
		if (expr_uses_unwrap(s->value) || stmts_use_unwrap(s->body) ||
		    stmts_use_unwrap(s->else_body)) {
			return 1;
		}
	}
	return 0;
}

static int
decls_use_unwrap(const st_decl *decls)
{
	const st_decl *d;

	for (d = decls; d != NULL; d = d->next) {
		if (stmts_use_unwrap(d->body)) {
			return 1;
		}
	}
	return 0;
}

static int
class_uses_unwrap(const st_class *c)
{
	size_t i;

	if (decls_use_unwrap(c->decls)) {
		return 1;
	}
	for (i = 0; i < c->nested_count; i++) {
		if (class_uses_unwrap(c->nested[i])) {
			return 1;
		}
	}
	return 0;
}

/* §7.4's extensions carry bodies too, so they can unwrap as well. */
static int
program_uses_unwrap(const st_program *program)
{
	size_t i;

	for (i = 0; i < program->class_count; i++) {
		if (class_uses_unwrap(program->classes[i])) {
			return 1;
		}
	}
	for (i = 0; i < program->extension_count; i++) {
		if (decls_use_unwrap(program->extensions[i]->decls)) {
			return 1;
		}
	}
	return 0;
}

static void
emit_trap_support(FILE *out)
{
	fprintf(out,
		"/* §6/§3.14: `x!` — the language's one piece of runtime behaviour. The\n"
		"   trap is a BUILTIN rather than an assert, because an NDEBUG-stripped\n"
		"   assert would silently make `!` non-trapping in an optimised build. */\n");
	fprintf(out, "#define sterlingc_trap_null() __builtin_trap()\n");
	fprintf(out, "#define STERLING_UNWRAP(v) \\\n"
		     "\t({ __typeof__(v) __sterling_v = (v); \\\n"
		     "\tif (!__sterling_v) sterlingc_trap_null(); __sterling_v; })\n\n");
}

int
st_emit_header(FILE *out, const st_program *program, const char *source_label,
	       const char **error)
{
	size_t i;
	char stem[256];

	/*
	 * A top-level struct or enum has no emission ANYWHERE yet, and the tree
	 * does not even hold one — the parse reads the declaration and keeps
	 * nothing. Refused by name, so the file that comes out is never quietly
	 * missing a type the source declared.
	 */
	if (program->struct_count > 0) {
		return refuse("a struct", error);
	}
	if (program->enum_count > 0) {
		return refuse("an enum", error);
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
	/*
	 * Which class names this file declares, before anything reads a type: a
	 * nested class is a reference type §4's table does not know and whose
	 * pointer `type_text` has to add.
	 */
	declared_classes.count = 0;
	for (i = 0; i < program->class_count; i++) {
		collect_declared_classes(program->classes[i], &declared_classes);
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
	/*
	 * §6's `x!` support, only where the unit unwraps something. Placed after
	 * the import and BEFORE the assumed-non-null region: a macro is not a
	 * declaration, so it has no nullability to be inside one for.
	 */
	if (program_uses_unwrap(program)) {
		emit_trap_support(out);
	}
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

	/*
	 * §3's `@class` lines: one for each class this file declares AND a
	 * declaration here names as a type. Placed with the protocol forwards,
	 * before anything that could use them.
	 */
	{
		type_refs refs;
		size_t r;
		size_t printed = 0;

		refs.count = 0;
		for (i = 0; i < program->class_count; i++) {
			collect_class_type_refs(program->classes[i], &refs);
		}
		for (r = 0; r < refs.count; r++) {
			size_t ci;

			for (ci = 0; ci < program->class_count; ci++) {
				if (declares_name(program->classes[ci],
						  refs.names[r])) {
					fprintf(out, "@class %s;\n",
						refs.names[r]);
					printed++;
					break;
				}
			}
		}
		if (printed > 0) {
			fprintf(out, "\n");
		}
	}

	for (i = 0; i < program->class_count; i++) {
		if (!emit_interface(out, program->classes[i], error)) {
			return 0;
		}
	}
	/*
	 * §7.4: the extensions' own interfaces, AFTER every class. An extension
	 * needs its class's `@interface` visible — measured, `@class X;` followed
	 * by `@interface X ()` is "cannot define class extension for undefined
	 * class 'X'" — and emitting them here rather than in source order is what
	 * makes `extension X` work whichever order the file wrote `X` in. A
	 * category on a class this unit does not own gets the same placement, and
	 * needs only the import that carries the class to be visible.
	 */
	for (i = 0; i < program->extension_count; i++) {
		if (!emit_extension_interface(out, program->extensions[i],
					      program, error)) {
			return 0;
		}
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

/*
 * §7.42: "an unqualified call in a method body is a message send when the name
 * resolves to a method — this class's or an inherited one — and falls back to a
 * global C function otherwise."
 *
 * The tree is REWRITTEN in place for the resolvable half: a call naming one of
 * THIS class's methods becomes a send on `self`, which is what the rest of the
 * emitter already walks. A rewrite rather than a rule inside emit_expr because
 * the resolution needs the enclosing CLASS, and threading it through every
 * expression function to be consulted at exactly one node is worse than
 * resolving once.
 *
 * ONLY this class's own methods resolve. An inherited one needs the superclass's
 * declarations, which for a Foundation superclass means §9.5's header importer,
 * so it falls through to the C-call path — which emits no `extern` for a name it
 * cannot see and therefore fails at clang, loudly, rather than quietly calling
 * something that does not exist. That is the honest half of a rule whose other
 * half is not available yet.
 */
static void
resolve_calls_in_expr(const st_class *c, st_expr *e)
{
	size_t i;

	if (e == NULL) {
		return;
	}
	if (e->kind == ST_EXPR_CALL && e->base != NULL &&
	    e->base->kind == ST_EXPR_IDENT && e->base->text.text != NULL &&
	    name_is_method(c, e->base->text.text)) {
		st_expr *receiver = st_arena_alloc(sizeof(st_expr));

		if (receiver != NULL) {
			receiver->kind = ST_EXPR_SELF;
			/* The callee's name becomes the selector's first piece. */
			e->text = e->base->text;
			e->base = receiver;
			e->kind = ST_EXPR_SEND;
		}
		/*
		 * On an allocation failure the node is left as a CALL, which is
		 * the pre-§7.42 behaviour and fails at clang rather than
		 * silently.
		 */
	}
	resolve_calls_in_expr(c, e->base);
	for (i = 0; i < e->arg_count; i++) {
		resolve_calls_in_expr(c, e->args[i].value);
	}
}

static void
resolve_calls_in_stmts(const st_class *c, st_stmt *list)
{
	st_stmt *s;

	for (s = list; s != NULL; s = s->next) {
		/*
		 * `s->value` is the initializer for a LET/VAR as well as the
		 * expression for an EXPR and the value for a RETURN — the one
		 * field, per `kind`.
		 */
		resolve_calls_in_expr(c, s->value);
		resolve_calls_in_stmts(c, s->body);
		resolve_calls_in_stmts(c, s->else_body);
	}
}

/*
 * §7.49/§7.8: `T(value: 3)` is a CLASS-RECEIVER send and not a C call — the
 * initializer named `init` is what it calls — and §7.49 gives the emitted form as
 * `[[T alloc] initValue:3]`, `alloc` and `instancetype` included.
 *
 * A REWRITE, for the same reason §7.42's resolution is one, and with a second
 * consequence that matters as much: the `extern` collection below walks the tree
 * afterwards and consults node KIND, so a construction left as a CALL was handed
 * `extern void T(BOOL)` — a declaration for a C function named after the class,
 * which is what `self = Base(0)` used to emit.
 *
 * The selector pieces are a SEND's rather than a CALL's, and that is not a
 * detail: `init(x: Int32, y: Int32)` DECLARES `init:(int32_t)x y:(int32_t)y` (the
 * first piece is the method's name and later pieces are the labels), so a call
 * built by any other rule disagrees with it by a piece — and clang then rejects a
 * send to a method that is right there in the same file. An ST_EXPR_SEND node
 * makes the agreement structural, and the compile leg is what proves it.
 *
 * Only a class THIS UNIT declares constructs. An imported name keeps the old
 * behaviour — a C call, and an `extern` — which fails at clang loudly rather than
 * inventing an `alloc` for a class the emitter cannot see.
 */
static int
is_construction_call(const st_expr *e)
{
	return e != NULL && e->kind == ST_EXPR_CALL && e->base != NULL &&
	       e->base->kind == ST_EXPR_IDENT && e->base->text.text != NULL &&
	       type_name_is_class(e->base->text.text);
}

/*
 * A send node the arena owns. NULL leaves the caller's node alone, which keeps
 * that case LOUD — a C call that will not compile — rather than silent.
 */
static st_expr *
new_message(st_expr *receiver, const char *piece)
{
	st_expr *s = st_arena_alloc(sizeof(st_expr));

	if (s == NULL) {
		return NULL;
	}
	s->kind = ST_EXPR_SEND;
	/* `piece` is a literal; nothing writes through it. */
	s->text.text = (char *)piece;
	s->base = receiver;
	return s;
}

static int
resolve_constructions_in_expr(st_expr *e, const st_class *c, int in_init,
			      const char **error)
{
	size_t i;

	if (e == NULL) {
		return 1;
	}
	/*
	 * §7.49's CHAIN first: `self = Superclass()` inside an initializer is the
	 * one place `self` is writable, and the emitted receiver is `super` rather
	 * than a fresh object — `[[Superclass alloc] init]` would discard the
	 * object being initialized, which is the opposite of chaining.
	 */
	if (in_init && e->kind == ST_EXPR_ASSIGN && e->base != NULL &&
	    e->base->kind == ST_EXPR_SELF && e->arg_count == 1 &&
	    is_construction_call(e->args[0].value)) {
		st_expr *chain = e->args[0].value;
		st_expr *super = st_arena_alloc(sizeof(st_expr));

		/*
		 * §7.49: "the receiver names the superclass". CHECKED rather than
		 * assumed — the emitted `[super init]` is a send to whatever
		 * `super` is, so a receiver naming some other class would quietly
		 * call a different initializer than the one that was written.
		 */
		if (strcmp(chain->base->text.text, c->superclass.text) != 0) {
			return refuse("an initializer chaining to a class that is "
				      "not its superclass", error);
		}
		if (super != NULL) {
			super->kind = ST_EXPR_SUPER;
			chain->base = super;
			chain->kind = ST_EXPR_SEND;
			chain->text.text = (char *)"init";
		}
	}
	if (is_construction_call(e)) {
		st_expr *alloc = new_message(e->base, "alloc");

		if (alloc != NULL) {
			e->base = alloc;
			e->kind = ST_EXPR_SEND;
			e->text.text = (char *)"init";
		}
	}
	if (!resolve_constructions_in_expr(e->base, c, in_init, error)) {
		return 0;
	}
	for (i = 0; i < e->arg_count; i++) {
		if (!resolve_constructions_in_expr(e->args[i].value, c, in_init,
						   error)) {
			return 0;
		}
	}
	return 1;
}

static int
resolve_constructions_in_stmts(const st_class *c, st_stmt *list, int in_init,
			       const char **error)
{
	st_stmt *s;

	for (s = list; s != NULL; s = s->next) {
		if (!resolve_constructions_in_expr(s->value, c, in_init, error)) {
			return 0;
		}
		if (!resolve_constructions_in_stmts(c, s->body, in_init, error)) {
			return 0;
		}
		if (!resolve_constructions_in_stmts(c, s->else_body, in_init,
						    error)) {
			return 0;
		}
	}
	return 1;
}

static int
resolve_constructions_in_class(const st_class *c, const char **error)
{
	const st_decl *d;
	size_t i;

	for (d = c->decls; d != NULL; d = d->next) {
		if (!resolve_constructions_in_stmts(c, d->body, is_initializer(d),
						    error)) {
			return 0;
		}
	}
	for (i = 0; i < c->nested_count; i++) {
		if (!resolve_constructions_in_class(c->nested[i], error)) {
			return 0;
		}
	}
	return 1;
}

/*
 * The whole program, once, before anything is emitted. A construction is a CLASS
 * question — which class, and is it one of ours — and the chain is an
 * initializer question — which method is it in — so both are answered here, per
 * decl, rather than threaded through every expression function to be consulted at
 * one node each.
 */
static int
resolve_constructions_in_program(const st_program *program, const char **error)
{
	size_t i;

	for (i = 0; i < program->class_count; i++) {
		if (!resolve_constructions_in_class(program->classes[i], error)) {
			return 0;
		}
	}
	/*
	 * §7.4's extensions carry bodies too, and an extension's bodies belong to
	 * the class it extends — the class §7.42 resolves against, and the class
	 * whose superclass a chain inside one must name.
	 */
	for (i = 0; i < program->extension_count; i++) {
		const st_extension *e = program->extensions[i];
		const st_class *owner = NULL;
		const st_decl *d;
		size_t k;

		for (k = 0; k < program->class_count; k++) {
			if (strcmp(program->classes[k]->name.text,
				   e->target.text) == 0) {
				owner = program->classes[k];
				break;
			}
		}
		if (owner == NULL) {
			continue;	/* imported: the CALLs stay loud */
		}
		for (d = e->decls; d != NULL; d = d->next) {
			if (!resolve_constructions_in_stmts(owner, d->body,
							    is_initializer(d),
							    error)) {
				return 0;
			}
		}
	}
	return 1;
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

/*
 * §7.4: one declaration list's DEFINITIONS — the method bodies, and the computed
 * property whose block IS its getter (§7.54). Shared by a class's own
 * `@implementation` and a category's, which is where a category's bodies go.
 *
 * A class EXTENSION's bodies come through here too, but from the CLASS's
 * `@implementation`: measured, `@implementation X ()` is not legal ObjC — clang
 * says `expected identifier` at the `)` — so an extension contributes
 * declarations to `@interface X ()` and definitions to `@implementation X`.
 */
static int
emit_definitions(FILE *out, const st_decl *decls, const char **error)
{
	const st_decl *d;

	for (d = decls; d != NULL; d = d->next) {
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
		/*
		 * §7.49: an initializer's body ENDS in `return self;` whatever the
		 * author wrote. It is the EMITTER that needs the statement, not the
		 * language: ObjC's `- (instancetype)init` has to return the object,
		 * and a Sterling initializer writes no `return` at all.
		 *
		 * Appended unconditionally, which is what makes a spelled-out
		 * `return self` "accepted and ignored" rather than a special case —
		 * the author's own return becomes a redundant statement in front of
		 * the one the language always writes, and the last word is the
		 * emitter's.
		 */
		if (is_initializer(d)) {
			fprintf(out, "\treturn self;\n");
		}
		fprintf(out, "}\n\n");
	}
	/* §7.54: a read-only property's block *is* its getter. */
	for (d = decls; d != NULL; d = d->next) {
		if (d->kind != ST_DECL_PROPERTY || d->body == NULL) {
			continue;
		}
		/*
		 * The signature's shape rather than a hand-rolled `- (%s)%s`: a
		 * getter's type goes through the same mangling and the same
		 * `_Nullable` rule as every other type here.
		 */
		fprintf(out, "- (");
		emit_type(out, &d->type);
		fprintf(out, ")%s\n", d->name.text);
		fprintf(out, "{\n");
		if (!emit_stmt_list(out, d->body, 1,
				    map_type(d->type.name.text), error)) {
			return 0;
		}
		fprintf(out, "}\n\n");
	}
	return 1;
}

/*
 * One class's `@implementation`, preceded by the types declared inside it, for
 * the same reason the interface does it that way: a nested type is a class of
 * its own — Objective-C has no nesting — so it is emitted beside its outer class
 * under its mangled name.
 *
 * `first` spans the whole FILE: the blank line between two classes is owed only
 * BETWEEN them, so the flag is threaded rather than the separator being
 * unconditional — §2's specimen is one class and its `.m` ends at `@end`.
 */
static int
emit_implementation_of(FILE *out, const st_class *c,
		       const st_program *program, int *first,
		       const char **error)
{
	char namebuf[256];
	const st_decl *d;
	extern_set set;
	size_t i;

	if (c->parameter_count > 0) {
		return refuse("generic parameters", error);
	}
	if (c->struct_count > 0) {
		return refuse("a nested struct", error);
	}
	if (c->enum_count > 0) {
		return refuse("a nested enum", error);
	}
	for (i = 0; i < c->nested_count; i++) {
		if (!emit_implementation_of(out, c->nested[i], program, first,
					    error)) {
			return 0;
		}
	}
	if (!*first) {
		fprintf(out, "\n");
	}
	*first = 0;

	/*
	 * §7.42 first: a call naming this class's own method becomes a send on
	 * `self`, so the `extern` collection below — which sees only what is left
	 * as a CALL — does not declare a C function for it.
	 *
	 * §7.4's class extensions resolve here TOO, and this is the place the
	 * feature was most likely to go quietly wrong: an extension's bodies are
	 * part of THIS class's implementation, so a call of the class's own method
	 * written inside one has to resolve exactly as a call written in the class
	 * body does. Resolving the extension's bodies later — or not at all —
	 * would leave the call a CALL, and the collection below would then give it
	 * an `extern` declaring a C function that does not exist.
	 */
	for (d = c->decls; d != NULL; d = d->next) {
		resolve_calls_in_stmts(c, d->body);
	}
	for (i = 0; i < program->extension_count; i++) {
		const st_extension *e = program->extensions[i];

		if (e->is_category || strcmp(e->target.text, c->name.text) != 0) {
			continue;
		}
		for (d = e->decls; d != NULL; d = d->next) {
			resolve_calls_in_stmts(c, d->body);
		}
	}
	set.count = 0;
	for (d = c->decls; d != NULL; d = d->next) {
		collect_stmt_calls(d->body, c, &set);
	}
	for (i = 0; i < program->extension_count; i++) {
		const st_extension *e = program->extensions[i];

		if (e->is_category || strcmp(e->target.text, c->name.text) != 0) {
			continue;
		}
		for (d = e->decls; d != NULL; d = d->next) {
			collect_stmt_calls(d->body, c, &set);
		}
	}
	for (i = 0; i < set.count; i++) {
		emit_extern(out, set.calls[i]);
	}

	mangle_name(c->name.text, namebuf, sizeof(namebuf));
	fprintf(out, "@implementation %s\n\n", namebuf);
	if (!emit_definitions(out, c->decls, error)) {
		return 0;
	}
	/*
	 * §7.4: a class extension's DEFINITIONS land here, inside the class's own
	 * `@implementation` — the declarations went into `@interface X ()` above,
	 * and this is the other half the measurement forces. A category's bodies
	 * go in its own `@implementation X (Name)`, emitted after the classes.
	 */
	for (i = 0; i < program->extension_count; i++) {
		const st_extension *e = program->extensions[i];

		if (e->is_category || strcmp(e->target.text, c->name.text) != 0) {
			continue;
		}
		if (!emit_definitions(out, e->decls, error)) {
			return 0;
		}
	}
	fprintf(out, "@end\n");
	return 1;
}

int
st_emit_implementation(FILE *out, const st_program *program,
		       const char *source_label, const char **error)
{
	size_t i;
	int first = 1;
	char stem[256];

	/*
	 * A top-level struct or enum has no emission ANYWHERE yet, and the tree
	 * does not even hold one — the parse reads the declaration and keeps
	 * nothing. Refused by name, so the file that comes out is never quietly
	 * missing a type the source declared.
	 */
	if (program->struct_count > 0) {
		return refuse("a struct", error);
	}
	if (program->enum_count > 0) {
		return refuse("an enum", error);
	}
	if (program->class_count == 0) {
		return refuse("a program with no class (the file's name comes from "
			      "one)", error);
	}
	/* The declared-class set again: this function is called on its own. */
	declared_classes.count = 0;
	for (i = 0; i < program->class_count; i++) {
		collect_declared_classes(program->classes[i], &declared_classes);
	}
	/*
	 * §7.49's construction calls and chains, rewritten before a single line is
	 * emitted — and specifically before `emit_implementation_of`, because that
	 * is where the `extern` collection runs. It consults node KIND, so a
	 * `T(value: 3)` that is still a CALL gets `extern void T(BOOL)`, a
	 * declaration for a C function named after a class.
	 */
	if (!resolve_constructions_in_program(program, error)) {
		return 0;
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
		if (!emit_implementation_of(out, program->classes[i], program,
					    &first, error)) {
			return 0;
		}
	}
	/*
	 * §7.4: the CATEGORIES' implementations, after every class. A category's
	 * bodies have nowhere else to go — `@implementation X (Name)` IS the
	 * category, and clang turns it into a real `.objc_category_X_Name`
	 * object. A class extension does NOT come through here: it has no
	 * implementation of its own, which is what the measurement above settles.
	 */
	for (i = 0; i < program->extension_count; i++) {
		const st_extension *e = program->extensions[i];
		const st_class *owner = NULL;
		const st_decl *d;
		extern_set set;
		char namebuf[256];
		size_t k;

		if (!e->is_category) {
			continue;
		}
		/*
		 * §7.42 resolves against the TARGET class's methods, so the class is
		 * looked up by its Sterling name. A category on a class this unit
		 * does not own has no class to look up, and its unqualified calls
		 * fall back to the C-call path — collected below only when there is
		 * a class to decide against, which is the honest half of a rule
		 * whose other half is §9.5's header importer.
		 */
		for (k = 0; k < program->class_count; k++) {
			if (strcmp(program->classes[k]->name.text,
				   e->target.text) == 0) {
				owner = program->classes[k];
				break;
			}
		}
		if (owner != NULL) {
			for (d = e->decls; d != NULL; d = d->next) {
				resolve_calls_in_stmts(owner, d->body);
			}
			set.count = 0;
			for (d = e->decls; d != NULL; d = d->next) {
				collect_stmt_calls(d->body, owner, &set);
			}
			for (k = 0; k < set.count; k++) {
				emit_extern(out, set.calls[k]);
			}
		}

		/*
		 * The blank line before `@implementation` matches the one between
		 * two classes, which `first` threads for the class path: here every
		 * category is separate from whatever preceded it, so it is
		 * unconditional rather than a flag.
		 */
		fprintf(out, "\n");
		mangle_name(e->target.text, namebuf, sizeof(namebuf));
		fprintf(out, "@implementation %s (%s)\n\n", namebuf,
			e->name.text);
		if (!emit_definitions(out, e->decls, error)) {
			return 0;
		}
		fprintf(out, "@end\n");
	}
	return 1;
}
