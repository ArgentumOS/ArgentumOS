/*
 * fiwix/kernel64/idt64.c
 *
 * Fiwix64 M2 (phase C): IDT64 + exception handling + demand paging.
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

#include <fiwix/efi.h>
#include <fiwix/sigcontext.h>
#include "serial64.h"

#define PAGE_OFFSET64	0xFFFFFFFF80000000ULL

/* M4: compat syscall handler (defined in main64.c) */
void syscall80_handler(unsigned long *gprs);

#define DEMAND_VA	0xFFFFFFFFD0000000ULL	/* demand-paged demo region */
#define COW_VA		0xFFFFFFFFD0200000ULL	/* copy-on-write demo region */

#define IDT_GATE_INT	0x8E	/* present, DPL0, 64-bit interrupt gate */

#define P2V64(a)	(((unsigned long)(a) < 0xFFFFFFFF80000000ULL) ? \
				((unsigned long)(a) + 0xFFFFFFFF80000000ULL) : (unsigned long)(a))

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

unsigned long alloc_pages64(int);
void free_pages64(unsigned long, int);
int map_page64(unsigned long, unsigned long, unsigned long);
unsigned long virt_to_phys64(unsigned long);
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
DECL_STUB(128)

/* absolute addresses of the stubs (PE base-relocated on load) */
static unsigned long stub_addr[48] = {
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
	(unsigned long)&isr_stub_47
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
	for(n = 0; n < 48; n++) {
		/* DPL3 for the user-catchable vectors: #BP, #OF, #BR */
		dpl = (n == 3 || n == 4 || n == 17) ? 3 : 0;
		set_gate(n, stub_addr[n], cs, dpl);
	}
	/* M4: int 0x80 (compat syscall entry) as a DPL3 interrupt gate */
	set_gate(0x80, (unsigned long)&isr_stub_128, cs, 3);

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
	serial_puts("\n");
	serial_puts("!!! halting.\n");
	for(;;) {
		__asm__ __volatile__("hlt");
	}
}

/*
 * #PF: three cases, selected by the error code and CR2:
 *  - write to a read-only page (P set, W set) at COW_VA: copy-on-write -
 *    allocate a page, copy the old contents, remap RW, flush the TLB, and
 *    return so iretq retries the write (which now lands in the copy).
 *  - not-present access (P clear) at DEMAND_VA: demand-map a fresh page.
 *  - anything else: panic.
 */
static void handle_page_fault(const struct x86_frame64 *f)
{
	unsigned long cr2, paddr, new_phys, i;
	unsigned char *src, *dst;

	cr2 = get_cr2();
	if(f->error & 0x04) {
		/* M6: fault in USER mode (the INIT trampoline / an exec'd 32-bit
		 * program). Let the real kernel's do_page_fault() handle it:
		 * it walks current's vma_table and calls map_page() (which, via
		 * map_page_flags(), also maps the page in the ACTIVE shared
		 * tables under __x86_64__), then we return and the CPU retries
		 * the faulting access. */
		extern void do_page_fault(unsigned int, struct sigcontext *);
		struct sigcontext sc;
		unsigned long *lvl, cr3_now, pml4e, pdpte, pde, pte;
		extern unsigned long paging64_pml4(void);

#define PF_PML4_INDEX(a) (((a) >> 39) & 0x1FFUL)
#define PF_PDPT_INDEX(a) (((a) >> 30) & 0x1FFUL)
#define PF_PD_INDEX(a)	 (((a) >> 21) & 0x1FFUL)
#define PF_PT_INDEX(a)	 (((a) >> 12) & 0x1FFUL)
#define PF_PAGE_MASK	 0x000FFFFFFFFFF000ULL
		__asm__ __volatile__("movq %%cr3, %0" : "=r"(cr3_now));
		lvl = (unsigned long *)P2V64(paging64_pml4());
		pml4e = lvl[PF_PML4_INDEX(cr2)];
		pdpte = (pml4e & PF_PAGE_MASK) ?
			((unsigned long *)P2V64(pml4e & PF_PAGE_MASK))[PF_PDPT_INDEX(cr2)] : 0;
		pde = (pdpte & PF_PAGE_MASK) ?
			((unsigned long *)P2V64(pdpte & PF_PAGE_MASK))[PF_PD_INDEX(cr2)] : 0;
		pte = (pde & PF_PAGE_MASK) ?
			((unsigned long *)P2V64(pde & PF_PAGE_MASK))[PF_PT_INDEX(cr2)] : 0;
#undef PF_PML4_INDEX
#undef PF_PDPT_INDEX
#undef PF_PD_INDEX
#undef PF_PT_INDEX
#undef PF_PAGE_MASK

		memset_b(&sc, 0, sizeof(sc));
		sc.err = (unsigned int)f->error;
		sc.eip = (unsigned int)f->rip;
		sc.cs = (unsigned int)f->cs;
		sc.eflags = (unsigned int)f->rflags;
		sc.oldesp = (unsigned int)f->rsp;
		sc.oldss = (unsigned int)f->ss;
		do_page_fault(14, &sc);
		return;
	}
	if((f->error & 0x3) == 0x3) {		/* protection violation during write: copy-on-write */
		if((cr2 & ~0xFFFUL) != (COW_VA & ~0xFFFUL)) {
			panic(f);
		}
		new_phys = alloc_pages64(1);
		if(!new_phys) {
			panic(f);
		}
		src = (unsigned char *)cr2;		/* still readable (RO) */
		dst = (unsigned char *)P2V64(new_phys);
		for(i = 0; i < 4096; i++) {
			dst[i] = src[i];
		}
		if(map_page64(cr2, new_phys, 0x002)) {	/* X86_PTE_RW */
			free_pages64(new_phys, 1);
			panic(f);
		}
		tlb_flush64();	/* drop the stale RO TLB entry */
		serial_puts("[M2-F] copy-on-write ");
		serial_hex((UINT64)cr2);
		serial_puts(" -> new phys ");
		serial_hex((UINT64)new_phys);
		serial_puts("\n");
		return;
	}

	if((cr2 & ~0xFFFUL) != (DEMAND_VA & ~0xFFFUL)) {
		panic(f);
	}
	paddr = alloc_pages64(1);
	if(!paddr) {
		panic(f);
	}
	if(map_page64(DEMAND_VA, paddr, 0x002)) {	/* X86_PTE_RW */
		free_pages64(paddr, 1);
		panic(f);
	}
	serial_puts("[M2-C] demand-mapped faulting page ");
	serial_hex((UINT64)cr2);
	serial_puts(" -> phys ");
	serial_hex((UINT64)paddr);
	serial_puts("\n");
}

