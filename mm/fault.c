/*
 * fnx/mm/fault.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/sigcontext.h>
#include <fnx/asm.h>
#include <fnx/mm.h>
#include <fnx/process.h>
#include <fnx/traps.h>
#include <fnx/sched.h>
#include <fnx/fs.h>
#include <fnx/mman.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/syscalls.h>
#include <fnx/shm.h>

static int page_not_present(struct vma *vma, addr_t cr2, struct sigcontext *sc);

/* send the SIGSEGV signal to the ofending process */
static void send_sigsegv(struct sigcontext *sc)
{
#if defined(CONFIG_VERBOSE_SEGFAULTS)
	dump_registers(14, sc);
	printk("Memory map:\n");
	show_vma_regions(current);
#endif /* CONFIG_VERBOSE_SEGFAULTS */
	send_sig(current, SIGSEGV);
}

/*
 * A page fault whose mapping the kernel could not satisfy: map_page()/
 * map_page_flags() return 0 when a page or a page-table page cannot be
 * allocated (out of memory - there is no swap here), and the file-backed
 * path returns 1 when bread_page() fails.
 *
 * Linux answers this with SIGBUS - "the page cannot be faulted in" - and
 * keeps SIGSEGV for a bad address; it also says so. These paths used to
 * send SIGKILL: uncatchable, with no reason recorded anywhere (the fault
 * report named the process and the address but never said *why*, and one
 * of the sites printed nothing at all). That is how a resize churning
 * MIT-SHM segments took a whole session down with nothing to read. SIGBUS
 * is catchable, so a client can shut itself down cleanly instead.
 */
static void fault_unmappable(addr_t cr2, struct sigcontext *sc)
{
	static addr_t last_cr2;
	static int last_pid;
	static int reported;

	if(!reported || cr2 != last_cr2 || current->pid != last_pid) {
		last_cr2 = cr2;
		last_pid = current->pid;
		reported = 1;
#if defined(CONFIG_VERBOSE_SEGFAULTS)
		dump_registers(14, sc);
		show_vma_regions(current);
#endif /* CONFIG_VERBOSE_SEGFAULTS */
		printk("do_page_fault(): cannot map the page of process '%s' (pid %d) at 0x%lx - out of memory?\n",
		       current->argv0, current->pid, (unsigned long)cr2);
	}
	send_sig(current, SIGBUS);
}

