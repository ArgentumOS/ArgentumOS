/*
 * fnx/kernel/kconf.h
 *
 * kernel.conf subset parser interface, docs/design/kernel-conf-plan.md M1.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _KERNEL_KCONF_H
#define _KERNEL_KCONF_H

#define KCONF_KV_STRING	0	/* bare/quoted string, or empty */
#define KCONF_KV_BOOL	1	/* 'true' or 'false' */
#define KCONF_KV_INT	2	/* decimal or 0x integer */
#define KCONF_KV_ERR	-1	/* malformed line (skipped) */

struct kconf_kv {
	char key[128];
	char value[256];
	int kind;		/* KCONF_KV_* */
	int is_true;		/* KCONF_KV_BOOL only */
};

int kconf_next(const char *data, unsigned int size, unsigned int *off,
	       struct kconf_kv *out);

/*
 * Is a kernel.conf buffer in the plist spelling? The same content rule libconfig
 * uses (userland/libconfig_plist.c config_text_is_plist): skip a UTF-8 BOM and
 * whitespace, then look for '<'. Exposed because kernel_conf_apply() has to
 * REPORT a file it cannot read: as of P3f the line grammar is retired, so the
 * kernel takes its settings from an XML plist or says so, rather than reading a
 * spelling nothing writes any more.
 */
int kconf_text_is_plist(const char *data, unsigned int size);

#endif /* _KERNEL_KCONF_H */
