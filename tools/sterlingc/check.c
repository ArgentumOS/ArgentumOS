/*
 * check.c — Sterling's semantic pass.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * §7.48: **a type that claims conformance to a protocol and does not implement
 * every required member is a compile-time error.** This is the first thing in
 * the compiler that cannot be decided from syntax — everything before it is a
 * parser and an emitter — which is why it needed the parser to start *recording*
 * conformance lists, protocol requirements, and the `optional` marker rather
 * than dropping them.
 *
 * What counts as implemented:
 *   - a **method**, matched by *selector* rather than by name, since the
 *     selector is what the runtime dispatches on;
 *   - a **property**, matched by name;
 *   - one reached through the **superclass chain**, because an inherited
 *     implementation satisfies a requirement — a `Node` subclass conforming
 *     because `Node` implements the member is conformant;
 *   - and `optional` requirements are **exempt** entirely (§7.48: ObjC's runtime
 *     asks before sending to one).
 *
 * What it does not do yet, stated rather than implied:
 *   - a protocol that *inherits* another does not yet pull in that protocol's
 *     requirements, so a conformance to `Q` where `Q: P` checks only `Q`'s own;
 *   - an `extension`/`category` conformance list is not recorded by the parser
 *     yet, so a claim made there is not checked;
 *   - there is no source position, because the AST carries none — the message
 *     names the type and the missing selector instead, which is enough to find
 *     it and not enough to point at it.
 * Each of those is a next step, and none of them is an assumption.
 */
#include "ast.h"

#include <stdio.h>
#include <string.h>

/*
 * A parameter's selector label, by the same rule emit_signature writes the
 * signature with: a distinct external name is the label, otherwise the name is
 * the parameter's own, and the *first* parameter contributes no label at all
 * (§7.50's `_` and the bare `name:` spelling both land here).
 */
static const char *
label_of(const st_decl *d, size_t i)
{
	const st_param *p = &d->params[i];

	if (p->external.text != NULL && p->internal.text != NULL &&
	    strcmp(p->external.text, p->internal.text) != 0) {
		return p->external.text;
	}
	if (i > 0) {
		return p->internal.text != NULL ? p->internal.text : "";
	}
	return "";
}

/* Two declarations name the same selector. */
static int
same_selector(const st_decl *a, const st_decl *b)
{
	size_t i;

	if (a->name.text == NULL || b->name.text == NULL ||
	    strcmp(a->name.text, b->name.text) != 0) {
		return 0;
	}
	if (a->param_count != b->param_count) {
		return 0;
	}
	for (i = 0; i < a->param_count; i++) {
		if (strcmp(label_of(a, i), label_of(b, i)) != 0) {
			return 0;
		}
	}
	return 1;
}

/* The class with this name, or NULL — the superclass walk resolves through it. */
static const st_class *
class_named(const st_program *prog, const char *name)
{
	size_t i;

	if (name == NULL) {
		return NULL;
	}
	for (i = 0; i < prog->class_count; i++) {
		const st_class *c = prog->classes[i];

		if (c->name.text != NULL && strcmp(c->name.text, name) == 0) {
			return c;
		}
	}
	return NULL;
}

/* The protocol with this name, or NULL. */
static const st_protocol *
protocol_named(const st_program *prog, const char *name)
{
	size_t i;

	if (name == NULL) {
		return NULL;
	}
	for (i = 0; i < prog->protocol_count; i++) {
		const st_protocol *prot = prog->protocols[i];

		if (prot->name.text != NULL && strcmp(prot->name.text, name) == 0) {
			return prot;
		}
	}
	return NULL;
}

/* Does one class's own declarations satisfy `req`? */
static int
declares(const st_class *c, const st_decl *req)
{
	const st_decl *d;

	for (d = c->decls; d != NULL; d = d->next) {
		if (d->kind != req->kind) {
			continue;
		}
		if (req->kind == ST_DECL_METHOD) {
			/* A class method is a different selector namespace, so a
			 * `+` implementation does not satisfy a `-` requirement. */
			if (d->is_class_method != req->is_class_method) {
				continue;
			}
			if (same_selector(d, req)) {
				return 1;
			}
		} else if (d->name.text != NULL && req->name.text != NULL &&
			   strcmp(d->name.text, req->name.text) == 0) {
			return 1;
		}
	}
	return 0;
}

/*
 * Does `c`, or anything in its superclass chain, satisfy `req`? The depth guard
 * is not defensive padding: a cycle in a malformed source would otherwise walk
 * forever, and a front end that hangs on bad input is worse than one that
 * refuses it.
 */
static int
implements_locally(const st_program *prog, const st_class *c,
		   const st_decl *req, int depth)
{
	if (c == NULL || depth > 64) {
		return 0;
	}
	if (declares(c, req)) {
		return 1;
	}
	return implements_locally(prog, class_named(prog, c->superclass.text),
				  req, depth + 1);
}

/* `selector` for a message, written the way ObjC would name it. */
static void
selector_text(const st_decl *d, char *buf, size_t size)
{
	size_t i, used;

	if (d->name.text == NULL) {
		snprintf(buf, size, "(unnamed)");
		return;
	}
	used = (size_t)snprintf(buf, size, "%s", d->name.text);
	for (i = 0; i < d->param_count && used + 1 < size; i++) {
		used += (size_t)snprintf(buf + used, size - used, "%s:",
					 label_of(d, i));
	}
}

/*
 * §7.26's type-argument rule is **amended (2026-09)**: an argument may be *any*
 * type. The bullet that made "must be an object type" a rule was true of ObjC's
 * *bare* lightweight generics and false of this language, which does more with
 * the argument than annotate it — §7.63 erases for an object argument and
 * monomorphises otherwise.
 *
 * A checker enforcing the old reading lived here, and is deliberately **gone**
 * rather than dormant: it rejected `Array<Int32>` in five corpus files, and the
 * corpus was right. What `st_type.arguments` is *for* is §7.63's decision at
 * emission — erase or instantiate — which is the emitter's step, not a check.
 */

/*
 * The pass. Returns NULL when the program satisfies the rule enforced here, and
 * a message naming the first violation otherwise.
 *
 * One rule so far: §7.48's conformance rule. §7.26's type-argument bullet was
 * the second candidate and turned out not to be a rule at all — see the note
 * above — so it is a *deliberate* absence rather than an unbuilt piece.
 */
const char *
st_check(const st_program *program)
{
	static char message[512];
	size_t i;

	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		size_t k;

		for (k = 0; k < c->conformance_count; k++) {
			const st_protocol *prot =
				protocol_named(program, c->conformances[k].text);
			const st_decl *req;

			/* A conformance naming an unknown protocol is not this
			 * check's business: it may be one of ObjC's, imported. */
			if (prot == NULL) {
				continue;
			}
			for (req = prot->requirements; req != NULL;
			     req = req->next) {
				char selector[256];

				if (req->is_optional) {
					continue;
				}
				if (implements_locally(program, c, req, 0)) {
					continue;
				}
				selector_text(req, selector, sizeof(selector));
				snprintf(message, sizeof(message),
					 "%s claims conformance to %s but does "
					 "not implement required member `%s`",
					 c->name.text != NULL ? c->name.text : "(unnamed)",
					 prot->name.text != NULL ? prot->name.text
								 : "(unnamed)",
					 selector);
				return message;
			}
		}
	}
	return NULL;
}
