/*
 * fnx/fs/elf.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/asm.h>
#include <fnx/types.h>
#include <fnx/buffer.h>
#include <fnx/fs.h>
#include <fnx/i386elf.h>
#include <fnx/mm.h>
#include <fnx/mman.h>
#include <fnx/fs.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/process.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

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
 * FNX (native 64-bit port): the 64-bit initial stack builder. The
 * SysV x86-64 ABI: at process entry RSP points at argc (unsigned long),
 * followed by argv[] (NULL-terminated), envp[] (NULL-terminated) and the
 * auxv (u64 pairs, AT_NULL-terminated), 16-byte aligned. auxv values are
 * 8 bytes (not 4 like the old 32-bit builder).
 */
static void elf_create_stack64(struct binargs *barg, unsigned long long *sp,
	unsigned long long str_ptr, unsigned long long at_base,
	Elf64_Ehdr *e, unsigned long long phdr_addr, __u32 euid, __u32 egid)
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
		/* AT_EUID/AT_EGID describe the process as it will look AFTER this
		 * exec commits (do_execve applies S_ISUID/S_ISGID only after
		 * elf_load returns). musl derives libc.secure from the auxv id
		 * mismatch, and a setuid-root DYNAMIC binary whose auxv still
		 * said euid==uid would honor an attacker's LD_PRELOAD inside a
		 * root process. */
		*sp++ = AT_EUID;	*sp++ = euid;
		*sp++ = AT_GID;		*sp++ = current->gid;
		*sp++ = AT_EGID;	*sp++ = egid;
		*sp++ = AT_RANDOM;	*sp++ = (unsigned long long)rnd;
		*sp++ = AT_NULL;	*sp++ = 0;
	}
}
#endif /* __x86_64__ */

#ifdef __x86_64__
/*
 * FNX (native 64-bit port): ELF64 loader.
 *
 * Static executables are ET_EXEC non-PIE at 0x400000 (everything below
 * 4GB). Dynamic executables (docs/design/shared-libraries-plan.md) are also
 * ET_EXEC non-PIE: when the main image carries a PT_INTERP the kernel
 * additionally maps the interpreter (ld-musl-x86_64.so.1, an ET_DYN
 * shared object) at the fixed base ELF_INTERP_BASE and enters the process
 * there, letting musl's ld.so self-relocate and load the dependencies.
 * auxv keeps the SysV contract: AT_PHDR/AT_PHENT/AT_PHNUM/AT_ENTRY
 * describe the MAIN image (the loader derives the main program's load
 * address and entry from them), AT_BASE is the interpreter's load base.
 */

/* fixed load base for the dynamic linker. The mmap() region starts at
 * MMAP_START (64TB) and the initial stack grows down from USER_STACK_TOP
 * (128TB), so this slot collides with neither. Fixed (no ASLR) per the
 * plan §2.3; randomization is a later, additive milestone. */
#define ELF_INTERP_BASE	0x00007f0000000000ULL

/* map every PT_LOAD of an image whose header lives in 'data' at
 * base + p_vaddr (base is 0 for an ET_EXEC main image, whose p_vaddrs are
 * absolute). Same overflow / file-range validation and do_mmap calls the
 * static loader used for the main image. Returns 0, or -ENOEXEC (a
 * SIGSEGV has been sent for user-half violations). On success
 * *last_ptload points at the image's last PT_LOAD. */
