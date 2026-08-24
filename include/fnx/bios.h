/*
 * fnx/include/fnx/bios.h
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_BIOS_H
#define _FNX_BIOS_H

#include <fnx/multiboot1.h>

#define NR_BIOS_MM_ENT		50	/* entries in BIOS memory map */

struct bios_mem_map {
	unsigned int from;
	unsigned int from_hi;
	unsigned int to;
	unsigned int to_hi;
	int type;
};
extern struct bios_mem_map bios_mem_map[NR_BIOS_MM_ENT];
extern struct bios_mem_map kernel_mem_map[NR_BIOS_MM_ENT];
extern char bios_data[256];

int is_addr_in_bios_map(unsigned int);
void bios_map_reserve(unsigned int, unsigned int);
void bios_map_init(struct multiboot_mmap_entry *, unsigned int);

#endif /* _FNX_BIOS_H */
