/*
 * fiwix/include/fiwix/sigcontext.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FIWIX_SIGCONTEXT_H
#define _FIWIX_SIGCONTEXT_H

struct sigcontext {
	unsigned int gs;
	unsigned int fs;
	unsigned int es;
	unsigned int ds;
	unsigned int edi;
	unsigned int esi;
	unsigned int ebp;
	unsigned int esp;
	int ebx;
	int edx;
	int ecx;
	int eax;
	int err;
	unsigned int eip;
	unsigned int cs;
	unsigned int eflags;
	unsigned int oldesp;
	unsigned int oldss;
#ifdef __x86_64__
	/* Fiwix64 (canonical amd64 split): the 32-bit eip/oldesp above are
	 * the i386-compat layout; a NATIVE 64-bit process's entry RIP/RSP
	 * can be anywhere in the 128TB user half, so elf_load64() stores
	 * the full 64-bit values here and the exec iretq uses these. */
	unsigned long long rip;
	unsigned long long rsp;
#endif /* __x86_64__ */
};

#endif /* _FIWIX_SIGCONTEXT_H */
