/*
 * fnx/kernel/syscalls/fork.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/segments.h>
#include <fnx/sigcontext.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/mm.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

static void free_vma_table(struct proc *p)
{
	struct vma *vma, *tmp;

	vma = p->vma_table;
	while(vma) {
		tmp = vma;
		vma = vma->next;
		kfree((addr_t)tmp);
	}
}

int sys_fork(int arg1, int arg2, int arg3, int arg4, int arg5, struct sigcontext *sc)
{
	return do_fork_like(sc, 0, 0, 0, 0, 0, 0);
}

/*
 * FNX: clone(2) (syscall 56). musl's pthread_create/posix_spawn use it.
 * The kernel has no shared-address-space threads, so CLONE_VM (and the
 * thread-only flags that require it) are rejected; the supported subset
 * is fork-equivalent clone: SIGCHLD (and benign flags) with a child
 * stack. The child iretq's with RSP = child_stack and R9 = fn; musl's
 * __clone asm then does `pop %rdi; call *%r9` (arg was stored at
 * child_stack-8), so the child runs fn(arg).
 *
 * x86-64 ABI: clone(flags=rdi, child_stack=rsi, ptid=rdx, tls=rcx, ctid=r8)
 * with fn arriving in r9 (the 6th arg, stashed in sc->r9 by the dispatcher).
 */
#define CLONE_VM		0x00000100
#define CLONE_FS		0x00000200
#define CLONE_FILES		0x00000400
#define CLONE_SIGHAND		0x00000800
#define CLONE_PTRACE		0x00002000
#define CLONE_VFORK		0x00004000
#define CLONE_THREAD		0x00010000
#define CLONE_SETTLS		0x00080000
#define CLONE_CHILD_SETTID	0x01000000
#define CLONE_CHILD_CLEARTID	0x00200000
#define CLONE_PARENT_SETTID	0x00100000

/* NOTE: arg1/arg2 are long, not int - the dispatcher passes 64-bit
 * registers (the child_stack is a user address like 0x7ffffffffxxx); an
 * int prototype truncates it and sign-extends to a kernel address.
 *
 * musl __clone asm (pthread_create): clone(func, stack, flags, arg,
 * ptid, tls, ctid). After the register shuffle the syscall sees
 * rdi=flags rsi=stack rdx=ptid r10=ctid r8=tls, and r9 (6th arg) = func.
 * The dispatcher maps rdi/rsi/rdx/r10/r8 to a1..a5, so:
 *   arg1 = flags, arg2 = child_stack, arg3 = ptid, arg4 = ctid,
 *   arg5 = tls, sc->r9 = func. */
int sys_clone(long arg1, long arg2, long arg3, long arg4, long arg5, struct sigcontext *sc)
{
	unsigned int flags = (unsigned int)arg1;
	addr_t child_stack = (addr_t)arg2;
	addr_t ptid = (addr_t)arg3;
	addr_t ctid = (addr_t)arg4;
	addr_t tls = (addr_t)arg5;
	addr_t fn = (addr_t)sc->r9;

	/* CLONE_VM without CLONE_THREAD is the vfork-style optimization
	 * used by musl's posix_spawn: the child execs immediately, so a
	 * COW fork is a correct (and safe) realization - the child's copy
	 * of the args lives in its own stack. CLONE_THREAD (real threads)
	 * is handled by do_fork_like() sharing the parent's address space. */
	return do_fork_like(sc, flags, child_stack, fn, ptid, ctid, tls);
}

