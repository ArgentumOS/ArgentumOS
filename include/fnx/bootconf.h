/*
 * fnx/include/fnx/bootconf.h
 *
 * FNX kernel.conf (ESP boot config) handoff, docs/design/kernel-conf-plan.md.
 *
 * The EFI stub reads kernel.conf (next to the loaded image on the boot
 * volume) into fnx_kconf.data before ExitBootServices; the real kernel
 * parses it early in start_kernel(), before mount_root. fnx_kconf lives
 * in the EFI image's .bss (below fnx_bss_end), so it is never handed out
 * as free memory and is readable through the image's high-half mapping,
 * like fnx_gop_fb.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_BOOTCONF_H
#define _FNX_BOOTCONF_H

#define FNX_KCONF_MAX	8192	/* v1 cap (a kernel.conf is ~1-2KB) */

struct fnx_kconf {
	char data[FNX_KCONF_MAX];
	unsigned int size;	/* bytes read; 0 = no file (defaults) */
};

extern struct fnx_kconf fnx_kconf;

#endif /* _FNX_BOOTCONF_H */
