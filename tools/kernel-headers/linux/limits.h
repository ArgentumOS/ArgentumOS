/*
 * Minimal Linux-uapi-compatible <linux/limits.h> for FNX userland builds
 * (see tools/kernel-headers).
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY IT EXISTS: libdispatch's src/init.c includes <linux/limits.h> with the comment "for PATH_MAX"
 * and sizes a stack buffer with it (init.c:1126, `char path[PATH_MAX]`). musl's sysroot carries no
 * <linux/...> headers at all, so this directory is the tree's substitute, and until this file existed
 * libdispatch could not compile. The value is the kernel's own: 4096, "# chars in a path name
 * including nul" (linux/limits.h).
 */
#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

#define PATH_MAX        4096	/* # chars in a path name including nul */

#endif /* _LINUX_LIMITS_H */
