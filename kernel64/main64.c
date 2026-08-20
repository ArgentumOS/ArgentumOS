/*
 * fiwix/kernel64/main64.c
 *
 * Fiwix64 long-mode entry (M1).
 *
 * Runs in long mode after ExitBootServices(), still on the firmware's
 * identity-mapped page tables. Prints the boot banner and a summary of the
 * EFI memory map over the serial port (COM1, 115200 8N1), then halts.
 *
 * This is the minimal "the kernel is alive in long mode via UEFI" proof.
 * The real start_kernel()/MM/scheduler integration happens in M2/M3/M4.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fiwix/efi.h>
#include "serial64.h"

/* M4-B: boot the real Fiwix kernel (start_kernel) on top of the 64-bit
 * primitives. See kernel64/kreal64.c. */
#define KREAL64_BOOT	1
extern void kreal64_boot(void);

/* M2-A: installs the kernel's own 4-level page tables and jumps to the high
 * half, re-entering kernel64_main() there. Never returns on first call. */
void paging64_init(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN, UINTN, EFI_SYSTEM_TABLE *);

/* M2-B: physical page allocator + dynamic 4KB-page mapping (mm64.c) */
void mm64_init(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN);
void mm64_demo(void);
unsigned long alloc_pages64(int);
void free_pages64(unsigned long, int);
int map_page64(unsigned long, unsigned long, unsigned long);
void unmap_page64(unsigned long);
unsigned long pages_free64(void);
void *kmalloc64(unsigned long);
void kfree64(void *);
void kmem_stats64(void);
unsigned long virt_to_phys64(unsigned long);
unsigned long pages_total64(void);

/* M2-D: 4KB page inside a 2MB-mapped region (splits PD[4]'s huge page) */
#define SPLIT_VA	0xFFFFFFFF80800000ULL

/* M2-C: IDT64 + exception handling + demand paging (idt64.c) */
void idt64_init(void);

/* M3-A: GDT64/TSS64 (gdt64.c) and PIC/PIT interrupts (irq64.c) */
void gdt64_init(void);
void irq64_init(void);
unsigned long get_ticks64(void);

/* M4-A: user pages (mm64.c) + 32-bit compat mode demo */
int map_user_page64(unsigned long, unsigned long, unsigned long);

/* 36 bytes of 32-bit user code (as --32): fills a syscall-args struct at
 * 0x300200 {nr, fd, buf, count} then int 0x80; loops. The args go through
 * memory because QEMU's compat->64-bit gate transition drops 32-bit
 * register writes (see docs); see tools/m4a_sc.S */
static const unsigned char m4a_blob[36] = {
	0xb8, 0x00, 0x02, 0x30, 0x00, 0xc7, 0x00, 0x04,
	0x00, 0x00, 0x00, 0xc7, 0x40, 0x04, 0x01, 0x00,
	0x00, 0x00, 0xc7, 0x40, 0x08, 0x00, 0x01, 0x30,
	0x00, 0xc7, 0x40, 0x0c, 0x17, 0x00, 0x00, 0x00,
	0xcd, 0x80, 0xeb, 0xdc
};
static const char m4a_msg[] = "Hello from 32-bit user!";

#define M4A_SC_VA	0x300200UL	/* user syscall-args area */

/* iretq into 32-bit compat user mode (CPL3): the target CS (0x53, L=0)
 * makes the CPU pop a 32-bit frame and switch to compatibility mode */
static void enter_user32(void)
{
	__asm__ __volatile__(
		"pushq $0x5B\n\t"	/* SS: user data32, RPL3 */
		"pushq $0x301000\n\t"	/* ESP */
		"pushq $0x202\n\t"	/* EFLAGS: IF=1 */
		"pushq $0x53\n\t"	/* CS: user code32, RPL3 */
		"pushq $0x300000\n\t"	/* EIP */
		"iretq\n\t"
		::: "memory");
}

/* M3-B: kernel threads + round-robin scheduler (sched64.c, switch64.S) */
void thread_create64(void (*)(void));
unsigned long thread_id64(void);

/* worker thread body: prints its id once per ~200 ms slice */
static void worker64(void)
{
	unsigned long id = thread_id64();
	unsigned long next = get_ticks64() + 20;

	for(;;) {
		if(get_ticks64() >= next) {
			serial_puts("[T");
			putdec64(id);
			serial_puts("] slice at tick ");
			putdec64((UINT64)get_ticks64());
			serial_puts("\n");
			next += 20;
		}
	}
}

#define DEMAND_VA	0xFFFFFFFFD0000000ULL
#define COW_VA		0xFFFFFFFFD0200000ULL

