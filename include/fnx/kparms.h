/*
 * fnx/include/fnx/kparms.h
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_KPARMS_H
#define _FNX_KPARMS_H

#include <fnx/limits.h>

#define CMDL_ARG_LEN	100	/* max. length of cmdline argument */
#define CMDL_NUM_VALUES	64	/* max. values of cmdline parameter */

#define KPARMS_IDE_NODMA	0x01	/* disable DMA in all ATA drives */
#define KPARMS_PS2_NORESET	0x02	/* disable PS/2 controller reset */

struct kernel_params {
	int flags;
	int recovery;		/* boot the static recovery shell (kernel-parameters.txt) */
	char initrd[DEVNAME_MAX + 1];
	int memsize;
	int extmemsize;
	int ramdisksize;
	int ro;
	int rootdev;
	char rootfstype[10 + 1];
	char rootdevname[DEVNAME_MAX + 1];
	int syscondev;
};

extern struct kernel_params kparms;

struct kernel_params_value {
	char *name;
	char *value[CMDL_NUM_VALUES];
	unsigned int sysval[CMDL_NUM_VALUES];
};

#endif /* _FNX_KPARMS_H */
