/*
 * fnx/kernel/boot64/asm64.c
 *
 * FNX M4 (phase B): 64-bit replacements for the i386 assembly symbols
 * that kernel/core386.S + kernel/boot.S used to provide.
 *
 * Real implementations: port I/O, CPUID / CPU identification, RDTSC, TLB
 * flush, ltr. Inert stubs: the 32-bit exception/IRQ gate entries and the
 * process/syscall/user-mode paths (do_switch, switch_to_user_mode, syscall,
 * sighandler_trampoline, ...) - the 64-bit kernel keeps its own IDT64 /
 * process switching (kernel/boot64/idt64.c, sched64.c) and these are only
 * needed for linking until those paths are ported.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/string.h>
#include <fnx/asm.h>

/* CPU identification globals (kernel.h externs, formerly in core386.S) */
int _cputype;
int _cpusignature;
int _cpuflags;
char _vendorid[12];
char _brandstr[48];
unsigned int _tlbinfo_eax;
unsigned int _tlbinfo_ebx;
unsigned int _tlbinfo_ecx;
unsigned int _tlbinfo_edx;

/* ---- port I/O ---- */

unsigned char inport_b(unsigned int port)
{
	unsigned char v;

	__asm__ __volatile__("inb %1, %0" : "=a"(v) : "d"((unsigned short)(port)));
	return v;
}

unsigned short int inport_w(unsigned int port)
{
	unsigned short int v;

	__asm__ __volatile__("inw %%dx, %0" : "=a"(v) : "d"(port));
	return v;
}

unsigned int inport_l(unsigned int port)
{
	unsigned int v;

	__asm__ __volatile__("inl %1, %0" : "=a"(v) : "d"((unsigned short)(port)));
	return v;
}

void inport_sw(unsigned int port, void *addr, unsigned int count)
{
	unsigned short int *p = (unsigned short int *)addr;

	while(count--) {
		*p++ = inport_w(port);
	}
}

void inport_sl(unsigned int port, void *addr, unsigned int count)
{
	unsigned int *p = (unsigned int *)addr;

	while(count--) {
		*p++ = inport_l(port);
	}
}

void outport_b(unsigned int port, unsigned char value)
{
	__asm__ __volatile__("outb %0, %1" :: "a"(value), "d"((unsigned short)(port)));
}

void outport_w(unsigned int port, unsigned short int value)
{
	__asm__ __volatile__("outw %0, %%dx" :: "a"(value), "d"(port));
}

void outport_l(unsigned int port, unsigned int value)
{
	__asm__ __volatile__("outl %0, %1" :: "a"(value), "d"((unsigned short)(port)));
}

void outport_sw(unsigned int port, void *addr, unsigned int count)
{
	unsigned short int *p = (unsigned short int *)addr;

	while(count--) {
		outport_w(port, *p++);
	}
}

void outport_sl(unsigned int port, void *addr, unsigned int count)
{
	unsigned int *p = (unsigned int *)addr;

	while(count--) {
		outport_l(port, *p++);
	}
}

/* ---- CPU identification ---- */

int cpuid(void)
{
	unsigned int eax, ebx, ecx, edx;

	/* leaf 0: vendor string */
	eax = 0;
	__asm__ __volatile__("cpuid"
			     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			     : "a"(eax));
	memcpy_b(_vendorid, &ebx, 4);
	memcpy_b(_vendorid + 4, &edx, 4);
	memcpy_b(_vendorid + 8, &ecx, 4);

	/* leaf 1: signature + feature flags */
	eax = 1;
	__asm__ __volatile__("cpuid"
			     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			     : "a"(eax));
	_cpusignature = eax;
	_cpuflags = edx;
	_cputype = eax;		/* family/model bits; caller interprets */

	/* leaf 0x80000000: check for extended leafs (brand string) */
	eax = 0x80000000;
	__asm__ __volatile__("cpuid"
			     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			     : "a"(eax));
	if(eax >= 0x80000004) {
		char *p = _brandstr;
		unsigned int leaf;

		for(leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
			__asm__ __volatile__("cpuid"
					     : "=a"(eax), "=b"(ebx),
					       "=c"(ecx), "=d"(edx)
					     : "a"(leaf));
			memcpy_b(p, &eax, 4); p += 4;
			memcpy_b(p, &ebx, 4); p += 4;
			memcpy_b(p, &ecx, 4); p += 4;
			memcpy_b(p, &edx, 4); p += 4;
		}
	}
	return (_cpusignature >> 8) & 0xF;	/* family */
}

int get_cpu_vendor_id(void)
{
	return 0;	/* vendor identification is done via _vendorid */
}

int signature_flags(void)
{
	return _cpuflags;
}

int brand_str(void)
{
	return 1;	/* the brand string was captured in _brandstr */
}

int tlbinfo(void)
{
	return 0;
}

int getfpu(void)
{
	return 1;	/* x86-64 CPUs always have an FPU */
}

unsigned long long int get_rdtsc(void)
{
	unsigned int lo, hi;

	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long int)hi << 32) | lo;
}

/* ---- MMU / segments ---- */

void invalidate_tlb(void)
{
	unsigned long cr3;

	__asm__ __volatile__("movq %%cr3, %0" : "=r"(cr3));
	/* QEMU-TCG quirk: a reload with the SAME cr3 value is optimized
	 * away, leaving the stale entry cached. Hop CR3 through the kernel
	 * pml4 (which maps this code) so the value actually changes and the
	 * TCG flushes the whole TLB.
	 *
	 * The hop target MUST be the PHYSICAL address of the kernel pml4.
	 * paging64_pml4() returns &pml4_page, which - because the kernel64
	 * code uses RIP-relative (PIC) addressing and executes from a mix of
	 * the identity and the high-half alias - resolves to a virtual
	 * address, never a usable CR3. Loading a VA into CR3 feeds the CPU a
	 * non-RAM physical address: QEMU-TCG hangs hard at the mov %cr3
	 * (observed: a killed sleeping child wedges in
	 * do_exit->release_binary->invalidate_tlb, so the parent's waitpid()
	 * hangs forever), and real hardware would #GP (CR3[63:52] set) or
	 * #PF. paging64_pml4_phys() normalizes &pml4_page to its physical
	 * address regardless of which alias the code runs from. */
	extern unsigned long paging64_pml4_phys(void);
	__asm__ __volatile__("movq %0, %%cr3" :: "r"(paging64_pml4_phys()) : "memory");
	__asm__ __volatile__("movq %0, %%cr3" :: "r"(cr3) : "memory");
}

void activate_kpage_dir(void)
{
	/* the 64-bit kernel keeps its own 4-level tables (kernel/boot64/paging64.c);
	 * the real 32-bit page directory must never take over CR3 */
}

void load_tr(unsigned int selector)
{
	extern void gdt64_ltr(unsigned int);
	gdt64_ltr(selector);
}

void load_gdt(addr_t addr)
{
	__asm__ __volatile__("lgdt %0" :: "m"(*(volatile void *)addr) : "memory");
}

void load_idt(addr_t addr)
{
	__asm__ __volatile__("lidt %0" :: "m"(*(volatile void *)addr) : "memory");
}


/* return_from_syscall is now implemented in kernel/boot64/switch64.S (M6-E) */