#define P2V64(a)	(((unsigned long)(a) < 0xFFFFFFFF80000000ULL) ? \
				((unsigned long)(a) + 0xFFFFFFFF80000000ULL) : (unsigned long)(a))

/* set before the first paging64_init() call so the high-half re-entry skips it */
static int paging64_done;

static const char *memtype_name(UINT32 type)
{
	switch(type) {
		case EfiReservedMemoryType:		return "Reserved";
		case EfiLoaderCode:			return "LoaderCode";
		case EfiLoaderData:			return "LoaderData";
		case EfiBootServicesCode:		return "BS_Code";
		case EfiBootServicesData:		return "BS_Data";
		case EfiRuntimeServicesCode:		return "RT_Code";
		case EfiRuntimeServicesData:		return "RT_Data";
		case EfiConventionalMemory:		return "Conventional";
		case EfiUnusableMemory:			return "Unusable";
		case EfiACPIReclaimMemory:		return "ACPI_Reclaim";
		case EfiACPIMemoryNVS:			return "ACPI_NVS";
		case EfiMemoryMappedIO:			return "MMIO";
		case EfiMemoryMappedIOPortSpace:	return "MMIO_Port";
		case EfiPalCode:			return "PalCode";
		case EfiPersistentMemory:		return "Persistent";
		default:				return "Unknown";
	}
}

