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
#ifdef __x86_64__
					/* Fiwix64: the buffer sits below the current
					 * stack vma start (stack growth). The 32-bit
					 * kernel let do_page_fault retry the CPL0
					 * access; here the low identity 2MB pages
					 * mask the not-present state, so pre-demand-map
					 * (and grow the stack vma) instead. */
					extern int fiwix64_fault_user_pages(addr_t, unsigned int);
					if((fiwix64_fault_user_pages(start, size)) < 0) {
						return -EFAULT;
					}
#endif /* __x86_64__ */
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


#ifdef __x86_64__
/* =====================================================================
 * Fiwix64 (native 64-bit port): the x86_64 syscall table (musl numbers).
 * Entries reuse the existing syscall functions with the same 5-args + sc
 * dispatch contract as the i386 table; the adapters below handle the ABI
 * differences (mmap offset in bytes, arch_prctl %fs, exit_group, the
 * rt_* clock syscalls). Unimplemented slots are NULL (-ENOSYS).
 * ===================================================================== */

/* x86-64 mmap(9): the 6th arg (r9) is the file offset in BYTES; the
 * dispatcher stashes it in sc->r9. The i386 mmap2 wants pages. */
long sys_mmap64(addr_t start, addr_t length, unsigned int prot,
	unsigned int user_flags, int fd, struct sigcontext *sc)
{
	extern long do_mmap2(addr_t, addr_t, unsigned int,
		unsigned int, int, addr_t);
	/* x86-64 mmap(9): the 6th arg (r9) is the offset in BYTES (stashed in
	 * sc->r9); mmap2 wants pages. */
	return do_mmap2(start, length, prot, user_flags, fd,
		((addr_t)sc->r9) >> PAGE_SHIFT);
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

int sys_clock_settime64(unsigned int clock_id, struct timespec *tp,
	long a3, long a4, long a5, struct sigcontext *sc)
{
	extern void set_system_time(__time_t);

	if(!IS_SUPERUSER) {
		return -EPERM;
	}
	if(check_user_area(VERIFY_READ, tp, sizeof(struct timespec))) {
		return -EFAULT;
	}
	if(clock_id == 1) {	/* CLOCK_MONOTONIC: not settable */
		return -EPERM;
	}
	/* CLOCK_REALTIME (0): set the system clock; set_system_time() also
	 * writes the RTC/CMOS so the new time survives reboot */
	set_system_time((__time_t)tp->tv_sec);
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
extern addr_t sys_shmat(int, char *, int, unsigned int *);
extern int sys_shmdt(char *);
extern int sys_shmget(key_t, __size_t, int);
extern int sys_shmctl(int, int, struct shmid_ds *);
extern int sys_msgget(key_t, int);
extern int sys_msgsnd(int, const void *, __size_t, int);
extern int sys_msgrcv(int, void *, __size_t, int, int);
extern int sys_msgctl(int, int, struct msqid_ds *);
#endif /* CONFIG_SYSVIPC */

extern int sys_setfsuid(__uid_t);
extern int sys_setfsgid(__gid_t);
extern int sys_getgroups(__ssize_t, __gid_t *);
extern int sys_setgroups(__ssize_t, const __gid_t *);
extern int sys_setresuid(__uid_t, __uid_t, __uid_t);
extern int sys_getresuid(__uid_t *, __uid_t *, __uid_t *);
extern int sys_setresgid(__gid_t, __gid_t, __gid_t);
extern int sys_getresgid(__gid_t *, __gid_t *, __gid_t *);
extern int sys_getsid(__pid_t);
extern int sys_rt_sigpending(void *, int);
extern int sys_rt_sigsuspend(const void *, int);
extern int sys_mknod(const char *, __mode_t, __dev_t);

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
	[13] = sys_rt_sigaction,	/* rt_sigaction */
	[14] = sys_rt_sigprocmask,	/* rt_sigprocmask */
	[15] = sys_rt_sigreturn,	/* rt_sigreturn */
	[16] = sys_ioctl,		/* ioctl */
	[19] = sys_readv,		/* readv */
	[20] = sys_writev,		/* writev */
	[21] = sys_access,		/* access */
	[22] = sys_pipe,		/* pipe */
	[23] = sys_select,		/* select */
	/* 24 sched_yield: not implemented */
	/* 25 mremap: not implemented */
	/* 26 msync: not implemented */
	/* 27 mincore: not implemented */
	/* 28 madvise: not implemented */
#ifdef CONFIG_SYSVIPC
	[29] = sys_shmget,		/* shmget */
	[30] = sys_shmat,		/* shmat (returns the address) */
	[31] = sys_shmctl,		/* shmctl */
#endif /* CONFIG_SYSVIPC */
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
	[115] = sys_getgroups,		/* getgroups */
	[116] = sys_setgroups,		/* setgroups */
	[117] = sys_setresuid,		/* setresuid */
	[118] = sys_getresuid,		/* getresuid */
	[119] = sys_setresgid,		/* setresgid */
	[120] = sys_getresgid,		/* getresgid */
	[121] = sys_getpgid,		/* getpgid */
	[122] = sys_setfsuid,		/* setfsuid */
	[123] = sys_setfsgid,		/* setfsgid */
	[124] = sys_getsid,		/* getsid */
	[127] = sys_rt_sigpending,	/* rt_sigpending */
	[130] = sys_rt_sigsuspend,	/* rt_sigsuspend */
	[133] = sys_mknod,		/* mknod */
	[137] = sys_statfs,		/* statfs */
	[138] = sys_fstatfs,		/* fstatfs */
	[158] = sys_arch_prctl64,	/* arch_prctl */
	[160] = sys_setrlimit,		/* setrlimit */
	[161] = sys_chroot,		/* chroot */
	[162] = sys_sync,		/* sync */
	/* 164 settimeofday: musl's settimeofday() uses clock_settime(227) */
	[165] = sys_mount,		/* mount */
	[166] = sys_umount,		/* umount2 */
	[169] = sys_reboot,		/* reboot */
	[170] = sys_sethostname,	/* sethostname */
	[171] = sys_setdomainname,	/* setdomainname */
	[172] = sys_iopl,		/* iopl */
	[173] = sys_ioperm,		/* ioperm */
	/* 186 gettid: not implemented */
	[200] = sys_tkill,		/* tkill */
	[217] = sys_getdents64,		/* getdents64 */
	[218] = sys_set_tid_address,	/* set_tid_address */
	[227] = sys_clock_settime64,	/* clock_settime */
	[228] = sys_clock_gettime64,	/* clock_gettime */
	[229] = sys_clock_getres64,	/* clock_getres */
	[231] = sys_exit_group64,	/* exit_group */
	[234] = sys_tgkill,		/* tgkill */
	[262] = sys_newfstatat,		/* newfstatat (musl stat/lstat/fstatat) */
	[267] = sys_readlinkat,		/* readlinkat (musl readlink, ls -l targets) */
	[269] = sys_faccessat,		/* faccessat (dash test -x/-r/-w, eaccess) */
};
#endif /* __x86_64__ */