static int page_protection_violation(struct vma *vma, addr_t cr2, struct sigcontext *sc)
{
#ifdef __x86_64__
	/* FNX (native-MM): the process pml4 is the single source of truth.
	 * A user write to a present-but-read-only leaf is copy-on-write (or a
	 * real violation); a write to an address that is NOT user-mapped (absent
	 * or a supervisor 2MB identity page) is demand-paging. No 2-level
	 * shadow, no mirroring, no desync. */
	unsigned long pml4, leaf, newaddr;

	extern unsigned long user_leaf64_in(unsigned long, unsigned long);
	extern int map_user_page64_in(unsigned long, unsigned long, unsigned long, unsigned long);
	extern unsigned long paging64_pml4(void);

	pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4();
	leaf = user_leaf64_in(pml4, (unsigned long)cr2);
	if(!leaf) {
		/* not user-mapped yet (identity 2MB page or absent leaf): if the
		 * vma allows writes, demand-map it (splits huge pages); a
		 * non-writable vma is a genuine violation */
		if((sc->err & PFAULT_U) && (vma->prot & PROT_WRITE)) {
			return page_not_present(vma, cr2, sc);
		}
		send_sigsegv(sc);
		return 0;
	}

	/* Copy On Write (FNX native): a fork shares the writable user
	 * leaves read-only in BOTH sides (create_pml4_64), so the first
	 * write by either side gets a fresh private copy. Only do this for
	 * writable PRIVATE vmas - a genuine read-only page (text/rodata,
	 * PROT_READ mmap) must SIGSEGV, not silently become writable. */
	if(!(vma->prot & PROT_WRITE) || (vma->flags & MAP_SHARED)) {
		send_sigsegv(sc);
		return 0;
	}
	if(!(newaddr = kmalloc(PAGE_SIZE))) {
		printk("%s(): not enough memory!\n", __FUNCTION__);
		return 1;
	}
	current->rss++;
	memcpy_b((void *)P2V(newaddr), (void *)P2V(leaf), PAGE_SIZE);
	if(map_user_page64_in(pml4, (unsigned long)cr2,
			(unsigned long)V2P(newaddr), 0x003)) {
		kfree(newaddr);
		current->rss--;
		return 1;
	}
	/* FNX (pivot): the faulting process drops its reference to the
	 * old shared (CoW) leaf - the fresh copy replaces it in THIS pml4,
	 * while the other process(es) still map the original. create_pml4_64
	 * incremented the count for every fork copy, so the count is now
	 * balanced: 1 reference per pml4 mapping. Without this decrement the
	 * original page is never freed (count leaks), and without the
	 * create_pml4_64 increment the child's exit would free a page the
	 * parent still maps. */
	{
		extern void page_ref_put(unsigned long);
		page_ref_put(leaf);
	}
	invalidate_tlb();
	return 0;
#else
	unsigned int *pgdir;
	unsigned int *pgtbl;
	unsigned int pde, pte;
	addr_t addr;
	struct page *pg;
	int page;

	pde = GET_PGDIR(cr2);
	pte = GET_PGTBL(cr2);
	pgdir = (unsigned int *)P2V(current->tss.cr3);
	pgtbl = (unsigned int *)P2V((pgdir[pde] & PAGE_MASK));
	page = (pgtbl[pte] & PAGE_MASK) >> PAGE_SHIFT;

	pg = &page_table[page];

	/* Copy On Write feature */
	if(pg->count > 1) {
		/* a page not marked as copy-on-write means it's read-only */
		if(!(pg->flags & PAGE_COW)) {
			printk("Oops!, page %d NOT marked for CoW.\n", pg->page);
			send_sigsegv(sc);
			return 0;
		}
		if(!(addr = kmalloc(PAGE_SIZE))) {
			printk("%s(): not enough memory!\n", __FUNCTION__);
			return 1;
		}
		current->rss++;
		memcpy_b((void *)addr, (void *)P2V((page << PAGE_SHIFT)), PAGE_SIZE);
		pgtbl[pte] = V2P(addr) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
		/* the other CoW process(es) still reference the old page: drop
		 * our reference instead of freeing it, or the parent's next
		 * write faults on a freed page (page 0 count 0 corruption) */
		pg->count--;
		current->rss--;
		invalidate_tlb();
		return 0;
	} else {
		/* last page of Copy On Write procedure */
		if(pg->count == 1) {
			/* a page not marked as copy-on-write means it's read-only */
			if(!(pg->flags & PAGE_COW)) {
				printk("Oops!, last page %d NOT marked for CoW.\n", pg->page);
				send_sigsegv(sc);
				return 0;
			}
			pgtbl[pte] = (page << PAGE_SHIFT) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
			invalidate_tlb();
			return 0;
		}
	}
	printk("WARNING: %s(): page %d with pg->count = 0!\n", __FUNCTION__, pg->page);
	return 1;
#endif /* __x86_64__ */
}

