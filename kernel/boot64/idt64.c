/*
 * fnx/kernel/boot64/idt64.c
 *
 * FNX M2 (phase C): IDT64 + exception handling + demand paging.
 *
 * A minimal 64-bit IDT is installed for vectors 0-31: 32 tiny asm stubs
 * (one per vector, pushed onto the stack so the C dispatcher knows which
 * vector fired, plus a unified error-code slot) jump to isr_common64, which
 * saves the GPRs, calls isr64_dispatch(), restores them and iretq's.
 *
 * The dispatcher handles #PF (vector 14) specially: if CR2 is the known
 * demand-page address it allocates a page, maps it with mm64.c and returns
 * (the CPU retries the faulting access). Any other exception or unknown
 * fault is reported (vector/error/rip/cr2/rsp) and halts.
 *
 * The stub addresses live in a C table built with visibility("hidden")
 * externs - hidden symbols are referenced RIP-relatively (no GOT), and the
 * absolute table values get PE base relocations, both exactly like the
 * .bss page tables. The IDT itself is a static array (internal linkage).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/string.h>
#include <fnx/efi.h>
#include <fnx/linker.h>
#include <fnx/sigcontext.h>
#include "serial64.h"

/* the stub's name for the kernel base (include/fnx/linker.h) */
#define PAGE_OFFSET64	PAGE_OFFSET

/* M4: compat syscall handler (defined in main64.c) */
void syscall80_handler(unsigned long *gprs);

#define IDT_GATE_INT	0x8E	/* present, DPL0, 64-bit interrupt gate */

struct idt_entry {
	unsigned short offset_lo;
	unsigned short selector;
	unsigned char ist;
	unsigned char type_attr;
	unsigned short offset_mid;
	unsigned int offset_hi;
	unsigned int reserved;
};

struct idtr64 {
	unsigned short limit;
	unsigned long base;
} __attribute__((packed));

/* the CPU-pushed frame, below the 15 saved GPRs */
struct x86_frame64 {
	unsigned long vector;
	unsigned long error;
	unsigned long rip;
	unsigned long cs;
	unsigned long rflags;
	unsigned long rsp;
	unsigned long ss;
};

/* the real kernel's page-fault path (mm/fault.c): walks current's vma_table
 * and demand-maps / copy-on-write / SIGSEGVs; called for user-mode faults
 * and for kernel-mode faults on vma-covered pages (K1/CoW) */
extern struct vma *find_vma_region(unsigned long);
extern void do_page_fault(unsigned int, struct sigcontext *);

void tlb_flush64(void);
void irq64_handler(unsigned long);

static struct idt_entry idt[256] __attribute__((aligned(16)));

static struct idtr64 idtr;

/*
 * 32 per-vector stubs: push a fake error code for vectors without one, push
 * the vector number, then jump to the common save/restore body.
 */
