/*
 * fnx/include/fnx/syscalls.h
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_SYSCALLS_H
#define _FNX_SYSCALLS_H

#include <fnx/types.h>
#include <fnx/system.h>
#include <fnx/time.h>
#include <fnx/times.h>
#include <fnx/timeb.h>
#include <fnx/utime.h>
#include <fnx/statbuf.h>
#include <fnx/ustat.h>
#include <fnx/signal.h>
#include <fnx/utsname.h>
#include <fnx/resource.h>
#include <fnx/dirent.h>
#include <fnx/statfs.h>
#include <fnx/sigcontext.h>
#include <fnx/mman.h>
#include <fnx/ipc.h>

#define NR_SYSCALLS	(sizeof(syscall_table) / sizeof(void *))

int do_syscall(unsigned int, int, int, int, int, int, struct sigcontext);

int sys_epoll_create(int);
int sys_epoll_create1(int);
int sys_epoll_ctl(int, int, int, struct epoll_event *);
int sys_epoll_wait(int, struct epoll_event *, int, int);
int sys_epoll_pwait(int, struct epoll_event *, int, int, const unsigned long *, int);
int sys_exit(int);
void do_exit(int);
int sys_fork(int, int, int, int, int, struct sigcontext *);
int sys_clone(long, long, long, long, long, struct sigcontext *);
int sys_read(unsigned int, char *, int);
int sys_write(unsigned int, const char *, int);
int sys_open(const char *, int, __mode_t);
int sys_openat(int, const char *, int, __mode_t);
int sys_close(unsigned int);
int sys_waitpid(__pid_t, int *, int);
int sys_creat(const char *, __mode_t);
int sys_link(const char *, const char *);
int sys_unlink(const char *);
int sys_unlinkat(int, const char *, int);
int sys_execve(const char *, char **, char **, int, int, struct sigcontext *);
int sys_chdir(const char *);
int sys_time(__time_t *);
int sys_mknod(const char *, __mode_t, __dev_t);
int sys_chmod(const char *, __mode_t);
int sys_lchown(const char *, __uid_t, __gid_t);
int sys_lseek(unsigned int, __off_t, unsigned int);
int sys_getpid(void);
int sys_gettid(void);
int sys_futex(int *, int, int, const struct timespec *, int *, int);
int sys_mount(const char *, const char *, const char *, unsigned int, const void *);
int sys_umount(const char *);
int sys_setuid(__uid_t);
int sys_getuid(void);
int sys_stime(__time_t *);
int sys_alarm(unsigned int);
int sys_pause(void);
int sys_utime(const char *, struct utimbuf *);
int sys_access(const char *, __mode_t);
void sys_sync(void);
int sys_kill(__pid_t, __sigset_t);
int sys_tkill(int, __sigset_t);
int sys_tgkill(int, int, __sigset_t);
int sys_rename(const char *, const char *);
int sys_mkdir(const char *, __mode_t);
int sys_rmdir(const char *);
int sys_dup(unsigned int);
int sys_pipe(int *);
int sys_pipe2(int *, int);
int sys_times(struct tms *);
long sys_brk(addr_t);
int sys_setgid(__gid_t);
int sys_getgid(void);
int sys_geteuid(void);
int sys_getegid(void);
int sys_umount2(const char *, int);
int sys_ioctl(unsigned int, int, addr_t);
int sys_fcntl(unsigned int, int, addr_t);
int sys_setpgid(__pid_t, __pid_t);
int sys_umask(__mode_t);
int sys_chroot(const char *);
int sys_dup2(unsigned int, unsigned int);
int sys_dup3(unsigned int, unsigned int, int);
int sys_getpriority(int, int);
int sys_setpriority(int, int, int);
int sys_sched_yield(void);
int sys_sendfile(int, int, __off_t *, __size_t);
int sys_getrandom(char *, __size_t, unsigned int);
int sys_inotify_init(void);
int sys_inotify_init1(int);
int sys_inotify_add_watch(int, const char *, __u32);
int sys_inotify_rm_watch(int, int);
int sys_mkdirat(int, const char *, __mode_t);
int sys_getppid(void);
int sys_getpgrp(void);
int sys_setsid(void);
int sys_rt_sigaction(__sigset_t, const void *, void *, int);
int sys_setreuid(__uid_t, __uid_t);
int sys_setregid(__gid_t, __gid_t);
int sys_rt_sigsuspend(const void *, int);
int sys_rt_sigpending(void *, int);
int sys_sethostname(const char *, int);
int sys_setrlimit(int, const struct rlimit *);
int sys_getrlimit(int, struct rlimit *);
int sys_prlimit64(int, int, const struct rlimit64 *, struct rlimit64 *);
int sys_getrusage(int, struct rusage *);
int sys_gettimeofday(struct timeval *, struct timezone *);
int sys_settimeofday(const struct timeval *, const struct timezone *);
int sys_getgroups(__ssize_t, __gid_t *);
int sys_setgroups(__ssize_t, const __gid_t *);
int old_select(unsigned int *);
int sys_symlink(const char *, const char *);
int sys_readlink(const char *, char *, __size_t);
int sys_faccessat(int, const char *, __mode_t, int);
int sys_readlinkat(int, const char *, char *, __size_t);
int sys_reboot(int, int, int);
int old_mmap(struct mmap *);
int sys_munmap(addr_t, __size_t);
int sys_truncate(const char *, __off_t);
int sys_ftruncate(unsigned int, __off_t);
int sys_fchmod(unsigned int, __mode_t);
int sys_fchmodat(int, const char *, __mode_t);
int sys_fchown(unsigned int, __uid_t, __gid_t);
int sys_statfs(const char *, struct statfs *);
int sys_statfs64(const char *, struct fnx_statfs64 *);
int sys_fstatfs(unsigned int, struct statfs *);
int sys_fstatfs64(unsigned int, struct fnx_statfs64 *);
int sys_fstatfs(unsigned int, struct statfs *);
int sys_ioperm(unsigned int, unsigned int, int);
int sys_syslog(int, char *, int);
int sys_setitimer(int, const struct itimerval *, struct itimerval *);
int sys_getitimer(int, struct itimerval *);
void fill_new_stat(struct inode *, struct new_stat *);
int sys_newstat(const char *, struct new_stat *);
int sys_newlstat(const char *, struct new_stat *);
int sys_newfstat(unsigned int, struct new_stat *);
int sys_newfstatat(int, const char *, struct new_stat *, int);
int sys_uname(struct old_utsname *);
int sys_iopl(int, int, int, int, int, struct sigcontext *);
int sys_wait4(__pid_t, int *, int, struct rusage *);
int sys_sysinfo(struct sysinfo *);
#ifdef CONFIG_SYSVIPC
#endif /* CONFIG_SYSVIPC */
int sys_fsync(unsigned int);
int sys_rt_sigreturn(unsigned int, int, int, int, int, struct sigcontext *);
int sys_setdomainname(const char *, int);
int sys_newuname(struct new_utsname *);
int sys_mprotect(addr_t, __size_t, int);
int sys_rt_sigprocmask(int, const void *, void *, int);
int sys_set_tid_address(int *);
int sys_getpgid(__pid_t);
int sys_fchdir(unsigned int);
int sys_personality(unsigned int);
int sys_setfsuid(__uid_t);
int sys_setfsgid(__gid_t);
int sys_getdents(unsigned int, struct dirent *, unsigned int);
int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int sys_flock(unsigned int, int);
int sys_readv(int, struct iovec *, int);
int sys_writev(int, struct iovec *, int);
int sys_getsid(__pid_t);
int sys_fdatasync(int);
int sys_nanosleep(const struct timespec *, struct timespec *);
int sys_chown(const char *, __uid_t, __gid_t);
int sys_fchownat(int, const char *, __uid_t, __gid_t, int);
int sys_getcwd(char *, __size_t);
struct user_desc;
int sys_getdents64(unsigned int, struct dirent64 *, unsigned int);

#endif /* _FNX_SYSCALLS_H */
