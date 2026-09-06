/*
 * fnx/kernel/boot64/paging64.c
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

/* PE32+ base-relocation types (image base relocation directory) */
#define PE_REL_BASED_DIR64	10	/* 64-bit absolute address */

/*
 * FNX (single-alias): the firmware loads the PE image at a low physical
 * base and applies base relocations, so every absolute pointer in the
 * image DATA (syscall_table64, file_operations, IDT-adjacent tables, ...)
 * holds an IDENTITY (low) address. The kernel executes from the high-half
 * alias after the switch below; those low data pointers then dispatch
 * indirect calls back into the identity alias, so the SAME symbol
 * (&sys_wait4, &pml4_page, ...) has two runtime addresses and every
 * cross-alias pointer comparison (sleep_address matching, CR3 pml4 loads,
 * ...) is a latent bug. Re-bias every relocated 64-bit absolute by
 * PAGE_OFFSET64 so ALL pointers reference the high-half alias: the kernel
 * then executes from a single address space.
 *
 * Safety: must run AFTER the identity phase's last data write (the paging
 * tables above) and BEFORE the jump to the high-half entry; between the
 * re-bias and the jump the code only moves register arguments. The EFI
 * memory map and the PE headers are read at their identity addresses, and
 * the relocation TABLE (.reloc) is never itself a relocation target.
 */
static void rebase_image_data(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size,
			      UINTN desc_size)
{
	unsigned long base = 0, pe_off, opt, reloc_rva, reloc_size;
	unsigned long off, end;
	UINTN i, count;
	EFI_MEMORY_DESCRIPTOR *d;

	count = map_size / desc_size;
	for(i = 0; i < count; i++) {
		d = (EFI_MEMORY_DESCRIPTOR *)((char *)map + (i * desc_size));
		if(d->Type == EfiLoaderCode) {
			base = (unsigned long)d->PhysicalStart;
			break;
		}
	}
	if(!base) {
		serial_puts("WARNING: rebase_image_data(): no LoaderCode range, image stays identity\n");
		return;
	}

	/* PE headers: DOS e_lfanew -> "PE\0\0" -> COFF -> optional header */
	pe_off = base + *(unsigned int *)(base + 0x3C);
	if(*(unsigned int *)pe_off != 0x00004550) {	/* "PE\0\0" */
		serial_puts("WARNING: rebase_image_data(): bad PE signature\n");
		return;
	}
	opt = pe_off + 4 + 20;
	if(*(unsigned short *)opt != 0x20B) {		/* PE32+ */
		serial_puts("WARNING: rebase_image_data(): not PE32+\n");
		return;
	}

	/* data directory 5 = base relocation table {rva, size} */
	reloc_rva = *(unsigned int *)(opt + 112 + 5 * 8);
	reloc_size = *(unsigned int *)(opt + 112 + 5 * 8 + 4);
	if(!reloc_size) {
		serial_puts("WARNING: rebase_image_data(): no base reloc directory\n");
		return;
	}

	off = base + reloc_rva;
	end = off + reloc_size;
	while(off + 8 <= end) {
		unsigned long page_rva = *(unsigned int *)off;
		unsigned int block_size = *(unsigned int *)(off + 4);
		unsigned int nent, k;

		if(block_size < 8 || off + block_size > end) {
			break;
		}
		nent = (block_size - 8) / 2;
		for(k = 0; k < nent; k++) {
			unsigned int w = *(unsigned short *)(off + 8 + k * 2);
			unsigned int type = w >> 12;
			unsigned int rel = w & 0xFFF;
			if(type == PE_REL_BASED_DIR64) {
				*(unsigned long *)(base + page_rva + rel) += PAGE_OFFSET64;
			}
			/* type 0 (ABS) and others: no fixup needed */
		}
		off += block_size;
	}

	serial_puts("[M2-A] re-biased image data pointers to the high half\n");
}

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
 * is the physical address; the kernel now executes from a single
 * address space (the high-half alias), so plain subtraction is exact. */
unsigned long paging64_pml4_phys(void)
{
	return (unsigned long)&pml4_page - PAGE_OFFSET64;
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

	/* Re-bias the image's absolute data pointers to the high-half alias
	 * (see rebase_image_data). After this, ALL pointers in kernel DATA
	 * reference the high half, so the kernel executes from a single
	 * address space once we jump below. Runs BEFORE CR0.WP is set: the
	 * walk writes every DIR64 relocation target, and some may live in
	 * sections the loader mapped read-only. */
	rebase_image_data(map, map_size, desc_size);

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
