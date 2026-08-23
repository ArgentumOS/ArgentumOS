/*
 * fiwix/fs/elf.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/kernel.h>
#include <fiwix/asm.h>
#include <fiwix/types.h>
#include <fiwix/buffer.h>
#include <fiwix/fs.h>
#include <fiwix/i386elf.h>
#include <fiwix/mm.h>
#include <fiwix/mman.h>
#include <fiwix/fs.h>
#include <fiwix/fcntl.h>
#include <fiwix/process.h>
#include <fiwix/errno.h>
#include <fiwix/stdio.h>
#include <fiwix/string.h>

#define AT_ITEMS	12	/* ELF Auxiliary Vectors */

/*
 * Setup the initial process stack (UNIX System V ABI for i386)
 * ------------------------------------------------------------
 * 0xBFFFFFFF
 * 	+---------------+ \
 * 	| envp[] str    | |
 * 	+---------------+ |
 * 	| argv[] str    | |
 * 	+---------------+ |
 * 	| NULL          | |
 * 	+---------------+ |
 * 	| ELF Aux.Vect. | |
 * 	+---------------+ |
 * 	| NULL          | | elf_create_stack() setups this section
 * 	+---------------+ |
 * 	| envp[] ptr    | |
 * 	+---------------+ |
 * 	| NULL          | |
 * 	+---------------+ |
 * 	| argv[] ptr    | |
 * 	+---------------+ |
 * 	| argc          | |
 * 	+---------------+ /
 * 	| stack pointer | grows toward lower addresses
 * 	+---------------+ ||
 * 	|...............| \/
 * 	|...............|
 * 	|...............|
 * 	|...............| /\
 * 	+---------------+ ||
 * 	|  brk (heap)   | grows toward higher addresses
 * 	+---------------+
 * 	| .bss section  |
 * 	+---------------+
 * 	| .data section |
 * 	+---------------+
 * 	| .text section |
 * 	+---------------+
 * 0x08048000
 */
#ifdef __x86_64__
/*
 * Fiwix64 (native 64-bit port): the 64-bit initial stack builder. The
 * SysV x86-64 ABI: at process entry RSP points at argc (unsigned long),
 * followed by argv[] (NULL-terminated), envp[] (NULL-terminated) and the
 * auxv (u64 pairs, AT_NULL-terminated), 16-byte aligned. auxv values are
 * 8 bytes (not 4 like the old 32-bit builder).
 */
static void elf_create_stack64(struct binargs *barg, unsigned long long *sp,
	unsigned long long str_ptr, unsigned long long at_base,
	Elf64_Ehdr *e, unsigned long long phdr_addr)
{
	unsigned int n, first;
	unsigned long long addr;
	char *str;

	/* copy the strings into the reserved stack area. sys_execve laid the
	 * argv/envp strings out contiguously in barg->page[p..ARG_MAX-1]
	 * (page[p] + barg->offset holds the first string byte), and the
	 * loader reserved the string area at the TOP of the 64-bit user
	 * stack: 'str_ptr' is the address of the first string there. Map
	 * page[p] to str_ptr - barg->offset and the rest right after it.
	 * (The old 32-bit builder wrote them at PAGE_OFFSET - ..., which in
	 * the canonical 128TB-user split is unmapped kernel address space.) */
	first = ARG_MAX;
	for(n = 0; n < ARG_MAX; n++) {
		if(barg->page[n]) {
			first = n;
			break;
		}
	}
	addr = str_ptr - (unsigned long long)barg->offset;
	for(n = first; n < ARG_MAX; n++) {
		if(!barg->page[n]) {
			break;	/* defensive: execve.c always fills p..ARG_MAX-1 */
		}
		memcpy_b((void *)addr, (void *)barg->page[n], PAGE_SIZE);
		addr += PAGE_SIZE;
	}

	current->argc = barg->argc;
	*sp = barg->argc;
	sp++;

	current->argv = (char **)sp;
	for(n = 0; n < barg->argc; n++) {
		*sp = str_ptr;
		str = (char *)str_ptr;
		sp++;
		str_ptr += strlen(str) + 1;
	}
	*sp = 0;
	sp++;

	current->envc = barg->envc;
	current->envp = (char **)sp;
	for(n = 0; n < barg->envc; n++) {
		*sp = str_ptr;
		str = (char *)str_ptr;
		sp++;
		str_ptr += strlen(str) + 1;
	}
	*sp = 0;
	sp++;

	/* the Auxiliary Vector Table (u64 pairs), AT_NULL-terminated. The
	 * 32-bit table's AT_ITEMS order is kept, plus AT_RANDOM (musl's
	 * __init_libc reads it for the pointer guard seed). The 16 seed
	 * bytes are reserved at the top of the auxv region (they end up just
	 * below envp[]'s NULL, above the AT_PHDR entry). */
	{
		char *rnd = (char *)sp;
		int k;
		for(k = 0; k < 16; k++) {
			rnd[k] = (char)(0xA5 + (k * 7));	/* deterministic "random" */
		}
		sp += 2;	/* 16 bytes = 2 u64 slots */
		*sp++ = AT_PHDR;	*sp++ = phdr_addr;
		*sp++ = AT_PHENT;	*sp++ = sizeof(Elf64_Phdr);
		*sp++ = AT_PHNUM;	*sp++ = e->e_phnum;
		*sp++ = AT_PAGESZ;	*sp++ = PAGE_SIZE;
		*sp++ = AT_BASE;	*sp++ = at_base;
		*sp++ = AT_FLAGS;	*sp++ = 0;
		*sp++ = AT_ENTRY;	*sp++ = e->e_entry;
		*sp++ = AT_UID;		*sp++ = current->uid;
		*sp++ = AT_EUID;	*sp++ = current->euid;
		*sp++ = AT_GID;		*sp++ = current->gid;
		*sp++ = AT_EGID;	*sp++ = current->egid;
		*sp++ = AT_RANDOM;	*sp++ = (unsigned long long)rnd;
		*sp++ = AT_NULL;	*sp++ = 0;
	}
}
#endif /* __x86_64__ */

