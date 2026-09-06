/*
 * fnx/kernel/boot64/main64.c
 *
 * FNX long-mode entry (M1).
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

#include <fnx/efi.h>
#include "serial64.h"

/* M4-B: boot the real Fiwix kernel (start_kernel) on top of the 64-bit
 * primitives. See kernel/boot64/kreal64.c. */
#define KREAL64_BOOT	1
extern void kreal64_boot(void);

/* M2-A: installs the kernel's own 4-level page tables and jumps to the high
 * half, re-entering kernel64_main() there. Never returns on first call. */
void paging64_init(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN, UINTN, EFI_SYSTEM_TABLE *);

/* M2-B: physical page allocator + dynamic 4KB-page mapping (mm64.c) */
void mm64_init(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN);

/* M2-C: IDT64 + exception handling (idt64.c) */
void idt64_init(void);

/* M3-A: GDT64/TSS64 (gdt64.c) and PIC/PIT interrupts (irq64.c) */
void gdt64_init(void);
void irq64_init(void);

/* set before the first paging64_init() call so the high-half re-entry skips it */
static int paging64_done;

/* runtime physical load base of the .efi image (LoaderCode range), captured
 * from the EFI map; the kernel's high-half aliases are
 * PAGE_OFFSET64 + load_base + (section_offset) */
unsigned long fnx_load_base;
unsigned long fnx_image_size;	/* LoaderCode extent, bytes */

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

	/* FNX: the firmware leaves interrupts ENABLED after
	 * ExitBootServices() (its 8254 PIT is still running). Until
	 * idt64_init() installs the kernel IDT below, any IRQ vectors into
	 * OVMF's handler, which runs firmware memcpy()s into the loaded
	 * image (clobbering the .text tail / syscall entry). Close the
	 * window: IF=0 now, re-enabled only by the sti after irq64_init(). */
	__asm__ __volatile__("cli");

	serial_init();

	if(!paging64_done) {
		paging64_done = 1;
		paging64_init(map, map_size, desc_size, map_key, SystemTable);
		/* not reached: paging64_init jumps to the high half */
	}

	serial_puts("\n");
	serial_puts("================================================================\n");
	serial_puts("FNX M1: long-mode kernel alive, booted directly from UEFI\n");
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
		if(d->Type == EfiLoaderCode && !fnx_load_base) {
			fnx_load_base = (unsigned long)d->PhysicalStart;
			fnx_image_size = d->NumberOfPages << 12;
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

	/* FNX: install the kernel's own GDT64/TSS64, IDT64 and the
	 * PIC/PIT interrupt path, then hand control to the real FNX
	 * kernel (start_kernel). The primitives stay active underneath. */
	gdt64_init();
	idt64_init();
	irq64_init();
	__asm__ __volatile__("sti");

	kreal64_boot();
	for(;;) {
		/* FNX: IDLE never returns to user mode, so the CPL3 IRQ tail
		 * can never preempt on its behalf. When every process sleeps,
		 * do_sched() switches here; the timer BH then wakes a process
		 * (timeout expiry, serial input, ...) and sets need_resched,
		 * but with the IRQ in kernel mode the CPL3 consumer is
		 * skipped - without this check the woken process starves on
		 * the run queue forever and the system freezes (a DNS
		 * resolver polling with a timeout + a shell on serial input).
		 * Consume it here in normal context (switching from the IRQ
		 * frame itself is unsafe). */
		extern int need_resched;
		extern void do_sched(void);

		if(need_resched) {
			need_resched = 0;
			do_sched();
		}
		__asm__ __volatile__("hlt");
	}
}
