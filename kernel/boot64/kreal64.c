/*
 * fnx/kernel/boot64/kreal64.c
 *
 * FNX M4 (phase B): boot the REAL Fiwix kernel (start_kernel) on top of
 * the 64-bit primitives (paging64/gdt64/idt64/irq64). Synthesizes a minimal
 * Multiboot info (no bootloader is involved under UEFI) and hands control to
 * kernel/main.c's start_kernel().
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/multiboot1.h>
#include <fnx/string.h>
#include <fnx/mm.h>
#include <fnx/asm.h>
#include <fnx/efi.h>

extern char _end[];
extern char fnx_bss_end[];	/* FNX: highest .bss (last-linked object) */

/* the loader's LOW boot-structures window (kernel/boot64/efi_stub.c) */
extern unsigned long fnx_boot_window_base;
extern unsigned long fnx_boot_window_size;
extern void start_kernel(unsigned int magic, unsigned int info,
			 unsigned long last_boot_addr);

extern void serial_puts(const char *s);

/* identity alias of a kernel-high address: the kernel maps low physical
 * memory 1:1 as well as at PAGE_OFFSET, and the real kernel's early code
 * (multiboot.c, mem_init) reads bootloader-provided pointers raw */
#define PHYS(addr)	((unsigned int)((unsigned long)(addr) - PAGE_OFFSET))

/* The kernel's whole view of physical memory comes from this map
 * (mm/bios_map.c -> kstat.physical_pages). It used to be a hardcoded
 * 128MB/1MB/64MB triple no matter how much RAM the machine had, while the
 * loader's own structures (last_boot_addr, passed below) followed the REAL
 * map - so on a machine with more than 128MB the kernel carved its tables
 * above the memory it believed in, into pages it had never mapped, and died
 * with no output at all (printk's console does not exist yet there).
 *
 * So: build it from the UEFI map the loader already walks. AVAILABLE is
 * exactly what mm64.c frees - conventional and boot-services memory - and
 * everything else stays RESERVED, including the loader's own code/data, so
 * the kernel can never hand out its own image. */
#define STUB_MMAP_MAX	256

static struct multiboot_mmap_entry stub_mmap[STUB_MMAP_MAX];
static unsigned int stub_mmap_len;

static void
stub_mmap_add(unsigned long long from, unsigned long long len, unsigned int type)
{
	struct multiboot_mmap_entry *e;

	if(!len) {
		return;
	}
	/* merge with the previous region when type and boundary match: the
	 * UEFI map is long and fragmented */
	if(stub_mmap_len) {
		e = &stub_mmap[stub_mmap_len - 1];
		if(e->type == type && e->addr + e->len == from) {
			e->len += len;
			return;
		}
	}
	if(stub_mmap_len >= STUB_MMAP_MAX) {
		return;		/* the rest of the map is dropped: RAM stays
				 * unused rather than being mis-typed */
	}
	e = &stub_mmap[stub_mmap_len++];
	e->size = 20;
	e->addr = from;
	e->len = len;
	e->type = type;
}

