/*
 * fnx/kernel/syscalls/mm_adv.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * FNX: mremap(25), msync(26), mincore(27), madvise(28).
 * The i386 mmap2() path was deleted in the pure x86-64 port; these four
 * x86-64 syscalls are implemented over the shared do_mmap()/do_munmap()
 * machinery.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/mman.h>
#include <fnx/mm.h>
#include <fnx/process.h>
#include <fnx/string.h>
#include <fnx/stdio.h>

#define MREMAP_MAYMOVE		0x1
#define MREMAP_FIXED		0x2

#define MADV_NORMAL		0x0
#define MADV_RANDOM		0x1
#define MADV_SEQUENTIAL		0x2
#define MADV_WILLNEED		0x3
#define MADV_DONTNEED		0x4

extern unsigned long paging64_pml4_phys(void);
extern unsigned long user_leaf64_in(unsigned long, unsigned long);

/*
 * mremap(old_address, old_size, new_size, flags, new_address)
 * Grow/shrink a vma in place when the adjacent region allows it; with
 * MREMAP_MAYMOVE, move the mapping to a new region, copying the present
 * pages so the contents survive (mallocng's realloc uses this).
 */
long sys_mremap(addr_t old_address, __size_t old_size, __size_t new_size, unsigned int flags, addr_t new_address)
{
	struct vma *vma, *n;
	addr_t page, start;
	unsigned long pml4;

	if(flags & ~(MREMAP_MAYMOVE | MREMAP_FIXED)) {
		return -EINVAL;
	}

	if(!(vma = find_vma_region(old_address))) {
		return -EFAULT;
	}
	if(old_size != vma->end - vma->start) {
		return -EINVAL;
	}

	/* shrink in place: just truncate the vma */
	if(new_size < old_size) {
		vma->end = vma->start + PAGE_ALIGN(new_size);
		do_munmap(vma->end, old_size - new_size);
		return vma->start;
	}

	/* grow in place: the next vma must not overlap */
	n = vma->next;
	if(!n || vma->end + PAGE_ALIGN(new_size) <= n->start) {
		vma->end = vma->start + PAGE_ALIGN(new_size);
		return vma->start;
	}

	if(!(flags & MREMAP_MAYMOVE)) {
		return -ENOMEM;
	}

	/* MAYMOVE: pick a new region, remap, then move the present pages */
	if(flags & MREMAP_FIXED) {
		start = new_address;
	} else {
		start = 0;	/* let do_mmap pick */
	}
	if((start = do_mmap(vma->inode, start, PAGE_ALIGN(new_size), vma->prot,
			vma->flags, vma->offset, vma->s_type, vma->o_mode, vma->object)) < 0) {
		return (long)start;
	}

	/* the process may run on its own pml4 (cr3_64); the kernel boot pml4
	 * is only the fallback before the first fork (like free_vma_pages). */
	pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4_phys();

	/* move every present user page from the old range to the new one:
	 * allocate a fresh page, copy the old contents, map it at the new
	 * address. (do_munmap(old) below will free the old pages.) */
	for(page = 0; page < old_size; page += PAGE_SIZE) {
		unsigned long old_phys, new_phys;
		struct page *pg;
		extern int map_user_page64_in(unsigned long, unsigned long, unsigned long, unsigned long);
		old_phys = user_leaf64_in(pml4, old_address + page);
		if(!old_phys) {
			continue;	/* not present: demand-faults later */
		}
		if(!(pg = get_free_page())) {
			continue;
		}
		new_phys = (unsigned long)pg->page << PAGE_SHIFT;
		memcpy_b(pg->data, (void *)(page_table[old_phys >> PAGE_SHIFT].data), PAGE_SIZE);
		map_user_page64_in(pml4, start + page, new_phys, 0x003);
	}

	do_munmap(old_address, old_size);
	return start;
}

/*
 * msync(addr, length, flags) - there is no MAP_SHARED file writeback in
 * this kernel (no fsop->mmap implementations), so nothing to flush:
 * validate the range and report success.
 */
int sys_msync(addr_t addr, __size_t length, int flags)
{
	if(!length) {
		return -EINVAL;
	}
	if(flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC)) {
		return -EINVAL;
	}
	if(!find_vma_region(addr)) {
		return -ENOMEM;
	}
	return 0;
}

/*
 * mincore(addr, length, vec) - report which pages are resident.
 * vec is one byte per page: 1 if present, 0 otherwise.
 */
int sys_mincore(addr_t addr, __size_t length, unsigned char *vec)
{
	unsigned long pml4, page;
	int errno, count;

	if(!length) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, vec, length / PAGE_SIZE))) {
		return errno;
	}
	if(!find_vma_region(addr)) {
		return -ENOMEM;
	}

	pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4_phys();

	count = 0;
	for(page = 0; page < length; page += PAGE_SIZE) {
		unsigned char present = user_leaf64_in(pml4, addr + page) ? 1 : 0;
		if((errno = check_user_area(VERIFY_WRITE, &vec[count], 1))) {
			return errno;
		}
		memcpy_b(&vec[count], &present, 1);
		count++;
	}

	return 0;
}

/*
 * madvise(addr, length, advice) - advice is a hint; accept the valid
 * range and ignore the advice (nothing to do for any of the standard
 * behaviors in this kernel).
 */
int sys_madvise(addr_t addr, __size_t length, int advice)
{
	if(advice < MADV_NORMAL || advice > MADV_DONTNEED) {
		return -EINVAL;
	}
	if(!length) {
		return 0;
	}
	if(!find_vma_region(addr)) {
		return -ENOMEM;
	}
	return 0;
}
