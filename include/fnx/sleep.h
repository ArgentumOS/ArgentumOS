/*
 * fnx/include/fnx/sleep.h
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_SLEEP_H
#define _FNX_SLEEP_H

#include <fnx/process.h>
#include <fnx/linker.h>

/* FNX (single-alias): the kernel image executes from ONE address space -
 * the high-half alias (PAGE_OFFSET + phys) - because the boot stub
 * re-biases every absolute pointer in kernel DATA to the high half (see
 * rebase_image_data in kernel/boot64/paging64.c). Sleep addresses are plain
 * kernel pointers; SLEEP_ADDR is kept as an identity macro so the
 * sleep()/wakeup()/wait4() code reads the same everywhere. */
#define SLEEP_ADDR(a)	((addr_t)(a))

#define AREA_BH			0x00000001
#define AREA_CALLOUT		0x00000002
#define AREA_TTY_READ		0x00000004
#define AREA_SERIAL_READ	0x00000008

extern struct proc *proc_run_head;

struct resource {
	char locked;
	char wanted;
};

void runnable(struct proc *);
void not_runnable(struct proc *, int);
int sleep(void *, int);
void wakeup(void *);
void wakeup_proc(struct proc *);

void lock_resource(struct resource *);
void unlock_resource(struct resource *);
int can_lock_area(unsigned int);
int unlock_area(unsigned int);

void sleep_init(void);

#endif /* _FNX_SLEEP_H */
