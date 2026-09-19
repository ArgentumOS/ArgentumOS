/*
 * main.c — the Sterling lexer driver (K1).
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licence (see docs/LICENSE).
 *
 * Usage: sterlingc [file.ag]   — with no file, the built-in sample is lexed.
 */
#include "sterling.h"
#include <stdio.h>
#include <stdlib.h>

static const char *sample =
	"import Foundation\n"
	"operator .:. (lhs: Int32, rhs: Int32) -> Point { return Point() }\n"
	"class Shelf: Object {\n"
	"    operator [](i: Int32) -> Float32? { return nil }\n"
	"    method add(item which: Int32, scale: Float32 = 1.0) -> Bool {\n"
	"        let store: Shelf? = nil\n"
	"        store?[which] = 2\n"        /* expression: brackets are punctuation */
	"        var p: Point = 1 .:. 2\n"
	"        switch which { case 1...45 { } case 0..<3 { } default { } }\n"
	"        return true // trailing comment\n"
	"    }\n"
	"}\n";

int
main(int argc, char **argv)
{
	char *buf;
	long size;
	FILE *fp;

	if (argc > 1) {
		fp = fopen(argv[1], "rb");
		if (fp == NULL) {
			fprintf(stderr, "sterlingc: cannot open %s\n", argv[1]);
			return 1;
		}
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		buf = malloc((size_t)size + 1);
		if (buf == NULL) {
			fprintf(stderr, "sterlingc: out of memory\n");
			return 1;
		}
		if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
			fprintf(stderr, "sterlingc: short read\n");
			return 1;
		}
		buf[size] = '\0';
		fclose(fp);
		st_dump(buf);
		free(buf);
		return 0;
	}

	printf("--- lexing the built-in sample ---\n");
	st_dump(sample);
	return 0;
}