static int elf_map_loads64(struct inode *i, char *data, unsigned long long base,
	int is_main, Elf64_Phdr **last_ptload)
{
	int n;
	long errno;
	Elf64_Ehdr *e;
	Elf64_Phdr *ph;
	unsigned long long start, length, offset, vaddr;
	unsigned long long prot;
	char type;

	e = (Elf64_Ehdr *)data;
	*last_ptload = NULL;
	for(n = 0; n < e->e_phnum; n++) {
		ph = (Elf64_Phdr *)(data + e->e_phoff + (sizeof(Elf64_Phdr) * n));
		if(ph->p_type != PT_LOAD) {
			continue;
		}
		vaddr = base + ph->p_vaddr;
		/* overflow-safe user-half bound: base + p_vaddr + p_memsz
		 * must not wrap past the canonical 128TB user boundary */
		if(vaddr > 0x00007FFFFFFFFFFFULL ||
		   ph->p_memsz > 0x00007FFFFFFFFFFFULL - vaddr) {
			/* above the canonical user half (128TB) */
			send_sig(current, SIGSEGV);
			return -ENOEXEC;
		}
		/* the file-backed part of the segment must lie inside
		 * the file: p_offset may not underflow the page offset
		 * and p_offset+p_filesz may not run past EOF */
		if(ph->p_offset < (ph->p_vaddr & ~PAGE_MASK) ||
		   ph->p_offset > i->i_size ||
		   ph->p_filesz > i->i_size - ph->p_offset) {
			return -ENOEXEC;
		}
		start = vaddr & PAGE_MASK;
		length = (vaddr & ~PAGE_MASK) + ph->p_filesz;
		offset = ph->p_offset - (vaddr & ~PAGE_MASK);
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
			if(is_main) {
				current->end_code = (addr_t)(start + length);
			}
		}
		errno = do_mmap(i, (addr_t)start, (addr_t)length,
			(unsigned int)prot, MAP_PRIVATE | MAP_FIXED,
			(addr_t)offset, type, O_RDONLY, NULL);
		if(errno < 0 && errno > -PAGE_SIZE) {
			send_sig(current, SIGSEGV);
			return -ENOEXEC;
		}
		*last_ptload = ph;
	}
	return 0;
}

/* zero-fill the DATA tail of the image's last PT_LOAD: the fractional
 * last page of the file mapping, then a fresh anonymous BSS vma covering
 * the pages between the file-backed part and p_memsz. The dynamic
 * linker's own RW segment carries .bss (libc.so), and its self-
 * relocation in ldso/dlstart.c writes there before libc init has run. */
static int elf_zero_tail64(Elf64_Phdr *ph, unsigned long long base)
{
	int errno;
	unsigned long long start, end, length;
	extern int fnx_fault_user_pages(addr_t, unsigned int);

	/* only a writable segment can carry a zero-filled data tail; the
	 * kernel-mode memset below would fault on an RX/RO mapping */
	if(!(ph->p_flags & PF_W)) {
		return 0;
	}

	/* zero-fill the fractional page of the DATA section */
	end = PAGE_ALIGN(base + ph->p_vaddr + ph->p_filesz);
	start = base + ph->p_vaddr + ph->p_filesz;
	length = end - start;
	if(length) {
		if(fnx_fault_user_pages((addr_t)(start & PAGE_MASK), (unsigned int)length)) {
			send_sig(current, SIGSEGV);
			return -ENOEXEC;
		}
		memset_b((void *)(addr_t)start, 0, (__size_t)length);
	}

	/* setup the BSS section */
	start = base + ph->p_vaddr + ph->p_filesz;
	start = PAGE_ALIGN(start);
	end = base + ph->p_vaddr + ph->p_memsz;
	end = PAGE_ALIGN(end);
	length = end - start;
	if(!length) {
		return 0;
	}
	errno = do_mmap(NULL, (addr_t)start, (addr_t)length,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_BSS, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		return -ENOEXEC;
	}
	return 0;
}

/* read the first block of a file into a fresh kernel page. Mirrors
 * do_execve()'s read of the main image: copy + brelse immediately so the
 * buffer cannot conflict when the same block is faulted in later. */
static int elf_read_first_block(struct inode *i, char **out)
{
	__blk_t block;
	struct buffer *buf;
	char *data;

	*out = NULL;
	if(!(data = (void *)kmalloc(PAGE_SIZE))) {
		return -ENOMEM;
	}
	if((block = bmap(i, 0, FOR_READING)) < 0) {
		kfree((addr_t)data);
		return block;
	}
	if(!(buf = bread(i->dev, block, i->sb->s_blocksize))) {
		kfree((addr_t)data);
		return -EIO;
	}
	memcpy_b(data, buf->data, i->sb->s_blocksize);
	brelse(buf);
	*out = data;
	return 0;
}

