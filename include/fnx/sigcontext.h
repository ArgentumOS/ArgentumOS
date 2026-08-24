/*
 * fnx/include/fnx/sigcontext.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX (pure x86-64 port, no 32-bit compatibility): the kernel-internal
 * register-exchange format. It mirrors the isr_common64/syscall_entry64
 * frame layout (x86_frame64: err, rip, cs, rflags, rsp, ss) followed by the
 * 15 saved GPRs in push order (gprs[0]=r15 ... gprs[14]=rax), so building a
 * sigcontext from the CPU frame and writing it back is a mechanical copy.
 */

#ifndef _FNX_SIGCONTEXT_H
#define _FNX_SIGCONTEXT_H

struct sigcontext {
	/* mirrors x86_frame64 (see kernel64/idt64.c) */
	unsigned long long err;		/* error code (or vector) */
	unsigned long long rip;
	unsigned long long cs;
	unsigned long long rflags;
	unsigned long long rsp;
	unsigned long long ss;
	/* 15 GPRs, isr_common64/syscall_entry64 push order:
	 * gprs[0]=r15 ... gprs[14]=rax */
	unsigned long long r15;
	unsigned long long r14;
	unsigned long long r13;
	unsigned long long r12;
	unsigned long long r11;
	unsigned long long r10;
	unsigned long long r9;
	unsigned long long r8;
	unsigned long long rdi;
	unsigned long long rsi;
	unsigned long long rbp;
	unsigned long long rbx;
	unsigned long long rdx;
	unsigned long long rcx;
	unsigned long long rax;
};

#endif /* _FNX_SIGCONTEXT_H */
