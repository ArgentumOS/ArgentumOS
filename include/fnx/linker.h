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
/* FNX: how much physical memory the kernel keeps permanently reachable. The
 * direct map is built as 2MB pages at boot (kernel/boot64/paging64.c) over
 * phys [0, KERNEL_PHYS_LIMIT), so this is the hard ceiling on RAM the kernel
 * can use, and it is what the physical allocator (mm64.c LOW_LIMIT) and the
 * MM accounting (bios_map.c's physical_pages cap) must agree on. Raise it
 * together with the direct map, never on its own. PAGE_OFFSET64 is -2GiB, so
 * the largest span it can express is 2GB; going beyond that means moving the
 * direct map to its own base (see docs/design/ram-scale-plan.md).
 *
 * GDT_BASE is legacy: on x86_64 the GDT is set up by the stub (gdt64) and
 * this constant is no longer the physical cap. */
#define KERNEL_PHYS_LIMIT	0x80000000ULL	/* 2GB */
#define GDT_BASE	0x40000000
#else
#define GDT_BASE	(0xFFFFFFFF - (PAGE_OFFSET - 1))
#endif /* __x86_64__ */

#endif /* _FNX_LINKER_H */