int elf_load64(struct inode *i, struct binargs *barg, struct sigcontext *sc, char *data)
{
	int n, has_interp;
	long errno;
	Elf64_Ehdr *e, *ie;
	Elf64_Phdr *ph, *last_ptload, *ilast;
	unsigned long long start, end, length;
	unsigned long long load_addr = 0, phdr_addr = 0;
	unsigned long long sp, str, at_base;
	__u32 euid, egid;
	char interp_path[PATH_MAX + 1];
	struct inode *ii;
	char *idata;
	unsigned long long ae_ptr_len, ae_str_len;

	e = (Elf64_Ehdr *)data;
	/* FNX: native 64-bit ELF validation (EI_CLASS was already
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

	/* the program-header table lives in the first block, copied into a
	 * kmalloc(PAGE_SIZE) buffer: bound e_phoff/e_phnum or the phdr reads
	 * run past the buffer (OOB kernel heap read feeding attacker-
	 * controlled p_* values into do_mmap) */
	if(e->e_phnum > 65536 ||
	   e->e_phoff > PAGE_SIZE ||
	   e->e_phnum * sizeof(Elf64_Phdr) > PAGE_SIZE - e->e_phoff) {
		return -ENOEXEC;
	}

	/* pre-scan (still before the point of no return) for the auxv
	 * AT_PHDR source and, for dynamic executables, the PT_INTERP path */
	has_interp = 0;
	ii = NULL;
	idata = NULL;
	for(n = 0; n < e->e_phnum; n++) {
		ph = (Elf64_Phdr *)(data + e->e_phoff + (sizeof(Elf64_Phdr) * n));
		if(ph->p_type == PT_PHDR) {
			phdr_addr = ph->p_vaddr;
		} else if(ph->p_type == PT_INTERP) {
			/* p_filesz <= PATH_MAX, so PAGE_SIZE - p_filesz cannot
			 * underflow; the reordered comparison avoids the u64
			 * wrap of p_offset + p_filesz */
			if(ph->p_filesz < 2 || ph->p_filesz > PATH_MAX ||
			   ph->p_offset > PAGE_SIZE - ph->p_filesz) {
				return -ENOEXEC;
			}
			memcpy_b(interp_path, data + ph->p_offset, (__size_t)ph->p_filesz);
			interp_path[ph->p_filesz - 1] = 0;	/* NUL must fit in filesz */
			if(interp_path[0] != '/') {
				/* no PATH search for ld.so: absolute only */
				return -ENOEXEC;
			}
			has_interp = 1;
		} else if(ph->p_type == PT_LOAD) {
			if(!load_addr) {
				load_addr = ph->p_vaddr - ph->p_offset;
			}
		}
	}
	if(!phdr_addr) {
		/* static musl has no PT_PHDR; AT_PHDR must still point at the
		 * program header table (musl's static_init_tls walks it) */
		phdr_addr = load_addr + e->e_phoff;
	}

	/* resolve + validate the interpreter while the old address space is
	 * still intact, so a missing/corrupt ld.so is a clean exec error
	 * (-ENOENT/-EACCES/-ENOEXEC) instead of a dead process */
	if(has_interp) {
		if((errno = namei(interp_path, &ii, NULL, FOLLOW_LINKS)) < 0) {
			return errno;
		}
		if(!S_ISREG(ii->i_mode)) {
			errno = -EACCES;
			goto err_interp;
		}
		if(check_permission(TO_EXEC, ii) < 0) {
			errno = -EACCES;
			goto err_interp;
		}
		if((errno = elf_read_first_block(ii, &idata)) < 0) {
			goto err_interp;
		}
		ie = (Elf64_Ehdr *)idata;
		if(ie->e_ident[EI_MAG0] != ELFMAG0 || ie->e_ident[EI_MAG1] != ELFMAG1 ||
			ie->e_ident[EI_MAG2] != ELFMAG2 || ie->e_ident[EI_MAG3] != ELFMAG3 ||
			ie->e_ident[EI_CLASS] != ELFCLASS64 ||
			ie->e_type != ET_DYN || ie->e_machine != EM_X86_64) {
			errno = -ENOEXEC;
			goto err_interp;
		}
		if(ie->e_phnum > 65536 ||
		   ie->e_phoff > PAGE_SIZE ||
		   ie->e_phnum * sizeof(Elf64_Phdr) > PAGE_SIZE - ie->e_phoff) {
			errno = -ENOEXEC;
			goto err_interp;
		}
	}

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

	if((errno = elf_map_loads64(i, data, 0, 1, &last_ptload)) < 0) {
		goto err_interp;
	}
	if(has_interp) {
		if((errno = elf_map_loads64(ii, idata, ELF_INTERP_BASE, 0, &ilast)) < 0) {
			goto err_interp;
		}
		if(!ilast) {
			/* a valid ET_DYN with no PT_LOAD at all */
			errno = -ENOEXEC;
			goto err_interp;
		}
		if((errno = elf_zero_tail64(ilast, ELF_INTERP_BASE)) < 0) {
			goto err_interp;
		}
	}

	if(!last_ptload) {
		printk("%s(): no program headers.\n", __FUNCTION__);
		send_sig(current, SIGKILL);
		errno = -ENOEXEC;
		goto err_interp;
	}
	ph = last_ptload;

	/* zero-fill the fractional page of the DATA section + setup the BSS
	 * section. Both only apply to a writable last segment (the kernel-
	 * mode memset would fault on an RX/RO mapping). */
	if(ph->p_flags & PF_W) {
		end = PAGE_ALIGN(ph->p_vaddr + ph->p_filesz);
		start = ph->p_vaddr + ph->p_filesz;
		length = end - start;
		{
			extern int fnx_fault_user_pages(addr_t, unsigned int);

			if(fnx_fault_user_pages((addr_t)(start & PAGE_MASK), (unsigned int)length)) {
				send_sig(current, SIGSEGV);
				errno = -ENOEXEC;
				goto err_interp;
			}
		}
		memset_b((void *)(addr_t)start, 0, (__size_t)length);

		start = ph->p_vaddr + ph->p_filesz;
		start = PAGE_ALIGN(start);
		end = ph->p_vaddr + ph->p_memsz;
		end = PAGE_ALIGN(end);
		length = end - start;
		errno = do_mmap(NULL, (addr_t)start, (addr_t)length,
			PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_BSS, 0, NULL);
		if(errno < 0 && errno > -PAGE_SIZE) {
			send_sig(current, SIGSEGV);
			errno = -ENOEXEC;
			goto err_interp;
		}
		current->brk_lower = (addr_t)start;
	}

	/* setup the HEAP section */
	start = ph->p_vaddr + ph->p_memsz;
	start = PAGE_ALIGN(start);
	length = PAGE_SIZE;
	errno = do_mmap(NULL, (addr_t)start, (addr_t)length,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, P_HEAP, 0, NULL);
	if(errno < 0 && errno > -PAGE_SIZE) {
		send_sig(current, SIGSEGV);
		errno = -ENOEXEC;
		goto err_interp;
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
		errno = -ENOEXEC;
		goto err_interp;
	}
	{
		extern int fnx_fault_user_pages(addr_t, unsigned int);

		if(fnx_fault_user_pages((addr_t)(sp & PAGE_MASK), (unsigned int)length)) {
			send_sig(current, SIGSEGV);
			errno = -ENOEXEC;
			goto err_interp;
		}
	}

	at_base = has_interp ? ELF_INTERP_BASE : 0;
	/* the auxv must describe the post-exec credentials: do_execve applies
	 * S_ISUID/S_ISGID only after elf_load returns, but the loader consults
	 * AT_EUID/AT_EGID at startup (see elf_create_stack64). */
	euid = current->euid;
	egid = current->egid;
	if(i->i_mode & S_ISUID) {
		euid = i->i_uid;
	}
	if(i->i_mode & S_ISGID) {
		egid = i->i_gid;
	}
	elf_create_stack64(barg, (unsigned long long *)sp, str, at_base, e, phdr_addr, euid, egid);

	/* set %rsp to point at 'argc' (16-byte aligned). Native 64-bit
	 * processes store the full entry RIP/RSP in the 64-bit sigcontext
	 * fields (the address can be anywhere in the 128TB user half); the
	 * 32-bit eip/oldesp are left for the i386-compat layout. A dynamic
	 * executable enters at the interpreter's entry (loaded at
	 * ELF_INTERP_BASE), not at the main image's e_entry (that one goes
	 * to the loader via auxv AT_ENTRY). */
	sc->rip = has_interp ? (ELF_INTERP_BASE + ie->e_entry) : (unsigned long long)e->e_entry;
	sc->rsp = sp;
	sc->rflags = 0x202;
	sc->err = 0;
	current->flags |= PF_ELF64;

	if(ii) {
		iput(ii);
	}
	if(idata) {
		kfree((addr_t)idata);
	}
	return 0;

err_interp:
	if(ii) {
		iput(ii);
	}
	if(idata) {
		kfree((addr_t)idata);
	}
	return errno;
}
#endif /* __x86_64__ */

