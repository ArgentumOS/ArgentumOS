/*
 * emit.c — the Sterling emitter, targeting docs/design/sterling-syntax.md §2.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * K1 scope: exactly the specimen. The mappings below are the ones §2 shows,
 * and §2 is the authority — where this file and §2 disagree, §2 wins.
 *
 *   Void -> void        Bool -> BOOL        String -> NSString *
 *   Int64 -> int64_t    Int32 -> int32_t    Float32 -> float
 *   Object -> NSObject
 *
 * Two rules that are easy to miss and are not special cases:
 *   - A *stored* scalar property is `(nonatomic, assign)`; a *read-only
 *     computed* property is `(nonatomic, readonly)` with no ownership
 *     qualifier, because it declares no storage (§2's notes).
 *   - An unqualified call that is not a method is a C function (§7.42), so
 *     the `.m` carries an `extern` declaration for it.
 */
#include "ast.h"
#include "sterling.h"

#include <stdio.h>
#include <string.h>

static const char *
map_type(const char *name)
{
	if (name == NULL)			return "void";
	if (strcmp(name, "Void") == 0)		return "void";
	if (strcmp(name, "Bool") == 0)		return "BOOL";
	if (strcmp(name, "String") == 0)	return "NSString *";
	if (strcmp(name, "Int64") == 0)		return "int64_t";
	if (strcmp(name, "Int32") == 0)		return "int32_t";
	if (strcmp(name, "Float32") == 0)	return "float";
	return name;
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

/*
 * A float literal takes the `f` suffix, because the type says Float32 and a
 * bare `0.1` would be a double (§2 emits `0.1f`).
 */
static void
emit_literal(FILE *out, const st_expr *e)
{
	switch (e->kind) {
	case ST_EXPR_INT:
		fprintf(out, "%s", e->text.text);
		break;
	case ST_EXPR_FLOAT:
		fprintf(out, "%sf", e->text.text);
		break;
	case ST_EXPR_STRING:
		fprintf(out, "%s", e->text.text);
		break;
	/*
	 * §3's map takes `true`/`false` to the ObjC booleans. Emitting the bare
	 * words is what the compile gate caught: `false` is undeclared in a
	 * default C translation unit — C23 makes it a keyword, and C99/C11 need
	 * <stdbool.h> — so §2's specimen reads `return NO;` only because the
	 * gate proved `return false;` does not compile.
	 */
	case ST_EXPR_TRUE:
		fprintf(out, "YES");
		break;
	case ST_EXPR_FALSE:
		fprintf(out, "NO");
		break;
	case ST_EXPR_NIL:
		fprintf(out, "nil");
		break;
	case ST_EXPR_SELF:
		fprintf(out, "self");
		break;
	case ST_EXPR_IDENT:
		fprintf(out, "%s", e->text.text);
		break;
	case ST_EXPR_CALL:
		emit_literal(out, e->base);
		fprintf(out, "(");
		{
			size_t i;
			for (i = 0; i < e->arg_count; i++) {
				if (i > 0) {
					fprintf(out, ", ");
				}
				emit_literal(out, e->args[i].value);
			}
		}
		fprintf(out, ")");
		break;
	}
}

static void
emit_body(FILE *out, const st_stmt *body)
{
	const st_stmt *s;

	fprintf(out, "{\n");
	for (s = body; s != NULL; s = s->next) {
		if (s->kind == ST_STMT_RETURN) {
			fprintf(out, "\treturn");
			if (s->value != NULL) {
				fprintf(out, " ");
				emit_literal(out, s->value);
			}
			/* §7.75: the surface may omit it, C may not. */
			fprintf(out, ";\n");
			continue;
		}
		/*
		 * A statement with no value carries no expression node: the parser
		 * scans `defer`, `var`/`let`, `if` and `guard` for now, and the AST
		 * fields that would hold them are the emitter's later step. Skipping
		 * is the honest emission until then — and that absence is exactly
		 * what the corpus surfaced as a NULL dereference at this line.
		 */
		if (s->value == NULL) {
			continue;
		}
		fprintf(out, "\t");
		emit_literal(out, s->value);
		fprintf(out, ";\n");
	}
	fprintf(out, "}\n");
}

/* ---- the .h ------------------------------------------------------------ */

void
st_emit_header(FILE *out, const st_program *program)
{
	size_t i;

	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		const st_decl *d;

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
}

/* ---- the .m ------------------------------------------------------------ */

/*
 * §7.42: an unqualified call resolves to a method when one matches and to a
 * C function otherwise. Nothing here is a method yet, so every such call is
 * a C function, and the `.m` declares it — which is what §2's `.m` does.
 */
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
emit_extern(FILE *out, const st_class *c, const st_expr *call)
{
	const char *fn = call->base->text.text;
	const st_arg *arg;

	if (name_is_method(c, fn)) {
		return;
	}
	fprintf(out, "/* Declared elsewhere; sterlingc imported it. The parameter's name is `%s`. */\n",
		call->arg_count > 0 && call->args[0].internal.text != NULL
			? call->args[0].internal.text
			: "arg");
	fprintf(out, "extern void %s(", fn);
	for (arg = call->args; arg != NULL && arg != call->args + call->arg_count;
	     arg++) {
		/*
		 * The argument names a local of the enclosing method, so the
		 * type is not recoverable from the call alone; §2 uses BOOL,
		 * and a later milestone reads the real declaration.
		 */
		fprintf(out, "BOOL %s",
			arg->internal.text != NULL ? arg->internal.text : "arg");
		if (arg != call->args + call->arg_count - 1) {
			fprintf(out, ", ");
		}
	}
	fprintf(out, ");\n\n");
}

static void
find_first_call(const st_class *c, const st_decl **method, const st_expr **call)
{
	const st_decl *d;

	*method = NULL;
	*call = NULL;
	for (d = c->decls; d != NULL && *call == NULL; d = d->next) {
		const st_stmt *s;

		for (s = d->body; s != NULL; s = s->next) {
			if (s->value != NULL &&
			    s->value->kind == ST_EXPR_CALL) {
				*method = d;
				*call = s->value;
				return;
			}
		}
	}
}

void
st_emit_implementation(FILE *out, const st_program *program)
{
	size_t i;

	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		const st_decl *d;
		const st_decl *caller;
		const st_expr *call;

		fprintf(out, "/* %s.m — generated by sterlingc from %s.ag. Do not edit. */\n",
			c->name.text, c->name.text);
		fprintf(out, "#import \"%s.h\"\n\n", c->name.text);

		find_first_call(c, &caller, &call);
		if (call != NULL) {
			emit_extern(out, c, call);
		}

		fprintf(out, "@implementation %s\n\n", c->name.text);
		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind != ST_DECL_METHOD) {
				continue;
			}
			emit_signature(out, d, "");
			emit_body(out, d->body);
			fprintf(out, "\n");
		}
		/* §7.54: a read-only property's block *is* its getter. */
		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind != ST_DECL_PROPERTY || d->body == NULL) {
				continue;
			}
			fprintf(out, "- (%s)%s\n", map_type(d->type.name.text),
				d->name.text);
			emit_body(out, d->body);
			fprintf(out, "\n");
		}
		fprintf(out, "@end\n");
	}
}