__asm__(
	".altmacro\n"
	".macro ISR_NOERR vec\n"
	".p2align 4\n"
	"isr_stub_\\vec:\n"
	"pushq $0\n"
	"pushq $\\vec\n"
	"jmp isr_common64\n"
	".endm\n"
	".macro ISR_ERR vec\n"
	".p2align 4\n"
	"isr_stub_\\vec:\n"
	"pushq $\\vec\n"
	"jmp isr_common64\n"
	".endm\n"
	"ISR_NOERR 0\n"
	"ISR_NOERR 1\n"
	"ISR_NOERR 2\n"
	"ISR_NOERR 3\n"
	"ISR_NOERR 4\n"
	"ISR_NOERR 5\n"
	"ISR_NOERR 6\n"
	"ISR_NOERR 7\n"
	"ISR_ERR 8\n"
	"ISR_NOERR 9\n"
	"ISR_ERR 10\n"
	"ISR_ERR 11\n"
	"ISR_ERR 12\n"
	"ISR_ERR 13\n"
	"ISR_ERR 14\n"
	"ISR_NOERR 15\n"
	"ISR_NOERR 16\n"
	"ISR_ERR 17\n"
	"ISR_NOERR 18\n"
	"ISR_NOERR 19\n"
	"ISR_NOERR 20\n"
	"ISR_ERR 21\n"
	"ISR_NOERR 22\n"
	"ISR_NOERR 23\n"
	"ISR_NOERR 24\n"
	"ISR_NOERR 25\n"
	"ISR_NOERR 26\n"
	"ISR_NOERR 27\n"
	"ISR_NOERR 28\n"
	"ISR_NOERR 29\n"
	"ISR_NOERR 30\n"
	"ISR_NOERR 31\n"
	"ISR_NOERR 32\n"
	"ISR_NOERR 33\n"
	"ISR_NOERR 34\n"
	"ISR_NOERR 35\n"
	"ISR_NOERR 36\n"
	"ISR_NOERR 37\n"
	"ISR_NOERR 38\n"
	"ISR_NOERR 39\n"
	"ISR_NOERR 40\n"
	"ISR_NOERR 41\n"
	"ISR_NOERR 42\n"
	"ISR_NOERR 43\n"
	"ISR_NOERR 44\n"
	"ISR_NOERR 45\n"
	"ISR_NOERR 46\n"
	"ISR_NOERR 47\n"
	"ISR_NOERR 48\n"
	"ISR_NOERR 49\n"
	"ISR_NOERR 50\n"
	"ISR_NOERR 51\n"
	"ISR_NOERR 52\n"
	"ISR_NOERR 53\n"
	"ISR_NOERR 54\n"
	"ISR_NOERR 55\n"
	"ISR_NOERR 56\n"
	"ISR_NOERR 57\n"
	"ISR_NOERR 58\n"
	"ISR_NOERR 59\n"
	"ISR_NOERR 60\n"
	"ISR_NOERR 61\n"
	"ISR_NOERR 62\n"
	"ISR_NOERR 63\n"
	"ISR_NOERR 128\n"
	"isr_common64:\n"
	"pushq %rax\n"
	"pushq %rcx\n"
	"pushq %rdx\n"
	"pushq %rbx\n"
	"pushq %rbp\n"
	"pushq %rsi\n"
	"pushq %rdi\n"
	"pushq %r8\n"
	"pushq %r9\n"
	"pushq %r10\n"
	"pushq %r11\n"
	"pushq %r12\n"
	"pushq %r13\n"
	"pushq %r14\n"
	"pushq %r15\n"
	"movq %rsp, %rdi\n"
	"call isr64_dispatch\n"
	"popq %r15\n"
	"popq %r14\n"
	"popq %r13\n"
	"popq %r12\n"
	"popq %r11\n"
	"popq %r10\n"
	"popq %r9\n"
	"popq %r8\n"
	"popq %rdi\n"
	"popq %rsi\n"
	"popq %rbp\n"
	"popq %rbx\n"
	"popq %rdx\n"
	"popq %rcx\n"
	"popq %rax\n"
	"addq $16, %rsp\n"
	"iretq\n"
);

/* hidden externs: referenced RIP-relatively, no GOT */
#define DECL_STUB(n) extern void isr_stub_##n(void) __attribute__((visibility("hidden")));

