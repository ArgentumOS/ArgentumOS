/*
 * fiwix/mm/page.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

/*
 * page.c implements a cache with a free list as a doubly circular linked
 * list and a chained hash table with doubly linked lists.
 *
 * hash table
 * +--------+  +--------------+  +--------------+  +--------------+
 * | index  |  |prev|data|next|  |prev|data|next|  |prev|data|next|
 * |   0   --> | /  |    | --->  <--- |    | --->  <--- |    |  / |
 * +--------+  +--------------+  +--------------+  +--------------+
 * +--------+  +--------------+  +--------------+  +--------------+
 * | index  |  |prev|data|next|  |prev|data|next|  |prev|data|next|
 * |   1   --> | /  |    | --->  <--- |    | --->  <--- |    |  / |
 * +--------+  +--------------+  +--------------+  +--------------+
 *              (page)            (page)            (page)  
 *    ...
 */

#include <fiwix/asm.h>
#include <fiwix/kernel.h>
#include <fiwix/mm.h>
#include <fiwix/mman.h>
#include <fiwix/bios.h>
#include <fiwix/sleep.h>
#include <fiwix/sched.h>
#include <fiwix/devices.h>
#include <fiwix/buffer.h>
#include <fiwix/errno.h>
#include <fiwix/stdio.h>
#include <fiwix/string.h>
#include <fiwix/blk_queue.h>

#define PAGE_HASH(inode, offset)	(((__ino_t)(inode) ^ (__off_t)(offset)) % (NR_PAGE_HASH))
#define NR_PAGES	(page_table_size / sizeof(struct page))
#define NR_PAGE_HASH	(page_hash_table_size / sizeof(struct page *))

struct page *page_table;		/* page pool */
struct page *page_head;			/* page pool head */
struct page **page_hash_table;

static void insert_to_hash(struct page *pg)
{
	struct page **h;
	int i;

	i = PAGE_HASH(pg->inode, pg->offset);
	h = &page_hash_table[i];

	if(!*h) {
		*h = pg;
		(*h)->prev_hash = (*h)->next_hash = NULL;
	} else {
		pg->prev_hash = NULL;
		pg->next_hash = *h;
		(*h)->prev_hash = pg;
		*h = pg;
	}
	kstat.cached += (PAGE_SIZE / 1024);
}

static void remove_from_hash(struct page *pg)
{
	struct page **h;
	int i;

	if(!pg->inode) {
		return;
	}

	i = PAGE_HASH(pg->inode, pg->offset);
	h = &page_hash_table[i];

	while(*h) {
		if(*h == pg) {
			if((*h)->next_hash) {
				(*h)->next_hash->prev_hash = (*h)->prev_hash;
			}
			if((*h)->prev_hash) {
				(*h)->prev_hash->next_hash = (*h)->next_hash;
			}
			if(h == &page_hash_table[i]) {
				*h = (*h)->next_hash;
			}
			kstat.cached -= (PAGE_SIZE / 1024);
			break;
		}
		h = &(*h)->next_hash;
	}
}

static void insert_on_free_list(struct page *pg)
{
#ifdef __x86_64__
	/* Fiwix64 (pivot): the page_head free-list is not built; pages live
	 * in the single 64-bit bitmap. Nothing to link. */
	return;
#else
	if(!page_head) {
		pg->prev_free = pg->next_free = pg;
		page_head = pg;
	} else {
		pg->next_free = page_head;
		pg->prev_free = page_head->prev_free;
		page_head->prev_free->next_free = pg;
		page_head->prev_free = pg;
	}

	kstat.free_pages++;
#endif /* __x86_64__ */
}

static void remove_from_free_list(struct page *pg)
{
#ifdef __x86_64__
	/* Fiwix64 (pivot): nothing is ever on the page_head free-list. */
	return;
#else
	if(!kstat.free_pages) {
		return;
	}

	pg->prev_free->next_free = pg->next_free;
	pg->next_free->prev_free = pg->prev_free;
	kstat.free_pages--;
	if(pg == page_head) {
		page_head = pg->next_free;
	}

	if(!kstat.free_pages) {
		page_head = NULL;
	}
#endif /* __x86_64__ */
}

void page_lock(struct page *pg)
{
	unsigned int flags;

	for(;;) {
		SAVE_FLAGS(flags); CLI();
		if(pg->flags & PAGE_LOCKED) {
			RESTORE_FLAGS(flags);
			sleep(pg, PROC_UNINTERRUPTIBLE);
		} else {
			break;
		}
	}
	pg->flags |= PAGE_LOCKED;
	RESTORE_FLAGS(flags);
}

void page_unlock(struct page *pg)
{
	unsigned int flags;

	SAVE_FLAGS(flags); CLI();
	pg->flags &= ~PAGE_LOCKED;
	wakeup(pg);
	RESTORE_FLAGS(flags);
}

