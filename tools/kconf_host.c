/*
 * fnx/tools/kconf_host.c
 *
 * Host-side driver for the kernel kernel.conf subset parser
 * (kernel/kconf.c). Prints the effective value of every key in a file:
 * one "key\tvalue" line per key, last occurrence wins (the kernel applies
 * the final value of a duplicated key). Strings print raw (unquoted),
 * booleans as true/false, integers in decimal.
 *
 * Usage: kconf_host <file.conf>
 * Built by tools/kconf_corpus.sh. Copyright 2026 Fiwix License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/kconf.h"

int main(int argc, char **argv)
{
	FILE *f;
	char *buf;
	long len;
	struct kconf_kv last[64];
	int nlast = 0;
	unsigned int off = 0;
	int malformed = 0;

	if(argc != 2) {
		fprintf(stderr, "usage: kconf_host <file.conf>\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if(!f) {
		perror(argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t)len + 1);
	if(!buf) {
		return 2;
	}
	if(fread(buf, 1, (size_t)len, f) != (size_t)len) {
		return 2;
	}
	fclose(f);
	buf[len] = 0;

	for(;;) {
		struct kconf_kv kv;
		int k;

		if(!kconf_next(buf, (unsigned int)len, &off, &kv)) {
			break;
		}
		if(kv.kind == KCONF_KV_ERR) {
			malformed++;
			continue;
		}
		/* last wins: overwrite an existing key, else append */
		for(k = 0; k < nlast; k++) {
			if(!strcmp(last[k].key, kv.key)) {
				break;
			}
		}
		if(k == nlast && nlast < 64) {
			nlast++;
		}
		last[k] = kv;
	}
	{
		int k;

		for(k = 0; k < nlast; k++) {
			printf("%s\t", last[k].key);
			if(last[k].kind == KCONF_KV_BOOL) {
				printf("%s\n", last[k].is_true ? "true" : "false");
			} else if(last[k].kind == KCONF_KV_INT) {
				printf("%lld\n", (long long)strtoll(last[k].value, NULL, 0));
			} else {
				printf("%s\n", last[k].value);
			}
		}
	}
	fprintf(stderr, "malformed lines skipped: %d\n", malformed);
	return malformed ? 1 : 0;
}