/* gprs points at the saved rax; the frame is 15 pushed GPRs above */
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
	sc.err = (unsigned int)f->error;
	sc.eip = (unsigned int)f->rip;
	sc.cs = (unsigned int)f->cs;
	sc.eflags = (unsigned int)f->rflags;
	sc.oldesp = (unsigned int)f->rsp;
	sc.oldss = (unsigned int)f->ss;
	sc.eax = (unsigned int)gprs[14];	/* rax */
	sc.ecx = (unsigned int)gprs[13];	/* rcx */
	sc.edx = (unsigned int)gprs[12];	/* rdx */
	sc.ebx = (unsigned int)gprs[11];	/* rbx */
	sc.ebp = (unsigned int)gprs[10];	/* rbp */
	sc.esi = (unsigned int)gprs[9];	/* rsi */
	sc.edi = (unsigned int)gprs[8];	/* rdi */
	sc.esp = (unsigned int)f->rsp;

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

/* gprs points at the saved rax; the frame is 15 pushed GPRs above */
/* M6-E: the 32-bit kernel checks pending signals on every return to user
 * mode (core386.S CHECK_IF_SIGNALS -> issig()/psig()). The 64-bit return
 * path does the same: build a 32-bit sigcontext from the frame, let psig()
 * rewrite it (handler trampoline or do_exit for default signals), and write
 * the result back into the iretq frame. psig() never returns on do_exit. */
static void check_signals64(unsigned long *gprs)
{
	struct x86_frame64 *f;
	struct sigcontext sc;
	extern int issig(void);
	extern void psig(struct sigcontext *);

	f = (struct x86_frame64 *)((char *)gprs + (15 * 8));
	memset_b(&sc, 0, sizeof(sc));
	sc.err = (unsigned int)f->error;
	sc.eip = (unsigned int)f->rip;
	sc.cs = (unsigned int)f->cs;
	sc.eflags = (unsigned int)f->rflags;
	sc.oldesp = (unsigned int)f->rsp;
	sc.oldss = (unsigned int)f->ss;
	sc.eax = (unsigned int)gprs[14];	/* rax */
	sc.ecx = (unsigned int)gprs[13];	/* rcx */
	sc.edx = (unsigned int)gprs[12];	/* rdx */
	sc.ebx = (unsigned int)gprs[11];	/* rbx */
	sc.ebp = (unsigned int)gprs[10];	/* rbp */
	sc.esi = (unsigned int)gprs[9];	/* rsi */
	sc.edi = (unsigned int)gprs[8];	/* rdi */
	sc.esp = (unsigned int)f->rsp;

	if(issig()) {
		psig(&sc);
		/* psig() rewrote the sigcontext (or never returned: do_exit) */
		f->rip = sc.eip;
		f->cs = sc.cs;
		f->rflags = sc.eflags;
		f->rsp = sc.oldesp;
		f->ss = sc.oldss;
		gprs[14] = sc.eax;	/* rax */
		gprs[13] = sc.ecx;	/* rcx */
		gprs[12] = sc.edx;	/* rdx */
		gprs[11] = sc.ebx;	/* rbx */
		gprs[10] = sc.ebp;	/* rbp */
		gprs[9] = sc.esi;	/* rsi */
		gprs[8] = sc.edi;	/* rdi */
	}
}

/* gprs points at the saved rax; the frame is 15 pushed GPRs above */
void isr64_dispatch(unsigned long *gprs)
{
	struct x86_frame64 *f;

	f = (struct x86_frame64 *)((char *)gprs + (15 * 8));
	if(f->vector >= 32 && f->vector <= 47) {
		irq64_handler(f->vector);
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
	}
}