#ifdef __x86_64__
/*
 * Fiwix64 (native 64-bit port): ELF64 loader. The x86_64 musl static
 * binaries are ET_EXEC non-PIE at 0x400000, so all load/stack addresses
 * stay below 4GB and the existing (32-bit) vma layer + the 32-bit
 * sigcontext fields still work; the differences are the 64-bit program
 * headers, the 64-bit initial stack (u64 argv/envp/auxv, 16-byte aligned)
 * and the 64-bit user-mode entry (UCODE64, selected via PF_ELF64).
 */
int elf_load64(struct inode *i, struct binargs *barg, struct sigcontext *sc, char *data)
{
	int n;
	long errno;
	Elf64_Ehdr *e;
	Elf64_Phdr *ph, *last_ptload;
	unsigned long long start, end, length, offset;
	unsigned long long prot;
	unsigned long long load_addr = 0, phdr_addr = 0;
	unsigned long long sp, str;
	char type;
	unsigned long long ae_ptr_len, ae_str_len;

	e = (Elf64_Ehdr *)data;
	/* Fiwix64: native 64-bit ELF validation (EI_CLASS was already
	 * checked by elf_load() before routing here). */
	if(e->e_ident[EI_MAG0] != ELFMAG0 || e->e_ident[EI_MAG1] != ELFMAG1 ||
		e->e_ident[EI_MAG2] != ELFMAG2 || e->e_ident[EI_MAG3] != ELFMAG3 ||
		(e->e_type != ET_EXEC && e->e_type != ET_DYN) ||
		e->e_machine != EM_X86_64) {
		return -ENOEXEC;
	}

	/* pointer area: argc + argv[] + NULL + envp[] + NULL + auxv, all u64 */
	ae_ptr_len = (1 + (barg->argc + 1) + (barg->envc + 1)) * sizeof(unsigned long long);
	ae_str_len = barg->argv_len + barg->envp_len;

	/* point of no return */
	release_binary();
	current->rss = 0;
	{
		/* exec drops the old address space: free the process's 4-level
		 * tables and switch to a fresh copy of the kernel pml4 (the
		 * high half is shared, so the mid-execution CR3 switch is safe) */
		extern unsigned long create_pml4_64(unsigned long);
		extern void free_pml4_64(unsigned long);
		extern unsigned long paging64_pml4_phys(void);
		unsigned long new_pml4 = create_pml4_64(paging64_pml4_phys());
		if(!new_pml4) {
			PANIC("exec: unable to allocate the new page tables.\n");
		}
		if(current->cr3_64 && current->cr3_64 != paging64_pml4_phys()) {
			free_pml4_64(current->cr3_64);
		}
		current->cr3_64 = new_pml4;
		__asm__ __volatile__("movq %0, %%cr3" :: "r"(new_pml4) : "memory");
	}

	current->entry_address = e->e_entry;

	last_ptload = NULL;
	for(n = 0; n < e->e_phnum; n++) {
		ph = (Elf64_Phdr *)(data + e->e_phoff + (sizeof(Elf64_Phdr) * n));
		if(ph->p_type == PT_PHDR) {
			phdr_addr = ph->p_vaddr;
		}
		if(ph->p_type == PT_LOAD) {
			if(!load_addr) {
				load_addr = ph->p_vaddr - ph->p_offset;
			}
			if((ph->p_vaddr + ph->p_memsz) > 0x00007FFFFFFFFFFFULL) {
				/* above the canonical user half (128TB) */
				send_sig(current, SIGSEGV);
				return -ENOEXEC;
			}
			start = ph->p_vaddr & PAGE_MASK;
			length = (ph->p_vaddr & ~PAGE_MASK) + ph->p_filesz;
			offset = ph->p_offset - (ph->p_vaddr & ~PAGE_MASK);
			type = P_DATA;
			prot = 0;
			if(ph->p_flags & PF_R) {
				prot = PROT_READ;
			}
			if(ph->p_flags & PF_W) {
				prot |= PROT_WRITE;
			}
			if(ph->p_flags & PF_X) {
				prot |= PROT_EXEC;
				type = P_TEXT;
				current->end_code = (addr_t)(start + length);
			}
			errno = do_mmap(i, (addr_t)start, (addr_t)length,
				(unsigned int)prot, MAP_PRIVATE | MAP_FIXED,
				(addr_t)offset, type, O_RDONLY, NULL);
			if(errno < 0 && errno > -PAGE_SIZE) {
				send_sig(current, SIGSEGV);
				return -ENOEXEC;
			}
			last_ptload = ph;
		}
	}

	if(!phdr_addr) {
		/* static musl has no PT_PHDR; AT_PHDR must still point at the
		 * program header table (musl's static_init_tls walks it) */
		phdr_addr = load_addr + e->e_phoff;
	}

	if(!last_ptload) {
		printk("%s(): no program headers.\n", __FUNCTION__);
		send_sig(current, SIGKILL);
		return -ENOEXEC;
	}
	ph = last_ptload;

	/* zero-fill the fractional page of the DATA section */
	end = PAGE_ALIGN(ph->p_vaddr + ph->p_filesz);
	start = ph->p_vaddr + ph->p_filesz;
	length = end - start;
	{
		extern int fiwix64_fault_user_pages(addr_t, unsigned int);

		if(fiwix64_fault_user_pages((addr_t)(start & PAGE_MASK), (unsigned int)length)) {
			send_sig(current, SIGSEGV);
			return -ENOEXEC;
		}
	}
	memset_b((void *)(addr_t)start, 0, (__size_t)length);

	/* setup the BSS section */
	start = ph->p_vaddr + ph->p_filesz;
	start = PAGE_ALIGN(start);
	end = ph->p_vaddr + ph->p_memsz;
	end = PAGE_ALIGN(end);
	length = end - start;
	errno = do_mmap(NULL, (addr_t)start, (addr_t)length,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_BSS, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}
	current->brk_lower = (addr_t)start;

	/* setup the HEAP section */
	start = ph->p_vaddr + ph->p_memsz;
	start = PAGE_ALIGN(start);
	length = PAGE_SIZE;
	errno = do_mmap(NULL, (addr_t)start, (addr_t)length,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_HEAP, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}
	current->brk = (addr_t)start;

	/* setup the STACK section (16-byte aligned, u64 slot layout) */
	sp = USER_STACK_TOP - 4;
	sp -= ae_str_len;
	str = sp;	/* this is the address of the first string (argv[0]) */
	sp &= ~15ULL;
	sp -= (2 + 2) * sizeof(unsigned long long);	/* AT_RANDOM seed (16B) */
	sp -= (AT_ITEMS + 1) * 2 * sizeof(unsigned long long);	/* auxv pairs + AT_NULL */
	sp -= ae_ptr_len;
	length = USER_STACK_TOP - (sp & PAGE_MASK);
	errno = do_mmap(NULL, (addr_t)(sp & PAGE_MASK), (addr_t)length,
		PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_FIXED, 0, P_STACK, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}
	{
		extern int fiwix64_fault_user_pages(addr_t, unsigned int);

		if(fiwix64_fault_user_pages((addr_t)(sp & PAGE_MASK), (unsigned int)length)) {
			send_sig(current, SIGSEGV);
			return -ENOEXEC;
		}
	}

	elf_create_stack64(barg, (unsigned long long *)sp, str, 0, e, phdr_addr);

	/* set %rsp to point at 'argc' (16-byte aligned). Native 64-bit
	 * processes store the full entry RIP/RSP in the 64-bit sigcontext
	 * fields (the address can be anywhere in the 128TB user half); the
	 * 32-bit eip/oldesp are left for the i386-compat layout. */
	sc->rip = current->entry_address;
	sc->rsp = sp;
	sc->oldesp = (unsigned int)sp;
	sc->eflags = 0x202;
	sc->eip = (unsigned int)current->entry_address;
	sc->err = 0;
	current->flags |= PF_ELF64;

	return 0;
}
#endif /* __x86_64__ */

int elf_load(struct inode *i, struct binargs *barg, struct sigcontext *sc, char *data)
{
	/* Fiwix64: NO 32-bit compatibility - this kernel is native x86_64
	 * only. The only supported user binary format is ELF64
	 * (EI_CLASS = ELFCLASS64, e_machine = EM_X86_64); ELF32 (and any
	 * other class) is rejected outright. */
	extern int elf_load64(struct inode *, struct binargs *, struct sigcontext *, char *);

	if(((struct elf32_hdr *)data)->e_ident[EI_CLASS] != ELFCLASS64) {
		return -ENOEXEC;
	}
	return elf_load64(i, barg, sc, data);
}
