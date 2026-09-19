/*
 * ast.h — the Sterling AST, sized to the §1 specimen.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * K1 scope: this tree holds exactly what the specimen needs — a class with a
 * stored property, a read-only property carrying a getter body, a class
 * method, an instance method, and the statements those bodies contain. Nodes
 * are arena-allocated and never freed individually; a program calls
 * st_arena_free() when it is done.
 */
#ifndef STERLING_AST_H
#define STERLING_AST_H

#include <stddef.h>

/* ---- names ------------------------------------------------------------- */

typedef struct {
	char *text;		/* NUL-terminated copy, owned by the arena */
} st_name;

/* ---- types ------------------------------------------------------------- */

typedef enum {
	ST_TYPE_NAMED = 0,	/* Int64, Float32, Bool, String, Void, ... */
} st_type_kind;

typedef struct st_type {
	st_type_kind kind;
	st_name name;		/* ST_TYPE_NAMED */
	/*
	 * §7.26: the lightweight-generic arguments a type carries, when it carries
	 * any — `Array<String>`'s `String`. Recorded rather than dropped, because
	 * §7.26 makes two rules about them (a type argument must be an object type,
	 * and a C type is out entirely) and a rule about something the AST does not
	 * hold cannot be checked.
	 *
	 * Only the **outermost** argument per position is recorded. The rule is
	 * about whether the argument is an object type, and `Array<Box<String>>`'s
	 * argument is `Box` either way — so a C type nested *inside* a legal one is
	 * not caught yet. Stated rather than assumed.
	 */
	st_name *arguments;
	size_t argument_count;
	/*
	 * §4's `T?`. RECORDED because dropping it is not neutral: the generated
	 * header opens with `_Pragma("clang assume_nonnull begin")`, so a `.ag`
	 * that says `String?` and emits `NSString *` inside that region asserts
	 * the *opposite* of what was written — non-null. The emitter refuses a
	 * nullable type until the `_Nullable` emission exists, which is §4's own
	 * table's (`String?` → `NSString * _Nullable`) and arrives with the
	 * header importer's nullability rules (§9.5).
	 *
	 * A scalar's `?` is §7.62's pair-struct, a different mechanism entirely.
	 */
	int nullable;
} st_type;

/* ---- expressions ------------------------------------------------------- */

typedef enum {
	ST_EXPR_INT = 0,
	ST_EXPR_FLOAT,
	ST_EXPR_STRING,
	ST_EXPR_NIL,
	ST_EXPR_TRUE,
	ST_EXPR_FALSE,
	ST_EXPR_IDENT,		/* a name in scope */
	ST_EXPR_SELF,
	/*
	 * §3's map: `super` is written as it is emitted — `[super init]` and
	 * friends — so it is a receiver like `self` and not a name. It was not a
	 * node at all, and `super` being a KEYWORD rather than an identifier made
	 * `super.foo()` a parse error with no way to spell it.
	 */
	ST_EXPR_SUPER,
	/*
	 * A bare `name(args)` — §7.42: a C function when no method matches and a
	 * message to `self` when one does. It is NOT the message-send node; the
	 * two are separate because the emission differs (a call needs no
	 * receiver and may need an `extern`), and §2's specimen is this case.
	 */
	ST_EXPR_CALL,
	ST_EXPR_MEMBER,		/* base.text — §6's member access */
	ST_EXPR_SEND,		/* base.text(args) — the message send of §6 */
	ST_EXPR_BINARY,		/* base <text> args[0].value */
	ST_EXPR_UNARY,		/* <text> base, prefix */
	ST_EXPR_ASSIGN,		/* base = args[0].value — §7.21 makes it an expression */
	/*
	 * An expression form the surface has and this emitter does not. Like
	 * ST_STMT_UNSUPPORTED, it exists so the emitter can refuse it BY NAME:
	 * the nodes these replace printed something *wrong* — a closure emitted
	 * `nil`, `x!` emitted its operand with the trap missing, `a[i]` emitted
	 * `a` — which is worse than refusing, because the output compiles and
	 * the loss is invisible.
	 */
	ST_EXPR_UNSUPPORTED,
} st_expr_kind;

typedef struct st_expr st_expr;

/* A call argument carries both names: §7.1 makes the external one a piece of
 * the selector and the internal one the name the body sees. At a *call site*
 * only `external` is written and `internal` is unset; on a declaration both
 * are. */
typedef struct st_arg {
	st_name external;
	st_name internal;
	st_expr *value;
} st_arg;

/*
 * One node for every expression form, with the fields' meaning fixed by
 * `kind` — the alternative, a union, would buy nothing here because the
 * widest form (a send) uses base, text and args at once:
 *
 *   INT/FLOAT/STRING/IDENT   text
 *   TRUE/FALSE/NIL/SELF      —
 *   MEMBER                   base (the receiver), text (the member name)
 *   SEND                     base, text (the method name), args
 *   CALL                     base (the callee), args
 *   BINARY                   base (left), text (the operator), args[0].value
 *   UNARY                    base (the operand), text (the operator)
 *   ASSIGN                   base (the target), args[0].value
 *   UNSUPPORTED              text (the construct, for the refusal message)
 */