struct page *get_free_page(void)
{
#ifdef __x86_64__
	/* Fiwix64 (pivot): the 64-bit bitmap is the single physical
	 * allocator. The Fiwix page_head free-list is not built (page_init
	 * skips it), so get_free_page() is a thin wrapper over the bitmap. */
	unsigned long phys;
	struct page *pg;
	extern unsigned long alloc_pages64(int);

	if(!(phys = alloc_pages64(1))) {
		return NULL;
	}
	if(phys >> PAGE_SHIFT >= NR_PAGES) {
		/* the bitmap may cover more RAM than page_table[] does (the
		 * Fiwix pool is capped at GDT_BASE); release and fail */
		extern void free_pages64(unsigned long, int);
		free_pages64(phys, 1);
		return NULL;
	}
	pg = &page_table[phys >> PAGE_SHIFT];
	remove_from_hash(pg);	/* drop any stale page-cache reference */
	pg->count = 1;
	pg->inode = 0;
	pg->offset = 0;
	pg->dev = 0;
	pg->flags = 0;
	pg->data = (char *)P2V(phys);
	return pg;
#else
	unsigned int flags;
	struct page *pg;

repeat:
	/* if the number of pages is low then reclaim some buffers */
	if(kstat.free_pages <= kstat.min_free_pages) {
		/* reclaim memory from buffer cache */
		wakeup(&kswapd);
		if(!kstat.free_pages) {
			sleep(&get_free_page, PROC_UNINTERRUPTIBLE);
			if(!kstat.free_pages) {
				if(kstat.pages_reclaimed) {
					goto repeat;
				}
				/* definitely out of memory! (no more pages) */
				printk("WARNING: %s(): out of memory and swapping is not implemented yet, sorry.\n", __FUNCTION__);
				printk("%s(): pid %d ran out of memory. OOM killer needed!\n", __FUNCTION__, current->pid);
				return NULL;
			}
		}
		/* this reduces the number of iterations */
		if(kstat.min_free_pages > NR_BUF_RECLAIM) {
			kstat.min_free_pages -= NR_BUF_RECLAIM;
		}
	} else {
		/* recalculate if free memory is back to normal levels */
		if(kstat.min_free_pages <= NR_BUF_RECLAIM) {
			if(kstat.free_pages > NR_BUF_RECLAIM) {
				kstat.min_free_pages = (kstat.total_mem_pages * FREE_PAGES_RATIO) / 100;
			}
		}
	}

	SAVE_FLAGS(flags); CLI();

	if(!(pg = page_head)) {
		printk("WARNING: page_head returned NULL! (free_pages = %d)\n", kstat.free_pages);
		RESTORE_FLAGS(flags);
		return NULL;
	}

	remove_from_free_list(pg);
	remove_from_hash(pg);	/* remove it from its old hash */
	pg->count = 1;
	pg->inode = 0;
	pg->offset = 0;
	pg->dev = 0;

	RESTORE_FLAGS(flags);
	return pg;
#endif /* __x86_64__ */
}

struct page *search_page_hash(struct inode *inode, __off_t offset)
{
	struct page *pg;
	int i;

	i = PAGE_HASH(inode->inode, offset);
	pg = page_hash_table[i];

	while(pg) {
		if(pg->inode == inode->inode && pg->offset == offset && pg->dev == inode->dev) {
			if(!pg->count) {
				remove_from_free_list(pg);
			}
			pg->count++;
			return pg;
		}
		pg = pg->next_hash;
	}

	return NULL;
}

void release_page(struct page *pg)
{
#ifdef __x86_64__
	/* Fiwix64 (pivot): return the page to the bitmap. The refcount is
	 * the pml4/allocator usage; at zero the phys goes back to the pool.
	 * The bitmap never hands out reserved/static pages, so no
	 * PAGE_RESERVED handling is needed here. */
	extern void free_pages64(unsigned long, int);

	if(--pg->count > 0) {
		return;
	}
	/* drop any page-cache reference BEFORE returning the phys to the
	 * pool: a stale hash entry makes search_page_hash()/bread_page()
	 * write file content into the page after the bitmap re-granted it
	 * as a table page (the phys 0x400000 ELF-content corruption). */
	remove_from_hash(pg);
	free_pages64((unsigned long)pg->page << PAGE_SHIFT, 1);
	return;
#else
	unsigned int flags;

	if(!is_valid_page(pg->page)) {
		PANIC("Unexpected inconsistency in hash_table. Missing page %d (0x%x).\n", pg->page, pg->page);
	}

	if(!pg->count) {
	}

	if(--pg->count > 0) {
		return;
	}

	SAVE_FLAGS(flags); CLI();

	insert_on_free_list(pg);

	/* remove all flags except PAGE_RESERVED */
	pg->flags &= PAGE_RESERVED;

	/* if page is not cached then place it at the head of the free list */
	if(!pg->inode) {
		page_head = pg;
	}

	RESTORE_FLAGS(flags);

	/*
	 * We need to wait for free pages to be far greater than NR_BUF_RECLAIM,
	 * otherwise get_free_pages() could run out of pages _again_, and it
	 * would think that 'definitely there are no more free pages', killing
	 * the current process prematurely.
	 */
	if(kstat.free_pages > (NR_BUF_RECLAIM * 3)) {
		wakeup(&get_free_page);
	}
#endif /* __x86_64__ */
}

