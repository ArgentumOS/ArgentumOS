/*
 * fnx/kernel/syscalls/brk.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/process.h>
#include <fnx/mm.h>
#include <fnx/mman.h>
#include <fnx/errno.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

long sys_brk(addr_t brk)
{
	addr_t newbrk;

#ifdef __DEBUG__
	printk("(pid %d) sys_brk(0x%08x) -> ", current->pid, brk);
#endif /*__DEBUG__ */

	if(!brk || brk < current->brk_lower) {
#ifdef __DEBUG__
		printk("0x%08x\n", current->brk);
#endif /*__DEBUG__ */
		return current->brk;
	}

	newbrk = PAGE_ALIGN(brk);
	if(newbrk == current->brk || newbrk < current->brk_lower) {
#ifdef __DEBUG__
		printk("0x%08x\n", current->brk);
#endif /*__DEBUG__ */
		return brk;
	}

	if(brk < current->brk) {
		do_munmap(newbrk, current->brk - newbrk);
		current->brk = brk;
#ifdef __DEBUG__
		printk("0x%08x\n", current->brk);
#endif /*__DEBUG__ */
		return current->brk;
	}
	if(!expand_heap(newbrk)) {
		current->brk = brk;
	} else {
		return -ENOMEM;
	}
#ifdef __DEBUG__
	printk("0x%08x\n", current->brk);
#endif /*__DEBUG__ */
	return current->brk;
}