DECL_STUB(0)
DECL_STUB(1)
DECL_STUB(2)
DECL_STUB(3)
DECL_STUB(4)
DECL_STUB(5)
DECL_STUB(6)
DECL_STUB(7)
DECL_STUB(8)
DECL_STUB(9)
DECL_STUB(10)
DECL_STUB(11)
DECL_STUB(12)
DECL_STUB(13)
DECL_STUB(14)
DECL_STUB(15)
DECL_STUB(16)
DECL_STUB(17)
DECL_STUB(18)
DECL_STUB(19)
DECL_STUB(20)
DECL_STUB(21)
DECL_STUB(22)
DECL_STUB(23)
DECL_STUB(24)
DECL_STUB(25)
DECL_STUB(26)
DECL_STUB(27)
DECL_STUB(28)
DECL_STUB(29)
DECL_STUB(30)
DECL_STUB(31)
DECL_STUB(32)
DECL_STUB(33)
DECL_STUB(34)
DECL_STUB(35)
DECL_STUB(36)
DECL_STUB(37)
DECL_STUB(38)
DECL_STUB(39)
DECL_STUB(40)
DECL_STUB(41)
DECL_STUB(42)
DECL_STUB(43)
DECL_STUB(44)
DECL_STUB(45)
DECL_STUB(46)
DECL_STUB(47)
DECL_STUB(48)
DECL_STUB(49)
DECL_STUB(50)
DECL_STUB(51)
DECL_STUB(52)
DECL_STUB(53)
DECL_STUB(54)
DECL_STUB(55)
DECL_STUB(56)
DECL_STUB(57)
DECL_STUB(58)
DECL_STUB(59)
DECL_STUB(60)
DECL_STUB(61)
DECL_STUB(62)
DECL_STUB(63)
DECL_STUB(128)

/* absolute addresses of the stubs (PE base-relocated on load) */
static unsigned long stub_addr[64] = {
	(unsigned long)&isr_stub_0,
	(unsigned long)&isr_stub_1,
	(unsigned long)&isr_stub_2,
	(unsigned long)&isr_stub_3,
	(unsigned long)&isr_stub_4,
	(unsigned long)&isr_stub_5,
	(unsigned long)&isr_stub_6,
	(unsigned long)&isr_stub_7,
	(unsigned long)&isr_stub_8,
	(unsigned long)&isr_stub_9,
	(unsigned long)&isr_stub_10,
	(unsigned long)&isr_stub_11,
	(unsigned long)&isr_stub_12,
	(unsigned long)&isr_stub_13,
	(unsigned long)&isr_stub_14,
	(unsigned long)&isr_stub_15,
	(unsigned long)&isr_stub_16,
	(unsigned long)&isr_stub_17,
	(unsigned long)&isr_stub_18,
	(unsigned long)&isr_stub_19,
	(unsigned long)&isr_stub_20,
	(unsigned long)&isr_stub_21,
	(unsigned long)&isr_stub_22,
	(unsigned long)&isr_stub_23,
	(unsigned long)&isr_stub_24,
	(unsigned long)&isr_stub_25,
	(unsigned long)&isr_stub_26,
	(unsigned long)&isr_stub_27,
	(unsigned long)&isr_stub_28,
	(unsigned long)&isr_stub_29,
	(unsigned long)&isr_stub_30,
	(unsigned long)&isr_stub_31,
	(unsigned long)&isr_stub_32,
	(unsigned long)&isr_stub_33,
	(unsigned long)&isr_stub_34,
	(unsigned long)&isr_stub_35,
	(unsigned long)&isr_stub_36,
	(unsigned long)&isr_stub_37,
	(unsigned long)&isr_stub_38,
	(unsigned long)&isr_stub_39,
	(unsigned long)&isr_stub_40,
	(unsigned long)&isr_stub_41,
	(unsigned long)&isr_stub_42,
	(unsigned long)&isr_stub_43,
	(unsigned long)&isr_stub_44,
	(unsigned long)&isr_stub_45,
	(unsigned long)&isr_stub_46,
	(unsigned long)&isr_stub_47,
	(unsigned long)&isr_stub_48,
	(unsigned long)&isr_stub_49,
	(unsigned long)&isr_stub_50,
	(unsigned long)&isr_stub_51,
	(unsigned long)&isr_stub_52,
	(unsigned long)&isr_stub_53,
	(unsigned long)&isr_stub_54,
	(unsigned long)&isr_stub_55,
	(unsigned long)&isr_stub_56,
	(unsigned long)&isr_stub_57,
	(unsigned long)&isr_stub_58,
	(unsigned long)&isr_stub_59,
	(unsigned long)&isr_stub_60,
	(unsigned long)&isr_stub_61,
	(unsigned long)&isr_stub_62,
	(unsigned long)&isr_stub_63
};