int is_valid_page(int page)
{
	return (page >= 0 && page < NR_PAGES);
}

/* Fiwix64 (pivot): refcount helpers used by the 4-level fork/exec code
 * (kernel64/mm64.c). page_table[].count is the number of pml4 mappings
 * (plus the allocator's own reference) on a physical page; COW decrements
 * it when a process replaces a shared leaf with a private copy, and
 * free_vma_pages() kfree()s the page when it hits zero. */
void page_ref_get(unsigned long phys)
{
	struct page *pg;
	unsigned long page;

	page = phys >> PAGE_SHIFT;
	if(page >= NR_PAGES) {
		return;
	}
	pg = &page_table[page];
	pg->count++;
}

void page_ref_put(unsigned long phys)
{
	struct page *pg;
	unsigned long page;

	page = phys >> PAGE_SHIFT;
	if(page >= NR_PAGES) {
		return;
	}
	pg = &page_table[page];
	if(pg->count > 0) {
		pg->count--;
	}
}

void invalidate_inode_pages(struct inode *i)
{
	struct page *pg;
	__off_t offset;

	for(offset = 0; offset < i->i_size; offset += PAGE_SIZE) {
		if((pg = search_page_hash(i, offset))) {
			page_lock(pg);
			release_page(pg);
			page_unlock(pg);
			remove_from_hash(pg);
		}
	}
}

void update_page_cache(struct inode *i, __off_t offset, const char *buf, int count)
{
	__off_t poffset;
	struct page *pg;
	int bytes;

	poffset = offset & (PAGE_SIZE - 1);	/* mod PAGE_SIZE */
	offset &= PAGE_MASK;
	bytes = PAGE_SIZE - poffset;

	if(count) {
		bytes = MIN(bytes, count);
		if((pg = search_page_hash(i, offset))) {
			page_lock(pg);
			memcpy_b(pg->data + poffset, buf, bytes);
			page_unlock(pg);
			release_page(pg);
		}
	}
}

int write_page(struct page *pg, struct inode *i, __off_t offset, unsigned int length)
{
	struct fd fdt;
	unsigned int size;
	int errno;

	size = MIN(i->i_size - offset, length);
	fdt.inode = i;
	fdt.flags = 0;
	fdt.count = 0;
	fdt.offset = offset;
	if(i->fsop && i->fsop->write) {
		errno = i->fsop->write(i, &fdt, pg->data, size);
	} else {
		errno = -EINVAL;
	}

	return errno;
}

int bread_page(struct page *pg, struct inode *i, __off_t offset, char prot, char flags)
{
	__blk_t block;
	__off_t size_read;
	int blksize, retval;
	struct device *d;
	struct blk_request brh, *br, *tmp;

	blksize = i->sb->s_blocksize;
	retval = size_read = 0;
	tmp = NULL;

	if(!(d = get_device(BLK_DEV, i->dev))) {
		printk("WARNING: %s(): device major %d not found!\n", __FUNCTION__, MAJOR(i->dev));
		return 1;
	}

	memset_b(&brh, 0, sizeof(struct blk_request));
	page_lock(pg);

	/* cache any read-only or public (shared) pages */
	if(!(prot & PROT_WRITE) || flags & MAP_SHARED) {
		pg->inode = i->inode;
		pg->offset = offset;
		pg->dev = i->dev;
		insert_to_hash(pg);
	}

	while(size_read < PAGE_SIZE) {
		if(!(br = (struct blk_request *)kmalloc(sizeof(struct blk_request)))) {
			printk("WARNING: %s(): no more free memory for block requests.\n", __FUNCTION__);
			retval = 1;
			break;
		}
		if((block = bmap(i, offset + size_read, FOR_READING)) < 0) {
			retval = 1;
			break;
		}
		memset_b(br, 0, sizeof(struct blk_request));
		br->dev = i->dev;
		br->block = block;
		br->flags = block ? 0 : BRF_NOBLOCK;
		br->size = blksize;
		br->device = d;
		br->fn = d->fsop->read_block;
		br->head_group = &brh;
		if(!brh.next_group) {
			brh.next_group = br;
		} else {
			tmp->next_group = br;
		}
		tmp = br;
		size_read += blksize;
	}
	if(!retval) {
		retval = gbread(d, &brh);
	}
	/*
	 * We must zero retval if is not negative because block drivers
	 * that still don't use the new I/O mechanism will return the
	 * bytes read, and this value could be interpreted below as an error.
	 */
	retval = retval < 0 ? retval : 0;
	br = brh.next_group;
	size_read = 0;
	while(br) {
		if(!retval) {
			if(br->block) {
				memcpy_b(pg->data + size_read, br->buffer->data, br->size);
				br->buffer->flags |= BUFFER_VALID;
			} else {
				/* fill the hole with zeros */
				memset_b(pg->data + size_read, 0, br->size);
			}
			size_read += br->size;
		}
		if(br->block) {
			brelse(br->buffer);
		}
		tmp = br->next_group;
		kfree((addr_t)br);
		br = tmp;
	}

	page_unlock(pg);
	return retval;
}

