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
/* FNX: the kernel's virtual base. Phys [0, KERNEL_PHYS_LIMIT) is mapped
 * permanently at PAGE_OFFSET + phys (the direct map), and the kernel IMAGE
 * runs there too - all kernel data pointers are re-biased by this offset at
 * boot (rebase_image_data). It is a real base now, not a fixed -2GiB: with
 * -2GiB the direct map could only ever express phys < 2GB, so a large
 * guest's image (the firmware loads it near the top of RAM) had no valid
 * kernel address at all and the boot died.
 *
 * Keep it inside pml4[256..511]: that range is shared into every user
 * process's pml4 by create_pml4_64() (kernel/boot64/mm64.c), so moving it
 * into pml4[0..255] would hide the kernel from user processes. */
#define PAGE_OFFSET	0xFFFF800000000000ULL	/* pml4[256]: the high half */
/* FNX: NOTE - this is the ceiling. The direct map can express at most 2GB
 * while PAGE_OFFSET is -2GiB, so RAM above 2GB has no kernel address.
 *
 * Moving it to a real base (0xFFFF800000000000 = pml4[256], which stays
 * inside the pml4[256..511] range create_pml4_64() shares with user
 * processes) plus a 4GB direct map was implemented twice and measured both
 * times. The boot gets all the way to INIT (mounts, hostname, display mode)
 * and then takes a supervisor write fault: cr2 = 0x4b6000, which is the
 * PHYSICAL address of an early kmalloc'd page, i.e. an address that is only
 * reachable through the kernel's low-4GB identity map.
 *
 * That is the real blocker, and it is a class, not one site: the kernel runs
 * on the boot page tables (activate_kpage_dir() is a no-op on x86-64), whose
 * identity map at pml4[0] makes "physical address used as a pointer" work by
 * accident. A user process's pml4 has an empty low half, so the same code
 * faults the moment it runs in a syscall context. Resolving the fault (print
 * the image's load base, print rip minus that base and use it as an RVA)
 * put the first one in the ATA probe: drivers/block/ata.c's
 * ata_channel_init() on the SECONDARY channel, before ata_hd_init() is
 * entered - the primary channel passes. Note that kmalloc(), inport_sw/sl()
 * and memset_l() all take real 64-bit pointers, so it is neither those nor a
 * plain truncation there.
 *
 * So the order is: make the kernel stop relying on the identity map (nothing
 * low may be dereferenced except through P2V), then move this constant. See
 * KERNEL_PHYS_LIMIT below, which must move with it. */
/* FNX (canonical amd64 split): user space is the whole low canonical
 * half, 0 .. 0x00007FFFFFFFFFFF (128TB, pml4[0..255]). The user stack grows
 * down from just below this boundary (matching Linux's TASK_SIZE); the gap
 * between it and PAGE_OFFSET above is not a valid user address. */
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
#define KERNEL_PHYS_LIMIT	0x1000000000ULL	/* 64GB: direct map 0..64GB */
#define GDT_BASE	0x40000000
#else
#define GDT_BASE	(0xFFFFFFFF - (PAGE_OFFSET - 1))
#endif /* __x86_64__ */

#endif /* _FNX_LINKER_H */
