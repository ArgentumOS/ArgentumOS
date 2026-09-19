/*
 * emit_main.c — the K1 emitter driver.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * Reads §1's specimen (or a named file) and writes §2's two outputs. With
 * `-o <dir>` the files land as <Class>.h and <Class>.m for the golden check
 * to diff against the document; without it they go to stdout between
 * markers, so the check can be a simple comparison.
 */
#include "ast.h"
#include "sterling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *
slurp(const char *path)
{
	FILE *fp = fopen(path, "rb");
	long size;
	char *buf;

	if (fp == NULL) {
		return NULL;
	}
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = malloc((size_t)size + 1);
	if (buf == NULL) {
		fclose(fp);
		return NULL;
	}
	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
		fclose(fp);
		free(buf);
		return NULL;
	}
	buf[size] = '\0';
	fclose(fp);
	return buf;
}

int
main(int argc, char **argv)
{
	const char *error = NULL;
	const char *outdir = NULL;
	st_program *program;
	char *source;
	size_t i;

	for (i = 1; i < (size_t)argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < (size_t)argc) {
			outdir = argv[++i];
		} else {
			source = slurp(argv[i]);
			if (source == NULL) {
				fprintf(stderr, "sterlingc: cannot read %s\n", argv[i]);
				return 1;
			}
		}
	}
	/* No file named: use the specimen. */
	if (argc == 1 || (argc == 3 && outdir != NULL)) {
		/*
		 * The specimen is a string literal and must never be freed, so
		 * it is copied: that keeps `source` uniformly owned here and
		 * lets the single free() at the end stay unconditional. A
		 * literal reaching free() was the segfault seen when no file
		 * was named — which is also what `sterlingc.sh` used to do,
		 * because its driver branch shifted the filename away.
		 */
		source = malloc(strlen(specimen) + 1);
		if (source == NULL) {
			fprintf(stderr, "sterlingc: out of memory\n");
			return 1;
		}
		memcpy(source, specimen, strlen(specimen) + 1);
	}

	program = st_parse(source, &error);
	if (program == NULL) {
		fprintf(stderr, "sterlingc: %s\n", error != NULL ? error : "parse error");
		return 1;
	}
	/*
	 * §7.48's semantic pass, run on the whole program. It cannot live in the
	 * parser: a protocol may be declared after the type that conforms to it, so
	 * the check needs everything read before it can start.
	 */
	error = st_check(program);
	if (error != NULL) {
		fprintf(stderr, "sterlingc: %s\n", error);
		return 1;
	}

	for (i = 0; i < program->class_count; i++) {
		const st_class *c = program->classes[i];

		if (outdir != NULL) {
			char path[1024];
			FILE *fp;

			snprintf(path, sizeof(path), "%s/%s.h", outdir,
				 c->name.text);
			fp = fopen(path, "wb");
			if (fp == NULL) {
				fprintf(stderr, "sterlingc: cannot write %s\n", path);
				return 1;
			}
			st_emit_header(fp, program);
			fclose(fp);

			snprintf(path, sizeof(path), "%s/%s.m", outdir,
				 c->name.text);
			fp = fopen(path, "wb");
			if (fp == NULL) {
				fprintf(stderr, "sterlingc: cannot write %s\n", path);
				return 1;
			}
			st_emit_implementation(fp, program);
			fclose(fp);
			continue;
		}

		printf("----- %s.h -----\n", c->name.text);
		st_emit_header(stdout, program);
		printf("----- %s.m -----\n", c->name.text);
		st_emit_implementation(stdout, program);
	}

	st_arena_free();
	if (outdir == NULL) {
		free(source);
	}
	return 0;
}