void kreal64_boot(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size, UINTN desc_size)
{
	static struct multiboot_info mbi;
	static struct multiboot_mod_list mods[1];
	EFI_MEMORY_DESCRIPTOR *d;
	unsigned long long usable_kb;
	UINTN n, count;
	/* no rootfstype= on the command line: the root filesystem type is
	 * probed by mount_root() (minix -> ext2 -> iso9660 -> agfs).
	 * FNX_RECOVERY_PARAM (build-time, via the kreal64.o rule) appends
	 * the 'recovery' param for a forced-recovery test boot. */
#ifdef FNX_RECOVERY_PARAM
	static char cmdline[] = "fnx console=/System/Devices/Serial/Port0 root=/System/Devices/Disk/AHCI/Disk0/WholeDisk recovery";
#else
	static char cmdline[] = "fnx console=/System/Devices/Serial/Port0 root=/System/Devices/Disk/AHCI/Disk0/WholeDisk";
#endif
	static char initrd_name[] = "fnxinitrd";	/* unused (no initrd= in cmdline) */
	unsigned long last_boot_addr;

	extern const unsigned char initrd64_img[];
	extern const unsigned int initrd64_size;

	serial_puts("\n[M4-B] calling the real FNX kernel start_kernel()\n");

	/* printk()'s debugcon path (port 0xE9) is gated on this flag; the
	 * probe in start_kernel reads 0xE9 back, which QEMU's -debugcon
	 * device does not guarantee, so set it explicitly. */
	kstat.flags |= KF_HAS_DEBUGCON;

	/* M4-C: present the minix-v1 initrd (with /bin/init) as a Multiboot
	 * module so the kernel's root mount has a filesystem to read */
	mods[0].mod_start = PHYS(initrd64_img);
	mods[0].mod_end = PHYS(initrd64_img) + initrd64_size;
	mods[0].cmdline = PHYS(initrd_name);
	mods[0].pad = 0;

	/* the real memory map, in Multiboot terms */
	stub_mmap_len = 0;
	usable_kb = 0;
	count = map_size / desc_size;
	for(n = 0; n < count; n++) {
		unsigned long long len;
		unsigned int type;

		d = (EFI_MEMORY_DESCRIPTOR *)((char *)map + (n * desc_size));
		len = (unsigned long long)d->NumberOfPages << 12;
		if(fnx_boot_window_base &&
		   d->PhysicalStart >= fnx_boot_window_base &&
		   d->PhysicalStart < fnx_boot_window_base + fnx_boot_window_size) {
			/* The kernel's own boot-structures window: AVAILABLE in
			 * THIS map, so mem_init()'s per-stage is_addr_in_bios_map()
			 * guards accept the carve-out. It stays EfiLoaderData in the
			 * UEFI map, which is the one mm64.c walks, so its bitmap
			 * never frees these pages to the allocator. */
			type = MULTIBOOT_MEMORY_AVAILABLE;
		} else if(d->Type == EfiConventionalMemory ||
		   d->Type == EfiBootServicesCode ||
		   d->Type == EfiBootServicesData) {
			type = MULTIBOOT_MEMORY_AVAILABLE;
			usable_kb += len >> 10;
		} else {
			type = MULTIBOOT_MEMORY_RESERVED;
		}
		stub_mmap_add((unsigned long long)d->PhysicalStart, len, type);
	}
	memset_b(&mbi, 0, sizeof(struct multiboot_info));
	mbi.flags = MULTIBOOT_INFO_MEMORY | MULTIBOOT_INFO_CMDLINE |
		    MULTIBOOT_INFO_MODS | MULTIBOOT_INFO_MEM_MAP;
	mbi.mem_lower = 640;
	mbi.mem_upper = (usable_kb > 1024) ? (unsigned int)(usable_kb - 1024) : 0;
	mbi.cmdline = PHYS(cmdline);
	mbi.mods_count = 1;
	mbi.mods_addr = PHYS(mods);
	mbi.mmap_addr = PHYS(stub_mmap);
	mbi.mmap_length = stub_mmap_len * sizeof(struct multiboot_mmap_entry);

	/* the low window when the loader got one, else the image's end (which
	 * only fits small guests: the carve-out then runs into the firmware's
	 * holes near the top of RAM) */
	last_boot_addr = fnx_boot_window_base ?
			 (fnx_boot_window_base + PAGE_OFFSET) : (unsigned long)&fnx_bss_end;

	start_kernel(MULTIBOOT_BOOTLOADER_MAGIC, PHYS(&mbi), last_boot_addr);

	/* start_kernel never returns; if it does, park here */
	serial_puts("[M4-B] start_kernel returned! halting.\n");
	__asm__ __volatile__("cli; hlt");
	for(;;) {
	}
}