static void set_gate(int vec, unsigned long handler, unsigned short cs, int dpl)
{
	struct idt_entry *e;

	e = &idt[vec];
	e->offset_lo = (unsigned short)(handler & 0xFFFF);
	e->selector = cs;
	e->ist = 0;
	e->type_attr = (unsigned char)(IDT_GATE_INT | ((dpl & 3) << 5));
	e->offset_mid = (unsigned short)((handler >> 16) & 0xFFFF);
	e->offset_hi = (unsigned int)((handler >> 32) & 0xFFFFFFFF);
	e->reserved = 0;
}

void idt64_init(void)
{
	unsigned short cs;
	int n, dpl;

	__asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
	for(n = 0; n < 64; n++) {
		/* DPL3 for the user-catchable vectors: #BP, #OF, #BR */
		dpl = (n == 3 || n == 4 || n == 17) ? 3 : 0;
		set_gate(n, stub_addr[n], cs, dpl);
	}
	/* M4: int 0x80 (compat syscall entry) as a DPL3 interrupt gate */
	set_gate(0x80, (unsigned long)&isr_stub_128, cs, 3);

	/* FNX (native port): the 'syscall' instruction used by musl
	 * x86_64. LSTAR = the entry (switch64.S syscall_entry64); STAR's
	 * low syscall-CS field = KCODE64 (0x08) so the CPU loads CS=0x08 /
	 * SS=0x10 for the kernel; FMASK clears IF/DF on entry (RFLAGS is
	 * restored from R11 by the iretq epilogue). The frame the entry
	 * builds (cs=UCODE64|RPL3, ss=UDATA32|RPL3) drives the return mode. */
	{
		extern void syscall_entry64(void);
		unsigned long long v;

		/* STAR: SYSCALL CS=0x08 (KCODE64), SS=0x10; SYSRET CS=0x08<<48 */
		v = 0x08ULL << 32;
		__asm__ __volatile__("wrmsr" :: "c"((unsigned long)0xC0000081),
			"a"((unsigned int)v), "d"((unsigned int)(v >> 32)));	/* STAR */
		/* LSTAR: the FULL 64-bit high-half runtime address of
		 * syscall_entry64 - writing only EAX would zero the high half
		 * and the 'syscall' would enter at the low canonical alias
		 * (identity-mapped 2.1GB MMIO hole = zeros). */
		v = (unsigned long long)syscall_entry64;
		__asm__ __volatile__("wrmsr" :: "c"((unsigned long)0xC0000082),
			"a"((unsigned int)v), "d"((unsigned int)(v >> 32)));	/* LSTAR */
		__asm__ __volatile__("wrmsr" :: "c"((unsigned long)0xC0000084),
			"a"((unsigned long)0x202), "d"(0));			/* FMASK */
		/* FNX (native port): enable IA32_EFER.SCE - without it the
		 * 'syscall' instruction #UDs and a native musl program dies at
		 * its first syscall (arch_prctl during TLS setup). */
		{
			unsigned int lo, hi;

			__asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"((unsigned long)0xC0000080));
			lo |= 0x1;	/* SCE */
			__asm__ __volatile__("wrmsr" :: "c"((unsigned long)0xC0000080), "a"(lo), "d"(hi));
		}
	}

	idtr.limit = (unsigned short)(sizeof(idt) - 1);
	idtr.base = (unsigned long)idt;
	__asm__ __volatile__("lidt %0" :: "m"(idtr));

	serial_puts("[M2-C] IDT64 installed: 64 gates (0-31 exceptions, 32-47 IRQs), code selector ");
	serial_hex((UINT64)cs);
	serial_puts("\n");
}