struct st_expr {
	st_expr_kind kind;
	st_name text;		/* literals and identifiers */
	st_expr *base;		/* ST_EXPR_CALL: the callee (an identifier) */
	st_arg *args;
	size_t arg_count;
};

/* ---- statements -------------------------------------------------------- */

typedef enum {
	ST_STMT_RETURN = 0,
	ST_STMT_EXPR,		/* an expression statement */
	ST_STMT_LET,		/* §5's Local, constant */
	ST_STMT_VAR,		/* §5's Local, writable */
	ST_STMT_IF,		/* §7.67 */
	ST_STMT_WHILE,		/* §7.69 */
	/*
	 * A statement the surface has and this emitter does not. It is a kind of
	 * its own rather than a NULL-valued ST_STMT_EXPR because the emitter must
	 * *refuse* it by name: the version before this one skipped every
	 * statement it could not write, which is the same failure as a scanned
	 * declaration — the input is accepted and nothing says so.
	 */
	ST_STMT_UNSUPPORTED,
} st_stmt_kind;

/*
 * One node for every statement form, again with the fields fixed by `kind`:
 *
 *   RETURN                   value (or NULL)
 *   EXPR                     value
 *   LET/VAR                  name, type (unset when the initializer carries
 *                            it), value (the initializer)
 *   IF                       value (condition), body (then), else_body
 *   WHILE                    value (condition), body
 *   UNSUPPORTED              text (the keyword that owns the construct)
 *
 * `has_else` exists because `else_body == NULL` cannot say whether an `else` was
 * written: `else { }` and no `else` at all are the same NULL, and a compiler that
 * cannot tell them apart is one that drops a written branch.
 */
typedef struct st_stmt {
	st_stmt_kind kind;
	st_expr *value;
	st_name name;		/* ST_STMT_LET/VAR */
	st_type type;		/* ST_STMT_LET/VAR; .name.text == NULL if inferred */
	struct st_stmt *body;	/* ST_STMT_IF/WHILE */
	struct st_stmt *else_body; /* ST_STMT_IF; the else branch, when written */
	int has_else;		/* ST_STMT_IF */
	st_name text;		/* ST_STMT_UNSUPPORTED */
	struct st_stmt *next;
} st_stmt;

/* ---- declarations ------------------------------------------------------ */

typedef enum {
	ST_DECL_PROPERTY = 0,	/* property x: T (optionally with a body) */
	ST_DECL_METHOD,		/* method / class method */
} st_decl_kind;

typedef struct st_param {
	st_name external;	/* may be empty for `_` suppression (§7.50) */
	st_name internal;
	st_type type;
} st_param;

/*
 * §7.52: **exactly one ownership attribute per property** — ObjC's `weak`,
 * `copy`, `strong` and `assign` are mutually exclusive, so `weak copy property
 * x: T` is an error. `strong` and `assign` are the *inferred* pair (§5: a class
 * type is `strong`, a scalar or struct is `assign`) which §7.15 lets an author
 * write out loud.
 *
 * ST_OWN_INFER is "nothing was written", which is NOT the same as
 * ST_OWN_ASSIGN: the emitter still has to infer, and for a class-typed property
 * the answer is `strong`. The emitter's predecessor hardcoded `(nonatomic,
 * assign)` for every non-readonly property, so `property x: Foo` — a class
 * type — emitted `assign`, which is the wrong ownership and a real memory bug in
 * the output.
 */
typedef enum {
	ST_OWN_INFER = 0,	/* nothing written; infer from the type */
	ST_OWN_STRONG,
	ST_OWN_WEAK,
	ST_OWN_COPY,
	ST_OWN_ASSIGN,
	ST_OWN_UNOWNED,		/* §7.53 — parsed, and refused by the emitter */
} st_ownership;

typedef struct st_decl {
	st_decl_kind kind;
	st_name name;
	int is_class_method;	/* ST_DECL_METHOD: `class method` */
	int is_readonly;	/* ST_DECL_PROPERTY */
	/*
	 * §5/§7.48: `optional` on a protocol member. A *required* member must be
	 * implemented by whatever claims conformance; an optional one is exempt,
	 * because ObjC's runtime asks before sending to it.
	 */
	int is_optional;
	st_ownership ownership;	/* ST_DECL_PROPERTY (§7.52) */
	/*
	 * §9.16's stored-property default. RECORDED rather than dropped — the
	 * parser used to parse it into a variable named `discard` — because its
	 * emission is a synthesised *defaults* method rather than an initializer
	 * (neither an ivar nor a C struct member may carry one), so the value has a
	 * reader coming and the emitter refuses the form until that lands.
	 */
	st_expr *initial;
	int has_initial;
	st_type type;		/* both */
	st_param *params;	/* ST_DECL_METHOD */
	size_t param_count;
	st_stmt *body;		/* either; NULL when absent */
	struct st_decl *next;
} st_decl;

