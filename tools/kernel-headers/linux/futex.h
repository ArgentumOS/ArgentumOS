/*
 * Minimal Linux-uapi-compatible <linux/futex.h> for FNX userland builds
 * (see tools/kernel-headers). Provides just the futex command constants
 * that libc++'s std::atomic wait support references (libcxx/src/atomic.cpp
 * on __linux__). FNX has no futex syscall; a runtime SYS_futex call just
 * fails with ENOSYS and callers treat it as a spurious wake.
 */
#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#define FUTEX_WAIT		0
#define FUTEX_WAKE		1
#define FUTEX_REQUEUE		3
#define FUTEX_WAIT_PRIVATE	128
#define FUTEX_WAKE_PRIVATE	129
#define FUTEX_REQUEUE_PRIVATE	131

#endif /* _LINUX_FUTEX_H */