int file_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	__size_t total_read;
	unsigned int addr, poffset, bytes;
	struct page *pg;

	inode_lock(i);

	if(f->offset > i->i_size) {
		f->offset = i->i_size;
	}

	total_read = 0;

	for(;;) {
		count = (f->offset + count > i->i_size) ? i->i_size - f->offset : count;
		if(!count) {
			break;
		}

		poffset = f->offset & (PAGE_SIZE - 1);	/* mod PAGE_SIZE */
		if(!(pg = search_page_hash(i, f->offset & PAGE_MASK))) {
			if(!(addr = kmalloc(PAGE_SIZE))) {
				inode_unlock(i);
				printk("%s(): returning -ENOMEM\n", __FUNCTION__);
				return -ENOMEM;
			}
			pg = &page_table[V2P(addr) >> PAGE_SHIFT];
			if(bread_page(pg, i, f->offset & PAGE_MASK, 0, MAP_SHARED)) {
				kfree(addr);
				inode_unlock(i);
				printk("%s(): returning -EIO\n", __FUNCTION__);
				return -EIO;
			}
		} else {
			addr = (addr_t)pg->data;
		}

		page_lock(pg);
		bytes = PAGE_SIZE - poffset;
		bytes = MIN(bytes, count);
		memcpy_b(buffer + total_read, pg->data + poffset, bytes);
		total_read += bytes;
		count -= bytes;
		f->offset += bytes;
		kfree(addr);
		page_unlock(pg);
	}

	inode_unlock(i);
	return total_read;
}

void reserve_pages(unsigned int from, unsigned int to)
{
	struct page *pg;

	while(from < to) {
		pg = &page_table[from >> PAGE_SHIFT];
		pg->data = NULL;
		pg->flags = PAGE_RESERVED;
		kstat.physical_reserved++;
		remove_from_hash(pg);
		remove_from_free_list(pg);
		from += PAGE_SIZE;
	}

	/* recalculate */
	kstat.total_mem_pages = kstat.free_pages;
	kstat.min_free_pages = (kstat.total_mem_pages * FREE_PAGES_RATIO) / 100;
}

void page_init(int pages)
{
	struct page *pg;
	unsigned int n, addr;

	memset_b(page_table, 0, page_table_size);
	memset_b(page_hash_table, 0, page_hash_table_size);

	for(n = 0; n < pages; n++) {
		pg = &page_table[n];
		pg->page = n;

		addr = n << PAGE_SHIFT;
		if(addr >= KERNEL_ADDR && addr < V2P(_last_data_addr)) {
			pg->flags = PAGE_RESERVED;
			kstat.kernel_reserved++;
			continue;
		}

		/* reserve the kernel stack page */
		if(addr == 0x0000F000) {
			pg->flags = PAGE_RESERVED;
			kstat.physical_reserved++;
			continue;
		}

		/*
		 * Some memory addresses are reserved, like the memory between
		 * 0xA0000 and 0x100000 and other addresses, mostly used by the
		 * VGA graphics adapter and BIOS.
		 */
		if(!is_addr_in_bios_map(addr)) {
			pg->flags = PAGE_RESERVED;
			kstat.physical_reserved++;
			continue;
		}

		pg->data = (char *)P2V(addr);
#ifndef __x86_64__
		/* Fiwix64 (pivot): the bitmap is the single allocator; the
		 * page_head free-list is NOT built, so pages are NOT inserted
		 * here. page_table[] metadata (data/refcount/flags) is still
		 * initialized above for shmat/meminfo/free_vma_pages. */
		insert_on_free_list(pg);
#endif /* __x86_64__ */
	}

	kstat.total_mem_pages = kstat.free_pages;
	kstat.min_free_pages = (kstat.total_mem_pages * FREE_PAGES_RATIO) / 100;
}