static unsigned long get_cr2(void)
{
	unsigned long cr2;

	__asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
	return cr2;
}

static void panic(const struct x86_frame64 *f)
{
	unsigned long a, b, c, d, si, di, r8, r9, r10, r11, r12, r13, r14, r15;

	serial_puts("\n!!! KERNEL EXCEPTION vector 0x");
	puthex32((unsigned int)f->vector);
	serial_puts(" error=0x");
	puthex32((unsigned int)f->error);
	serial_puts(" rip=");
	serial_hex((UINT64)f->rip);
	serial_puts(" cr2=");
	serial_hex((UINT64)get_cr2());
	serial_puts(" rsp=");
	serial_hex((UINT64)f->rsp);
	__asm__ __volatile__("mov %%rax,%0; mov %%rbx,%1; mov %%rcx,%2; mov %%rdx,%3"
		: "=r"(a), "=r"(b), "=r"(c), "=r"(d));
	__asm__ __volatile__("mov %%rsi,%0; mov %%rdi,%1; mov %%r8,%2; mov %%r9,%3; mov %%r10,%4; mov %%r11,%5"
		: "=r"(si), "=r"(di), "=r"(r8), "=r"(r9), "=r"(r10), "=r"(r11));
	__asm__ __volatile__("mov %%r12,%0; mov %%r13,%1; mov %%r14,%2; mov %%r15,%3"
		: "=r"(r12), "=r"(r13), "=r"(r14), "=r"(r15));
	serial_puts(" rax=");
	serial_hex(a);
	serial_puts(" rbx=");
	serial_hex(b);
	serial_puts(" rcx=");
	serial_hex(c);
	serial_puts(" rdx=");
	serial_hex(d);
	serial_puts("\n rsi=");
	serial_hex(si);
	serial_puts(" rdi=");
	serial_hex(di);
	serial_puts(" r8=");
	serial_hex(r8);
	serial_puts(" r9=");
	serial_hex(r9);
	serial_puts(" r10=");
	serial_hex(r10);
	serial_puts(" r11=");
	serial_hex(r11);
	serial_puts("\n r12=");
	serial_hex(r12);
	serial_puts(" r13=");
	serial_hex(r13);
	serial_puts(" r14=");
	serial_hex(r14);
	serial_puts(" r15=");
	serial_hex(r15);
	serial_puts("\n");
	/* dump the kernel stack return addresses (top 16 words) */
	{
		unsigned long *sp = (unsigned long *)(f->rsp & ~0xfUL);
		int k;
		for(k = 0; k < 16; k++) {
			serial_puts("  [sp+");
			puthex32((unsigned int)(k * 8));
			serial_puts("]=");
			serial_hex(sp[k]);
			serial_puts("\n");
		}
	}
	serial_puts("!!! halting.\n");
	for(;;) {
		__asm__ __volatile__("hlt");
	}
}

/*
 * #PF: handled by error code and CR2:
 *  - fault in USER mode: the real kernel's do_page_fault() walks current's
 *    vma_table and demand-maps (or SIGSEGVs); we return and the CPU retries.
 *  - kernel-mode fault on a NOT-PRESENT page with a vma (K1): the process
 *    pml4 has no low identity map in the canonical split, so a kernel write
 *    to a not-yet-demand-mapped user page (e.g. elf_load64's BSS zero-fill)
 *    genuinely faults; do_page_fault()'s kernel-mode path maps it and we
 *    retry. A kernel fault with NO vma (K2) is a kernel bug - panic (do NOT
 *    enter do_page_fault()'s 32-bit user-stack probe, which recurses on a
 *    64-bit frame).
 *  - anything else (present-page protection violation in kernel mode): panic.
 */