static int page_not_present(struct vma *vma, addr_t cr2, struct sigcontext *sc)
{
	addr_t addr, file_offset;
	struct page *pg;

	if(!vma) {
		if(cr2 >= (sc->rsp - 32) && cr2 < USER_STACK_TOP) {
			if(!(vma = find_vma_region(USER_STACK_TOP - 1))) {
				printk("WARNING: %s(): process %d doesn't have an stack region in vma_table!\n", __FUNCTION__, current->pid);
				send_sigsegv(sc);
				return 0;
			} else {
				/* assuming stack will never reach heap */
				vma->start = cr2;
				vma->start = vma->start & PAGE_MASK;
			}
		}
	}

	/* if still a non-valid vma is found then kill the process! */
	if(!vma || vma->prot == PROT_NONE) {
		send_sigsegv(sc);
		return 0;
	}

	/* fill the page with its corresponding file content */
	if(vma->inode) {
		file_offset = (cr2 & PAGE_MASK) - vma->start + vma->offset;
		file_offset &= PAGE_MASK;
		pg = NULL;

		if(!(vma->prot & PROT_WRITE) || vma->flags & MAP_SHARED) {
			/* check if it's already in cache */
			if((pg = search_page_hash(vma->inode, file_offset))) {
				if(!map_page(current, cr2, (addr_t)V2P(pg->data), vma->prot)) {
					printk("%s(): Oops, map_page() returned 0!\n", __FUNCTION__);
					return 1;
				}
				page_lock(pg);
				addr = (addr_t)pg->data;
				page_unlock(pg);
			}
		}
		if(!pg) {
			if(!(addr = map_page(current, cr2, 0, vma->prot))) {
				printk("%s(): Oops, map_page() returned 0!\n", __FUNCTION__);
				return 1;
			}
			pg = &page_table[V2P(addr) >> PAGE_SHIFT];
			if(bread_page(pg, vma->inode, file_offset, vma->prot, vma->flags)) {
				unmap_page(cr2);
				return 1;
			}
			current->usage.ru_majflt++;
		}
	} else {
		current->usage.ru_minflt++;
		addr = 0;
#ifdef CONFIG_SYSVIPC
		if(vma->s_type == P_SHM) {
			if(shm_map_page(vma, cr2)) {
				return 1;
			}
		}
#endif /* CONFIG_SYSVIPC */
	}

	if(vma->flags & ZERO_PAGE) {
		if(!addr) {
			if(!(addr = map_page(current, cr2, 0, vma->prot))) {
				printk("%s(): Oops, map_page() returned 0!\n", __FUNCTION__);
				return 1;
			}
		}
		memset_b((void *)(addr & PAGE_MASK), 0, PAGE_SIZE);
	}

	return 0;
}

#ifdef __x86_64__
/*
 * FNX (M6): the low-1GB identity 2MB pages make every user address in
 * 0-1GB look "present" to CPL0 accesses, so a kernel-side copy to/from a
 * not-yet-demand-mapped user page never faults (the 32-bit kernel relied
 * on the page-fault retry). Before the kernel copies user memory, walk the
 * ACTIVE 4-level tables and pre-demand-map every page in the range that is
 * still covered by a supervisor identity huge page (or has no U/S leaf).
 * Returns 0 on success, -EFAULT if a page could not be mapped.
 */
#define FNX64_P2V(a)	(((unsigned long)(a) < 0xFFFFFFFF80000000ULL) ? \
				((unsigned long)(a) + 0xFFFFFFFF80000000ULL) : (unsigned long)(a))
#define FNX64_PMASK	0x000FFFFFFFFFF000ULL
int fnx_fault_user_pages(addr_t start, unsigned int size)
{
	struct sigcontext sc;
	struct vma *vma;
	unsigned long *lvl, e1, e2, e3, e4, page, pml4;
	addr_t a;

	{
		extern unsigned long paging64_pml4(void);
		pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4();
	}

	memset_b(&sc, 0, sizeof(sc));
	for(a = start & PAGE_MASK; a < start + (addr_t)size; a += PAGE_SIZE) {
		page = (unsigned long)a;

		/* FNX: kernel-mode demand-map must grow the user stack vma
		 * exactly like the user-mode fault path (page_not_present) does.
		 * A syscall whose user buffer sits below the current stack vma
		 * start (e.g. getcwd's 4096-byte buffer one page below a 1-page
		 * stack vma) otherwise faults in CPL0 with no vma -> K2 panic. */
		vma = find_vma_region(a);
		if(!vma && a < USER_STACK_TOP) {
			struct vma *s;

			if((s = find_vma_region(USER_STACK_TOP - 1)) &&
			   s->s_type == P_STACK && a < s->start &&
			   a >= s->start - 0x20000) {
				s->start = a & PAGE_MASK;
				vma = s;
			}
		}

		lvl = (unsigned long *)FNX64_P2V(pml4);
		if(!(e1 = lvl[(page >> 39) & 0x1FF]) || !(e1 & 0x1)) {
			if(vma) {
				if(page_not_present(vma, a, &sc)) {
					return -EFAULT;
				}
			}
			continue;	/* unmapped: a CPL0 access would fault and
					 * be handled by the K1 path, like 32-bit */
		}
		lvl = (unsigned long *)FNX64_P2V(e1 & FNX64_PMASK);
		if(!(e2 = lvl[(page >> 30) & 0x1FF]) || !(e2 & 0x1)) {
			if(vma) {
				if(page_not_present(vma, a, &sc)) {
					return -EFAULT;
				}
			}
			continue;
		}
		lvl = (unsigned long *)FNX64_P2V(e2 & FNX64_PMASK);
		if(!(e3 = lvl[(page >> 21) & 0x1FF]) || !(e3 & 0x1)) {
			if(vma) {
				if(page_not_present(vma, a, &sc)) {
					return -EFAULT;
				}
			}
			continue;
		}
		if(e3 & 0x80) {		/* 2MB huge page: supervisor identity page,
					 * masks the not-present state for CPL0 */
			if(!vma) {
				continue;
			}
			if(page_not_present(vma, a, &sc)) {
				return -EFAULT;
			}
			continue;
		}
		lvl = (unsigned long *)FNX64_P2V(e3 & FNX64_PMASK);
		e4 = lvl[(page >> 12) & 0x1FF];
		if(!(e4 & 0x1) || !(e4 & 0x4)) {	/* not present or not U/S */
			if(!vma) {
				continue;
			}
			if(page_not_present(vma, a, &sc)) {
				return -EFAULT;
			}
		}
	}
	return 0;
}
#undef FNX64_P2V
#undef FNX64_PMASK
#endif /* __x86_64__ */

