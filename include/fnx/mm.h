/*
 * fnx/include/fnx/mm.h
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_MEMORY_H
#define _FNX_MEMORY_H

#include <fnx/types.h>
#include <fnx/segments.h>
#include <fnx/process.h>

/* convert from physical to virtual the addresses below PAGE_OFFSET only */
#ifdef __x86_64__
/* FNX (pivot): PAGE_OFFSET is 0xFFFFFFFF80000000ULL, which is a
 * NEGATIVE signed 64-bit value. The plain `addr < PAGE_OFFSET` test then
 * compares signed (0x400000 < -0x80000000 is false) and the macro returns
 * the RAW phys instead of the high-half alias - so page-table walks read
 * through the low identity map, which user demand-maps have remapped
 * (the phys 0x400000 = shell-binary ELF corruption). Cast to unsigned. */
#define P2V(addr)		((unsigned long)(addr) < (unsigned long)PAGE_OFFSET ? \
				 (unsigned long)(addr) + (unsigned long)PAGE_OFFSET : (unsigned long)(addr))
#else
#define P2V(addr)		(addr < PAGE_OFFSET ? addr + PAGE_OFFSET : addr)
#endif /* __x86_64__ */

#define V2P(addr)		(addr - PAGE_OFFSET)

#define PAGE_SIZE		4096
#define PAGE_SHIFT		0x0C
#define PAGE_MASK		~(PAGE_SIZE - 1)	/* 0xFFFFF000 */
#define PAGE_MASK64		~(4096UL - 1)		/* 0xFFFFFFFFFFFFF000 */
#define PAGE_ALIGN(addr)	(((addr) + (PAGE_SIZE - 1)) & PAGE_MASK)
#define PT_ENTRIES		(PAGE_SIZE / sizeof(unsigned int))
#define PD_ENTRIES		(PAGE_SIZE / sizeof(unsigned int))

#define PAGE_LOCKED		0x001
#define PAGE_BUDDYLOW		0x010	/* page belongs to buddy_low */
#define PAGE_RESERVED		0x100	/* kernel, BIOS address, ... */
#define PAGE_COW		0x200	/* marked for Copy-On-Write */

#define PFAULT_V		0x01	/* protection violation */
#define PFAULT_W		0x02	/* during write */
#define PFAULT_U		0x04	/* in user mode */

#define GET_PGDIR(address)	((unsigned int)((address) >> 22) & 0x3FF)
#define GET_PGTBL(address)	((unsigned int)((address) >> 12) & 0x3FF)

struct page {
	int page;		/* page number */
	int count;		/* usage counter */
	int flags;
	__ino_t inode;		/* inode of the file */
	__off_t offset;		/* file offset */
	__dev_t dev;		/* device where file resides */
	char *data;		/* page contents */
	struct page *prev_hash;
	struct page *next_hash;
	struct page *prev_free;
	struct page *next_free;
};

extern struct page *page_table;
extern struct page **page_hash_table;

/* values to be determined during system startup */
extern unsigned int page_table_size;		/* size in bytes */
extern unsigned int page_hash_table_size;	/* size in bytes */

extern addr_t *kpage_dir;


/* buddy_low.c */
static const unsigned int bl_blocksize[] = {
	32,
	64,
	128,
	256,
	512,
	1024,
	2048,
	4096
};

struct bl_head {
	unsigned char level;	/* size class (exponent of the power of 2) */
	struct bl_head *prev;
	struct bl_head *next;
};

addr_t bl_malloc(__size_t);
void bl_free(addr_t);
void buddy_low_init(void);

/* alloc.c */
addr_t kmalloc(__size_t);
void kfree(addr_t);

/* kernel64/mm64.c - 64-bit kernel heap (high-half VAs, arbitrary sizes:
 * slab buckets up to 2048 bytes, contiguous multi-page runs above) */
void *kmalloc64(unsigned long);
void kfree64(void *);

/* page.c */
void page_lock(struct page *);
void page_unlock(struct page *);
struct page *get_free_page(void);
struct page *search_page_hash(struct inode *, __off_t);
void release_page(struct page *);
int is_valid_page(int);
void invalidate_inode_pages(struct inode *);
void update_page_cache(struct inode *, __off_t, const char *, int);
int write_page(struct page *, struct inode *, __off_t, unsigned int);
int bread_page(struct page *, struct inode *, __off_t, char, char);
int file_read(struct inode *, struct fd *, char *, __size_t);
void reserve_pages(unsigned int, unsigned int);
void page_init(int);

/* memory.c */
addr_t map_kaddr(addr_t *, unsigned int, unsigned int, unsigned int, int);
void bss_init(void);
unsigned int setup_tmp_pgdir(unsigned int, unsigned int);
addr_t get_mapped_addr(struct proc *, addr_t);
int clone_pages(struct proc *);
int free_page_tables(struct proc *);
addr_t map_page(struct proc *, addr_t, addr_t, unsigned int);
addr_t map_page_flags(struct proc *, addr_t, addr_t, unsigned int, int);
int unmap_page(addr_t);
void mem_init(void);
void mem_stats(void);

/* swapper.c */
int kswapd(void);

#endif /* _FNX_MEMORY_H */
