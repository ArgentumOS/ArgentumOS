/*
 * fnx/tools/kconf_libconfig_host.c
 *
 * Userland-side driver for the conformance corpus: parses a corpus file as
 * the userland libconfig parser does (it is copied to $ROOT/<domain>.conf
 * and read through the public config_get_all API) and prints the same
 * "key\tvalue" listing as tools/kconf_host.c, so the two parsers can be
 * diffed on identical input.
 *
 * Usage: kconf_libconfig_host <scratch-root> <domain>
 *   (the corpus file must already be at <root>/System/Configuration/<domain>.conf;
 *   FNX_CONFIG_ROOT must point at the scratch root)
 * Built by tools/kconf_corpus.sh. Copyright 2026 Fiwix License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../userland/libconfig.h"

int main(int argc, char **argv)
{
	char **keys = NULL;
	config_value_t *values = NULL;
	size_t count = 0, k;
	config_err_t e;

	if(argc != 3) {
		fprintf(stderr, "usage: kconf_libconfig_host <root> <domain>\n");
		return 2;
	}
	e = config_get_all(argv[2], "", &keys, &values, &count);
	if(e != CONFIG_OK) {
		fprintf(stderr, "config_get_all: err %d\n", (int)e);
		return 1;
	}
	for(k = 0; k < count; k++) {
		printf("%s\t", keys[k]);
		switch(values[k].type) {
		case CONFIG_TYPE_BOOL:
			printf("%s\n", values[k].v.boolean ? "true" : "false");
			break;
		case CONFIG_TYPE_INT:
			printf("%lld\n", (long long)values[k].v.integer);
			break;
		case CONFIG_TYPE_FLOAT:
			printf("%g\n", values[k].v.floating);
			break;
		default:
			printf("%s\n", values[k].v.string ? values[k].v.string : "");
			break;
		}
	}
	config_free_keys(keys, values, count);
	return 0;
}