/*
 * Exception 0xE: Page Fault
 *
 *		 +------+------+------+------+------+------+
 *		 | user |kernel|  PV  |  PF  | read |write |
 * +-------------+------+------+------+------+------+------+
 * |the page     | U1   |    K1| U1 K1|      | U1 K1|    K1|
 * |has          | U2   |    K2| U2   |    K2|    K2| U2 K2|
 * |a vma region | U3   |      |      | U3   | U3   | U3   |
 * +-------------+------+------+------+------+------+------+
 * |the page     | U1   |    K1| U1   |    K1| U1 K1| U1 K1|
 * |doesn't have | U2   |    K2|    K2| U2   | U2 K2| U2 K2|
 * |a vma region |      |      |      |      |      |      |
 * +-------------+------+------+------+------+------+------+
 *
 * U1 - vma + user + PV + read
 *	(vma page in user-mode, page-violation during read)
 *	U1.1) if flags match			-> Demand paging
 *	U1.2) if flags don't match		-> SIGSEV
 *
 * U2 - vma + user + PV + write
 *	(vma page in user-mode, page-violation during write)
 *	U2.1) if flags match			-> Copy-On-Write
 *	U2.2) if flags don't match		-> SIGSEGV
 *
 * U3 - vma + user + PF + (read | write)	-> Demand paging
 *	(vma page in user-mode, page-fault during read or write)
 *
 * K1 - vma + kernel + PV + (read | write)	-> PANIC
 *	(vma page in kernel-mode, page-violation during read or write)
 * K2 - vma + kernel + PF + (read | write)	-> Demand paging (mmap)
 *	(vma page in kernel-mode, page-fault during read or write)
 *
 * ----------------------------------------------------------------------------
 *
 * U1 - !vma + user + PV + (read | write)	-> SIGSEGV
 *	(!vma page in user-mode, page-violation during read or write)
 * U2 - !vma + user + PF + (read | write)	-> STACK?
 *	(!vma page in user-mode, page-fault during read or write)
 *
 * K1 - !vma + kernel + PF + (read | write)	-> STACK?
 *	(!vma page in kernel-mode, page-fault during read or write)
 * K2 - !vma + kernel + PV + (read | write)	-> PANIC
 *	(!vma page in kernel-mode, page-violation during read or write)
 */