static void handle_page_fault(const struct x86_frame64 *f)
{
	unsigned long cr2;

	cr2 = get_cr2();
	if(f->error & 0x04) {
		/* fault in USER mode: let the real kernel's do_page_fault() handle
		 * it (vma walk + map_page(), which maps the ACTIVE tables), then
		 * the isr epilogue iretq retries the faulting access. */
		struct sigcontext sc;

		memset_b(&sc, 0, sizeof(sc));
		sc.err = f->error;
		sc.rip = f->rip;
		sc.cs = f->cs;
		sc.rflags = f->rflags;
		sc.rsp = f->rsp;
		sc.ss = f->ss;
		do_page_fault(14, &sc);
		return;
	}
	if(!(f->error & 0x1)) {
		/* kernel-mode fault on a NOT-PRESENT page: K1 (vma) / K2 (no vma) */
		struct sigcontext sc;

		if(!find_vma_region(cr2)) {
			/* K2. Before declaring a kernel bug, check the fault-
			 * recovering user-copy machinery: a syscall copying
			 * to/from a user pointer with NO vma (a bogus pointer
			 * such as open((void *)0x1)) faults here, and must
			 * unwind to -EFAULT instead of panicking. Route through
			 * the real kernel's do_page_fault() (its no-vma branch
			 * runs the same user-copy check); only panic if that
			 * path declines. */
			extern int user_copy_in_progress(void);
			if(user_copy_in_progress() && cr2 < 0x0000800000000000UL) {
				struct sigcontext sc;

				memset_b(&sc, 0, sizeof(sc));
				sc.err = f->error;
				sc.rip = f->rip;
				sc.cs = f->cs;
				sc.rflags = f->rflags;
				sc.rsp = f->rsp;
				sc.ss = f->ss;
				do_page_fault(14, &sc);
				return;
			}
			panic(f);
		}
		memset_b(&sc, 0, sizeof(sc));
		sc.err = f->error;
		sc.rip = f->rip;
		sc.cs = f->cs;
		sc.rflags = f->rflags;
		sc.rsp = f->rsp;
		sc.ss = f->ss;
		do_page_fault(14, &sc);
		return;
	}

	/* kernel-mode fault on a PRESENT page: a protection violation. With a
	 * vma this is normally copy-on-write - fork demotes shared writable
	 * leaves to RO, so a CPL0 write to an inherited page (e.g. signal
	 * frame setup on a child's stack) faults here; do_page_fault()'s
	 * kernel-mode path runs page_protection_violation() (copy + remap RW)
	 * and we retry. Without a vma it is a genuine kernel bug: panic. */
	struct sigcontext sc;

	if(!find_vma_region(cr2)) {
		/* same user-copy escape hatch as the not-present K2 case: a
		 * present page whose vma vanished mid-copy must not panic */
		extern int user_copy_in_progress(void);
		extern void user_copy_fault_recover(void);
		if(user_copy_in_progress() && cr2 < 0x0000800000000000UL) {
			user_copy_fault_recover();
			/* not reached */
		}
		panic(f);
	}
	memset_b(&sc, 0, sizeof(sc));
	sc.err = f->error;
	sc.rip = f->rip;
	sc.cs = f->cs;
	sc.rflags = f->rflags;
	sc.rsp = f->rsp;
	sc.ss = f->ss;
	do_page_fault(14, &sc);
}

