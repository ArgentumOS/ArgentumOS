/*
 * fnx/mm/mmap.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Portions Copyright 2024, Greg Haerr.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/mm.h>
#include <fnx/fs.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/process.h>
#include <fnx/mman.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/shm.h>

void merge_vma_regions(struct vma *, struct vma *);

void show_vma_regions(struct proc *p)
{
	__ino_t inode;
	int major, minor;
	char *section;
	char r, w, x, f;
	struct vma *vma;
	unsigned int n;
	int count;

	if(!p) {
		return;
	}

	vma = p->vma_table;
	n = 0;
	printk("num  address range         flag offset     dev   inode      mod section cnt\n");
	printk("---- --------------------- ---- ---------- ----- ---------- --- ------- ----\n");

	while(vma) {
		r = vma->prot & PROT_READ ? 'r' : '-';
		w = vma->prot & PROT_WRITE ? 'w' : '-';
		x = vma->prot & PROT_EXEC ? 'x' : '-';
		if(vma->flags & MAP_SHARED) {
			f = 's';
		} else if(vma->flags & MAP_PRIVATE) {
			f = 'p';
		} else {
			f = '-';
		}
		switch(vma->s_type) {
			case P_TEXT:	section = "text ";
					break;
			case P_DATA:	section = "data ";
					break;
			case P_BSS:	section = "bss  ";
					break;
			case P_HEAP:	section = "heap ";
					break;
			case P_STACK:	section = "stack";
					break;
			case P_MMAP:	section = "mmap ";
					break;
#ifdef CONFIG_SYSVIPC
			case P_SHM:	section = "shm  ";
					break;
#endif /* CONFIG_SYSVIPC */
			default:
				section = NULL;
				break;
		}
		inode = major = minor = count = 0;
		if(vma->inode) {
			inode = vma->inode->inode;
			major = MAJOR(vma->inode->dev);
			minor = MINOR(vma->inode->dev);
			count = vma->inode->count;
		}
		printk("[%02d] 0x%08x-0x%08x %c%c%c%c 0x%08x %02d:%02d %- 10u <%d> [%s]  (%d)\n", n, vma->start, vma->end, r, w, x, f, vma->offset, major, minor, inode, vma->o_mode, section, count);
		vma = vma->next;
		n++;
	}
	if(!n) {
		printk("[no vma regions]\n");
	}
}

/* insert a vma structure into vma_table sorted by address */
static void insert_vma_region(struct vma *vma)
{
	struct vma *vmat;

	vmat = current->vma_table;

	while(vmat) {
		if(vmat->start > vma->start) {
			break;
		}
		vmat = vmat->next;
	}

	if(!vmat) {
		/* append */
		vma->prev = current->vma_table->prev;
		current->vma_table->prev->next = vma;
		current->vma_table->prev = vma;
	} else {
		/* insert */
		vma->prev = vmat->prev;
		vma->next = vmat;
		if(vmat == current->vma_table) {
			/* insert in the head */
			current->vma_table = vma;
		} else {
			/* insert in the middle */
			vmat->prev->next = vma;
		}
		vmat->prev = vma;
	}
	if(vma->inode) {
		vma->inode->count++;
	}

	if(vma != vma->prev && vma->start >= vma->prev->start && vma->start <= vma->prev->end) {
		merge_vma_regions(vma->prev, vma);
	}
}

static void add_vma_region(struct vma *vma)
{
	unsigned int flags;

	SAVE_FLAGS(flags); CLI();
	if(!current->vma_table) {
		current->vma_table = vma;
		current->vma_table->prev = vma;
		if(vma->inode) {
			vma->inode->count++;
		}
	} else {
		insert_vma_region(vma);
	}
	RESTORE_FLAGS(flags);
}

static void del_vma_region(struct vma *vma)
{
	unsigned int flags;
	struct vma *tmp;

	tmp = vma;

	if(!vma->next && !vma->prev) {
		printk("WARNING: %s(): trying to delete an unexistent vma region (%x).\n", __FUNCTION__, vma->start);
		return;
	}

	SAVE_FLAGS(flags); CLI();
	if(vma->next) {
		vma->next->prev = vma->prev;
	}
	if(vma->prev) {
		if(vma != current->vma_table) {
			vma->prev->next = vma->next;
		}
	}
	if(!vma->next) {
		current->vma_table->prev = vma->prev;
	}
	if(vma == current->vma_table) {
		current->vma_table = vma->next;
	}
	RESTORE_FLAGS(flags);

	kfree((addr_t)tmp);
}

