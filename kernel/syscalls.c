/*
 * fiwix/kernel/syscalls.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fiwix/asm.h>
#include <fiwix/types.h>
#include <fiwix/syscalls.h>
#include <fiwix/mm.h>
#include <fiwix/stat.h>
#include <fiwix/errno.h>
#include <fiwix/string.h>
#include <fiwix/timer.h>
#include <fiwix/kernel.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

static int verify_address(int type, const void *addr, unsigned int size)
{
	struct vma *vma;
	addr_t start;
	unsigned int gs;

	/* no need to verify anything if the caller is the kernel */
	GET_GS(gs);
	if(gs == KERNEL_DS) {
		return 0;
	}

	/*
	 * The vma_table of the INIT process is not setup yet when it
	 * calls sys_open() and sys_execve() from init_trampoline(),
	 * but these calls are trusted.
	 */
	if(!current->vma_table) {
		return 0;
	}

	start = (addr_t)addr;
	if(!(vma = find_vma_region(start))) {
		/*
		 * We need to check here if addr looks like a possible
		 * non-existent user stack address. If so, just return 0
		 * and let 'do_page_fault()' to handle the imminent page
		 * fault as soon as the kernel will try to access it.
		 */
		vma = current->vma_table->prev;
		if(vma) {
			if(vma->s_type == P_STACK) {
				if(start < vma->start && start > vma->prev->end) {
					return 0;
				}
			}
		}
		return -EFAULT;
	}

	for(;;) {
		if(type == VERIFY_WRITE) {
			if(!(vma->prot & PROT_WRITE)) {
				return -EFAULT;
			}
		} else {
			if(!(vma->prot & PROT_READ)) {
				return -EFAULT;
			}
		}
		if(start + size <= vma->end) {
			break;
		}
		if(!(vma = find_vma_region(vma->end))) {
			return -EFAULT;
		}
	}

#ifdef __x86_64__
	/* Fiwix64 (M6): the low identity 2MB pages mask the not-present state
	 * for CPL0 accesses, so the kernel's copy would read/write garbage
	 * (phys = vaddr, beyond RAM) instead of faulting. Pre-demand-map the
	 * pages so the copy hits a real U/S RAM page. */
	{
		extern int fiwix64_fault_user_pages(addr_t, unsigned int);
		if((fiwix64_fault_user_pages(start, size)) < 0) {
			return -EFAULT;
		}
	}
#endif /* __x86_64__ */

	return 0;
}

void free_name(const char *name)
{
	kfree((addr_t)name);
}

/*
 * This function has two objectives:
 *
 * 1. verifies the memory address validity of the char pointer supplied by the
 *    user and, at the same time, limits its length to PAGE_SIZE (4096) bytes.
 * 2. creates a copy of 'string' in the kernel data space.
 */
int malloc_name(const char *string, char **name)
{
	char *b;
	int n, errno;

	if((errno = verify_address(PROT_READ, string, 0))) {
		return errno;
	}

	if(!(b = (char *)kmalloc(PAGE_SIZE))) {
		return -ENOMEM;
	}
	*name = b;
	for(n = 0; n < PAGE_SIZE; n++) {
		if(!(*b = *string)) {
			return 0;
		}
		b++;
		string++;
	}

	free_name(*name);
	return -ENAMETOOLONG;
}

int check_user_permission(struct inode *i)
{
	if(!IS_SUPERUSER) {
		if(current->euid != i->i_uid) {
			return 1;
		}
	}
	return 0;
}

int check_group(struct inode *i)
{
	int n;
	__gid_t gid;

	if(current->flags & PF_USEREAL) {
		gid = current->gid;
	} else {
		gid = current->egid;
	}

	if(i->i_gid == gid) {
		return 0;
	}

	for(n = 0; n < NGROUPS_MAX; n++) {
		if(current->groups[n] == -1) {
			break;
		}
		if(current->groups[n] == i->i_gid) {
			return 0;
		}
	}
	return 1;
}

