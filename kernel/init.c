/*
 * fnx/kernel/init.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/system.h>
#include <fnx/mm.h>
#include <fnx/timer.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/process.h>
#include <fnx/syscalls.h>
#include <fnx/unistd.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/kparms.h>

#define INIT_TRAMPOLINE_SIZE	256	/* max. size of init_trampoline() */

char *init_args;
char *init_argv[] = { INIT_PROGRAM, NULL, NULL };
/* PID 1's envp: the normal (dynamic) boot or the static recovery shell.
 * The recovery envp puts /System/Recovery/bin first so repair commands
 * resolve to the static toybox, and drops HOME to / (the Admin home may
 * be part of what needs repairing). The kernel-side consumer reads the
 * selected array by address, so init_envp stays a real array symbol. */
char *init_envp[] = { "HOME=/Users/Admin", "TERM=linux", NULL };
char *init_envp_rec[] = { "HOME=/", "TERM=linux",
	"PATH=/System/Recovery/bin:/System/Tools", "PS1=recovery# ", NULL };
char **init_pid1_envp = init_envp;
/* The INIT bootstrap trampoline (init_trampoline64.S) opens this console
 * device; its runtime address is written into the trampoline's fixed
 * table at user VA 0x100100 by init_init(). */
char init_console_dev[] = "/System/Devices/TTY/console";

/* The INIT bootstrap trampoline is a position-independent assembly
 * function (kernel/boot64/init_trampoline64.S) that uses absolute movabs
 * addresses for the kernel symbols, so it can be COPIED to user VA
 * 0x100000 and run at CPL3. It uses the native 'syscall' instruction
 * with x86-64 syscall numbers (open=2, dup=32, execve=59, exit=60). */
extern void init_trampoline(void);