/*
 * §7.45: a class's conformance list. It was scanned and dropped until the
 * conformance check needed it — §7.48's rule that a claiming type must
 * implement every required member is the first thing in this compiler that
 * cannot be decided from syntax, so the list has to survive the parse.
 */
typedef struct st_class {
	st_name name;
	st_name superclass;
	st_name *conformances;
	size_t conformance_count;
	/*
	 * §7.26/§7.63: a class may declare lightweight-generic *parameters* —
	 * `class Box<T>: Object`, which emits `@interface Box<T> : NSObject`. They
	 * are parameters here and *arguments* on a type, which is why the two fields
	 * are named differently despite sharing one scan.
	 */
	st_name *parameters;
	size_t parameter_count;
	st_decl *decls;
	/*
	 * A type declared INSIDE this one. `name` carries the QUALIFIED Sterling
	 * name — a `Bar` inside `Foo` is the type `Foo.Bar` — and the ObjC name is
	 * mangled from it lexically (`.` -> `_`), so nothing here needs a symbol
	 * table read to emit it.
	 *
	 * Nested CLASSES are emitted; a nested struct or enum is not, because
	 * neither kind has an emission at all yet, so those are counted and the
	 * emitter refuses them by name rather than dropping them.
	 */
	struct st_class **nested;
	size_t nested_count;
	size_t struct_count;
	size_t enum_count;
} st_class;

/*
 * §5's Protocol kind. A protocol carries *requirements* and no implementations,
 * so `requirements` is a list of signatures: a `method` whose body is NULL, or
 * a `property`. That is the other half of the data the check runs on — the
 * first half being the conforming type's own declarations.
 */
typedef struct {
	st_name name;
	st_name *inherits;
	size_t inherit_count;
	st_decl *requirements;
} st_protocol;

typedef struct {
	st_class **classes;
	size_t class_count;
	st_protocol **protocols;
	size_t protocol_count;
	/*
	 * §7.4's categories and extensions. `parse_extension` reads them and the
	 * result went nowhere — an entire declaration block, dropped with the
	 * parse reporting success. The count is kept so the emitter can REFUSE
	 * by name; the blocks themselves are the emitter's later step, and a
	 * category is not a translation of anything (it is a second
	 * `@interface X (Name)` plus a second `@implementation`).
	 */
	size_t extension_count;
	/*
	 * The two types with no emission ANYWHERE yet: a top-level struct and a
	 * top-level enum. Both were parsed and dropped with the parse reporting
	 * success — `struct Point { … }` alone reported "a program with no class",
	 * which names the wrong loss, and beside a class it reported nothing at
	 * all. Counted so the emitter can name what it cannot write.
	 */
	size_t struct_count;
	size_t enum_count;
} st_program;

/* ---- the arena --------------------------------------------------------- */

void *st_arena_alloc(size_t size);
char *st_arena_strdup(const char *src, size_t len);
void st_arena_free(void);

/* ---- parsing ----------------------------------------------------------- */

/*
 * Parse a whole source file. Returns NULL and sets *error to a message on
 * failure. The returned program lives in the arena.
 */
st_program *st_parse(const char *src, const char **error);

/* ---- checking (check.c) ------------------------------------------------ */

/*
 * §7.48's semantic pass: a type that claims conformance to a protocol and does
 * not implement every required member is an error. Returns NULL when the
 * program satisfies it, and a message naming the first violation otherwise.
 *
 * It is deliberately a separate pass rather than something the parser does: it
 * needs the *whole* program — a protocol may be declared after its conformer,
 * and a superclass may be in a different class declaration — while a parser
 * knows only what it has read so far.
 */
const char *st_check(const st_program *program);

/* ---- emitting (emit.c) ------------------------------------------------- */

#include <stdio.h>

/*
 * §2 is the authority for both: the header carries the interface, the
 * implementation carries the extern declarations of any C function a body
 * calls (§7.42) and the getters of read-only properties (§7.54).
 *
 * ONE `.h`/`.m` pair per PROGRAM. A `.ag` file may declare several classes —
 * ordinary Sterling, and one translation unit — so the pair holds all of them,
 * and it is named after `source_label`, the input file, the way a C compiler
 * names its output. `source_label` is the file's basename; NULL means the
 * built-in specimen, which falls back to its single class's name (§2's
 * `MyClass.h` from `MyClass.ag`).
 *
 * Both return 0 and set *error when the program uses a construct this emitter
 * cannot write. That is a refusal, not a warning: the emitter's predecessor
 * skipped anything it could not represent, so a program could compile to
 * source that had silently lost statements. A construct with no emission
 * stops the compile and says which one it was.
 */
int st_emit_header(FILE *out, const st_program *program,
		   const char *source_label, const char **error);
int st_emit_implementation(FILE *out, const st_program *program,
			   const char *source_label, const char **error);

/*
 * The output stem for a source label: its basename with the extension removed.
 * `label` is the path as given; NULL is the specimen's case and writes nothing.
 * One implementation, because the file names and the banner's "generated from"
 * clause have to agree.
 */
void st_source_stem(const char *label, char *buf, size_t size);

#endif /* STERLING_AST_H */
