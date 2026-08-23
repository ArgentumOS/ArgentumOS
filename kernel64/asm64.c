/*
 * fiwix/kernel64/asm64.c
 *
 * Fiwix64 M4 (phase B): 64-bit replacements for the i386 assembly symbols
 * that kernel/core386.S + kernel/boot.S used to provide.
 *
 * Real implementations: port I/O, CPUID / CPU identification, RDTSC, TLB
 * flush, ltr. Inert stubs: the 32-bit exception/IRQ gate entries and the
 * process/syscall/user-mode paths (do_switch, switch_to_user_mode, syscall,
 * sighandler_trampoline, ...) - the 64-bit kernel keeps its own IDT64 /
 * process switching (kernel64/idt64.c, sched64.c) and these are only
 * needed for linking until those paths are ported.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fiwix/kernel.h>
#include <fiwix/string.h>
#include <fiwix/asm.h>

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

	__asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
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

	__asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(port));
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
	__asm__ __volatile__("outb %0, %1" :: "a"(value), "Nd"(port));
}

void outport_w(unsigned int port, unsigned short int value)
{
	__asm__ __volatile__("outw %0, %%dx" :: "a"(value), "d"(port));
}

void outport_l(unsigned int port, unsigned int value)
{
	__asm__ __volatile__("outl %0, %1" :: "a"(value), "Nd"(port));
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
	 * TCG flushes the whole TLB. */
	extern unsigned long paging64_pml4(void);
	__asm__ __volatile__("movq %0, %%cr3" :: "r"(paging64_pml4()) : "memory");
	__asm__ __volatile__("movq %0, %%cr3" :: "r"(cr3) : "memory");
}

void activate_kpage_dir(void)
{
	/* the 64-bit kernel keeps its own 4-level tables (kernel64/paging64.c);
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

/* ---- inert stubs (32-bit gate/process paths, unused in the 64-bit build
 * until ported: the 64-bit kernel uses idt64.c gates and sched64.c) ---- */

static void asm64_stub(void)
{
	__asm__ __volatile__("hlt");
	for(;;) {
	}
}

void except0(void) { asm64_stub(); }
void except1(void) { asm64_stub(); }
void except2(void) { asm64_stub(); }
void except3(void) { asm64_stub(); }
void except4(void) { asm64_stub(); }
void except5(void) { asm64_stub(); }
void except6(void) { asm64_stub(); }
void except7(void) { asm64_stub(); }
void except8(void) { asm64_stub(); }
void except9(void) { asm64_stub(); }
void except10(void) { asm64_stub(); }
void except11(void) { asm64_stub(); }
void except12(void) { asm64_stub(); }
void except13(void) { asm64_stub(); }
void except14(void) { asm64_stub(); }
void except15(void) { asm64_stub(); }
void except16(void) { asm64_stub(); }
void except17(void) { asm64_stub(); }
void except18(void) { asm64_stub(); }
void except19(void) { asm64_stub(); }
void except20(void) { asm64_stub(); }
void except21(void) { asm64_stub(); }
void except22(void) { asm64_stub(); }
void except23(void) { asm64_stub(); }
void except24(void) { asm64_stub(); }
void except25(void) { asm64_stub(); }
void except26(void) { asm64_stub(); }
void except27(void) { asm64_stub(); }
void except28(void) { asm64_stub(); }
void except29(void) { asm64_stub(); }
void except30(void) { asm64_stub(); }
void except31(void) { asm64_stub(); }
void irq0(void) { asm64_stub(); }
void irq1(void) { asm64_stub(); }
void irq2(void) { asm64_stub(); }
void irq3(void) { asm64_stub(); }
void irq4(void) { asm64_stub(); }
void irq5(void) { asm64_stub(); }
void irq6(void) { asm64_stub(); }
void irq7(void) { asm64_stub(); }
void irq8(void) { asm64_stub(); }
void irq9(void) { asm64_stub(); }
void irq10(void) { asm64_stub(); }
void irq11(void) { asm64_stub(); }
void irq12(void) { asm64_stub(); }
void irq13(void) { asm64_stub(); }
void irq14(void) { asm64_stub(); }
void irq15(void) { asm64_stub(); }
void unknown_irq(void) { asm64_stub(); }
/* switch_to_user_mode is now implemented in kernel64/switch64.S (M6) */
void syscall(void) { asm64_stub(); }
/* return_from_syscall is now implemented in kernel64/switch64.S (M6-E) */

/* M6-A: the real 32-bit signal trampoline is defined as adjacent labels
 * in kernel64/switch64.S (a C array pair would not be adjacent in the
 * link, so psig()'s end-start length would be garbage). */