void kernel64_main(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size, UINTN desc_size,
		   UINTN map_key, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_MEMORY_DESCRIPTOR *d;
	UINTN n, count, pages, usable_pages;
	char *p;
	volatile unsigned char *q;
	unsigned long paddr;
	unsigned int apic_id;
	int ok, st;

	serial_init();

	if(!paging64_done) {
		paging64_done = 1;
		paging64_init(map, map_size, desc_size, map_key, SystemTable);
		/* not reached: paging64_init jumps to the high half */
	}

	serial_puts("\n");
	serial_puts("================================================================\n");
	serial_puts("Fiwix64 M1: long-mode kernel alive, booted directly from UEFI\n");
	serial_puts("================================================================\n");
	serial_puts("firmware: ");
	serial_puts16(SystemTable->FirmwareVendor);
	serial_puts(" rev ");
	puthex32(SystemTable->FirmwareRevision);
	serial_puts("\n");
	serial_puts("system table at ");
	serial_hex((UINT64)SystemTable);
	serial_puts(" (signature valid: ");
	serial_puts(*(UINT64 *)&SystemTable->Signature == EFI_SYSTEM_TABLE_SIGNATURE ? "yes" : "NO!");
	serial_puts(")\n");
	serial_puts("exit boot services key: ");
	putdec64(map_key);
	serial_puts("\n\n");

	serial_puts("  #  type            phys start              end                 pages    bytes\n");
	serial_puts("---  --------------  ----------------------  ----------------------  --------  ---------\n");
	count = map_size / desc_size;
	pages = usable_pages = 0;
	for(n = 0; n < count; n++) {
		d = (EFI_MEMORY_DESCRIPTOR *)((char *)map + (n * desc_size));
		serial_puts("[");
		serial_putc('0' + (n / 100) % 10);
		serial_putc('0' + (n / 10) % 10);
		serial_putc('0' + (n % 10));
		serial_puts("] ");
		serial_puts(memtype_name(d->Type));
		/* right-align the 14-char type name */
		{
			int pad, len;

			len = 0;
			while(memtype_name(d->Type)[len]) {
				len++;
			}
			for(pad = 14 - len; pad > 0; pad--) {
				serial_putc(' ');
			}
		}
		serial_hex(d->PhysicalStart);
		serial_puts("  ");
		serial_hex(d->PhysicalStart + (d->NumberOfPages << 12));
		serial_puts("  ");
		putdec64(d->NumberOfPages);
		serial_puts("  ");
		putdec64(d->NumberOfPages << 12);
		serial_puts("\n");
		pages += d->NumberOfPages;
		if(d->Type == EfiConventionalMemory) {
			usable_pages += d->NumberOfPages;
		}
	}
	serial_puts("\n");
	serial_puts("total memory: ");
	putdec64(pages << 12);
	serial_puts(" bytes (");
	putdec64(pages >> 8);
	serial_puts(" MB), conventional: ");
	putdec64(usable_pages << 12);
	serial_puts(" bytes (");
	putdec64(usable_pages >> 8);
	serial_puts(" MB)\n");

	mm64_init(map, map_size, desc_size);
	mm64_demo();

	/* M2-C: kmalloc64 over the page allocator */
	p = (char *)kmalloc64(8192);
	if(p) {
		for(n = 0; n < 8192; n++) {
			p[n] = (char)(n ^ 0x5A);
		}
		ok = 1;
		for(n = 0; n < 8192; n++) {
			if(p[n] != (char)(n ^ 0x5A)) {
				ok = 0;
				break;
			}
		}
		serial_puts("\n[M2-C] kmalloc64(8192) = ");
		serial_hex((UINT64)(unsigned long)p);
		serial_puts(": write/read ");
		serial_puts(ok ? "OK" : "MISMATCH");
		serial_puts("\n");
		kfree64(p);
	} else {
		serial_puts("\n[M2-C] kmalloc64 FAILED\n");
	}

	/* M2-C: IDT64 + demand paging: first touch of DEMAND_VA faults, the
	 * #PF handler allocates and maps the page, iretq retries the access */
	gdt64_init();
	idt64_init();
	q = (volatile unsigned char *)DEMAND_VA;
	*q = 0x5A;
	serial_puts("[M2-C] demand-paged access to ");
	serial_hex((UINT64)DEMAND_VA);
	serial_puts(": read back ");
	serial_puts(*q == 0x5A ? "OK (0x5A)" : "MISMATCH");
	serial_puts(", phys ");
	serial_hex((UINT64)virt_to_phys64(DEMAND_VA));
	serial_puts("\n");

	/* M2-D: 4KB page inside a 2MB-mapped region (huge-page split) */
	paddr = alloc_pages64(1);
	if(paddr) {
		st = map_page64(SPLIT_VA, paddr, 0x002);	/* X86_PTE_RW */
		if(!st) {
			*(volatile unsigned char *)SPLIT_VA = 0x7D;
			ok = (*(volatile unsigned char *)SPLIT_VA == 0x7D);
			serial_puts("\n[M2-D] 4KB page inside 2MB region at ");
			serial_hex((UINT64)SPLIT_VA);
			serial_puts(" -> phys ");
			serial_hex((UINT64)paddr);
			serial_puts(": write/read ");
			serial_puts(ok ? "OK" : "MISMATCH");
			serial_puts(", walk ");
			serial_hex((UINT64)virt_to_phys64(SPLIT_VA));
			serial_puts("\n");
			unmap_page64(SPLIT_VA);
			free_pages64(paddr, 1);
		} else {
			serial_puts("\n[M2-D] split map FAILED (st=");
			serial_hex((UINT64)st);
			serial_puts(")\n");
		}
	}

	/* M2-D: MMIO above 1GB - map the local APIC page and read its ID */
	if(!map_page64(0xFEE00000, 0xFEE00000, 0x002)) {
		apic_id = *(volatile unsigned int *)0xFEE00020;
		serial_puts("[M2-D] local APIC MMIO mapped (0xFEE00000): APIC ID = ");
		puthex32(apic_id);
		serial_puts("\n");
	} else {
		serial_puts("[M2-D] APIC map FAILED\n");
	}

	/* M2-D: memory stats */
	serial_puts("[M2-D] mem stats: total pages=");
	putdec64((UINT64)pages_total64());
	serial_puts(" free=");
	putdec64((UINT64)pages_free64());
	serial_puts("\n");

	/* M2-E: slab kmalloc across size classes, then free in reverse order */
	{
		unsigned long sizes[6] = { 8, 40, 200, 1000, 3000, 9000 };
		char *ptrs[6];
		unsigned long si;

		for(si = 0; si < 6; si++) {
			ptrs[si] = (char *)kmalloc64(sizes[si]);
			if(!ptrs[si]) {
				serial_puts("\n[M2-E] kmalloc64(");
				putdec64((UINT64)sizes[si]);
				serial_puts(") FAILED\n");
				for(;;) {
					__asm__ __volatile__("hlt");
				}
			}
			for(n = 0; n < sizes[si]; n++) {
				ptrs[si][n] = (char)(n * 3 + si);
			}
			ok = 1;
			for(n = 0; n < sizes[si]; n++) {
				if(ptrs[si][n] != (char)(n * 3 + si)) {
					ok = 0;
					break;
				}
			}
			serial_puts("\n[M2-E] kmalloc64(");
			putdec64((UINT64)sizes[si]);
			serial_puts(") = ");
			serial_hex((UINT64)(unsigned long)ptrs[si]);
			serial_puts(": write/read ");
			serial_puts(ok ? "OK" : "MISMATCH");
			serial_puts("\n");
		}
		kmem_stats64();
		for(si = 6; si > 0; si--) {
			kfree64(ptrs[si - 1]);
		}
		serial_puts("[M2-E] all freed, slabs now:\n");
		kmem_stats64();
	}

	/* M2-F: copy-on-write - map a page read-only, write to it; the #PF
	 * handler allocates a copy, remaps RW, and iretq retries the write */
	paddr = alloc_pages64(1);
	if(paddr) {
		unsigned long cow_phys;

		if(!map_page64(COW_VA, paddr, 0x000)) {	/* read-only */
			/* seed the old page through its direct phys alias */
			((char *)P2V64(paddr))[0] = (char)0xA5;
			((char *)P2V64(paddr))[1] = (char)0x5A;
			*(volatile unsigned char *)COW_VA = 0xEE;	/* -> #PF(W,P) */
			cow_phys = virt_to_phys64(COW_VA);
			serial_puts("\n[M2-F] COW: write to ");
			serial_hex((UINT64)COW_VA);
			serial_puts(" moved to phys ");
			serial_hex((UINT64)cow_phys);
			serial_puts(" (orig ");
			serial_hex((UINT64)paddr);
			serial_puts("): read back ");
			serial_puts(*(volatile unsigned char *)COW_VA == 0xEE ? "OK (0xEE)" : "MISMATCH");
			serial_puts(", orig intact ");
			serial_puts(((((unsigned char *)P2V64(paddr))[0] == (unsigned char)0xA5) &&
				     (((unsigned char *)P2V64(paddr))[1] == (unsigned char)0x5A)) ? "OK" : "MISMATCH");
			serial_puts(cow_phys != paddr ? ", detached" : ", NOT DETACHED!");
			serial_puts("\n");
			unmap_page64(COW_VA);
			free_pages64(cow_phys, 1);
			free_pages64(paddr, 1);
		} else {
			serial_puts("\n[M2-F] RO map FAILED\n");
		}
	}

/* M3-A: interrupt infrastructure - PIC/PIT timer, clock advances */
	irq64_init();
	__asm__ __volatile__("sti");
	serial_puts("\n[M3-A] interrupts enabled, waiting for timer ticks...\n");
	while(get_ticks64() < 50) {
	}
	serial_puts("[M3-A] tick 50 reached (0.5 s): ticks=");
	putdec64((UINT64)get_ticks64());
	serial_puts("\n");
	while(get_ticks64() < 100) {
	}
	serial_puts("[M3-A] tick 100 reached (1.0 s): ticks=");
	putdec64((UINT64)get_ticks64());
	serial_puts("\n");

	/* M4-B: hand control to the real Fiwix kernel (start_kernel). The
	 * M2/M3-A primitives above (4-level paging, IDT64, GDT64/TSS64,
	 * PIC/PIT 100 Hz) stay active underneath. The M4-A compat demo and
	 * M3-B thread demo below are kept for reference and skipped here. */
	kreal64_boot();
	for(;;) {
		__asm__ __volatile__("hlt");
	}

	/* M4-A: 32-bit compat user mode + int 0x80 syscall trap */
	{
		unsigned long phys;
		int i;

		serial_puts("\n[M4-A] mapping user pages at 0x300000 (identity, U/S)...\n");
		phys = alloc_pages64(2);
		if(!phys) {
			serial_puts("[M4-A] alloc_pages64 failed\n");
			for(;;) {
				__asm__ __volatile__("hlt");
			}
		}
		map_user_page64(0x300000, phys, 0x002);	/* code page, RW */
		map_user_page64(0x301000, phys + 0x1000, 0x002);	/* stack page */
		for(i = 0; i < 36; i++) {
			((char *)0x300000)[i] = (char)m4a_blob[i];
		}
		for(i = 0; i < 23; i++) {
			((char *)0x300100)[i] = m4a_msg[i];
		}
		serial_puts("[M4-A] entering 32-bit compat mode at 0x300000 (CPL3, IF=1)...\n");
		enter_user32();
		/* not reached */
	}

	/* M3-B: kernel threads + round-robin scheduler (sched64.c, switch64.S) */
	serial_puts("\n[M3-B] creating two kernel threads...\n");
	thread_create64(worker64);
	thread_create64(worker64);
	serial_puts("[M3-B] scheduler active (100 ms quantum); workers print on their slices\n");
	serial_puts("M3-B OK: kernel threads schedule, clock advances; idle now.\n");
	/* become the idle thread: the timer hands the CPU to the workers and
	 * (policy) never switches back while a worker is runnable */
	for(;;) {
		__asm__ __volatile__("hlt");
	}

	for(;;) {
		__asm__ __volatile__("hlt");
	}
}