void do_page_fault(unsigned int trap, struct sigcontext *sc)
{
	addr_t cr2;
	struct vma *vma;
	int panic;

	GET_CR2(cr2);
	if((vma = find_vma_region(cr2))) {

		/* in user mode */
		if(sc->err & PFAULT_U) {
			if(sc->err & PFAULT_V) {	/* violation */
				if(sc->err & PFAULT_W) {
					if((page_protection_violation(vma, cr2, sc))) {
						fault_unmappable(cr2, sc);
					}
					return;
				}
#ifdef __x86_64__
				/* FNX: a user read/fetch "violation" on a present
				 * page is normally a supervisor-only 2MB identity page
				 * that needs to be demand-mapped with the U/S bit
				 * (the 32-bit kernel's user pages were always mapped
				 * U/S, so this case never existed). Real protection
				 * violations on U/S pages are writes, handled above. */
				if((page_not_present(vma, cr2, sc))) {
					fault_unmappable(cr2, sc);
				}
#else
				send_sigsegv(sc);
#endif /* __x86_64__ */
			} else {			/* page not present */
				if((page_not_present(vma, cr2, sc))) {
					fault_unmappable(cr2, sc);
				}
			}
			return;

		/* in kernel mode */
		} else {
			/* 
			 * WP bit marks the order: first check if the page is
			 * present, then check for protection violation.
			 */
			if(!(sc->err & PFAULT_V)) {	/* page not present */
				if((page_not_present(vma, cr2, sc))) {
					send_sig(current, SIGKILL);
					printk("%s(): kernel was unable to read a page of process '%s' (pid %d).\n", __FUNCTION__, current->argv0, current->pid);
				}
				return;
			}
			if(sc->err & PFAULT_W) {	/* copy-on-write? */
				if((page_protection_violation(vma, cr2, sc))) {
					send_sig(current, SIGKILL);
					printk("%s(): kernel was unable to write a page of process '%s' (pid %d).\n", __FUNCTION__, current->argv0, current->pid);
				}
				return;
			}
		}
	} else {
		/* in user mode */
		if(sc->err & PFAULT_U) {
			if(sc->err & PFAULT_V) {	/* violation */

#ifdef __x86_64__
				/* FNX: with no vma, a user "violation" (read OR
				 * write) below the stack top is stack growth below
				 * the stack vma - the 0-4GB identity map makes the
				 * not-present page look present (V bit set). Route
				 * it to page_not_present(), which grows the stack
				 * (it re-checks the stack heuristic and SIGSEGVs if
				 * the address isn't stack-like). */
				if(cr2 < USER_STACK_TOP) {
					if((page_not_present(vma, cr2, sc))) {
						fault_unmappable(cr2, sc);
					}
					return;
				}
#endif /* __x86_64__ */
				send_sigsegv(sc);
			} else {			/* stack? */
				if((page_not_present(vma, cr2, sc))) {
					fault_unmappable(cr2, sc);
				}
			}
			return;

		/* in kernel mode */
		} else {
			/*
			 * The kernel may incur in a page fault when trying to
			 * access a possible user stack address. In that case,
			 * sc->rsp doesn't point to the user stack, but to
			 * the kernel stack, because the page fault was raised
			 * in kernel mode.
			 * We need to get the original user sigcontext struct
			 * in order to obtain the user stack pointer sc->rsp,
			 * and see if CR2 looks like a user stack address.
			 */
			struct sigcontext *usc;

			/*
			 * FNX (native port): syscall80_handler() keeps the
			 * active user sigcontext at current->sp for the whole
			 * syscall (set before dispatch), so a kernel-mode fault
			 * inside a syscall (e.g. copy_from_user) finds the user
			 * state there. The old i386 SAVE_ALL stack-digging
			 * ((unsigned int *)sc->esp + 16) is 32-bit baggage.
			 */
			usc = (struct sigcontext *)current->sp;

			/* does it look like a user stack address? */
			if(cr2 >= (usc->rsp - 32) && cr2 < USER_STACK_TOP) {
				if((!page_not_present(vma, cr2, usc))) {
					return;
				}
			}

			/* no */

			/* recoverable user-copy fault: the kernel was copying
			 * to/from a user buffer that a racing thread unmapped
			 * between verify_address() and the copy (no vma left
			 * to demand-map). Unwind to the copy site, which
			 * returns -EFAULT to the syscall instead of panicking. */
			extern int user_copy_in_progress(void);
			extern void user_copy_fault_recover(void);
			if(user_copy_in_progress() && cr2 < USER_STACK_TOP) {
				user_copy_fault_recover();
				/* not reached */
			}
		}
	}

	panic = dump_registers(trap, sc);
	show_vma_regions(current);
	if(panic) {
		PANIC("");
	}
	do_exit(SIGTERM);
}
