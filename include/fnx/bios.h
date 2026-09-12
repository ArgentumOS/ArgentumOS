/*
 * fnx/include/fnx/bios.h
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_BIOS_H
#define _FNX_BIOS_H

#include <fnx/multiboot1.h>

/* FNX: the loader now passes the real UEFI map (which is long and
 * fragmented - a 128MB QEMU guest alone reports ~40 descriptors), so this
 * has to hold that map rather than the handful of BIOS-era entries. */
#define NR_BIOS_MM_ENT		256	/* entries in the memory map */

struct bios_mem_map {
	unsigned long long from;	/* 64-bit: RAM above 4GB must be countable */
	unsigned long long to;
	int type;
};
extern struct bios_mem_map bios_mem_map[NR_BIOS_MM_ENT];
extern struct bios_mem_map kernel_mem_map[NR_BIOS_MM_ENT];
extern char bios_data[256];

int is_addr_in_bios_map(addr_t);
void bios_map_reserve(unsigned int, unsigned int);
void bios_map_init(struct multiboot_mmap_entry *, unsigned int);

#endif /* _FNX_BIOS_H */
