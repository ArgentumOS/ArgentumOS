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

/* FNX: the kernel image is PIC and executes from BOTH the identity alias
 * (phys == VA, reached through identity function pointers stored in kernel
 * DATA - syscall_table64, tty->output, file_operations, ...) and the
 * high-half alias (PAGE_OFFSET + phys, the direct call/return path). A
 * kernel symbol such as &sys_wait4 therefore resolves to a DIFFERENT value
 * depending on which alias the referencing code runs from, so a
 * sleep_address stored by one call chain may not match the same symbol
 * compared by another (a parent in wait4() is never woken when the child's
 * do_exit() compares its high-half &sys_wait4 against the parent's stored
 * identity value -> waitpid() hangs forever). Normalize to the identity
 * value before storing/hashing/comparing: values >= PAGE_OFFSET are the
 * high-half alias (subtract PAGE_OFFSET), everything below (identity
 * alias, kmalloc heap, user addresses) is already canonical. */
#define SLEEP_ADDR(a)	(((addr_t)(a) >= PAGE_OFFSET) ? \
			 ((addr_t)(a) - PAGE_OFFSET) : (addr_t)(a))

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
