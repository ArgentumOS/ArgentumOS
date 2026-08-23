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
static void elf_create_stack(struct binargs *barg, unsigned int *sp, unsigned int str_ptr, int at_base, struct elf32_hdr *elf32_h, unsigned int phdr_addr)
{	unsigned int n, addr;
	char *str;

	/* copy strings */
	for(n = 0; n < ARG_MAX; n++) {
		if(barg->page[n]) {
			addr = PAGE_OFFSET - ((ARG_MAX - n) * PAGE_SIZE);
			memcpy_b((void *)addr, (void *)barg->page[n], PAGE_SIZE);
		}
	}

#ifdef __DEBUG__
	printk("sp = 0x%08x\n", sp);
#endif /*__DEBUG__ */

	/* copy the value of 'argc' into the stack */
	current->argc = barg->argc;
	*sp = barg->argc;
#ifdef __DEBUG__
	printk("at 0x%08x -> argc\n", sp);
#endif /*__DEBUG__ */
	sp++;

	/* copy as many pointers to strings as 'argc' */
	current->argv = (char **)sp;
	for(n = 0; n < barg->argc; n++) {
		*sp = str_ptr;
		str = (char *)str_ptr;
#ifdef __DEBUG__
		printk("at 0x%08x -> str_ptr(%d) = 0x%08x (+ %d)\n", sp, n, str_ptr, strlen(str) + 1);
#endif /*__DEBUG__ */
		sp++;
		str_ptr += strlen(str) + 1;
	}

	/* the last element of 'argv[]' must be a NULL-pointer */
	*sp = 0;
#ifdef __DEBUG__
	printk("at 0x%08x -> -------------- = 0x%08x\n", sp, 0);
#endif /*__DEBUG__ */
	sp++;

	/* copy as many pointers to strings as 'envc' */
	current->envc = barg->envc;
	current->envp = (char **)sp;
	for(n = 0; n < barg->envc; n++) {
		*sp = str_ptr;
		str = (char *)str_ptr;
#ifdef __DEBUG__
		printk("at 0x%08x -> str_ptr(%d) = 0x%08x (+ %d)\n", sp, n, str_ptr, strlen(str) + 1);
#endif /*__DEBUG__ */
		sp++;
		str_ptr += strlen(str) + 1;
	}

	/* the last element of 'envp[]' must be a NULL-pointer */
	*sp = 0;
#ifdef __DEBUG__
	printk("at 0x%08x -> -------------- = 0x%08x\n", sp, 0);
#endif /*__DEBUG__ */
	sp++;


	/* copy the Auxiliar Table Items (dlinfo_items) - always emitted:
	 * static musl/glibc binaries need AT_PHDR/AT_PHNUM/AT_PHENT for their
	 * TLS setup and AT_PAGESZ for libc.page_size; AT_BASE is 0 when there
	 * is no dynamic interpreter. */
	{
		*sp = AT_PHDR;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_PHDR = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = (unsigned int)phdr_addr;
#ifdef __DEBUG__
		printk("\t\tAT_PHDR = 0x%08x\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_PHENT;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_PHENT = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = sizeof(struct elf32_phdr);
#ifdef __DEBUG__
		printk("\t\tAT_PHENT = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_PHNUM;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_PHNUM = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = (unsigned int)elf32_h->e_phnum;
#ifdef __DEBUG__
		printk("\t\tAT_PHNUM = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_PAGESZ;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_PGSIZE = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = PAGE_SIZE;
#ifdef __DEBUG__
		printk("\t\tAT_PGSIZE = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_BASE;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_BASE = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = (unsigned int)at_base;
#ifdef __DEBUG__
		printk("\t\tAT_BASE = 0x%08x\n", sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_FLAGS;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_FLAGS = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = 0;
#ifdef __DEBUG__
		printk("\t\tAT_FLAGS = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_ENTRY;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_ENTRY = %d ", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = (unsigned int)elf32_h->e_entry;
#ifdef __DEBUG__
		printk("\t\tAT_ENTRY = 0x%08x\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_UID;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_UID = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = current->uid;
#ifdef __DEBUG__
		printk("\t\tAT_UID = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_EUID;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_EUID = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = current->euid;
#ifdef __DEBUG__
		printk("\t\tAT_EUID = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_GID;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_GID = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = current->gid;
#ifdef __DEBUG__
		printk("\t\tAT_GID = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = AT_EGID;
#ifdef __DEBUG__
		printk("at 0x%08x -> AT_EGID = %d", sp, *sp);
#endif /*__DEBUG__ */
		sp++;

		*sp = current->egid;
#ifdef __DEBUG__
		printk("\t\tAT_EGID = %d\n", *sp);
#endif /*__DEBUG__ */
		sp++;
	}

	*sp = AT_NULL;
#ifdef __DEBUG__
	printk("at 0x%08x -> AT_NULL = %d", sp, *sp);
#endif /*__DEBUG__ */
	sp++;

	*sp = 0;
#ifdef __DEBUG__
	printk("\t\tAT_NULL = %d\n", *sp);
#endif /*__DEBUG__ */

#ifdef __DEBUG__
	for(n = 0; n < barg->argc; n++) {
		printk("at 0x%08x -> argv[%d] = '%s'\n", current->argv[n], n, current->argv[n]);
	}
	for(n = 0; n < barg->envc; n++) {
		printk("at 0x%08x -> envp[%d] = '%s'\n", current->envp[n], n, current->envp[n]);
	}
#endif /*__DEBUG__ */
}

#ifdef __x86_64__
/*
 * Fiwix64 (native 64-bit port): the 64-bit initial stack builder. The
 * SysV x86-64 ABI: at process entry RSP points at argc (unsigned long),
 * followed by argv[] (NULL-terminated), envp[] (NULL-terminated) and the
 * auxv (u64 pairs, AT_NULL-terminated), 16-byte aligned. auxv values are
 * 8 bytes (not 4 like the 32-bit elf_create_stack).
 */
static void elf_create_stack64(struct binargs *barg, unsigned long long *sp,
	unsigned long long str_ptr, unsigned long long at_base,
	Elf64_Ehdr *e, unsigned long long phdr_addr)
{
	unsigned int n;
	unsigned long long addr;
	char *str;

	/* copy the strings into the reserved stack area */
	for(n = 0; n < ARG_MAX; n++) {
		if(barg->page[n]) {
			addr = PAGE_OFFSET - ((ARG_MAX - n) * PAGE_SIZE);
			memcpy_b((void *)addr, (void *)barg->page[n], PAGE_SIZE);
		}
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

static int elf_load_interpreter(struct inode *ii)
{
	int n, errno;
	struct buffer *buf;
	struct elf32_hdr *elf32_h;
	struct elf32_phdr *elf32_ph, *last_ptload;
	__blk_t block;
	unsigned int start, end, length, offset;
	unsigned int prot;
	char *data;
	char type;

	if((block = bmap(ii, 0, FOR_READING)) < 0) {
		return block;
	}
	if(!(buf = bread(ii->dev, block, ii->sb->s_blocksize))) {
		return -EIO;
	}

	/*
	 * The contents of the buffer is copied and then freed immediately to
	 * make sure that it won't conflict while zeroing the BSS fractional
	 * page, in case that the same block is requested during the page fault.
	 */
	if(!(data = (void *)kmalloc(PAGE_SIZE))) {
		brelse(buf);
		return -ENOMEM;
	}
	memcpy_b(data, buf->data, ii->sb->s_blocksize);
	brelse(buf);

	elf32_h = (struct elf32_hdr *)data;
	if(check_elf(elf32_h)) {
		kfree((addr_t)data);
		return -ELIBBAD;
	}

	last_ptload = NULL;
	for(n = 0; n < elf32_h->e_phnum; n++) {
		elf32_ph = (struct elf32_phdr *)(data + elf32_h->e_phoff + (sizeof(struct elf32_phdr) * n));
		if(elf32_ph->p_type == PT_LOAD) {
#ifdef __DEBUG__
			printk("p_offset = 0x%08x\n", elf32_ph->p_offset);
			printk("p_vaddr  = 0x%08x\n", elf32_ph->p_vaddr);
			printk("p_paddr  = 0x%08x\n", elf32_ph->p_paddr);
			printk("p_filesz = 0x%08x\n", elf32_ph->p_filesz);
			printk("p_memsz  = 0x%08x\n\n", elf32_ph->p_memsz);
#endif /*__DEBUG__ */
			start = (elf32_ph->p_vaddr & PAGE_MASK) + MMAP_START;
			length = (elf32_ph->p_vaddr & ~PAGE_MASK) + elf32_ph->p_filesz;
			offset = elf32_ph->p_offset - (elf32_ph->p_vaddr & ~PAGE_MASK);
			type = P_DATA;
			prot = 0;
			if(elf32_ph->p_flags & PF_R) {
				prot = PROT_READ;
			}
			if(elf32_ph->p_flags & PF_W) {
				prot |= PROT_WRITE;
			}
			if(elf32_ph->p_flags & PF_X) {
				prot |= PROT_EXEC;
				type = P_TEXT;
			}
			errno = do_mmap(ii, start, length, prot, MAP_PRIVATE | MAP_FIXED, offset, type, O_RDONLY, NULL);
			if(errno < 0 && errno > -PAGE_SIZE) {
				kfree((addr_t)data);
				send_sig(current, SIGSEGV);
				return -ENOEXEC;
			}
			last_ptload = elf32_ph;
		}
	}

	if(!last_ptload) {
		printk("%s(): no headers in interpreter.");
		kfree((addr_t)data);
		return -ENOEXEC;
	}

	elf32_ph = last_ptload;

	/* zero-fill the fractional page of the DATA section */
	end = PAGE_ALIGN(elf32_ph->p_vaddr + elf32_ph->p_filesz) + MMAP_START;
	start = (elf32_ph->p_vaddr + elf32_ph->p_filesz) + MMAP_START;
	length = end - start;

	/* this will generate a page fault which will load the page in */
	memset_b((void *)start, 0, length);

	/* setup the BSS section */
	start = (elf32_ph->p_vaddr + elf32_ph->p_filesz) + MMAP_START;
	start = PAGE_ALIGN(start);
	end = (elf32_ph->p_vaddr + elf32_ph->p_memsz) + MMAP_START;
	end = PAGE_ALIGN(end);
	length = end - start;
	errno = do_mmap(NULL, start, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_BSS, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		kfree((addr_t)data);
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}
	kfree((addr_t)data);
	return elf32_h->e_entry + MMAP_START;
}

int check_elf(struct elf32_hdr *elf32_h)
{
	if(elf32_h->e_ident[EI_MAG0] != ELFMAG0 ||		elf32_h->e_ident[EI_MAG1] != ELFMAG1 ||
		elf32_h->e_ident[EI_MAG2] != ELFMAG2 ||
		elf32_h->e_ident[EI_MAG3] != ELFMAG3 ||
		(elf32_h->e_type != ET_EXEC && elf32_h->e_type != ET_DYN) ||
		elf32_h->e_machine != EM_386) {
		return -EINVAL;
	}
	return 0;
}

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
	int n, errno;
	Elf64_Ehdr *e;
	Elf64_Phdr *ph, *last_ptload;
	unsigned long long start, end, length, offset;
	unsigned long long prot;
	unsigned long long load_addr = 0, phdr_addr = 0;
	unsigned long long sp, str;
	char type;
	unsigned long long ae_ptr_len, ae_str_len;

	e = (Elf64_Ehdr *)data;
	/* Fiwix64 (native port): the 32-bit check_elf() enforces EM_386 and
	 * would reject every ELF64; validate the 64-bit header here instead
	 * (EI_CLASS was already checked by elf_load() before routing here). */
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
			if((ph->p_vaddr + ph->p_memsz) > 0xFFFFFFFFULL) {
				/* the vma layer is still 32-bit (port phase B) */
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
			errno = do_mmap(i, (unsigned int)start, (unsigned int)length,
				(unsigned int)prot, MAP_PRIVATE | MAP_FIXED,
				(unsigned int)offset, type, O_RDONLY, NULL);
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
	errno = do_mmap(NULL, (unsigned int)start, (unsigned int)length,
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
	errno = do_mmap(NULL, (unsigned int)start, (unsigned int)length,
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
	errno = do_mmap(NULL, (unsigned int)(sp & PAGE_MASK), (unsigned int)length,
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

	/* set %rsp to point at 'argc' (16-byte aligned); %eip/oldesp are the
	 * 32-bit sigcontext fields and both fit (addresses < 4GB) */
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
	int n, errno;
	struct elf32_hdr *elf32_h;
	struct elf32_phdr *elf32_ph, *last_ptload;
	struct inode *ii;
	unsigned int start, end, length, offset;
	unsigned int prot;
	char *interpreter;
	int at_base, phdr_addr;
	char type;
	unsigned int ae_ptr_len, ae_str_len;
	unsigned int sp, str;
	unsigned int load_addr = 0;

	elf32_h = (struct elf32_hdr *)data;
#ifdef __x86_64__
	/* Fiwix64 (native 64-bit port): route ELF64 binaries to the native
	 * loader (64-bit headers, u64 stack/auxv, UCODE64 entry). */
	if(elf32_h->e_ident[EI_CLASS] == ELFCLASS64) {
		extern int elf_load64(struct inode *, struct binargs *, struct sigcontext *, char *);
		return elf_load64(i, barg, sc, data);
	}
#endif /* __x86_64__ */
	if(check_elf(elf32_h)) {
		if(current->pid == INIT) {
			PANIC("%s has an unrecognized binary format.\n", INIT_PROGRAM);
		}
		return -ENOEXEC;
	}

	/* check if an interpreter is required */
	interpreter = NULL;
	ii = NULL;
	phdr_addr = at_base = 0;
	for(n = 0; n < elf32_h->e_phnum; n++) {
		elf32_ph = (struct elf32_phdr *)(data + elf32_h->e_phoff + (sizeof(struct elf32_phdr) * n));
		if(elf32_ph->p_type == PT_INTERP) {
			at_base = MMAP_START;
			interpreter = data + elf32_ph->p_offset;
			if(namei(interpreter, &ii, NULL, FOLLOW_LINKS)) {
				printk("%s(): can't find interpreter '%s'.\n", __FUNCTION__, interpreter);
				send_sig(current, SIGSEGV);
				return -ELIBACC;
			}
#ifdef __DEBUG__
			printk("p_offset = 0x%08x\n", elf32_ph->p_offset);
			printk("p_vaddr  = 0x%08x\n", elf32_ph->p_vaddr);
			printk("p_paddr  = 0x%08x\n", elf32_ph->p_paddr);
			printk("p_filesz = 0x%08x\n", elf32_ph->p_filesz);
			printk("p_memsz  = 0x%08x\n", elf32_ph->p_memsz);
			printk("using interpreter '%s'\n", interpreter);
#endif /*__DEBUG__ */
		}
	}

	/*
	 * calculate the final size of 'ae_ptr_len' based on:
	 *  - argc = 4 bytes (unsigned int)
	 *  - barg.argc = (num. of pointers to strings + 1 NULL) x 4 bytes (unsigned int)
	 *  - barg.envc = (num. of pointers to strings + 1 NULL) x 4 bytes (unsigned int)
	 */
	ae_ptr_len = (1 + (barg->argc + 1) + (barg->envc + 1)) * sizeof(unsigned int);
	ae_str_len = barg->argv_len + barg->envp_len;

#ifdef __DEBUG__
	printk("argc=%d (argv_len=%d) envc=%d (envp_len=%d)  ae_ptr_len=%d ae_str_len=%d\n", barg->argc, barg->argv_len, barg->envc, barg->envp_len, ae_ptr_len, ae_str_len);
#endif /*__DEBUG__ */


	/* point of no return */

	release_binary();
	current->rss = 0;
#ifdef __x86_64__
	{
		/* Fiwix64 (M6-next): exec drops the old address space - free the
		 * process's 4-level tables and switch to a fresh copy of the
		 * kernel's low-4GB hierarchy. The kernel high half is shared and
		 * mapped in both, so the mid-execution CR3 switch is safe; the
		 * new binary's demand-maps repopulate the low half. */
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

		/* Fiwix64 (M6-H): the 2-level pgdir is the shadow that map_page()
		 * writes (and release_binary() just freed the old one's page
		 * tables - they get reused as user heap, so a stale pde would
		 * make map_page() scribble PTEs into a user page). Give the
		 * exec'd process a fresh copy of the kernel pgdir, exactly like
		 * fork does. */
		{
			extern addr_t *kpage_dir;
			unsigned int *new_pgdir;
			if((new_pgdir = (unsigned int *)kmalloc(PAGE_SIZE))) {
				memcpy_b(new_pgdir, kpage_dir, PAGE_SIZE);
				current->rss++;
				kfree(P2V(current->tss.cr3));
				current->tss.cr3 = V2P((addr_t)new_pgdir);
			}
		}
	}
#endif /* __x86_64__ */

	current->entry_address = elf32_h->e_entry;
	if(interpreter) {
		errno = elf_load_interpreter(ii);
		if(errno < 0) {
			printk("%s(): unable to load the interpreter '%s'.\n", __FUNCTION__, interpreter);
			iput(ii);
			send_sig(current, SIGKILL);
			return errno;
		}
		current->entry_address = errno;
		iput(ii);
	}

	last_ptload = NULL;
	for(n = 0; n < elf32_h->e_phnum; n++) {
		elf32_ph = (struct elf32_phdr *)(data + elf32_h->e_phoff + (sizeof(struct elf32_phdr) * n));
		if(elf32_ph->p_type == PT_PHDR) {
			phdr_addr = elf32_ph->p_vaddr;
		}
		if(elf32_ph->p_type == PT_LOAD) {
			if(!load_addr) {
				load_addr = elf32_ph->p_vaddr - elf32_ph->p_offset;
			}
			start = elf32_ph->p_vaddr & PAGE_MASK;
			length = (elf32_ph->p_vaddr & ~PAGE_MASK) + elf32_ph->p_filesz;
			offset = elf32_ph->p_offset - (elf32_ph->p_vaddr & ~PAGE_MASK);
			type = P_DATA;
			prot = 0;
			if(elf32_ph->p_flags & PF_R) {
				prot = PROT_READ;
			}
			if(elf32_ph->p_flags & PF_W) {
				prot |= PROT_WRITE;
			}
			if(elf32_ph->p_flags & PF_X) {
				prot |= PROT_EXEC;
				type = P_TEXT;
				current->end_code = start + length;
			}
			errno = do_mmap(i, start, length, prot, MAP_PRIVATE | MAP_FIXED, offset, type, O_RDONLY, NULL);
			if(errno < 0 && errno > -PAGE_SIZE) {
				send_sig(current, SIGSEGV);
				return -ENOEXEC;
			}
			last_ptload = elf32_ph;
		}
	}

	if(!phdr_addr) {
		/* static musl/glibc executables have no PT_PHDR segment; AT_PHDR
		 * must still point at the program header table (musl's
		 * static_init_tls walks it for PT_TLS). */
		phdr_addr = load_addr + elf32_h->e_phoff;
	}

	if(!last_ptload) {
		printk("%s(): no program headers.");
		send_sig(current, SIGKILL);
		return -ENOEXEC;
	}

	elf32_ph = last_ptload;

	/* zero-fill the fractional page of the DATA section */
	end = PAGE_ALIGN(elf32_ph->p_vaddr + elf32_ph->p_filesz);
	start = elf32_ph->p_vaddr + elf32_ph->p_filesz;
	length = end - start;

#ifdef __x86_64__
	/* Fiwix64: the .bss tail shares the last DATA page; demand-map it to a
	 * real RAM page first, otherwise the memset_b below writes to the
	 * supervisor 2MB identity page (physical addr beyond RAM) and the
	 * zero-fill is lost - .bss globals (e.g. musl's main_tls) stay garbage. */
	{
		extern int fiwix64_fault_user_pages(addr_t, unsigned int);

		if(fiwix64_fault_user_pages(start & PAGE_MASK, length)) {
			send_sig(current, SIGSEGV);
			return -ENOEXEC;
		}
	}
#endif /* __x86_64__ */

	/* this will generate a page fault which will load the page in */
	memset_b((void *)start, 0, length);

	/* setup the BSS section */
	start = elf32_ph->p_vaddr + elf32_ph->p_filesz;
	start = PAGE_ALIGN(start);
	end = elf32_ph->p_vaddr + elf32_ph->p_memsz;
	end = PAGE_ALIGN(end);
	length = end - start;
	errno = do_mmap(NULL, start, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_BSS, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}
	current->brk_lower = start;

	/* setup the HEAP section */
	start = elf32_ph->p_vaddr + elf32_ph->p_memsz;
	start = PAGE_ALIGN(start);
	length = PAGE_SIZE;
	errno = do_mmap(NULL, start, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_HEAP, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}
	current->brk = start;

	/* setup the STACK section */
	sp = USER_STACK_TOP - 4;	/* formerly 0xBFFFFFFC */
	sp -= ae_str_len;
	str = sp;	/* this is the address of the first string (argv[0]) */
	sp &= ~3;
	sp -= (AT_ITEMS * 2) * sizeof(unsigned int);
	sp -= ae_ptr_len;
	length = USER_STACK_TOP - (sp & PAGE_MASK);
	errno = do_mmap(NULL, sp & PAGE_MASK, length, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_FIXED, 0, P_STACK, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}

#ifdef __x86_64__
	/* Fiwix64: elf_create_stack() below writes argc/argv/envp/auxv and the
	 * argv strings to the stack pages directly. In the 64-bit build those
	 * low addresses are covered by the supervisor 2MB identity pages (the
	 * 0-4GB identity map), so the writes would land on non-RAM physical
	 * pages and be LOST - the exec'd program would read garbage argv/envp.
	 * Demand-map the stack range to real RAM first (splits the huge pages,
	 * like the fault path does). */
	{
		extern int fiwix64_fault_user_pages(addr_t, unsigned int);

		if(fiwix64_fault_user_pages(sp & PAGE_MASK, length)) {
			send_sig(current, SIGSEGV);
			return -ENOEXEC;
		}
	}
#endif /* __x86_64__ */

	elf_create_stack(barg, (unsigned int *)sp, str, at_base, elf32_h, phdr_addr);

	/* set %esp to point to 'argc' */
	sc->oldesp = sp;
	sc->eflags = 0x202;	/* FIXME: linux 2.2 = 0x292 */
	sc->eip = current->entry_address;
	sc->err = 0;
	sc->eax = 0;
	sc->ecx = 0;
	sc->edx = 0;
	sc->ebx = 0;
	sc->ebp = 0;
	sc->esi = 0;
	sc->edi = 0;
	return 0;
}