/* gprs points at the saved r15 (the first of 15 pushed GPRs; the frame is above) */
static void handle_user_exception(const struct x86_frame64 *f, unsigned long *gprs)
{
	extern void do_divide_error(unsigned int, struct sigcontext *);
	extern void do_breakpoint(unsigned int, struct sigcontext *);
	extern void do_overflow(unsigned int, struct sigcontext *);
	extern void do_bound(unsigned int, struct sigcontext *);
	extern void do_invalid_opcode(unsigned int, struct sigcontext *);
	extern void do_double_fault(unsigned int, struct sigcontext *);
	extern void do_invalid_tss(unsigned int, struct sigcontext *);
	extern void do_segment_not_present(unsigned int, struct sigcontext *);
	extern void do_stack_segment_fault(unsigned int, struct sigcontext *);
	extern void do_general_protection(unsigned int, struct sigcontext *);
	extern void do_floating_point_error(unsigned int, struct sigcontext *);
	extern void do_alignment_check(unsigned int, struct sigcontext *);
	extern void do_machine_check(unsigned int, struct sigcontext *);
	extern void do_simd_fault(unsigned int, struct sigcontext *);
	struct sigcontext sc;

	memset_b(&sc, 0, sizeof(sc));
	sc.err = f->error;
	sc.rip = f->rip;
	sc.cs = f->cs;
	sc.rflags = f->rflags;
	sc.rsp = f->rsp;
	sc.ss = f->ss;
	sc.rax = gprs[14];
	sc.rcx = gprs[13];
	sc.rdx = gprs[12];
	sc.rbx = gprs[11];
	sc.rbp = gprs[10];
	sc.rsi = gprs[9];
	sc.rdi = gprs[8];
	sc.r8 = gprs[7];
	sc.r9 = gprs[6];
	sc.r10 = gprs[5];
	sc.r11 = gprs[4];
	sc.r12 = gprs[3];
	sc.r13 = gprs[2];
	sc.r14 = gprs[1];
	sc.r15 = gprs[0];

	switch(f->vector) {
		case 0:		do_divide_error(0, &sc);		break;
		case 3:		do_breakpoint(3, &sc);			break;
		case 4:		do_overflow(4, &sc);			break;
		case 5:		do_bound(5, &sc);			break;
		case 6:		do_invalid_opcode(6, &sc);		break;
		case 8:		do_double_fault(8, &sc);		break;
		case 10:	do_invalid_tss(10, &sc);		break;
		case 11:	do_segment_not_present(11, &sc);	break;
		case 12:	do_stack_segment_fault(12, &sc);	break;
		case 13:	do_general_protection(13, &sc);		break;
		case 16:	do_floating_point_error(16, &sc);	break;
		case 17:	do_alignment_check(17, &sc);		break;
		case 18:	do_machine_check(18, &sc);		break;
		case 19:	do_simd_fault(19, &sc);			break;
		default:	panic(f);
	}
}

/* gprs points at the saved r15 (the first of 15 pushed GPRs; the frame is above) */
/* M6-E: the 32-bit kernel checks pending signals on every return to user
 * mode (core386.S CHECK_IF_SIGNALS -> issig()/psig()). The 64-bit return
 * path does the same: build a 32-bit sigcontext from the frame, let psig()
 * rewrite it (handler trampoline or do_exit for default signals), and write
 * the result back into the iretq frame. psig() never returns on do_exit. */
void check_signals64(unsigned long *gprs)
{
	struct x86_frame64 *f;
	struct sigcontext sc;
	extern int issig(void);
	extern void psig(struct sigcontext *);

	f = (struct x86_frame64 *)((char *)gprs + (15 * 8));
	memset_b(&sc, 0, sizeof(sc));
	sc.err = f->error;
	sc.rip = f->rip;
	sc.cs = f->cs;
	sc.rflags = f->rflags;
	sc.rsp = f->rsp;
	sc.ss = f->ss;
	sc.rax = gprs[14];
	sc.rcx = gprs[13];
	sc.rdx = gprs[12];
	sc.rbx = gprs[11];
	sc.rbp = gprs[10];
	sc.rsi = gprs[9];
	sc.rdi = gprs[8];
	sc.r8 = gprs[7];
	sc.r9 = gprs[6];
	sc.r10 = gprs[5];
	sc.r11 = gprs[4];
	sc.r12 = gprs[3];
	sc.r13 = gprs[2];
	sc.r14 = gprs[1];
	sc.r15 = gprs[0];

	if(issig()) {
		psig(&sc);
		/* psig() rewrote the sigcontext (or never returned: do_exit) */
		f->rip = sc.rip;
		f->cs = sc.cs;
		f->rflags = sc.rflags;
		f->rsp = sc.rsp;
		f->ss = sc.ss;
		gprs[14] = sc.rax;
		gprs[13] = sc.rcx;
		gprs[12] = sc.rdx;
		gprs[11] = sc.rbx;
		gprs[10] = sc.rbp;
		gprs[9] = sc.rsi;
		gprs[8] = sc.rdi;
		gprs[7] = sc.r8;
		gprs[6] = sc.r9;
		gprs[5] = sc.r10;
		gprs[4] = sc.r11;
		gprs[3] = sc.r12;
		gprs[2] = sc.r13;
		gprs[1] = sc.r14;
		gprs[0] = sc.r15;
	}
}

