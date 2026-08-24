/*
 * fnx/include/fnx/linker.h
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_LINKER_H
#define _FNX_LINKER_H

#include <fnx/config.h>

#ifdef __x86_64__
#define PAGE_OFFSET	0xFFFFFFFF80000000ULL	/* FNX: kernel high half */
/* FNX (canonical amd64 split): user space is the whole low canonical
 * half, 0 .. 0x00007FFFFFFFFFFF (128TB, pml4[0..255]); the kernel high
 * half starts at pml4[256] = 0x0000800000000000. The user stack grows
 * down from just below this boundary (matching Linux's TASK_SIZE). */
#define USER_STACK_TOP	0x0000800000000000UL
#else
#ifdef CONFIG_VM_SPLIT22
#define PAGE_OFFSET	0x80000000	/* VM split: 2GB user / 2GB kernel */
#else
#define PAGE_OFFSET	0xC0000000	/* VM split: 3GB user / 1GB kernel */
#endif /* CONFIG_VM_SPLIT22 */
#define USER_STACK_TOP	PAGE_OFFSET
#endif /* __x86_64__ */

#define KERNEL_ADDR	0x100000
#define KERNEL_STACK	4096
#ifdef __x86_64__
/* FNX: GDT_BASE was the 32-bit "top of the kernel's 1GB direct map";
 * in long mode the kernel reaches phys via the high half and the physical
 * cap is the 64-bit bitmap's LOW_LIMIT (1GB), so keep this a 64-bit value
 * that bios_map's physical_pages cap can shift. */
#define GDT_BASE	0x40000000
#else
#define GDT_BASE	(0xFFFFFFFF - (PAGE_OFFSET - 1))
#endif /* __x86_64__ */

#endif /* _FNX_LINKER_H */