int elf_load(struct inode *i, struct binargs *barg, struct sigcontext *sc, char *data)
{
	/* FNX: NO 32-bit compatibility - this kernel is native x86_64
	 * only. The only supported user binary format is ELF64
	 * (EI_CLASS = ELFCLASS64, e_machine = EM_X86_64); ELF32 (and any
	 * other class) is rejected outright. */
	extern int elf_load64(struct inode *, struct binargs *, struct sigcontext *, char *);

	if(((struct elf32_hdr *)data)->e_ident[EI_CLASS] != ELFCLASS64) {
		return -ENOEXEC;
	}
	return elf_load64(i, barg, sc, data);
}

/*
 * elf_world_check() - the boot-time NEEDED-closure probe
 * (docs/design/shared-libraries-plan.md §3): before the kernel execs the
 * dynamic init it verifies the dynamic world is actually bootable.
 * 'path' (the dynamic init) must be a valid x86-64 ELF whose PT_INTERP
 * resolves to an existing ELF and whose DT_NEEDED libraries all resolve
 * under /System/Libraries. Any failure (missing/corrupt file, malformed
 * ELF) prints the reason and returns 0 so the caller boots the STATIC
 * recovery shell instead of letting PID 1 die with a cascade of exec
 * failures. Reads are boot-time-only, so a modest kmalloc budget is
 * fine.
 *
 * Returns 1 (world bootable) or 0 (broken -> recovery shell).
 */
