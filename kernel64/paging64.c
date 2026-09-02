/*
 * fnx/kernel64/paging64.c
 *
 * FNX M2 (phase A): install the kernel's own 4-level page tables.
 *
 * After ExitBootServices() the stub still runs on the firmware's
 * identity-mapped page tables. This module replaces them with 4-level
 * tables that:
 *   - identity-map the low 1GB (2MB pages), and
 *   - map the same 1GB at PAGE_OFFSET64 (0xFFFFFFFF80000000), the 64-bit
 *     kernel virtual base, so V2P(addr) = addr - PAGE_OFFSET64 still works
 *     as wraparound unsigned arithmetic.
 *
 * The page tables live in the stub image's .bss, so their physical address
 * is their runtime address (the firmware identity-maps the whole image).
 * After CR3 is switched, execution jumps to the high-half alias of
 * high_entry(), which proves code runs from the kernel virtual base, then
 * re-enters kernel64_main() (its direct call lands at the high alias too).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/efi.h>
#include "serial64.h"

#define PAGE_OFFSET64	0xFFFFFFFF80000000ULL

#define PML4_INDEX(a)	(((unsigned long)(a) >> 39) & 0x1FF)
#define PDPT_INDEX(a)	(((unsigned long)(a) >> 30) & 0x1FF)
#define PD_INDEX(a)	(((unsigned long)(a) >> 21) & 0x1FF)
#define PT_INDEX(a)	(((unsigned long)(a) >> 12) & 0x1FF)

#define X86_PTE_P	0x001ULL	/* present */
#define X86_PTE_RW	0x002ULL	/* read/write */
#define X86_PTE_PS	0x080ULL	/* 2MB page (PD entry) */

#define P2V64(a)	(((unsigned long)(a) < PAGE_OFFSET64) ? \
				((unsigned long)(a) + PAGE_OFFSET64) : (unsigned long)(a))
#define V2P64(a)	((unsigned long)(a) - PAGE_OFFSET64)

/* page-table pages, 4K-aligned, in the stub's .bss (identity-mapped) */
static unsigned long pml4_page[512] __attribute__((aligned(4096)));
static unsigned long pdpt_page[512] __attribute__((aligned(4096)));
static unsigned long pd_page[512] __attribute__((aligned(4096)));
/* 1GB-4GB identity (PCI MMIO hole at 2GB+, VGA BAR, APIC, ...) */
static unsigned long pd_page2[1536] __attribute__((aligned(4096)));

void kernel64_main(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN, UINTN, EFI_SYSTEM_TABLE *);

/* physical address of the kernel's PML4 (identity-mapped, so phys == VA);
 * mm64.c walks it to map/unmap 4KB pages */
unsigned long paging64_pml4(void)
{
	return (unsigned long)&pml4_page;
}

/* physical address of the kernel's PML4 for CR3. The page itself lives in
 * the image .bss; &pml4_page resolves via RIP-relative (PIC) addressing, so
 * its value depends on which alias the code executes from: the high-half
 * alias (PAGE_OFFSET64 + phys, after the boot switch) OR the identity alias
 * (phys, when the call chain was entered through an identity function
 * pointer - every kernel DATA pointer holds an identity address). Neither
 * &pml4_page (high-half VA) nor &pml4_page - PAGE_OFFSET64 (which is
 * &pml4_page + 0x80000000 when &pml4_page is already the identity value)
 * is the physical address in both cases; the identity value IS the phys, so
 * normalize: anything >= PAGE_OFFSET64 is a high-half VA (subtract),
 * anything below is already physical (keep). */
unsigned long paging64_pml4_phys(void)
{
	unsigned long va = (unsigned long)&pml4_page;

	return (va >= PAGE_OFFSET64) ? (va - PAGE_OFFSET64) : va;
}

/*
 * Runs at its high-half alias (PAGE_OFFSET64 + phys). Proves the 4-level
 * tables translate the kernel virtual base, then re-enters kernel64_main(),
 * which from here executes entirely from the high half.
 */
