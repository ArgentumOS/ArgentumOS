/*
 * fiwix/include/fiwix/linker.h
 *
 * Copyright 2023, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FIWIX_LINKER_H
#define _FIWIX_LINKER_H

#include <fiwix/config.h>

#ifdef __x86_64__
#define PAGE_OFFSET	0xFFFFFFFF80000000ULL	/* Fiwix64: kernel high half */
#define USER_STACK_TOP	0x80000000UL		/* Fiwix64: top of the 32-bit
						 * user space (2GB split); the
						 * exec'd compat programs get
						 * their stack here, not at the
						 * kernel high half */
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
#define GDT_BASE	(0xFFFFFFFF - (PAGE_OFFSET - 1))

#endif /* _FIWIX_LINKER_H */