void init_init(void)
{
	int n;
	addr_t page;	/* map_page() returns a P2V address: must be 64-bit */
	struct inode *i;
	unsigned int *pgdir;
	struct proc *init;

	/*
	 * PID 1 target: the dynamic init when the world is bootable, the
	 * STATIC recovery shell otherwise (docs/design/shared-libraries-plan.md
	 * §3). A 'recovery' kernel param forces it; otherwise the
	 * NEEDED-closure probe (elf_world_check) decides - a missing or
	 * corrupt /System/Libraries must yield the recovery shell with a
	 * clear message instead of a cascade of exec failures. The check
	 * runs here, after mount_root(), so the root filesystem is up.
	 */
	if(kparms.recovery) {
		printk("kernel: recovery mode requested on the command line; "
		       "booting the static recovery shell.\n");
		init_argv[0] = (char *)RECOVERY_PROGRAM;
		init_pid1_envp = init_envp_rec;
	} else if(!elf_world_check(INIT_PROGRAM)) {
		printk("kernel: the dynamic world is not bootable "
		       "(/System/Libraries missing or corrupt); booting the "
		       "static recovery shell (%s).\n", RECOVERY_PROGRAM);
		init_argv[0] = (char *)RECOVERY_PROGRAM;
		init_pid1_envp = init_envp_rec;
	}

	if(namei(init_argv[0], &i, NULL, FOLLOW_LINKS)) {
		PANIC("can't find %s.\n", init_argv[0]);
	}
	if(!S_ISREG(i->i_mode)) {
		PANIC("%s is not a regular file.\n", init_argv[0]);
	}
	iput(i);

	/* INIT slot was already created in main.c */
	init = &proc_table[INIT];

	/* INIT process starts with the current (kernel) Page Directory */
	if(!(pgdir = (void *)kmalloc(PAGE_SIZE))) {
		goto init_init__die;
	}
	init->rss++;
	memcpy_b(pgdir, kpage_dir, PAGE_SIZE);
	init->tss.cr3 = V2P((addr_t)pgdir);
#ifdef __x86_64__
	{
		/* FNX (M6-next): INIT gets its own 4-level tables (deep copy
		 * of the low-4GB identity/user hierarchy + shared kernel half).
		 * The init_trampoline page (PAGE_OFFSET - PAGE_SIZE) lives in the
		 * shared high half, so map_page() below still reaches it. */
		extern unsigned long create_pml4_64(unsigned long);
		extern unsigned long paging64_pml4_phys(void);
		if(!(init->cr3_64 = create_pml4_64(paging64_pml4_phys()))) {
			goto init_init__die;
		}
	}
#endif /* __x86_64__ */

	init->ppid = &proc_table[IDLE];
	init->pgid = INIT;	/* init is a session leader: SESS_LEADER() */
	init->sid = INIT;	/* requires pid == pgid == sid, so the console */
				/* open in tty_open() assigns it a ctty */
	/* FNX: the INIT bootstrap trampoline is native 64-bit code (it
	 * uses the 'syscall' instruction via USER_SYSCALL64), so INIT is a
	 * PF_ELF64 process - the syscall dispatcher must use the x86-64 ABI
	 * (rdi/rsi/rdx/r10/r8) and syscall_table64, not the i386 compat one. */
	init->flags = PF_ELF64;
	init->children = 0;
	init->priority = DEF_PRIORITY;
	init->start_time = CURRENT_TICKS;
	init->sleep_address = NULL;
	init->uid = init->gid = 0;
	init->euid = init->egid = 0;
	init->suid = init->sgid = 0;
	init->fsuid = init->fsgid = 0;
	memset_b(init->fd, 0, sizeof(init->fd));
	memset_b(init->fd_flags, 0, sizeof(init->fd_flags));
	init->root = current->root;
	init->pwd = current->pwd;
	strcpy(init->argv0, init_argv[0]);
	init_argv[1] = init_args;
	sprintk(init->pidstr, "%d", init->pid);
	init->sigpending = 0;
	init->sigblocked = 0;
	init->sigexecuting = 0;
	memset_b(init->sigaction, 0, sizeof(init->sigaction));
	memset_b(&init->usage, 0, sizeof(struct rusage));
	memset_b(&init->cusage, 0, sizeof(struct rusage));
	init->timeout = 0;
	for(n = 0; n < RLIM_NLIMITS; n++) {
		init->rlim[n].rlim_cur = init->rlim[n].rlim_max = RLIM_INFINITY;
	}
	init->rlim[RLIMIT_NOFILE].rlim_cur = OPEN_MAX;
	init->rlim[RLIMIT_NOFILE].rlim_max = NR_OPENS;
	init->rlim[RLIMIT_NPROC].rlim_cur = CHILD_MAX;
	init->rlim[RLIMIT_NPROC].rlim_max = NR_PROCS;
	init->umask = 0022;

	/* setup the stack: tss.esp0 is the KERNEL stack used by do_switch to
	 * enter switch_to_user_mode at CPL0. */
	if(!(init->tss.esp0 = (addr_t)kmalloc(PAGE_SIZE))) {
		goto init_init__die;
	}
	init->tss.esp0 += PAGE_SIZE - 4;
	init->rss++;
	init->tss.ss0 = KERNEL_DS;

	/* setup the init_trampoline in the USER half (canonical amd64
	 * split): the bootstrap code page is copied to a fixed low user
	 * address 0x100000, where switch_to_user_mode iretq's into it at
	 * CPL3. The trampoline is position-independent asm
	 * (init_trampoline64.S): it reads its 4 runtime pointers (console
	 * path, argv, envp, INIT_PROGRAM) from the fixed table at user VA
	 * 0x100100, which we fill here with the high-half runtime addresses
	 * of the C globals. The user stack is the NEXT page (0x101000). */
	page = map_page(init, 0x100000, 0, PROT_READ | PROT_WRITE | PROT_EXEC);
	memcpy_b((void *)page, (void *)&init_trampoline, INIT_TRAMPOLINE_SIZE);
	{
		/* the table is at offset 0x100 into the trampoline page
		 * (user VA 0x100100); fill the 4 u64 runtime addresses via
		 * the P2V pointer map_page returned (CPL0, so we cannot
		 * write the user VA directly) */
		unsigned long *t = (unsigned long *)((char *)page + 0x100);
		t[0] = (unsigned long)init_console_dev;
		t[1] = (unsigned long)init_argv;
		t[2] = (unsigned long)init_pid1_envp;
		t[3] = (unsigned long)init_argv[0];
	}

	init->tss.eip = (addr_t)switch_to_user_mode;
	init->tss.esp = init->tss.esp0;	/* kernel stack for do_switch */
	runnable(init);
	nr_processes++;
	return;

init_init__die:
	PANIC("unable to run init process.\n");
}