/* gprs points at the saved r15 (the first of 15 pushed GPRs; the frame is above) */
void isr64_dispatch(unsigned long *gprs)
{
	struct x86_frame64 *f;

	f = (struct x86_frame64 *)((char *)gprs + (15 * 8));
	if(f->vector >= 0x30 && f->vector <= 0x3F) {
		/* MSI-X vectors: delivered by the local APIC, no 8259 EOI */
		extern void msix64_handler(unsigned long);
		msix64_handler(f->vector);
		return;
	}
	if(f->vector >= 32 && f->vector <= 47) {
		irq64_handler(f->vector);
		/* FNX: consume need_resched before iretq when the IRQ
		 * interrupted USER mode. The timer BH (irq_timer_bh via do_bh)
		 * sets need_resched when the quantum expired; without a consumer
		 * here a pure CPU-bound process is never preempted and freezes
		 * the whole system (verified: a spin child starves even a
		 * sleeping parent). Kernel-mode IRQs (timer during a syscall)
		 * defer: the process completes the syscall and the tail check
		 * below preempts at the CPL3 return. do_sched() may
		 * context-switch away; on resume we continue here and the isr
		 * epilogue iretq's back to the interrupted frame. */
		if((f->cs & 3) == 3) {
			extern int need_resched;
			extern void do_sched(void);

			/* A CPU-bound process makes no syscalls, so this IRQ
			 * return is its only chance to process pending signals
			 * (e.g. SIGKILL) - without this a killed spin loop never
			 * dies. */
			check_signals64(gprs);
			if(need_resched) {
				need_resched = 0;
				do_sched();
			}
		}
		return;
	}
	if(f->vector == 0x80) {
		syscall80_handler(gprs);
	} else if(f->vector == 14) {
		handle_page_fault(f);
	} else if((f->cs & 3) == 3) {
		/* M6: user-mode faults on the real exception vectors go through the
		 * real kernel's handlers (register dump + signal), like the 32-bit
		 * kernel. Kernel-mode faults still panic. */
		handle_user_exception(f, gprs);
	} else {
		panic(f);
	}
	/* deliver any signal queued by the handler / syscall before iretq */
	if((f->cs & 3) == 3) {
		check_signals64(gprs);
		/* FNX: consume need_resched before iretq. The timer BH sets
		 * it (quantum expired) but nothing else acts on it - cpu_idle()
		 * only runs when no process is runnable, so a CPU-bound process
		 * would never be preempted and woken children would starve on
		 * the run queue. Gated on USER mode: kernel-mode returns (e.g. a
		 * #PF handled during a syscall) never preempt mid-syscall; the
		 * IRQ path above handles preemption for interrupts that hit user
		 * mode, and this tail covers syscalls and user-mode faults.
		 * do_sched() may context-switch away; on resume we continue here
		 * and the isr epilogue iretq's back to the interrupted frame. */
		{
			extern int need_resched;
			extern void do_sched(void);

			if(need_resched) {
				need_resched = 0;
				do_sched();
			}
		}
	}
}
