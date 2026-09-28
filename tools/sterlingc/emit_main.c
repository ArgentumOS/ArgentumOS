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
	const char *source_label = NULL;	/* the input file's basename */
	st_program *program;
	char *source;
	size_t i;
	/*
	 * `--parse` stops after the front end — lexer, parser, §7.48's check —
	 * and emits nothing.
	 *
	 * It exists because two checks were quietly conflating "the front end
	 * rejected this" with "the emitter refused this": the corpus reported
	 * `parses` from the *emitting* driver's exit status, and `--reject`
	 * counted any stderr line as a rejection. While the emitter accepted
	 * everything those were the same thing. They are not any more — an
	 * emitter refusal is a *supported* program and prints a message — so a
	 * reject case that the front end accepted would have passed as
	 * "rejected", which is exactly the false pass that leg exists to catch.
	 */
	int parse_only = 0;
	int have_source = 0;

	for (i = 1; i < (size_t)argc; i++) {
		if (strcmp(argv[i], "--parse") == 0) {
			parse_only = 1;
		} else if (strcmp(argv[i], "-o") == 0 && i + 1 < (size_t)argc) {
			outdir = argv[++i];
		} else {
			/*
			 * The file's BASENAME is kept, not its path: it names the
			 * output pair and the banner's "generated from" clause,
			 * and a banner naming a directory would be noise.
			 */
			const char *slash = strrchr(argv[i], '/');

			source_label = (slash != NULL) ? slash + 1 : argv[i];
			source = slurp(argv[i]);
			if (source == NULL) {
				fprintf(stderr, "sterlingc: cannot read %s\n", argv[i]);
				return 1;
			}
			have_source = 1;
		}
	}
	/* No file named: use the specimen. */
	if (!have_source) {
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

	/*
	 * `--parse`: the front end accepted the program, which is all this mode
	 * claims. Emission is a separate question and `--golden` is what asks it.
	 */
	if (parse_only) {
		st_arena_free();
		free(source);
		return 0;
	}

	/*
	 * ONE `.h`/`.m` pair per PROGRAM, not per class.
	 *
	 * A file may declare several classes — that is ordinary Sterling, and it is
	 * one translation unit — so the loop this replaces was wrong twice over: it
	 * wrote N files, and `st_emit_header` writes *every* class, so each of the
	 * N held the whole program. Two classes gave two identical headers that
	 * cannot both be imported.
	 *
	 * The pair is named after the INPUT FILE, the way a C compiler names its
	 * output, with the built-in specimen falling back to its single class's
	 * name (§2's `MyClass.h` from `MyClass.ag`).
	 *
	 * A program with NEITHER a class nor a struct has no name to be filed
	 * under, and the emitter is the thing that refuses it — so it is asked,
	 * with stdout, for the message. It refuses before writing anything.
	 *
	 * `struct_count` is part of the test because §5's structs can BE a file's
	 * substance: `tests/08-structs.ag` declares no class at all. While the
	 * test was `class_count == 0` alone such a file took THIS branch — its
	 * header went to stdout, its `.m` was never written, and the exit status
	 * was 0, which is a silent loss dressed as a success.
	 */
	if (program->class_count == 0 && program->struct_count == 0) {
		const char *eerror = NULL;

		if (!st_emit_header(stdout, program, source_label, &eerror)) {
			fprintf(stderr, "%s\n", eerror);
			return 1;
		}
		return 0;
	}

	{
		char base[256];
		const char *eerror = NULL;

		/*
		 * `st_source_stem`, not a second copy of the basename logic: the
		 * same answer here and in the banner, or the file and its own
		 * banner disagree about what the file is called.
		 */
		st_source_stem(source_label, base, sizeof(base));
		if (base[0] == '\0') {
			snprintf(base, sizeof(base), "%s",
				 program->classes[0]->name.text);
		}

		if (outdir != NULL) {
			char path[1024];
			FILE *fp;

			snprintf(path, sizeof(path), "%s/%s.h", outdir, base);
			fp = fopen(path, "wb");
			if (fp == NULL) {
				fprintf(stderr, "sterlingc: cannot write %s\n", path);
				return 1;
			}
			if (!st_emit_header(fp, program, source_label, &eerror)) {
				/*
				 * A refusal must not leave a partial file behind:
				 * the next `make` would see a header that is
				 * syntactically fine and silently incomplete.
				 */
				fclose(fp);
				remove(path);
				fprintf(stderr, "%s\n", eerror);
				return 1;
			}
			fclose(fp);

			snprintf(path, sizeof(path), "%s/%s.m", outdir, base);
			fp = fopen(path, "wb");
			if (fp == NULL) {
				fprintf(stderr, "sterlingc: cannot write %s\n", path);
				return 1;
			}
			if (!st_emit_implementation(fp, program, source_label, &eerror)) {
				fclose(fp);
				remove(path);
				fprintf(stderr, "%s\n", eerror);
				return 1;
			}
			fclose(fp);
		} else {
			printf("----- %s.h -----\n", base);
			if (!st_emit_header(stdout, program, source_label, &eerror)) {
				fprintf(stderr, "%s\n", eerror);
				return 1;
			}
			printf("----- %s.m -----\n", base);
			if (!st_emit_implementation(stdout, program, source_label, &eerror)) {
				fprintf(stderr, "%s\n", eerror);
				return 1;
			}
		}
	}

	st_arena_free();
	if (outdir == NULL) {
		free(source);
	}
	return 0;
}
