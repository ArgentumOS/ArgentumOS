/*
 * Minimal Linux-uapi-compatible <linux/sockios.h> for FNX userland builds
 * (see tools/kernel-headers).
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY IT EXISTS: libdispatch's src/event/event_epoll.c includes <linux/sockios.h> and then asks
 * SIOCOUTQ / SIOCINQ of a socket (event_epoll.c:534) to decide whether the dispatch monitor still
 * has bytes to drain. musl's sysroot carries no <linux/...> headers, so this directory is the tree's
 * substitute.
 *
 * THE TWO NAMES ARE ALIASES, WHICH IS THE KERNEL'S OWN SPELLING and the reason this file is four
 * lines rather than a copy: <linux/sockios.h> defines SIOCINQ as FIONREAD and SIOCOUTQ as TIOCOUTQ,
 * and musl's <sys/ioctl.h> already names both of those. Aliasing them keeps one source of truth for
 * the numbers instead of restating 0x541B and 0x5411 here, where they could drift from libc's.
 */
#ifndef _LINUX_SOCKIOS_H
#define _LINUX_SOCKIOS_H

#include <sys/ioctl.h>		/* FIONREAD, TIOCOUTQ */

#define SIOCINQ		FIONREAD
#define SIOCOUTQ	TIOCOUTQ	/* output queue size (not sent + not acked) */

#endif /* _LINUX_SOCKIOS_H */
