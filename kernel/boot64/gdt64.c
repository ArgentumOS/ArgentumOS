/*
 * fnx/kernel/boot64/gdt64.c
 *
 * FNX M3 (phase A) / M4-C: the kernel's own 64-bit GDT + TSS.
 *
 * The GDT uses the CPU's native 8-byte descriptor slots and mirrors the
 * selectors the real (32-bit) Fiwix kernel's C code relies on:
 *   KERNEL_CS=0x08, KERNEL_DS=0x10, USER_CS=0x18 (32-bit, for compat
 *   mode), USER_DS=0x20, TSS=0x28 (16-byte descriptor, two slots).
 * The 64-bit runtime keeps the firmware's flat CS=0x38 / SS=0x40
 * selectors (cached in the segment registers), so 0x38 and 0x40 carry
 * the fw code/data descriptors; the IDT gates use the runtime CS (0x38).
 * 0x50/0x58 hold the 32-bit user segments used by the M4-A compat demo.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/efi.h>
#include <fnx/linker.h>
#include "serial64.h"

/* the stub's name for the kernel base (include/fnx/linker.h) */
#define PAGE_OFFSET64	PAGE_OFFSET

#define KCODE64_SEL	0x08
#define KDATA64_SEL	0x10
#define UCODE32_SEL	0x18
#define UDATA32_SEL	0x20
#define UCODE64_SEL	0x48
#define TSS64_SEL	0x28
#define FWCODE_SEL	0x38
#define FWDATA_SEL	0x40
#define DEMO_UCODE32	0x50
#define DEMO_UDATA32	0x58
#define TLS_SEL		0x60	/* per-process TLS data32 (set_thread_area) */
#define TLS_SLOT	12

#define NR_GDT_SLOTS	13	/* TSS occupies slots 5-6 (0x28-0x38) */

struct gdtr64 {
	unsigned short limit;
	unsigned long base;
} __attribute__((packed));

/* layout matches the SDM's 64-bit TSS: RSP0 at offset 4, ISTs at 36..92 */
struct tss64 {
	unsigned int reserved0;		/* 0x00 */
	unsigned long rsp0;		/* 0x04 */
	unsigned long rsp1;		/* 0x0C */
	unsigned long rsp2;		/* 0x14 */
	unsigned long reserved1;	/* 0x1C */
	unsigned long ist[7];		/* 0x24 */
	unsigned long reserved2;	/* 0x5C */
	unsigned short reserved3;	/* 0x64 */
	unsigned short iomap_base;	/* 0x66 */
} __attribute__((packed));

static unsigned long gdt64_tab[NR_GDT_SLOTS] __attribute__((aligned(16)));
static struct tss64 tss64;
static unsigned char kstack64[16384] __attribute__((aligned(16)));
static struct gdtr64 gdtr;

/* Load the TSS. The CPU sets the descriptor's busy bit (type 9 -> 11) on
 * every ltr, so a later ltr of the same selector (e.g. the real kernel's
 * load_tr(TSS)) would #GP; clear the busy bit first. */
void gdt64_ltr(unsigned int selector)
{
	unsigned int idx = selector >> 3;

	if(idx < NR_GDT_SLOTS) {
		gdt64_tab[idx] &= ~(1ULL << 41);
	}
	__asm__ __volatile__("ltr %%ax" :: "a"(selector));
}

/* FNX (M6-E): the CPU's active TSS is gdt64.c's static tss64, not the
 * real kernel's per-process i386tss (set_tss() only touches the inert
 * kernel/gdt.c table). Update the active TSS's RSP0 so each process's
 * syscalls/exceptions run on its OWN kmalloc'd kernel stack; otherwise
 * every process shares the .bss kstack64 and switch contexts collide. */
/* FNX (native port): fnx_rsp0 mirrors tss64.rsp0 for the 'syscall'
 * instruction entry (switch64.S syscall_entry64), which must switch stacks
 * itself; fnx_syscall_userrsp is its scratch for the incoming RSP. */
unsigned long fnx_rsp0;
unsigned long fnx_syscall_userrsp;

/* FNX (native port): set the %fs base MSR (x86-64 TLS). The kernel
 * itself does not use %fs, so this is only meaningful for user mode. */
void fnx_set_fs_base(unsigned long base)
{
	__asm__ __volatile__("wrmsr" :: "c"((unsigned long)0xC0000100),
		"a"((unsigned long)base), "d"((unsigned long)(base >> 32)));
}

void gdt64_set_rsp0(unsigned long rsp0)
{
	tss64.rsp0 = rsp0;
	fnx_rsp0 = rsp0;
}

