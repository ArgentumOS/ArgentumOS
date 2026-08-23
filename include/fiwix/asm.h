/*
 * fiwix/include/fiwix/asm.h
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FIWIX_ASM_H
#define _FIWIX_ASM_H

#include <fiwix/types.h>

extern void except0(void);
extern void except1(void);
extern void except2(void);
extern void except3(void);
extern void except4(void);
extern void except5(void);
extern void except6(void);
extern void except7(void);
extern void except8(void);
extern void except9(void);
extern void except10(void);
extern void except11(void);
extern void except12(void);
extern void except13(void);
extern void except14(void);
extern void except15(void);
extern void except16(void);
extern void except17(void);
extern void except18(void);
extern void except19(void);
extern void except20(void);
extern void except21(void);
extern void except22(void);
extern void except23(void);
extern void except24(void);
extern void except25(void);
extern void except26(void);
extern void except27(void);
extern void except28(void);
extern void except29(void);
extern void except30(void);
extern void except31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);
extern void unknown_irq(void);

extern void switch_to_user_mode(void);
extern void sighandler_trampoline(void);
extern void end_sighandler_trampoline(void);
extern void syscall(void);
extern void return_from_syscall(void);
extern void do_switch(addr_t *, addr_t *, addr_t, addr_t, addr_t, unsigned short int);

int cpuid(void);
int getfpu(void);
int get_cpu_vendor_id(void);
int signature_flags(void);
int brand_str(void);
int tlbinfo(void);

unsigned char inport_b(unsigned int);
unsigned short int inport_w(unsigned int);
unsigned int inport_l(unsigned int);
void inport_sw(unsigned int, void *, unsigned int);
void inport_sl(unsigned int, void *, unsigned int);
void outport_b(unsigned int, unsigned char);
void outport_w(unsigned int, unsigned short int);
void outport_l(unsigned int, unsigned int);
void outport_sw(unsigned int, void *, unsigned int);
void outport_sl(unsigned int, void *, unsigned int);

void load_gdt(addr_t);
void load_idt(addr_t);
void activate_kpage_dir(void);
void load_tr(unsigned int);
unsigned long long int get_rdtsc(void);
void invalidate_tlb(void);

#define CLI() __asm__ __volatile__ ("cli":::"memory")
#define STI() __asm__ __volatile__ ("sti":::"memory")
#define NOP() __asm__ __volatile__ ("nop":::"memory")
#define HLT() __asm__ __volatile__ ("hlt":::"memory")

#ifndef __x86_64__
#define GET_CR2(cr2)	__asm__ __volatile__ ("movl %%cr2, %0" : "=r" (cr2));
#define GET_ESP(esp)	__asm__ __volatile__ ("movl %%esp, %0" : "=r" (esp));
#define SET_ESP(esp)	__asm__ __volatile__ ("movl %0, %%esp" :: "r" (esp));
#endif /* ! __x86_64__ */
#define GET_GS(gs)	__asm__ __volatile__ ("movl %%gs, %0" : "=r" (gs));

#ifdef __x86_64__
/*
 * 64-bit variants: CR2 and RSP are 64-bit registers in long mode; movl on
 * CR2 is not encodable, and movl on RSP cannot target a 64-bit operand.
 * The callers use addr_t variables for these.
 */
#define GET_CR2(cr2)	__asm__ __volatile__ ("movq %%cr2, %0" : "=r" (cr2));
#define GET_ESP(esp)	__asm__ __volatile__ ("movq %%rsp, %0" : "=r" (esp));
#define SET_ESP(esp)	__asm__ __volatile__ ("movq %0, %%rsp" :: "r" (esp));
#endif /* __x86_64__ */

#ifdef __x86_64__
/*
 * 64-bit variants of the flags save/restore macros. pushfl/popfl do not
 * exist in long mode; pushfq/popq operate on 64-bit operands, so the value
 * is routed through %rax and the low 32 bits are stored in the caller's
 * (unsigned int) variable. The upper 32 bits of RFLAGS are reserved and
 * pushed as zero.
 */
#define SAVE_FLAGS(flags)			\
	__asm__ __volatile__(			\
		"pushfq ; popq %%rax\n\t"	\
		"movl %%eax, %0\n\t"		\
		: "=r" (flags)			\
		: /* no input */		\
		: "rax", "memory"		\
	);

#define RESTORE_FLAGS(flags)			\
	__asm__ __volatile__(			\
		"movl %0, %%eax\n\t"		\
		"pushq %%rax ; popfq\n\t"	\
		: /* no output */		\
		: "r" (flags)			\
		: "rax", "memory"		\
	);
#else
#define SAVE_FLAGS(flags)			\
	__asm__ __volatile__(			\
		"pushfl ; popl %0\n\t"		\
		: "=r" (flags)			\
		: /* no input */		\
		: "memory"			\
	);

#define RESTORE_FLAGS(x)			\
	__asm__ __volatile__(			\
		"pushl %0 ; popfl\n\t"		\
		: /* no output */		\
		: "r" (x)			\
		: "memory"			\
	);
#endif /* __x86_64__ */

#ifdef __TINYC__
/*
 * tcc loads "r" (register) arguments automatically into registers using this order:
 * eax, ecx, edx, ebx
 * Therefore, we rearrange the arguments so they go into the correct registers.
 */
#define USER_SYSCALL(num, arg1, arg2, arg3)	\
	__asm__ __volatile__(			\
		"int    $0x80\n\t"		\
		: /* no output */		\
		: "r"((unsigned int)num), "r"((unsigned int)arg2), "r"((unsigned int)arg3), "r"((unsigned int)arg1)	\
	);
#else
#define USER_SYSCALL(num, arg1, arg2, arg3)	\
	__asm__ __volatile__(			\
		"movl   %0, %%eax\n\t"		\
		"movl   %1, %%ebx\n\t"		\
		"movl   %2, %%ecx\n\t"		\
		"movl   %3, %%edx\n\t"		\
		"int    $0x80\n\t"		\
		: /* no output */		\
		: "eax"((unsigned int)num), "ebx"((unsigned int)(addr_t)arg1), "ecx"((unsigned int)(addr_t)arg2), "edx"((unsigned int)(addr_t)arg3)	\
	);
#endif

#ifdef __x86_64__
/* Fiwix64 (no 32-bit compatibility): the native 64-bit syscall entry uses
 * the 'syscall' instruction (rax = number, rdi/rsi/rdx/r10/r8 = args),
 * NOT int 0x80. Used by the INIT bootstrap trampoline. */
#define USER_SYSCALL64(num, arg1, arg2, arg3)	\
	__asm__ __volatile__(			\
		"movq   %0, %%rax\n\t"		\
		"movq   %1, %%rdi\n\t"		\
		"movq   %2, %%rsi\n\t"		\
		"movq   %3, %%rdx\n\t"		\
		"syscall\n\t"			\
		: /* no output */		\
		: "r"((unsigned long)(num)), "r"((unsigned long)(addr_t)(arg1)), \
		  "r"((unsigned long)(addr_t)(arg2)), "r"((unsigned long)(addr_t)(arg3)) \
		: "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory");
#endif /* __x86_64__ */

/*
static inline unsigned long long int get_rdtsc(void)
{
	unsigned int eax, edx;

	__asm__ __volatile__("rdtsc" : "=a" (eax), "=d" (edx));
	return ((unsigned long long int)eax) | (((unsigned long long int)edx) << 32);
}
*/

#endif /* _FIWIX_ASM_H */