static void high_entry(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size,
		       UINTN desc_size, UINTN map_key, EFI_SYSTEM_TABLE *SystemTable)
{
	serial_puts("\n[M2-A] high-half entry: running at ");
	serial_hex((UINT64)get_rip());
	serial_puts("\n");
	serial_puts("[M2-A] P2V64(0x100000)          = ");
	serial_hex((UINT64)P2V64(0x100000));
	serial_puts("\n");
	serial_puts("[M2-A] V2P64(0xFFFFFFFF80100000)= ");
	serial_hex((UINT64)V2P64(0xFFFFFFFF80100000ULL));
	serial_puts("\n");
	serial_puts("[M2-A] re-entering kernel64_main from the high half.\n");

	kernel64_main(map, map_size, desc_size, map_key, SystemTable);
	/* not reached */
	for(;;) {
		__asm__ __volatile__("hlt");
	}
}

/*
 * Build the 4-level tables, switch CR3, and jump to the high-half alias of
 * high_entry(). Called once, from kernel64_main() while still identity
 * mapped; never returns.
 */
void paging64_init(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size,
		   UINTN desc_size, UINTN map_key, EFI_SYSTEM_TABLE *SystemTable)
{
	unsigned long cr3;
	int n;

	/* PML4: identity (entry 0) and high half (entry 511) share the PDPT */
	pml4_page[0] = (unsigned long)&pdpt_page | X86_PTE_P | X86_PTE_RW;
	pml4_page[PML4_INDEX(PAGE_OFFSET64)] = (unsigned long)&pdpt_page | X86_PTE_P | X86_PTE_RW;

	/* PDPT: identity (entry 0) and high half (entry 510) share the PD */
	pdpt_page[0] = (unsigned long)&pd_page | X86_PTE_P | X86_PTE_RW;
	pdpt_page[PDPT_INDEX(PAGE_OFFSET64)] = (unsigned long)&pd_page | X86_PTE_P | X86_PTE_RW;
	/* PDPT entry 1: identity 1GB-2GB */
	pdpt_page[1] = (unsigned long)&pd_page2[0] | X86_PTE_P | X86_PTE_RW;
	/* PDPT entries 2-3: identity 2GB-4GB (PCI MMIO, VGA, APIC, ACPI) */
	pdpt_page[2] = (unsigned long)&pd_page2[512] | X86_PTE_P | X86_PTE_RW;
	pdpt_page[3] = (unsigned long)&pd_page2[1024] | X86_PTE_P | X86_PTE_RW;
	/* PDPT entries 4-5: P2V aliases of phys 2GB-4GB (PCI MMIO, VGA BAR):
	 * P2V(x) = x + PAGE_OFFSET (mod 2^64) wraps to 0x100000000 for
	 * x >= 2GB, so the virtual 4GB-6GB range must alias phys 2GB-4GB. */
	pdpt_page[4] = (unsigned long)&pd_page2[512] | X86_PTE_P | X86_PTE_RW;
	pdpt_page[5] = (unsigned long)&pd_page2[1024] | X86_PTE_P | X86_PTE_RW;

	/* PD: 512 x 2MB pages covering the low 1GB */
	for(n = 0; n < 512; n++) {
		pd_page[n] = ((unsigned long)n << 21) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
	}
	/* PD2: 1536 x 2MB pages covering 1GB-4GB */
	for(n = 0; n < 1536; n++) {
		pd_page2[n] = ((unsigned long)(n + 512) << 21) | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
	}

	cr3 = (unsigned long)&pml4_page;
	serial_puts("\n[M2-A] installing 4-level paging (2MB pages, low 1GB identity + high half), CR3=");
	serial_hex((UINT64)cr3);
	serial_puts("\n");

	__asm__ __volatile__("mov %0, %%cr3" :: "r"(cr3) : "memory");

	/* CR0.WP: enforce read-only pages in supervisor mode (needed for CoW) */
	{
		unsigned long cr0;

		__asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
		__asm__ __volatile__("mov %0, %%cr0" :: "r"(cr0 | 0x10000) : "memory");
	}

	/* jump to the high-half alias of high_entry() */
	((void (*)(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN, UINTN, EFI_SYSTEM_TABLE *))
		((unsigned long)&high_entry + PAGE_OFFSET64))(map, map_size, desc_size, map_key, SystemTable);

	/* not reached */
	for(;;) {
		__asm__ __volatile__("hlt");
	}
}