struct elf64_dyn_rec {
	__s64 d_tag;
	union {
		__s64 d_val;
		void *d_ptr;
	} d_un;
};

static int elf_check_file(const char *path, int must_be_dyn)
{
	struct inode *i;
	char *data;
	Elf64_Ehdr *e;
	int errno;

	if((errno = namei((char *)path, &i, NULL, FOLLOW_LINKS)) < 0) {
		printk("kernel: world check: missing '%s'.\n", path);
		return 0;
	}
	if(!S_ISREG(i->i_mode)) {
		printk("kernel: world check: '%s' is not a regular file.\n", path);
		iput(i);
		return 0;
	}
	if((errno = elf_read_first_block(i, &data)) < 0) {
		printk("kernel: world check: cannot read '%s' (errno %d).\n", path, errno);
		iput(i);
		return 0;
	}
	e = (Elf64_Ehdr *)data;
	if(e->e_ident[EI_MAG0] != ELFMAG0 || e->e_ident[EI_MAG1] != ELFMAG1 ||
	   e->e_ident[EI_MAG2] != ELFMAG2 || e->e_ident[EI_MAG3] != ELFMAG3 ||
	   e->e_ident[EI_CLASS] != ELFCLASS64 || e->e_machine != EM_X86_64 ||
	   (must_be_dyn && e->e_type != ET_DYN)) {
		printk("kernel: world check: '%s' is not a valid x86-64 %s.\n",
		       path, must_be_dyn ? "ET_DYN" : "ELF");
		kfree((addr_t)data);
		iput(i);
		return 0;
	}
	kfree((addr_t)data);
	iput(i);
	return 1;
}