int do_fork_like(struct sigcontext *sc, unsigned int clone_flags, addr_t child_stack, addr_t fn, addr_t ptid, addr_t ctid, addr_t tls)
{
	int count, pages;
	unsigned int n;
	unsigned int *child_pgdir;
	struct sigcontext *stack;
	struct proc *child, *p;
	struct vma *vma, *child_vma;
	__pid_t pid;
	int is_thread = (clone_flags & CLONE_VM) ? 1 : 0;

#ifdef __DEBUG__
	printk("(pid %d) sys_fork()\n", current->pid);
#endif /*__DEBUG__ */

	/* check the number of processes already allocated by this UID */
	count = 0;
	FOR_EACH_PROCESS(p) {
		if(p->uid == current->uid) {
			count++;
		}
		p = p->next;
	}
	if(count > current->rlim[RLIMIT_NPROC].rlim_cur) {
		printk("WARNING: %s(): RLIMIT_NPROC exceeded.\n", __FUNCTION__);
		return -EAGAIN;
	}

	if(!(pid = get_unused_pid())) {
		return -EAGAIN;
	}
	if(!(child = get_proc_free())) {
		return -EAGAIN;
	}

	/* 
	 * This memcpy() will overwrite the prev and next pointers, so that's
	 * the reason why proc_slot_init() is separated from get_proc_free().
	 */
	memcpy_b(child, current, sizeof(struct proc));

	proc_slot_init(child);
	/* proc_slot_init() wipes groups[] to the empty (-1) state; restore the
	 * parent's supplementary group list that memcpy_b() above inherited */
	memcpy_b(child->groups, current->groups, sizeof(child->groups));
	child->pid = pid;
	sprintk(child->pidstr, "%d", child->pid);

	if(is_thread) {
		/* FNX: real thread (CLONE_VM). Share the parent's address
		 * space: same pml4, same vma table, no page copies. The
		 * thread's stack was mmap'd by the parent, so it is already
		 * mapped in this shared space. PF_THREAD marks the child so
		 * do_exit() does not tear down the shared address space. */
		child->cr3_64 = current->cr3_64;
		child->tss.cr3 = current->tss.cr3;
		child->vma_table = current->vma_table;
		child->flags |= PF_THREAD;
		if(clone_flags & CLONE_THREAD) {
			child->tgid = current->tgid;
		} else {
			child->tgid = pid;
		}
		if(clone_flags & CLONE_SETTLS) {
			/* x86-64 musl: TLS lives above the thread pointer, read
			 * via %fs (__get_tp = mov %%fs:0). The tls arg is
			 * TP_ADJ(new) = new+sizeof(struct pthread)+TP_OFFSET;
			 * set the thread's fs_base so the context switch loads
			 * this thread's own TLS (not the parent's). */
			child->fs_base = tls;
		}
		child_pgdir = NULL;
		pages = 0;
	} else {
		if(!(child_pgdir = (void *)kmalloc(PAGE_SIZE))) {
			release_proc(child);
			return -ENOMEM;
		}
		child->rss++;
		memcpy_b(child_pgdir, kpage_dir, PAGE_SIZE);
		child->tss.cr3 = V2P((addr_t)child_pgdir);
#ifdef __x86_64__
		{
			/* FNX (M6-next): the fork child gets its own 4-level tables,
			 * deep-copied from the PARENT's pml4 (current->cr3_64) so it
			 * inherits every demand-mapped user page (text/data/stack/TLS).
			 * Writable user leaves are shared read-only (CoW), mirroring
			 * clone_pages()'s 2-level PAGE_COW. */
			extern unsigned long create_pml4_64(unsigned long);
			if(!(child->cr3_64 = create_pml4_64(current->cr3_64))) {
				kfree((addr_t)child_pgdir);
				release_proc(child);
				return -ENOMEM;
			}
		}
#endif /* __x86_64__ */
		child->tgid = pid;
	}

	child->ppid = current;
	child->flags = is_thread ? PF_THREAD : 0;
	child->children = 0;
	child->cpu_count = (current->cpu_count >>= 1);
	child->start_time = CURRENT_TICKS;
	child->sleep_address = NULL;

	if(is_thread) {
		/* threads share the vma table; no copy */
	} else {
		vma = current->vma_table;
		child->vma_table = NULL;
		while(vma) {
			if(!(child_vma = (struct vma *)kmalloc(sizeof(struct vma)))) {
				kfree((addr_t)child_pgdir);
				free_vma_table(child);
				release_proc(child);
				return -ENOMEM;
			}
			*child_vma = *vma;
			child_vma->prev = child_vma->next = NULL;
			if(child_vma->inode) {
				child_vma->inode->count++;
			}
			if(!child->vma_table) {
				child->vma_table = child_vma;
			} else {
				child_vma->prev = child->vma_table->prev;
				child->vma_table->prev->next = child_vma;
			}
			child->vma_table->prev = child_vma;
			vma = vma->next;
		}
	}

	child->sigpending = 0;
	child->sigexecuting = 0;
	/* zero the WHOLE sc array: the old code zeroed only sc[0], so a
	 * fork child inherited the parent's stale saved sigcontexts for
	 * signals 1..31 (a later rt_sigreturn could resurrect them) */
	memset_b(&child->sc, 0, sizeof(child->sc));
	memset_b(&child->usage, 0, sizeof(struct rusage));
	memset_b(&child->cusage, 0, sizeof(struct rusage));
	child->it_real_interval = 0;
	child->it_real_value = 0;
	child->it_virt_interval = 0;
	child->it_virt_value = 0;
	child->it_prof_interval = 0;
	child->it_prof_value = 0;
#ifdef CONFIG_SYSVIPC
	current->semundo = NULL;
#endif /* CONFIG_SYSVIPC */


	if(!(child->tss.esp0 = kmalloc(PAGE_SIZE))) {
		if(!is_thread) {
			kfree((addr_t)child_pgdir);
			free_vma_table(child);
		}
		release_proc(child);
		return -ENOMEM;
	}

	if(is_thread) {
		/* threads run in the parent's mapped address space; no page
		 * copies needed */
	} else {
		if(!(pages = clone_pages(child))) {
			printk("WARNING: %s(): not enough memory when cloning pages.\n", __FUNCTION__);
			free_page_tables(child);
			kfree((addr_t)child_pgdir);
			free_vma_table(child);
			release_proc(child);
			return -ENOMEM;
		}
		child->rss += pages;
		invalidate_tlb();
	}

	child->tss.esp0 += PAGE_SIZE - 4;
	child->rss++;
	child->tss.ss0 = KERNEL_DS;

	memcpy_b((unsigned int *)(child->tss.esp0 & PAGE_MASK), (void *)((addr_t)(sc) & PAGE_MASK), PAGE_SIZE);
	stack = (struct sigcontext *)((child->tss.esp0 & PAGE_MASK) + ((addr_t)(sc) & ~PAGE_MASK));

	extern void return_from_syscall64(void);
	child->tss.eip = (addr_t)return_from_syscall64;
	child->flags |= PF_ELF64;
	child->tss.esp = (addr_t)stack;
	stack->rax = 0;		/* child returns 0 */

	if(clone_flags) {
		/* clone child: iretq with RSP = child_stack and R9 = fn so
		 * musl's __clone asm (`pop %rdi; call *%r9`) runs fn(arg).
		 * sc.rip is already the post-syscall user address (copied
		 * from the parent's frame above). */
		stack->rsp = child_stack;
		stack->r9 = fn;
	}

	/* CLONE_PARENT_SETTID: write the child's tid to *ptid (musl
	 * pthread_create stores the new thread's id here). */
	if((clone_flags & CLONE_PARENT_SETTID) && ptid) {
		if(!check_user_area(VERIFY_WRITE, (void *)ptid, sizeof(int))) {
			*(int *)ptid = pid;
		}
	}
	/* CLONE_CHILD_CLEARTID: the child clears *ctid (0) on exit and the
	 * kernel futex-wakes it - musl pthread_join() waits on this. */
	if((clone_flags & CLONE_CHILD_CLEARTID) && ctid) {
		child->set_child_tid = (void *)ctid;
	}

	/* increase file descriptors usage */
	for(n = 0; n < OPEN_MAX; n++) {
		if(current->fd[n]) {
			fd_table[current->fd[n]].count++;
		}
	}
	if(current->root) {
		current->root->count++;
	}
	if(current->pwd) {
		current->pwd->count++;
	}

	kstat.processes++;
	nr_processes++;
	current->children++;
	runnable(child);

	return child->pid;	/* parent returns child's PID */
}
