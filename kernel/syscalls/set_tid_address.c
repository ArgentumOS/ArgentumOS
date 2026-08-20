/*
 * fiwix/kernel/syscalls/set_tid_address.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64 (M6 userland): set_tid_address() (Linux #258). musl's fork()
 * child calls this unconditionally. Single-threaded, the "thread id" is
 * the pid and clear_child_tid is only meaningful with clone()+futex(),
 * which Fiwix doesn't implement yet - so tidptr is deliberately ignored.
 */

#include <fiwix/process.h>

int sys_set_tid_address(int *tidptr)
{
	(void)tidptr;
	return current->pid;
}