static int can_be_merged(struct vma *a, struct vma *b)
{
	if((a->end == b->start) &&
	   (a->prot == b->prot) &&
	   (a->flags == b->flags) &&
	   (a->offset == b->offset) &&
	   (a->s_type == b->s_type) &&
#ifdef CONFIG_SYSVIPC
	   (a->s_type != P_SHM) &&
#endif /* CONFIG_SYSVIPC */
	   (a->inode == b->inode)) {
		return 1;
	}

	return 0;
}

static int free_vma_region(struct vma *vma, addr_t start, __ssize_t length)
{
	struct vma *new;

	if(start + length < vma->end) {
		if(!(new = (struct vma *)kmalloc(sizeof(struct vma)))) {
			return -ENOMEM;
		}
		memset_b(new, 0, sizeof(struct vma));
		new->start = start + length;
		new->end = vma->end;
		new->prot = vma->prot;
		new->flags = vma->flags;
		new->offset = vma->offset;
		new->s_type = vma->s_type;
		new->inode = vma->inode;
		new->o_mode = vma->o_mode;
	} else {
		new = NULL;
	}

	if(vma->start == start) {
		if(vma->inode) {
			iput(vma->inode);
		}
		del_vma_region(vma);
	} else {
		vma->end = start;
	}

	if(new) {
		add_vma_region(new);
	}

	return 0;
}

/* this assumes that vma_table is sorted by address */
void merge_vma_regions(struct vma *a, struct vma *b)
{
	struct vma *new;

	if(b->start == a->end) {
		if(can_be_merged(a, b)) {
			a->end = b->end;
			del_vma_region(b);
			return;
		}
	}

	if((b->start < a->end)) {
		if(!(new = (struct vma *)kmalloc(sizeof(struct vma)))) {
			return;
		}
		memset_b(new, 0, sizeof(struct vma));
		new->start = b->end;
		new->end = a->end;
		new->prot = a->prot;
		new->flags = a->flags;
		new->offset = a->offset;
		new->s_type = a->s_type;
		new->inode = a->inode;
		new->o_mode = a->o_mode;
		free_vma_pages(a, b->start, b->end - b->start);
		invalidate_tlb();
		a->end = b->start;
		if(a->start == a->end) {
			del_vma_region(a);
		}
		if(new->start >= new->end) {
			kfree((addr_t)new);
		} else {
			insert_vma_region(new);
		}
	}
}

void free_vma_pages(struct vma *vma, addr_t start, __size_t length)
{
#ifdef __x86_64__
	/* FNX (native-MM): operate on the ACTIVE pml4 - no 2-level shadow,
	 * no pte-table refcounting, so the table-empty kfree (the source of
	 * the pde-32 table double-grant) is gone. */
	unsigned int offset;
	unsigned long n, pml4, leaf;
	struct page *pg;

	extern unsigned long user_leaf64_in(unsigned long, unsigned long);
	extern int unmap_user_page64_in(unsigned long, unsigned long);
	extern unsigned long paging64_pml4(void);

	pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4();

	for(n = 0; n < (length / PAGE_SIZE); n++) {
		leaf = user_leaf64_in(pml4, (unsigned long)start + (n * PAGE_SIZE));
		if(!leaf) {
			continue;
		}
		pg = &page_table[leaf >> PAGE_SHIFT];
		if(pg->flags & PAGE_RESERVED) {
			unmap_user_page64_in(pml4, (unsigned long)start + (n * PAGE_SIZE));
			continue;
		}

		if(vma->prot & PROT_WRITE && vma->flags & MAP_SHARED) {
			offset = start - vma->start + vma->offset + n * PAGE_SIZE;
			write_page(pg, vma->inode, offset, PAGE_SIZE);
		}

		if(!(leaf & PAGE_NOALLOC)) {
			if(pg->count > 1) {
				/* CoW / MAP_SHARED: another process still
				 * references this page - just drop our reference */
				pg->count--;
			} else {
				kfree(P2V(leaf));
			}
		}
		current->rss--;
#ifdef CONFIG_SYSVIPC
		if(vma->object) {
			shm_rss--;
		}
#endif /* CONFIG_SYSVIPC */
		unmap_user_page64_in(pml4, (unsigned long)start + (n * PAGE_SIZE));
	}
#else
	unsigned int n, offset;
	unsigned int *pgdir, *pgtbl;
	unsigned int pde, pte;
	struct page *pg;
	int page;

	pgdir = (unsigned int *)P2V(current->tss.cr3);
	pgtbl = NULL;

	for(n = 0; n < (length / PAGE_SIZE); n++) {
		pde = GET_PGDIR(start + (n * PAGE_SIZE));
		pte = GET_PGTBL(start + (n * PAGE_SIZE));
		if(pgdir[pde] & PAGE_PRESENT) {
			pgtbl = (unsigned int *)P2V((pgdir[pde] & PAGE_MASK));
			if(pgtbl[pte] & PAGE_PRESENT) {
				if (!(pgtbl[pte] & PAGE_NOALLOC)) {
					/* make sure to not free reserved pages */
					page = pgtbl[pte] >> PAGE_SHIFT;
					pg = &page_table[page];
					if(pg->flags & PAGE_RESERVED) {
						continue;
					}

					if(vma->prot & PROT_WRITE && vma->flags & MAP_SHARED) {
						offset = start - vma->start + vma->offset + n * PAGE_SIZE;
						write_page(pg, vma->inode, offset, PAGE_SIZE);
					}

					if(pg->count > 1) {
						/* CoW / MAP_SHARED: another process still
						 * references this page (e.g. exec-after-fork
						 * freeing the old binary must NOT free the
						 * parent's shared pages) - just drop our
						 * reference */
						pg->count--;
					} else {
						kfree(P2V(pgtbl[pte]) & PAGE_MASK);
					}
				}
				current->rss--;
#ifdef CONFIG_SYSVIPC
				if(vma->object) {
					shm_rss--;
				}
#endif /* CONFIG_SYSVIPC */
				pgtbl[pte] = 0;

#ifdef __x86_64__
				/* FNX: also clear the leaf from the ACTIVE 4-level
				 * tables, or a later mmap reusing this address would
				 * hit the stale present PTE and skip the page fault
				 * (reading the old content instead of the new file) */
				{
					extern int unmap_user_page64_in(unsigned long, unsigned long);
					extern unsigned long paging64_pml4(void);
					unsigned long pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4();
					unmap_user_page64_in(pml4, start + (n * PAGE_SIZE));
				}
#endif /* __x86_64__ */

				/* check if a page table can be freed */
				for(pte = 0; pte < PT_ENTRIES; pte++) {
					if(pgtbl[pte] & PAGE_MASK) {
						break;
					}
				}
				if(pte == PT_ENTRIES) {
					kfree((addr_t)pgtbl & PAGE_MASK);
					current->rss--;
					pgdir[pde] = 0;
				}
			}
		}
	}
#endif /* __x86_64__ */
}

