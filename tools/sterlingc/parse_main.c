/*
 * parse_main.c — the K1 parser driver.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * Lexes and parses §1's specimen, then reports what it found; given an
 * argument it parses that file instead. A malformed sample is parsed last so
 * the acceptance check can see a rejection as well as a success.
 */
#include "ast.h"
#include "sterling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* docs/design/sterling-syntax.md §1, verbatim. */
static const char *specimen =
	"class MyClass: Object {\n"
	"    property myValue: Int64\n"
	"    readonly property dynamicValue: Float32 {\n"
	"        return 0.1;\n"
	"    }\n"
	"\n"
	"    class method baz(arg1: Bool, arg2: String) -> Void {\n"
	"        callSomeFunc(arg: arg1);\n"
	"    }\n"
	"\n"
	"    method foobar(argname: Int32, arg2: Bool) -> Bool {\n"
	"        return false;\n"
	"    }\n"
	"}\n";

static const char *malformed =
	"class Broken: Object {\n"
	"    property x Int64\n"        /* the colon is missing */
	"}\n";

static void
dump_program(const st_program *program)
{
	size_t i, j;

	printf("program: %zu class(es)\n", program->class_count);
	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];
		const st_decl *d;

		printf("  class %s : %s\n", c->name.text, c->superclass.text);
		for (d = c->decls; d != NULL; d = d->next) {
			if (d->kind == ST_DECL_PROPERTY) {
				printf("    property %s : %s%s%s\n", d->name.text,
				       d->type.name.text,
				       d->is_readonly ? " [readonly]" : "",
				       d->body != NULL ? " [body]" : "");
				continue;
			}
			printf("    method %s%s : %s\n",
			       d->is_class_method ? "+" : "-", d->name.text,
			       d->type.name.text);
			for (j = 0; j < d->param_count; j++) {
				const st_param *p = &d->params[j];
				printf("      param ext=%s int=%s type=%s\n",
				       p->external.text != NULL ? p->external.text : "_",
				       p->internal.text != NULL ? p->internal.text : "_",
				       p->type.name.text);
			}
			if (d->body != NULL) {
				const st_stmt *s;
				for (s = d->body; s != NULL; s = s->next) {
					printf("      stmt %s",
					       s->kind == ST_STMT_RETURN ? "return" : "expr");
					if (s->value != NULL &&
					    s->value->kind == ST_EXPR_CALL) {
						printf(" call=%s argc=%zu",
						       s->value->base->text.text,
						       s->value->arg_count);
					} else if (s->value != NULL) {
						printf(" kind=%d", (int)s->value->kind);
					}
					printf("\n");
				}
			}
		}
	}
}

static char *
slurp(const char *path)
{
	FILE *fp = fopen(path, "rb");
	long size;
	char *buf;

	if (fp == NULL) { return NULL; }
	fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
	buf = malloc((size_t)size + 1);
	if (buf == NULL) { fclose(fp); return NULL; }
	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) { fclose(fp); free(buf); return NULL; }
	buf[size] = '\0';
	fclose(fp);
	return buf;
}

int
main(int argc, char **argv)
{
	const char *error = NULL;
	st_program *program;
	char *source;

	if (argc > 1) {
		source = slurp(argv[1]);
		if (source == NULL) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
	} else {
		source = (char *)specimen;
	}

	printf("--- parsing the §1 specimen ---\n");
	program = st_parse(source, &error);
	if (program == NULL) {
		printf("SAMPLE-FAIL %s\n", error != NULL ? error : "?");
		return 1;
	}
	dump_program(program);
	printf("SAMPLE-OK\n");
	st_arena_free();

	printf("--- rejecting a malformed class ---\n");
	program = st_parse(malformed, &error);
	if (program != NULL) {
		printf("REJECT-FAIL it parsed\n");
		st_arena_free();
		return 1;
	}
	printf("REJECT-OK %s\n", error != NULL ? error : "?");
	return 0;
}
