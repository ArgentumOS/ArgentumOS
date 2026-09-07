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

extern char _end[];
extern char fnx_bss_end[];	/* FNX: highest .bss (last-linked object) */
extern void start_kernel(unsigned int magic, unsigned int info,
			 unsigned long last_boot_addr);

extern void serial_puts(const char *s);

/* identity alias of a kernel-high address: the kernel maps low physical
 * memory 1:1 as well as at PAGE_OFFSET, and the real kernel's early code
 * (multiboot.c, mem_init) reads bootloader-provided pointers raw */
#define PHYS(addr)	((unsigned int)((unsigned long)(addr) - PAGE_OFFSET))

void kreal64_boot(void)
{
	static struct multiboot_info mbi;
	static struct multiboot_mod_list mods[1];
	static struct multiboot_mmap_entry mmap_tab[] = {
		{ 20, 0x000000, 0x100000, MULTIBOOT_MEMORY_RESERVED },
		{ 20, 0x100000, 0x7F00000, MULTIBOOT_MEMORY_AVAILABLE },
		{ 20, 0x8000000, 0x7800000, MULTIBOOT_MEMORY_RESERVED },
	};
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

	memset_b(&mbi, 0, sizeof(struct multiboot_info));
	mbi.flags = MULTIBOOT_INFO_MEMORY | MULTIBOOT_INFO_CMDLINE |
		    MULTIBOOT_INFO_MODS | MULTIBOOT_INFO_MEM_MAP;
	mbi.mem_lower = 640;
	mbi.mem_upper = 130048;		/* 128MB - 1MB, in KB */
	mbi.cmdline = PHYS(cmdline);
	mbi.mods_count = 1;
	mbi.mods_addr = PHYS(mods);
	mbi.mmap_addr = PHYS(mmap_tab);
	mbi.mmap_length = sizeof(mmap_tab);

	last_boot_addr = (unsigned long)&fnx_bss_end;	/* high address; start_kernel
						 * subtracts PAGE_OFFSET */

	start_kernel(MULTIBOOT_BOOTLOADER_MAGIC, PHYS(&mbi), last_boot_addr);

	/* start_kernel never returns; if it does, park here */
	serial_puts("[M4-B] start_kernel returned! halting.\n");
	__asm__ __volatile__("cli; hlt");
	for(;;) {
	}
}