/* read 'len' bytes at file offset 'off' into 'buf' (block-at-a-time) */
static int elf_read_bytes(struct inode *i, __off_t off, void *buf, __size_t len)
{
	__blk_t block;
	struct buffer *b;
	int blksize = i->sb->s_blocksize;
	__size_t got = 0, chunk;
	__off_t boff;

	while(got < len) {
		boff = (off + got) % blksize;
		/* bmap() takes a BYTE offset (it converts internally), not a
		 * block index - every other caller passes bytes. */
		if((block = bmap(i, off + got, FOR_READING)) < 0) {
			return -EIO;
		}
		if(!(b = bread(i->dev, block, blksize))) {
			return -EIO;
		}
		chunk = MIN((__size_t)(blksize - boff), len - got);
		memcpy_b((char *)buf + got, (char *)b->data + boff, chunk);
		brelse(b);
		got += chunk;
	}
	return 0;
}

int elf_world_check(const char *path)
{
	Elf64_Ehdr *e;
	Elf64_Phdr *ph;
	struct elf64_dyn_rec *dyn;
	struct inode *i;
	char *data, *dynseg, *strtab, *name;
	unsigned long long load_delta = 0, strtab_off = 0;
	unsigned long long strsz = 0;
	__off_t dyn_off = 0;
	__size_t dyn_size = 0;
	char libname[PATH_MAX + 1];
	int n, bad = 0, need_count = 0, has_interp = 0;
	int errno;

	data = NULL;
	if((errno = namei((char *)path, &i, NULL, FOLLOW_LINKS)) < 0 ||
	   !S_ISREG(i->i_mode)) {
		printk("kernel: world check: cannot resolve '%s'.\n", path);
		if(errno >= 0) {
			iput(i);
		}
		return 0;
	}
	if((errno = elf_read_first_block(i, &data)) < 0) {
		iput(i);
		return 0;
	}
	e = (Elf64_Ehdr *)data;
	if(e->e_ident[EI_MAG0] != ELFMAG0 || e->e_ident[EI_MAG1] != ELFMAG1 ||
	   e->e_ident[EI_MAG2] != ELFMAG2 || e->e_ident[EI_MAG3] != ELFMAG3 ||
	   e->e_ident[EI_CLASS] != ELFCLASS64 || e->e_machine != EM_X86_64) {
		printk("kernel: world check: '%s' is not a valid x86-64 ELF.\n", path);
		bad = 1;
		goto out;
	}
	if(e->e_phnum > 65536 || e->e_phoff > PAGE_SIZE ||
	   e->e_phnum * sizeof(Elf64_Phdr) > PAGE_SIZE - e->e_phoff) {
		printk("kernel: world check: '%s' has an invalid program-header table.\n", path);
		bad = 1;
		goto out;
	}

	for(n = 0; n < e->e_phnum; n++) {
		ph = (Elf64_Phdr *)(data + e->e_phoff + (sizeof(Elf64_Phdr) * n));
		if(ph->p_type == PT_INTERP) {
			char ipath[PATH_MAX + 1];
			__size_t fl = (__size_t)ph->p_filesz;

			if(fl < 2 || fl > PATH_MAX ||
			   ph->p_offset > PAGE_SIZE - fl) {
				printk("kernel: world check: '%s' has an invalid PT_INTERP.\n", path);
				bad = 1;
				goto out;
			}
			memcpy_b(ipath, data + ph->p_offset, fl);
			ipath[fl - 1] = 0;
			if(ipath[0] != '/') {
				printk("kernel: world check: '%s' has a non-absolute PT_INTERP.\n", path);
				bad = 1;
				goto out;
			}
			has_interp = 1;
			if(!elf_check_file(ipath, 1)) {
				bad = 1;
				goto out;
			}
		} else if(ph->p_type == PT_DYNAMIC) {
			dyn_off = ph->p_offset;
			dyn_size = (__size_t)ph->p_filesz;
		} else if(ph->p_type == PT_LOAD && !load_delta) {
			load_delta = ph->p_vaddr - ph->p_offset;
		}
	}
	if(!has_interp) {
		/* a static init is not the dynamic world we boot; treat as
		 * broken so the recovery shell (or an admin) investigates */
		printk("kernel: world check: '%s' has no PT_INTERP.\n", path);
		bad = 1;
		goto out;
	}
	if(!dyn_size || dyn_size > 65536) {
		printk("kernel: world check: '%s' has an invalid PT_DYNAMIC.\n", path);
		bad = 1;
		goto out;
	}
	if(!(dynseg = (char *)kmalloc((unsigned int)dyn_size))) {
		bad = 1;
		goto out;
	}
	if(elf_read_bytes(i, dyn_off, dynseg, dyn_size)) {
		printk("kernel: world check: cannot read '%s' dynamic section.\n", path);
		kfree((addr_t)dynseg);
		bad = 1;
		goto out;
	}
	for(n = 0; n < (int)(dyn_size / sizeof(struct elf64_dyn_rec)); n++) {
		dyn = (struct elf64_dyn_rec *)(dynseg + (n * sizeof(struct elf64_dyn_rec)));
		if(dyn->d_tag == DT_NULL) {
			break;
		}
		if(dyn->d_tag == DT_STRTAB) {
			strtab_off = (unsigned long long)dyn->d_un.d_val - load_delta;
		} else if(dyn->d_tag == DT_STRSZ) {
			strsz = (unsigned long long)dyn->d_un.d_val;
		} else if(dyn->d_tag == DT_NEEDED) {
			if(!need_count) {
				if(!(strtab = (char *)kmalloc(PATH_MAX + 1))) {
					kfree((addr_t)dynseg);
					bad = 1;
					goto out;
				}
				need_count = 1;
			}
		}
	}
	if(!need_count) {
		/* no NEEDED libs at all: an odd but harmless init */
		kfree((addr_t)dynseg);
		goto out_ok;
	}
	if(!strtab_off || !strsz) {
		printk("kernel: world check: '%s' has no DT_STRTAB/DT_STRSZ.\n", path);
		kfree((addr_t)dynseg);
		kfree((addr_t)strtab);
		bad = 1;
		goto out;
	}
	if(strsz > (unsigned long long)i->i_size) {
		strsz = (unsigned long long)i->i_size;
	}
	if(strtab_off > (unsigned long long)i->i_size ||
	   strsz < 2) {
		printk("kernel: world check: '%s' has an implausible DT_STRTAB.\n", path);
		kfree((addr_t)dynseg);
		kfree((addr_t)strtab);
		bad = 1;
		goto out;
	}
	/* second pass: read each DT_NEEDED name and verify it resolves */
	for(n = 0; n < (int)(dyn_size / sizeof(struct elf64_dyn_rec)); n++) {
		dyn = (struct elf64_dyn_rec *)(dynseg + (n * sizeof(struct elf64_dyn_rec)));
		if(dyn->d_tag == DT_NULL) {
			break;
		}
		if(dyn->d_tag != DT_NEEDED) {
			continue;
		}
		if((unsigned long long)dyn->d_un.d_val >= strsz) {
			printk("kernel: world check: '%s' NEEDED name out of range.\n", path);
			bad = 1;
			break;
		}
		if(elf_read_bytes(i, (__off_t)strtab_off + (__off_t)dyn->d_un.d_val,
				  strtab, PATH_MAX)) {
			printk("kernel: world check: cannot read '%s' NEEDED name.\n", path);
			bad = 1;
			break;
		}
		strtab[PATH_MAX] = 0;
		name = strtab;
		while(*name && *name != '\n' && *name != '\0' &&
		      (name - strtab) < PATH_MAX) {
			name++;
		}
		*name = 0;
		if(!*strtab) {
			continue;
		}
		strcpy(libname, "/System/Libraries/");
		strncat(libname, strtab, sizeof(libname) - 1 -
			(__ssize_t)strlen(libname));
		libname[sizeof(libname) - 1] = 0;
		if(!elf_check_file(libname, 1)) {
			bad = 1;
			break;
		}
	}
	kfree((addr_t)dynseg);
	kfree((addr_t)strtab);
out_ok:
	if(!bad) {
		iput(i);
		kfree((addr_t)data);
		return 1;
	}
out:
	iput(i);
	kfree((addr_t)data);
	return 0;
}