void release_binary(void)
{
	struct vma *vma, *tmp;

	vma = current->vma_table;

	while(vma) {
		tmp = vma->next;
		free_vma_pages(vma, vma->start, vma->end - vma->start);
		free_vma_region(vma, vma->start, vma->end - vma->start);
		vma = tmp;
	}

	invalidate_tlb();
}

struct vma *find_vma_region(addr_t addr)
{
	struct vma *vma;

	if(!addr) {
		return NULL;
	}

	addr &= PAGE_MASK;
	vma = current->vma_table;

	while(vma) {
		if((addr >= vma->start) && (addr < vma->end)) {
			return vma;
		}
		vma = vma->next;
	}
	return NULL;
}

/*
 * FNX: used by the 64-bit pml4 fork deep-copy (kernel64/mm64.c) to
 * decide whether a user-writable page must be made read-only for
 * copy-on-write. clone_pages() leaves MAP_SHARED pages writable, so only
 * MAP_PRIVATE (or vma-less) pages are COW'd. Returns 1 if the address is
 * in a MAP_SHARED vma.
 */
int vma_is_shared(addr_t addr)
{
	struct vma *vma = find_vma_region(addr);

	return vma && (vma->flags & MAP_SHARED);
}

struct vma *find_vma_intersection(addr_t start, addr_t end)
{
	struct vma *vma;

	vma = current->vma_table;

	while(vma) {
		if(end <= vma->start) {
			break;
		}
		if(start < vma->end) {
			return vma;
		}
		vma = vma->next;
	}
	return NULL;
}

int expand_heap(addr_t new)
{
	struct vma *vma, *heap;

	vma = current->vma_table;
	heap = NULL;

	while(vma) {
		/* make sure the new heap won't overlap the next region */
		if(heap && new < vma->start) {
			heap->end = new;
			return 0;
		} else {
			heap = NULL;	/* was a bad candidate */
		}
		if(!heap && vma->s_type == P_HEAP) {
			heap = vma;	/* possible candidate */
		}
		vma = vma->next;
	}

	/* out of memory! */
	return 1;
}

/* return the first free address that matches with the size of length */
addr_t get_unmapped_vma_region(addr_t length)
{
	addr_t addr;
	struct vma *vma;

	if(!length) {
		return 0;
	}

	addr = MMAP_START;
	vma = current->vma_table;

	while(vma) {
		if(vma->start < MMAP_START) {
			vma = vma->next;
			continue;
		}
		if(vma->start - addr >= length) {
			return PAGE_ALIGN(addr);
		}
		addr = PAGE_ALIGN(vma->end);
		vma = vma->next;
	}
	return 0;
}

