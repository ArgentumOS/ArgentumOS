/*
 * fnx/include/fnx/system.h
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_SYSTEM_H
#define _FNX_SYSTEM_H

#define UTS_SYSNAME	"FNX"
#define UTS_NODENAME	"(none)"
#define UTS_RELEASE	"0.0.1"
#define UTS_DOMAINNAME	"(none)"

/*
 * THE USERLAND ABI, and it is musl's `struct sysinfo` because that is the layout the CALLER's
 * compiler made. This structure used to be the 32-bit Fiwix one — `int uptime`, `unsigned int
 * totalram`, a 22-byte pad to 64 bytes — and on a 64-BIT port that is not a harmless difference:
 * every field lands at the wrong offset for the caller. Measured, and this is how it was found:
 * NSProcessInfo's -physicalMemory answered 0 on a machine with RAM, because `totalram` was read out
 * of the middle of `loads`. The syscall wrote a correct number; the struct it wrote it through was
 * 32-bit baggage from a 32-bit kernel.
 *
 * `mem_unit` IS SET TO 1 BY sys_sysinfo, so `totalram` and `freeram` are in BYTES: with the unit
 * field zero — which is what it was — a reader following the convention multiplied by nothing, and
 * musl's sysconf(_SC_PHYS_PAGES) DIVIDED by nothing.
 */
struct sysinfo {
	unsigned long uptime;		/* seconds since boot */
	unsigned long loads[3];		/* load average (1, 5 and 15 minutes) */
	unsigned long totalram;		/* total usable main memory size */
	unsigned long freeram;		/* available memory size */
	unsigned long sharedram;	/* amount of shared memory */
	unsigned long bufferram;	/* amount of memory used by buffers */
	unsigned long totalswap;	/* total swap space size */
	unsigned long freeswap;		/* available swap space */
	unsigned short procs;		/* number of current processes */
	unsigned short pad;
	unsigned long totalhigh;
	unsigned long freehigh;
	unsigned mem_unit;		/* the unit the memory fields are counted in */
	char __reserved[256];		/* the ABI's tail, which the caller laid out */
};


#ifdef CUSTOM_SYSTEM_H
#include <fnx/custom_system.h>
#endif

#endif /* _FNX_SYSTEM_H */