/* build an 8-byte segment descriptor (base, limit, access, flags) */
static unsigned long make_desc64(unsigned long base, unsigned int limit,
				 unsigned char access, unsigned char flags)
{
	return (unsigned long)(limit & 0xFFFF)
		| ((base & 0xFFFF) << 16)
		| (((base >> 16) & 0xFF) << 32)
		| ((unsigned long)access << 40)
		| ((unsigned long)flags << 48)
		| (((base >> 24) & 0xFF) << 56);
}

/* FNX (M6 userland): update the per-process TLS segment base. musl and
 * glibc call set_thread_area(243) to install their thread pointer and then
 * load the returned selector (TLS_SLOT<<3 | 3) into %gs; errno and the
 * thread-control block are then reached via %gs-relative addressing. */
void gdt64_set_tls_base(unsigned long base)
{
	gdt64_tab[TLS_SLOT] = make_desc64(base, 0xFFFFF, 0xF2, 0xCF);
}

void gdt64_init(void)
{
	unsigned long tss_base;

	/* .bss is zeroed at load: slot 0 (NULL) is already 0 */

	gdt64_tab[1] = make_desc64(0, 0xFFFFF, 0x9A, 0xAF);	/* 0x08 kcode, L=1 */
	gdt64_tab[2] = make_desc64(0, 0xFFFFF, 0x92, 0xCF);	/* 0x10 kdata */
	gdt64_tab[3] = make_desc64(0, 0xFFFFF, 0xFA, 0xCF);	/* 0x18 user code32 (D=1) */
	gdt64_tab[4] = make_desc64(0, 0xFFFFF, 0xF2, 0xCF);	/* 0x20 user data32 */
	gdt64_tab[7] = make_desc64(0, 0xFFFFF, 0x9A, 0xAF);	/* 0x38 fw code (runtime CS) */
	gdt64_tab[8] = make_desc64(0, 0xFFFFF, 0x92, 0xCF);	/* 0x40 fw data (runtime SS) */

	/* 0x28 TSS (16-byte descriptor: lower qword at 0x28, upper at 0x30).
	 * The base MUST be the HIGH-HALF linear address of the static TSS:
	 * the CPU reads RSP0 from the TSS on every user->kernel privilege
	 * switch, i.e. while the CURRENT process pml4 is active. Process
	 * pml4s map the kernel high half (pml4[256..511] shared with the
	 * kernel) but NOT the low identity alias (the boot stub re-biases
	 * all data pointers high, so the identity image map was dropped) -
	 * an identity base here faults the moment the first timer IRQ or
	 * syscall fires in a process (observed CR2 = tss64+4, e=0000). */
	tss_base = (unsigned long)&tss64;
	tss64.rsp0 = (unsigned long)&kstack64[sizeof(kstack64)];
	fnx_rsp0 = tss64.rsp0;
	gdt64_tab[5] = make_desc64(tss_base, sizeof(struct tss64) - 1, 0x89, 0x00);
	gdt64_tab[6] = (unsigned long)((tss_base >> 32) & 0xFFFFFFFF);
	/* 0x50 / 0x58: 32-bit user code/data (L=0, D=1) for M4 compat demo */
	gdt64_tab[10] = make_desc64(0, 0xFFFFF, 0xFA, 0xCF);
	gdt64_tab[11] = make_desc64(0, 0xFFFFF, 0xF2, 0xCF);
	/* 0x48: 64-bit user code (L=1, DPL3) for the INIT process - the
	 * init_trampoline() is compiled as 64-bit code and runs in 64-bit
	 * user mode; exec'd 32-bit programs switch to UCODE32 (compat) */
	gdt64_tab[9] = make_desc64(0, 0xFFFFF, 0xFA, 0xAF);
	/* 0x60: per-process TLS data32 (%gs); base updated by set_thread_area */
	gdt64_tab[TLS_SLOT] = make_desc64(0, 0xFFFFF, 0xF2, 0xCF);

	gdtr.limit = (unsigned short)(sizeof(gdt64_tab) - 1);
	gdtr.base = (unsigned long)&gdt64_tab[0];
	__asm__ __volatile__("lgdt %0" :: "m"(gdtr));

	/* switch the runtime SS away from the firmware's 0x30 (which is now
	 * the TSS descriptor's upper half) to the fw data dupe at 0x40 */
	__asm__ __volatile__("movw %0, %%ax; movw %%ax, %%ss" ::
			     "i"(FWDATA_SEL) : "ax");

	/* load the TSS (RSP0 = kernel stack top for user->kernel switches) */
	__asm__ __volatile__("ltr %%ax" :: "a"(TSS64_SEL));

	serial_puts("[M3-A] GDT64 installed (0x18/0x20 user32, 0x48 user64, TSS@0x28, fw 0x38/0x40, demo 0x50/0x58), TSS @ ");
	serial_hex((UINT64)tss_base);
	serial_puts(" RSP0=");
	serial_hex((UINT64)tss64.rsp0);
	serial_puts("\n");
}