long do_mmap(struct inode *i, addr_t start, addr_t length, unsigned int prot, unsigned int flags, addr_t offset, char type, char mode, void *object)
{
	struct vma *vma;
	int errno;

	/* reject absurd lengths up front: PAGE_ALIGN() would wrap them to 0
	 * (silently returning 'start' with no mapping) and a huge
	 * start+length could wrap the USER_STACK_TOP check */
	if(length > USER_STACK_TOP) {
		return -EINVAL;
	}
	if(!(length = PAGE_ALIGN(length))) {
		return start;
	}

	/* user addresses must stay in the canonical low half (< the 128TB
	 * user/kernel boundary, USER_STACK_TOP = 0x0000800000000000).
	 * With length <= USER_STACK_TOP, start+length cannot wrap. */
	if(start >= USER_STACK_TOP || start + length > USER_STACK_TOP) {
		return -EINVAL;
	}

	/* file mapping */
	if(i) {
		if(!S_ISREG(i->i_mode) && !S_ISCHR(i->i_mode)) {
			return -ENODEV;
		}

		/* 
		 * The file shall have been opened with read permission,
		 * regardless of the protection options specified.
		 * IEEE Std 1003.1, 2004 Edition.
		 */
		if(mode == O_WRONLY) {
			return -EACCES;
		}
		switch(flags & MAP_TYPE) {
			case MAP_SHARED:
				if(prot & PROT_WRITE) {
					if(!(mode & (O_WRONLY | O_RDWR))) {
						return -EACCES;
					}
				}
				break;
			case MAP_PRIVATE:
				break;
			default:
				return -EINVAL;
		}

	/* anonymous mapping */
	} else {
		if((flags & MAP_TYPE) != MAP_PRIVATE) {
			return -EINVAL;
		}

		/* anonymous objects must be filled with zeros */
		flags |= ZERO_PAGE;
#ifdef CONFIG_SYSVIPC
		/* ... except for SHM regions */
		if(type == P_SHM) {
			flags &= ~ZERO_PAGE;
		}
#endif /* CONFIG_SYSVIPC */
	}

	if(flags & MAP_FIXED) {
		if(start & ~PAGE_MASK) {
			return -EINVAL;
		}
	} else {
		start = get_unmapped_vma_region(length);
		if(!start) {
			printk("WARNING: %s(): unable to get an unmapped vma region.\n", __FUNCTION__);
			return -ENOMEM;
		}
	}

	if(!(vma = (struct vma *)kmalloc(sizeof(struct vma)))) {
                return -ENOMEM;
        }
        memset_b(vma, 0, sizeof(struct vma));
	vma->start = start;
	vma->end = start + length;
	vma->prot = prot;
	vma->flags = flags;
	vma->offset = offset;
	vma->s_type = type;
	vma->inode = i;
	vma->o_mode = mode;
#ifdef CONFIG_SYSVIPC
	vma->object = (struct shmid_ds *)object;
#endif /* CONFIG_SYSVIPC */

	do_munmap(start, length);   /* clear old maps */

	if(i && i->fsop->mmap) {
		if((errno = i->fsop->mmap(i, vma))) {
			free_vma_region(vma, start, length);
			kfree((addr_t)vma);
			return errno;
		}
	}

	add_vma_region(vma);
	return start;
}

int do_munmap(addr_t addr, __size_t length)
{
	struct vma *vma;
	addr_t size;

	if(addr & ~PAGE_MASK) {
		return -EINVAL;
	}

	length = PAGE_ALIGN(length);

	/* a huge length would wrap addr+length and make free_vma_pages
	 * spin forever (32-bit loop counter vs 2^48 pages); no legitimate
	 * unmap is larger than the whole user half */
	if(length > USER_STACK_TOP || addr + length > USER_STACK_TOP) {
		return -EINVAL;
	}

	while(length) {
		if(!(vma = find_vma_region(addr))) {
			break;
		}
		if((addr + length) > vma->end) {
			size = vma->end - addr;
		} else {
			size = length;
		}

		free_vma_pages(vma, addr, size);
		invalidate_tlb();
		free_vma_region(vma, addr, size);
		length -= size;
		addr += size;
	}
	return 0;
}

int do_mprotect(struct vma *vma, addr_t addr, __size_t length, int prot)
{
	struct vma *new;

	if(!(new = (struct vma *)kmalloc(sizeof(struct vma)))) {
                return -ENOMEM;
        }
        memset_b(new, 0, sizeof(struct vma));
	new->start = addr;
	new->end = addr + length;
	new->prot = prot;
	new->flags = vma->flags;
	new->offset = vma->offset;
	new->s_type = vma->s_type;
	new->inode = vma->inode;
	new->o_mode = vma->o_mode;
	add_vma_region(new);

	return 0;
}