int check_user_area(int type, const void *addr, unsigned int size)
{
	return verify_address(type, addr, size);
}

int check_permission(int mask, struct inode *i)
{
	__uid_t uid;

	if(current->flags & PF_USEREAL) {
		uid = current->uid;
	} else {
		uid = current->euid;
	}

	if(mask & TO_EXEC) {
		if(!(i->i_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
			return -EACCES;
		}
	}
	if(uid == 0) {
		return 0;
	}
	if(i->i_uid == uid) {
		if((((i->i_mode >> 6) & 7) & mask) == mask) {
			return 0;
		}
	}
	if(!check_group(i)) {
		if((((i->i_mode >> 3) & 7) & mask) == mask) {
			return 0;
		}
	}
	if(((i->i_mode & 7) & mask) == mask) {
		return 0;
	}

	return -EACCES;
}


/* Linux 2.0 i386 ABI system call (plus some from Linux 2.2 and Linux 2.4) */
void *syscall_table[] = {
	NULL,				/* 0 */	/* sys_setup (-ENOSYS) */
	sys_exit,
	sys_fork,
	sys_read,
	sys_write,
	sys_open,			/* 5 */
	sys_close,
	sys_waitpid,
	sys_creat,
	sys_link,
	sys_unlink,			/* 10 */
	sys_execve,
	sys_chdir,
	sys_time,
	sys_mknod,
	sys_chmod,			/* 15 */
	sys_lchown,
	NULL,					/* sys_break (-ENOSYS) */
	sys_stat,
	sys_lseek,
	sys_getpid,			/* 20 */
	sys_mount,
	sys_umount,
	sys_setuid,
	sys_getuid,
	sys_stime, 			/* 25 */
	NULL,	/* sys_ptrace */
	sys_alarm,
	sys_fstat,
	sys_pause,
	sys_utime,			/* 30 */
	NULL,					/* sys_stty (-ENOSYS) */
	NULL,					/* sys_gtty (-ENOSYS) */
	sys_access,
	NULL,	/* sys_nice */
	sys_ftime,			/* 35 */
	sys_sync,
	sys_kill,
	sys_rename,
	sys_mkdir,
	sys_rmdir,			/* 40 */
	sys_dup,
	sys_pipe,
	sys_times,
	NULL,	/* sys_prof */
	sys_brk,			/* 45 */
	sys_setgid,
	sys_getgid,
	sys_signal,
	sys_geteuid,
	sys_getegid,			/* 50 */
	NULL,	/* sys_acct */
	sys_umount2,
	NULL,					/* sys_lock (-ENOSYS) */
	sys_ioctl,
	sys_fcntl,			/* 55 */
	NULL,					/* sys_mpx (-ENOSYS) */
	sys_setpgid,
	NULL,					/* sys_ulimit (-ENOSYS) */
	sys_olduname,
	sys_umask,			/* 60 */
	sys_chroot,
	sys_ustat,
	sys_dup2,
	sys_getppid,
	sys_getpgrp,			/* 65 */
	sys_setsid,
	sys_sigaction,
	sys_sgetmask,
	sys_ssetmask,
	sys_setreuid,			/* 70 */
	sys_setregid,
	sys_sigsuspend,
	sys_sigpending,
	sys_sethostname,
	sys_setrlimit,			/* 75 */
	sys_getrlimit,
	sys_getrusage,
	sys_gettimeofday,
	sys_settimeofday,
	sys_getgroups,			/* 80 */
	sys_setgroups,
	old_select,
	sys_symlink,
	sys_lstat,
	sys_readlink,			/* 85 */
	NULL,	/* sys_uselib */
	NULL,	/* sys_swapon */
	sys_reboot,
	NULL,	/* old_readdir */
	old_mmap,			/* 90 */
	sys_munmap,
	sys_truncate,
	sys_ftruncate,
	sys_fchmod,
	sys_fchown,			/* 95 */
	NULL,	/* sys_getpriority */
	NULL,	/* sys_setpriority */
	NULL,					/* sys_profil (-ENOSYS) */
	sys_statfs,
	sys_fstatfs,			/* 100 */
	sys_ioperm,
	sys_socketcall,
	sys_syslog,
	sys_setitimer,
	sys_getitimer,			/* 105 */
	sys_newstat,
	sys_newlstat,
	sys_newfstat,
	sys_uname,
	sys_iopl,			/* 110 */
	NULL,	/* sys_vhangup */
	NULL,					/* sys_idle (-ENOSYS) */
	NULL,	/* sys_vm86old */
	sys_wait4,
	NULL,	/* sys_swapoff */	/* 115 */
	sys_sysinfo,
#ifdef CONFIG_SYSVIPC
	sys_ipc,
#else
	NULL,	/* sys_ipc */
#endif /* CONFIG_SYSVIPC */
	sys_fsync,
	sys_sigreturn,
	NULL,	/* sys_clone */		/* 120 */
	sys_setdomainname,
	sys_newuname,
	NULL,	/* sys_modify_ldt */
	NULL,	/* sys_adjtimex */
	sys_mprotect,			/* 125 */
	sys_sigprocmask,
	NULL,	/* sys_create_module */
	NULL,	/* sys_init_module */
	NULL,	/* sys_delete_module */
	NULL,	/* sys_get_kernel_syms */	/* 130 */
	NULL,	/* sys_quotactl */
	sys_getpgid,
	sys_fchdir,
	NULL,	/* sys_bdflush */
	NULL,	/* sys_sysfs */		/* 135 */
	sys_personality,
	NULL,					/* afs_syscall (-ENOSYS) */
	sys_setfsuid,
	sys_setfsgid,
	sys_llseek,			/* 140 */
	sys_getdents,
	sys_select,
	sys_flock,
	NULL,	/* sys_msync */
	sys_readv,			/* 145 */
	sys_writev,
	sys_getsid,
	sys_fdatasync,
	NULL,	/* sys_sysctl */
	NULL,	/* sys_mlock */		/* 150 */
	NULL,	/* sys_munlock */
	NULL,	/* sys_mlockall */
	NULL,	/* sys_munlockall */
	NULL,	/* sys_sched_setparam */
	NULL,	/* sys_sched_getparam */	/* 155 */
	NULL,	/* sys_sched_setscheduler */
	NULL,	/* sys_sched_getscheduler */
	NULL,	/* sys_sched_yield */
	NULL,	/* sys_sched_get_priority_max */
	NULL,	/* sys_sched_get_priority_min */	/* 160 */
	NULL,	/* sys_sched_rr_get_interval */
	sys_nanosleep,
	NULL,	/* sys_mremap */
	NULL,
	NULL,				/* 165 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 170 */
	NULL,
	NULL,
	NULL,				/* sys_rt_sigreturn */	/* 173 */
	sys_rt_sigaction,			/* 174 */
	sys_rt_sigprocmask,			/* 175 */
	sys_rt_sigpending,			/* 176 */
	NULL,
	NULL,
	sys_rt_sigsuspend,			/* 179 */
	NULL,				/* 180 */
	NULL,
	sys_chown,
	sys_getcwd,
	NULL,
	NULL,				/* 185 */
	NULL,
	NULL,
	NULL,
	NULL,
	sys_fork,			/* 190 (sys_vfork) */
	NULL,
#if defined(CONFIG_MMAP2) || defined(__x86_64__)
	sys_mmap2,
#else
	NULL,
#endif
	sys_truncate64,
	sys_ftruncate64,
	sys_stat64,			/* 195 */
	sys_lstat64,
	sys_fstat64,
	NULL,
	NULL,
	NULL,				/* 200 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 205 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 210 */
	NULL,
	sys_chown32,
	NULL,
	NULL,
	NULL,				/* 215 */
	NULL,
	NULL,
	NULL,
	NULL,
	sys_getdents64,			/* 220 */
	sys_fcntl64,
	NULL,
	NULL,
	NULL,
	NULL,				/* 225 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 230 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 235 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 240 */
	NULL,
	NULL,
#ifdef __x86_64__
	sys_set_thread_area,
#else
	NULL,
#endif
	NULL,
	NULL,				/* 245 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 250 */
	NULL,
	sys_exit,			/* sys_exit_group */	/* 252 */
	NULL,
	NULL,
	NULL,				/* 255 */
	NULL,
	NULL,
	sys_set_tid_address,			/* 258 */
	NULL,
	NULL,				/* 260 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 265 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,				/* 270 */
	sys_utimes,
};

static void do_bad_syscall(unsigned int num)
{
#ifdef __DEBUG__
	printk("***** (pid %d) system call %d not supported yet *****\n", current->pid, num);
#endif /*__DEBUG__ */
}

/*
 * The argument 'struct sigcontext' is needed because there are some system
 * calls (such as sys_iopl and sys_fork) that need to get information from
 * certain registers (EFLAGS and ESP). The rest of system calls will ignore
 * such extra argument.
 */
#ifdef CONFIG_SYSCALL_6TH_ARG
int do_syscall(unsigned int num, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, struct sigcontext sc)
#else
int do_syscall(unsigned int num, int arg1, int arg2, int arg3, int arg4, int arg5, struct sigcontext sc)
#endif /* CONFIG_SYSCALL_6TH_ARG */
{
	int (*sys_func)(int, ...);

	if(num > NR_SYSCALLS) {
		do_bad_syscall(num);
		return -ENOSYS;
	}
	sys_func = syscall_table[num];
	if(!sys_func) {
		do_bad_syscall(num);
		return -ENOSYS;
	}
	current->sp = (addr_t)&sc;
#ifdef CONFIG_SYSCALL_6TH_ARG
	return sys_func(arg1, arg2, arg3, arg4, arg5, arg6, &sc);
#else
	return sys_func(arg1, arg2, arg3, arg4, arg5, &sc);
#endif /* CONFIG_SYSCALL_6TH_ARG */
}

#ifdef __x86_64__
/* =====================================================================
 * Fiwix64 (native 64-bit port): the x86_64 syscall table (musl numbers).
 * Entries reuse the existing syscall functions with the same 5-args + sc
 * dispatch contract as the i386 table; the adapters below handle the ABI
 * differences (mmap offset in bytes, arch_prctl %fs, exit_group, the
 * rt_* clock syscalls). Unimplemented slots are NULL (-ENOSYS).
 * ===================================================================== */

/* x86-64 mmap(9): the 6th arg (r9) is the file offset in BYTES; the
 * dispatcher stashes it in sc->ebp. The i386 mmap2 wants pages. */
long sys_mmap64(addr_t start, addr_t length, unsigned int prot,
	unsigned int user_flags, int fd, struct sigcontext *sc)
{
	extern long do_mmap2(addr_t, addr_t, unsigned int,
		unsigned int, int, addr_t);
	/* x86-64 mmap(9): the 6th arg (r9) is the offset in BYTES (stashed in
	 * sc->ebp); mmap2 wants pages. */
	return do_mmap2(start, length, prot, user_flags, fd,
		((addr_t)sc->ebp) >> PAGE_SHIFT);
}

/* arch_prctl(158): x86-64 TLS uses the %fs base MSR. ARCH_SET_FS stores
 * the thread pointer; ARCH_GET_FS reads it back. */
extern void fiwix64_set_fs_base(unsigned long);

int sys_arch_prctl64(unsigned int code, unsigned long addr,
	long a3, long a4, long a5, struct sigcontext *sc)
{
	switch(code) {
	case 0x1002:	/* ARCH_SET_FS */
		current->fs_base = addr;
		fiwix64_set_fs_base(addr);
		return 0;
	case 0x1003:	/* ARCH_GET_FS */
		if(!addr) {
			return -EINVAL;
		}
		if(check_user_area(VERIFY_WRITE, (void *)addr, sizeof(unsigned long))) {
			return -EFAULT;
		}
		*(unsigned long *)addr = current->fs_base;
		return 0;
	}
	return -EINVAL;
}

int sys_clock_gettime64(unsigned int clock_id, struct timespec *tp,
	long a3, long a4, long a5, struct sigcontext *sc)
{
	if(check_user_area(VERIFY_WRITE, tp, sizeof(struct timespec))) {
		return -EFAULT;
	}
	if(clock_id == 1) {	/* CLOCK_MONOTONIC */
		tp->tv_sec = CURRENT_TICKS / HZ;
		tp->tv_nsec = (CURRENT_TICKS % HZ) * (1000000000 / HZ);
	} else {		/* CLOCK_REALTIME (0) */
		tp->tv_sec = CURRENT_TIME;
		tp->tv_nsec = ((CURRENT_TICKS % HZ) * (1000000000 / HZ)) + (gettimeoffset() * 1000);
	}
	return 0;
}

int sys_clock_getres64(unsigned int clock_id, struct timespec *tp,
	long a3, long a4, long a5, struct sigcontext *sc)
{
	if(check_user_area(VERIFY_WRITE, tp, sizeof(struct timespec))) {
		return -EFAULT;
	}
	tp->tv_sec = 0;
	tp->tv_nsec = 1000000000 / HZ;
	return 0;
}

int sys_exit_group64(int code, long a2, long a3, long a4, long a5, struct sigcontext *sc)
{
	extern int sys_exit(int);
	return sys_exit(code);
}

#ifdef CONFIG_SYSVIPC
extern int sys_semget(key_t, int);
extern int sys_semop(int, struct sembuf *, int);
extern int sys_semctl(int, int, int, void *);
extern int sys_shmdt(char *);
extern int sys_msgget(key_t, int);
extern int sys_msgsnd(int, const void *, __size_t, int);
extern int sys_msgrcv(int, void *, __size_t, int, int);
extern int sys_msgctl(int, int, struct msqid_ds *);
#endif /* CONFIG_SYSVIPC */

/* x86_64 syscall numbers (the subset Fiwix implements); NULL = -ENOSYS */
void *syscall_table64[] = {
	[0]  = sys_read,		/* read */
	[1]  = sys_write,		/* write */
	[2]  = sys_open,		/* open */
	[3]  = sys_close,		/* close */
	[4]  = sys_newstat,		/* stat */
	[5]  = sys_newfstat,		/* fstat */
	[6]  = sys_newlstat,		/* lstat */
	/* 7 poll: not implemented */
	[8]  = sys_lseek,		/* lseek */
	[9]  = sys_mmap64,		/* mmap */
	[10] = sys_mprotect,		/* mprotect */
	[11] = sys_munmap,		/* munmap */
	[12] = sys_brk,			/* brk */
	/* 13 rt_sigaction, 14 rt_sigprocmask, 15 rt_sigreturn: pending */
	[16] = sys_ioctl,		/* ioctl */
	[19] = sys_readv,		/* readv */
	[20] = sys_writev,		/* writev */
	[21] = sys_access,		/* access */
	[22] = sys_pipe,		/* pipe */
	[23] = sys_select,		/* select */
	/* 24 sched_yield: not implemented */
	/* 25 mremap: not implemented */
	/* 26 msync: not implemented */
	[32] = sys_dup,			/* dup */
	[33] = sys_dup2,		/* dup2 */
	[34] = sys_pause,		/* pause */
	[35] = sys_nanosleep,		/* nanosleep */
	[36] = sys_getitimer,		/* getitimer */
	[37] = sys_alarm,		/* alarm */
	[38] = sys_setitimer,		/* setitimer */
	[39] = sys_getpid,		/* getpid */
	[57] = sys_fork,		/* fork */
	[58] = sys_fork,		/* vfork -> fork */
	[59] = sys_execve,		/* execve */
	[60] = sys_exit,		/* exit */
	[61] = sys_wait4,		/* wait4 */
	[62] = sys_kill,		/* kill */
	[63] = sys_newuname,		/* uname */
#ifdef CONFIG_SYSVIPC
	[64] = sys_semget,		/* semget */
	[65] = sys_semop,		/* semop */
	[66] = sys_semctl,		/* semctl */
	[67] = sys_shmdt,		/* shmdt */
	[68] = sys_msgget,		/* msgget */
	[69] = sys_msgsnd,		/* msgsnd */
	[70] = sys_msgrcv,		/* msgrcv */
	[71] = sys_msgctl,		/* msgctl */
#endif /* CONFIG_SYSVIPC */
	[72] = sys_fcntl,		/* fcntl */
	[73] = sys_flock,		/* flock */
	[74] = sys_fsync,		/* fsync */
	[75] = sys_fdatasync,		/* fdatasync */
	[76] = sys_truncate,		/* truncate */
	[77] = sys_ftruncate,		/* ftruncate */
	[78] = sys_getdents,		/* getdents */
	[79] = sys_getcwd,		/* getcwd */
	[80] = sys_chdir,		/* chdir */
	[81] = sys_fchdir,		/* fchdir */
	[82] = sys_rename,		/* rename */
	[83] = sys_mkdir,		/* mkdir */
	[84] = sys_rmdir,		/* rmdir */
	[85] = sys_creat,		/* creat */
	[86] = sys_link,		/* link */
	[87] = sys_unlink,		/* unlink */
	[88] = sys_symlink,		/* symlink */
	[89] = sys_readlink,		/* readlink */
	[90] = sys_chmod,		/* chmod */
	[91] = sys_fchmod,		/* fchmod */
	[92] = sys_chown,		/* chown */
	[93] = sys_fchown,		/* fchown */
	[94] = sys_lchown,		/* lchown */
	[95] = sys_umask,		/* umask */
	[96] = sys_gettimeofday,	/* gettimeofday */
	[97] = sys_getrlimit,		/* getrlimit */
	[98] = sys_getrusage,		/* getrusage */
	[99] = sys_sysinfo,		/* sysinfo */
	[100] = sys_times,		/* times */
	[102] = sys_getuid,		/* getuid */
	[104] = sys_getgid,		/* getgid */
	[105] = sys_setuid,		/* setuid */
	[106] = sys_setgid,		/* setgid */
	[107] = sys_geteuid,		/* geteuid */
	[108] = sys_getegid,		/* getegid */
	[109] = sys_setpgid,		/* setpgid */
	[110] = sys_getppid,		/* getppid */
	[111] = sys_getpgrp,		/* getpgrp */
	[112] = sys_setsid,		/* setsid */
	[113] = sys_setreuid,		/* setreuid */
	[114] = sys_setregid,		/* setregid */
	/* 117-120 setresuid/getresuid/setresgid/getresgid: not implemented */
	[121] = sys_getpgid,		/* getpgid */
	[137] = sys_statfs,		/* statfs */
	[138] = sys_fstatfs,		/* fstatfs */
	[158] = sys_arch_prctl64,	/* arch_prctl */
	[160] = sys_setrlimit,		/* setrlimit */
	[161] = sys_chroot,		/* chroot */
	[162] = sys_sync,		/* sync */
	[165] = sys_mount,		/* mount */
	[166] = sys_umount,		/* umount2 */
	[169] = sys_reboot,		/* reboot */
	[170] = sys_sethostname,	/* sethostname */
	[171] = sys_setdomainname,	/* setdomainname */
	[172] = sys_iopl,		/* iopl */
	[173] = sys_ioperm,		/* ioperm */
	/* 186 gettid: not implemented */
	[217] = sys_getdents64,		/* getdents64 */
	[218] = sys_set_tid_address,	/* set_tid_address */
	[228] = sys_clock_gettime64,	/* clock_gettime */
	[229] = sys_clock_getres64,	/* clock_getres */
	[231] = sys_exit_group64,	/* exit_group */
};
#endif /* __x86_64__ */
