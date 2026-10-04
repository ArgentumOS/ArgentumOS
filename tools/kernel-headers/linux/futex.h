/*
 * Minimal Linux-uapi-compatible <linux/futex.h> for FNX userland builds
 * (see tools/kernel-headers).
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * Provides the futex command constants that libc++'s std::atomic wait support references
 * (libcxx/src/atomic.cpp on __linux__), and — since the CoreFoundation work — the four the swift
 * core libs need: libdispatch's src/shims/lock.h defines DLOCK_OWNER_MASK / DLOCK_WAITERS_BIT /
 * DLOCK_FAILED_TRYLOCK_BIT directly out of FUTEX_TID_MASK, FUTEX_WAITERS and FUTEX_OWNER_DIED at
 * lines 68-70, which is OUTSIDE its HAVE_FUTEX guard, so those three are needed even when futex is
 * switched off; and src/shims/lock.c uses FUTEX_PRIVATE_FLAG. All four values are the kernel's own.
 *
 * FNX HAS NO FUTEX SYSCALL, and that is worth knowing before reading this file as an enabler: a
 * runtime SYS_futex call just fails with ENOSYS and callers treat it as a spurious wake. These
 * constants therefore let the code COMPILE. The honest configuration for this platform is
 * libdispatch's own -DHAVE_FUTEX=0 -DHAVE_FUTEX_PI=0, which stops it choosing futex paths in the
 * first place — its lock.h defaults those to 1 on every __linux__.
 */
#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#define FUTEX_WAIT		0
#define FUTEX_WAKE		1
#define FUTEX_REQUEUE		3
#define FUTEX_WAIT_PRIVATE	128
#define FUTEX_WAKE_PRIVATE	129
#define FUTEX_REQUEUE_PRIVATE	131

/* The futex word's bit layout, and the flag that marks a PRIVATE operation. */
#define FUTEX_PRIVATE_FLAG	128
#define FUTEX_WAITERS		0x80000000
#define FUTEX_OWNER_DIED	0x40000000
#define FUTEX_TID_MASK		0x3fffffff
#define FUTEX_LOCK_PI	6
#define FUTEX_UNLOCK_PI	7
#define FUTEX_TRYLOCK_PI	8

#endif /* _LINUX_FUTEX_H */
