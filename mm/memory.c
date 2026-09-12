/*
 * fnx/mm/memory.c
 *
 * Copyright 2018-2023, Jordi Sanfeliu. All rights reserved.
 * Portions Copyright 2024, Greg Haerr.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/asm.h>
#include <fnx/multiboot1.h>
#include <fnx/kparms.h>
#include <fnx/mm.h>
#include <fnx/mman.h>
#include <fnx/bios.h>
#include <fnx/ramdisk.h>
#include <fnx/process.h>
#include <fnx/buffer.h>
#include <fnx/fs.h>
#include <fnx/kexec.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#define KERNEL_TEXT_SIZE	((addr_t)_etext - (PAGE_OFFSET + KERNEL_ADDR))
#define KERNEL_DATA_SIZE	((addr_t)_edata - (addr_t)_etext)
#define KERNEL_BSS_SIZE		((addr_t)_end - (addr_t)_edata)

addr_t *kpage_dir;

unsigned int proc_table_size = 0;
unsigned int buffer_hash_table_size = 0;
unsigned int inode_table_size = 0;
unsigned int inode_hash_table_size = 0;
unsigned int fd_table_size = 0;
unsigned int page_table_size = 0;
unsigned int page_hash_table_size = 0;

addr_t map_kaddr(addr_t *page_dir, unsigned int from, unsigned int to, unsigned int addr, int flags)
{
	unsigned int n;
	addr_t paddr;
	unsigned int *pgtbl;
	unsigned int pde, pte;

	paddr = addr;
	for(n = from; n < to; n += PAGE_SIZE) {
		pde = GET_PGDIR(n);
		pte = GET_PGTBL(n);
		if(!(page_dir[pde] & ~PAGE_MASK)) {
			if (!addr) {
				paddr = (addr_t)kmalloc(PAGE_SIZE);
				if (!paddr) {
					printk("%s(): no memory\n", __FUNCTION__);
					return 0;
				}
				paddr = V2P(paddr);
			}
			page_dir[pde] = paddr | flags;
			memset_b((void *)(paddr + PAGE_OFFSET), 0, PAGE_SIZE);
			paddr += PAGE_SIZE;
		}
		pgtbl = (unsigned int *)((page_dir[pde] & PAGE_MASK) + PAGE_OFFSET);
		pgtbl[pte] = n | flags;
	}

	return paddr;
}


addr_t get_mapped_addr(struct proc *p, addr_t addr)
{
#ifdef __x86_64__
	/* FNX (native port): walk the process's own 4-level pml4 (the
	 * per-process cr3_64) to translate a USER virtual address to its
	 * PHYSICAL page. Returns the raw PHYSICAL page address; callers
	 * apply & PAGE_MASK then V2P to read the page contents. */
	unsigned long pml4, *lvl, e1, e2, e3, e4;
#define P2V64x(a)	(((unsigned long)(a) < PAGE_OFFSET) ? \
				((unsigned long)(a) + PAGE_OFFSET) : (unsigned long)(a))
	extern unsigned long paging64_pml4_phys(void);
	pml4 = p->cr3_64 ? p->cr3_64 : paging64_pml4_phys();
	lvl = (unsigned long *)P2V64x(pml4);
	if(!(e1 = lvl[((unsigned long)addr >> 39) & 0x1FF]) || !(e1 & 0x001)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64x(e1 & ~0xFFFUL);
	if(!(e2 = lvl[((unsigned long)addr >> 30) & 0x1FF]) || !(e2 & 0x001)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64x(e2 & ~0xFFFUL);
	if(!(e3 = lvl[((unsigned long)addr >> 21) & 0x1FF]) || !(e3 & 0x001)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64x(e3 & ~0xFFFUL);
	e4 = lvl[((unsigned long)addr >> 12) & 0x1FF];
#undef P2V64x
	return (addr_t)e4;
#else
	unsigned int *pgdir, *pgtbl;
	unsigned int pde, pte;

	pgdir = (unsigned int *)P2V(p->tss.cr3);
	pde = GET_PGDIR(addr);
	pte = GET_PGTBL(addr);
	pgtbl = (unsigned int *)P2V((pgdir[pde] & PAGE_MASK));
	return pgtbl[pte];
#endif /* __x86_64__ */
}

int clone_pages(struct proc *child)

{
#ifdef __x86_64__
	/* FNX (native-MM): the fork child's 4-level tables were already	 * deep-copied with writable user leaves shared read-only by
	 * create_pml4_64(). The 2-level clone_pages() work is gone; here we
	 * only mirror its BOOKKEEPING: mark every shared writable user leaf
	 * PAGE_COW (the 4-level copy never touched page_table[].flags) and
	 * count the mapped pages for the child's rss. Returns >= 1 so the
	 * fork's "!clone_pages() == out of memory" check never misfires. */
	unsigned long pml4, *pml4p, *pdpt, *pd, *pt, e;
	unsigned long i, j, k;
	int m;
	struct page *pg;
	int pages;

	extern unsigned long paging64_pml4_phys(void);
	pml4 = child->cr3_64 ? child->cr3_64 : paging64_pml4_phys();
	pages = 0;
	/* FNX (canonical amd64 split): walk the whole USER half
	 * (pml4[0..255] = VA 0 .. 0x00007FFFFFFFFFFF, 128TB). The kernel
	 * half (pml4[256..511]) is shared and never walked here. */
	pml4p = (unsigned long *)P2V(pml4);
	for(i = 0; i < 256; i++) {	/* PML4[0..255] = user half */
		if(!(pml4p[i] & 0x001)) {
			continue;
		}
		pdpt = (unsigned long *)P2V(pml4p[i] & PAGE_MASK);
		for(j = 0; j < 512; j++) {
			if(!(pdpt[j] & 0x001)) {
				continue;
			}
			pd = (unsigned long *)P2V(pdpt[j] & PAGE_MASK);
			for(k = 0; k < 512; k++) {
				if(!(pd[k] & 0x001)) {
					continue;
				}
				if(pd[k] & 0x080) {	/* 2MB page */
					continue;
				}
				pt = (unsigned long *)P2V(pd[k] & PAGE_MASK);
				for(m = 0; m < 512; m++) {
					e = pt[m];
					if(!(e & 0x001)) {
						continue;
					}
					if(!(e & 0x004)) {
						continue;	/* supervisor leaf */
					}
					/* OS-managed / device pages (e.g. the
					 * framebuffer): their phys is outside
					 * RAM, so indexing page_table with it
					 * is out of bounds (and setting
					 * PAGE_COW would corrupt memory) -
					 * never count or CoW them. */
					if(e & PAGE_NOALLOC) {
						continue;
					}
					pg = &page_table[(e & PAGE_MASK) >> 12];
					if(pg->flags & PAGE_RESERVED) {
						continue;
					}
					if(!(e & 0x002)) {	/* shared RO -> CoW */
						pg->flags |= PAGE_COW;
					}
					pages++;
				}
			}
		}
	}
	return pages ? pages : 1;
#else
	unsigned int *src_pgdir, *dst_pgdir;
	unsigned int *src_pgtbl, *dst_pgtbl;
	unsigned int pde, pte;
	unsigned int p_addr, c_addr;
	unsigned int n, pages;
	struct page *pg;
	struct vma *vma;

	src_pgdir = (unsigned int *)P2V(current->tss.cr3);
	dst_pgdir = (unsigned int *)P2V(child->tss.cr3);
	vma = current->vma_table;
	pages = 0;

	while(vma) {
		if(vma->flags & MAP_SHARED) {
			vma = vma->next;
			continue;
		}
		for(n = vma->start; n < vma->end; n += PAGE_SIZE) {
			pde = GET_PGDIR(n);
			pte = GET_PGTBL(n);
			if(src_pgdir[pde] & PAGE_PRESENT) {
				src_pgtbl = (unsigned int *)P2V((src_pgdir[pde] & PAGE_MASK));
				if(!(dst_pgdir[pde] & PAGE_PRESENT)) {
					if(!(c_addr = kmalloc(PAGE_SIZE))) {
						printk("%s(): returning 0!\n", __FUNCTION__);
						return 0;
					}
					current->rss++;
					pages++;
					dst_pgdir[pde] = V2P(c_addr) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
					memset_b((void *)c_addr, 0, PAGE_SIZE);
				}
				dst_pgtbl = (unsigned int *)P2V((dst_pgdir[pde] & PAGE_MASK));
				if(src_pgtbl[pte] & PAGE_PRESENT) {
					if (src_pgtbl[pte] & PAGE_NOALLOC) {
						dst_pgtbl[pte] = src_pgtbl[pte];
						continue;
					}
					p_addr = src_pgtbl[pte] >> PAGE_SHIFT;
					pg = &page_table[p_addr];
					if(pg->flags & PAGE_RESERVED) {
						continue;
					}
					src_pgtbl[pte] &= ~PAGE_RW;
					/* mark writable pages as copy-on-write */
					if(vma->prot & PROT_WRITE) {
						pg->flags |= PAGE_COW;
					}
					dst_pgtbl[pte] = src_pgtbl[pte];
					if(!is_valid_page((dst_pgtbl[pte] & PAGE_MASK) >> PAGE_SHIFT)) {
						PANIC("%s: missing page %d during copy-on-write process.\n", __FUNCTION__, (dst_pgtbl[pte] & PAGE_MASK) >> PAGE_SHIFT);
					}
					pg = &page_table[(dst_pgtbl[pte] & PAGE_MASK) >> PAGE_SHIFT];
					pg->count++;
				}
			}
		}
		vma = vma->next;
	}
	return pages;
#endif /* __x86_64__ */
}

int free_page_tables(struct proc *p)
{
#ifdef __x86_64__
	/* FNX (native-MM): free the process's own 4-level tables. The
	 * caller's pml4 is not active (exit/reap), so this is safe. */
	extern void free_pml4_64(unsigned long);
	extern unsigned long paging64_pml4_phys(void);

	if(p->cr3_64 && p->cr3_64 != paging64_pml4_phys()) {
		free_pml4_64(p->cr3_64);
		p->cr3_64 = 0;
	}
	return 0;
#else
	unsigned int *pgdir;
	int n, count;

	pgdir = (unsigned int *)P2V(p->tss.cr3);
	for(n = 0, count = 0; n < PD_ENTRIES; n++) {
		if((pgdir[n] & (PAGE_PRESENT | PAGE_RW | PAGE_USER)) == (PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
			kfree(P2V(pgdir[n]) & PAGE_MASK);
			pgdir[n] = 0;
			count++;
		}
	}
	return count;
#endif /* __x86_64__ */
}

addr_t map_page(struct proc *p, addr_t vaddr, addr_t addr, unsigned int prot)
{
	return map_page_flags(p, vaddr, addr, prot, 0);
}

addr_t map_page_flags(struct proc *p, addr_t vaddr, addr_t addr, unsigned int prot, int flags)
{
#ifdef __x86_64__
	/* FNX (native-MM): the process's pml4 (p->cr3_64) is the single
	 * source of truth - no 2-level shadow, no mirroring. Walk it, split
	 * 2MB identity pages as needed (map_page64_in), and write the leaf.
	 * If the address is already user-mapped, hand back the EXISTING page
	 * (never P2V(0), and never stomp a CoW-shared leaf the caller didn't
	 * ask to replace). */
	unsigned long pml4, leaf;

	extern unsigned long user_leaf64_in(unsigned long, unsigned long);
	extern int map_page64_in(unsigned long, unsigned long, unsigned long, unsigned long);
	extern unsigned long paging64_pml4(void);

	pml4 = p->cr3_64 ? p->cr3_64 : paging64_pml4();
	leaf = user_leaf64_in(pml4, (unsigned long)vaddr);
	if(leaf) {
		if(!addr) {
			addr = leaf;
		}
	} else {
		if(!addr) {
			if(!(addr = (addr_t)kmalloc(PAGE_SIZE))) {
				return 0;
			}
			addr = V2P(addr);
			p->rss++;
		}
		if(map_page64_in(pml4, (unsigned long)vaddr, (unsigned long)addr,
				(unsigned long)(flags & 0xFFF) | 0x004 /* USER */ |
				(prot & PROT_WRITE ? 0x002 /* RW */ : 0))) {
			return 0;
		}
		/* FNX: propagate U/S up the walk. The leaf above got the US
		 * bit, but the CPU requires US at EVERY level of the walk - the
		 * pml4/pdpt/pd entries this page hangs off may still be
		 * supervisor-only (a demand-paged page inside a split identity
		 * region, or a fresh process pml4), and a user fetch/write then
		 * faults P+U+ID (0x15) even though the leaf is U/S. */
		{
			unsigned long *lvl;
#define P2V64x(a)	(((unsigned long)(a) < PAGE_OFFSET) ? \
				((unsigned long)(a) + PAGE_OFFSET) : (unsigned long)(a))
			lvl = (unsigned long *)P2V64x(pml4);
			lvl[((unsigned long)vaddr >> 39) & 0x1FF] |= 0x004UL;	/* US */
			lvl = (unsigned long *)P2V64x(lvl[((unsigned long)vaddr >> 39) & 0x1FF] & ~0xFFFUL);
			lvl[((unsigned long)vaddr >> 30) & 0x1FF] |= 0x004UL;
			lvl = (unsigned long *)P2V64x(lvl[((unsigned long)vaddr >> 30) & 0x1FF] & ~0xFFFUL);
			lvl[((unsigned long)vaddr >> 21) & 0x1FF] |= 0x004UL;
#undef P2V64x
		}
	}
	return P2V(addr);
#else
	unsigned int *pgdir, *pgtbl;
	unsigned int newaddr;
	int pde, pte;

	pgdir = (unsigned int *)P2V(p->tss.cr3);
	pde = GET_PGDIR(vaddr);
	pte = GET_PGTBL(vaddr);

	if(!(pgdir[pde] & PAGE_PRESENT)) {	/* allocating page table */
		if(!(newaddr = kmalloc(PAGE_SIZE))) {
			return 0;
		}
		p->rss++;
		pgdir[pde] = V2P(newaddr) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
		memset_b((void *)newaddr, 0, PAGE_SIZE);
	}
	pgtbl = (unsigned int *)P2V((pgdir[pde] & PAGE_MASK));
	if(!(pgtbl[pte] & PAGE_PRESENT)) {	/* allocating page */
		if(!addr) {
			if(!(addr = kmalloc(PAGE_SIZE))) {
				return 0;
			}
			addr = V2P(addr);
			p->rss++;
		}
		pgtbl[pte] = addr | PAGE_PRESENT | PAGE_USER | flags;
	} else if(!addr) {
		/* the page is already mapped in the 2-level tables (e.g. a
		 * CoW-shared page, or a re-fault after a 4-level desync):
		 * never return P2V(0) - hand back the EXISTING page so
		 * callers don't memset/kfree the kernel base page */
		addr = pgtbl[pte] & PAGE_MASK;
	}
	if(prot & PROT_WRITE) {
		pgtbl[pte] |= PAGE_RW;
	}
	return P2V(addr);
#endif /* __x86_64__ */
}

int unmap_page(addr_t vaddr)
{
#ifdef __x86_64__
	/* FNX (native-MM): clear the leaf in the ACTIVE pml4 (the 2-level
	 * shadow is gone). */
	extern int unmap_user_page64_in(unsigned long, unsigned long);
	extern unsigned long paging64_pml4(void);
	unsigned long pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4();

	unmap_user_page64_in(pml4, (unsigned long)vaddr);
	current->rss--;
	return 0;
#else
	unsigned int *pgdir, *pgtbl;
	unsigned int addr, desc;
	int pde, pte;

	pgdir = (unsigned int *)P2V(current->tss.cr3);
	pde = GET_PGDIR(vaddr);
	pte = GET_PGTBL(vaddr);
	if(!(pgdir[pde] & PAGE_PRESENT)) {
		printk("WARNING: %s(): trying to unmap an unallocated pde '0x%08x'\n", __FUNCTION__, vaddr);
		return 1;
	}

	pgtbl = (unsigned int *)P2V((pgdir[pde] & PAGE_MASK));
	if(!(pgtbl[pte] & PAGE_PRESENT)) {
		printk("WARNING: %s(): trying to unmap an unallocated page '0x%08x'\n", __FUNCTION__, vaddr);
		return 1;
	}

	desc = pgtbl[pte];
	addr = desc & PAGE_MASK;
	pgtbl[pte] = 0;
	if (!(desc & PAGE_NOALLOC)) {
		kfree(P2V(addr));
	}
	current->rss--;
	return 0;
#endif /* __x86_64__ */
}

/*
 * This function initializes and setups the kernel page directory and page
 * tables. It also reserves areas of contiguous memory spaces for internal
 * structures and for the RAMdisk drives.
 */
void mem_init(void)
{
	unsigned int sizek;
	addr_t physical_memory;
	unsigned int physical_page_tables;
	unsigned int *pgtbl;
	int n, pages, last_ramdisk;

	physical_page_tables = (kstat.physical_pages / 1024) + ((kstat.physical_pages % 1024) ? 1 : 0);
	physical_memory = (kstat.physical_pages << PAGE_SHIFT);	/* in bytes */

	/* align _last_data_addr to the next page */
	_last_data_addr = PAGE_ALIGN(_last_data_addr);

	/* Page Directory (1024 entries; 8 bytes each on x86-64 = 8KB) */
	kpage_dir = (addr_t *)_last_data_addr;
	memset_b(kpage_dir, 0,
#ifdef __x86_64__
		 PAGE_SIZE * 2
#else
		 PAGE_SIZE
#endif
	);
	_last_data_addr +=
#ifdef __x86_64__
		PAGE_SIZE * 2
#else
		PAGE_SIZE
#endif
	;

	/* Page Tables */
	pgtbl = (unsigned int *)_last_data_addr;
	memset_b(pgtbl, 0, physical_page_tables * PAGE_SIZE);
	_last_data_addr += physical_page_tables * PAGE_SIZE;

	/* Page Directory and Page Tables initialization */
	for(n = 0; n < kstat.physical_pages; n++) {
		pgtbl[n] = (n << PAGE_SHIFT) | PAGE_PRESENT | PAGE_RW;
		if(!(n % 1024)) {
			kpage_dir[GET_PGDIR(PAGE_OFFSET) + (n / 1024)] = (addr_t)&pgtbl[n] | PAGE_PRESENT | PAGE_RW;
		}
	}
	activate_kpage_dir();

	/* since Page Directory is now activated we can use virtual addresses */
	kpage_dir = (addr_t *)P2V((addr_t)kpage_dir);
	_last_data_addr = P2V(_last_data_addr);

	/* reserve memory space for proc_table[NR_PROCS] */
	proc_table_size = PAGE_ALIGN(sizeof(struct proc) * NR_PROCS);
	if(!is_addr_in_bios_map(V2P(_last_data_addr) + proc_table_size)) {
		PANIC("Not enough memory for proc_table.\n");
	}
	proc_table = (struct proc *)_last_data_addr;
	_last_data_addr += proc_table_size;


	/* reserve memory space for buffer_hash_table */
	kstat.max_buffers_size = kstat.physical_pages * (PAGE_SIZE / 1024);
	kstat.max_buffers_size = (kstat.max_buffers_size * BUFFER_PERCENTAGE) / 100;
	n = (kstat.max_buffers_size * BUFFER_HASH_PERCENTAGE) / 100;
	n = MAX(n, 10);	/* 10 buffer hashes as minimum */
	/* buffer_hash_table is an array of pointers */
	pages = ((n * sizeof(struct buffer *)) / PAGE_SIZE) + 1;
	buffer_hash_table_size = pages << PAGE_SHIFT;
	if(!is_addr_in_bios_map(V2P(_last_data_addr) + buffer_hash_table_size)) {
		PANIC("Not enough memory for buffer_hash_table.\n");
	}
	buffer_hash_table = (struct buffer **)_last_data_addr;
	_last_data_addr += buffer_hash_table_size;


	/* calculate the inode table size */
	sizek = physical_memory / 1024;	/* this helps to avoid overflow */
	inode_table_size = (sizek * INODE_PERCENTAGE) / 100;
	inode_table_size *= 1024;
	pages = inode_table_size >> PAGE_SHIFT;
	inode_table_size = pages << PAGE_SHIFT;

	/* reserve memory space for inode_hash_table */
	kstat.max_inodes = inode_table_size / sizeof(struct inode);
	n = (kstat.max_inodes * INODE_HASH_PERCENTAGE) / 100;
	n = MAX(n, 10);	/* 10 inode hash buckets as minimum */
	/* inode_hash_table is an array of pointers */
	pages = ((n * sizeof(struct inode *)) / PAGE_SIZE) + 1;
	inode_hash_table_size = pages << PAGE_SHIFT;
	if(!is_addr_in_bios_map(V2P(_last_data_addr) + inode_hash_table_size)) {
		PANIC("Not enough memory for inode_hash_table.\n");
	}
	inode_hash_table = (struct inode **)_last_data_addr;
	_last_data_addr += inode_hash_table_size;


	/* reserve memory space for fd_table[NR_OPENS] */
	fd_table_size = PAGE_ALIGN(sizeof(struct fd) * NR_OPENS);
	if(!is_addr_in_bios_map(V2P(_last_data_addr) + fd_table_size)) {
		PANIC("Not enough memory for fd_table.\n");
	}
	fd_table = (struct fd *)_last_data_addr;
	_last_data_addr += fd_table_size;


	/* reserve memory space for RAMdisk drives */
	last_ramdisk = 0;
	if(kparms.ramdisksize > 0 || ramdisk_table[0].addr) {
		/*
		 * If the 'initrd=' parameter was supplied, then the first
		 * RAMdisk drive was already assigned to the initrd image.
		 */
		if(ramdisk_table[0].addr) {
			ramdisk_table[0].addr += PAGE_OFFSET;
			last_ramdisk = 1;
		}
		for(; last_ramdisk < ramdisk_minors; last_ramdisk++) {
			if(!is_addr_in_bios_map(V2P(_last_data_addr) + (kparms.ramdisksize * 1024))) {
				kparms.ramdisksize = 0;
				ramdisk_minors -= RAMDISK_DRIVES;
				printk("WARNING: RAMdisk drive disabled (not enough physical memory).\n");
				break;
			}
			ramdisk_table[last_ramdisk].addr = (char *)_last_data_addr;
			ramdisk_table[last_ramdisk].size = kparms.ramdisksize;
			_last_data_addr += kparms.ramdisksize * 1024;
		}
	}

	/*
	 * FIXME: this is ugly!
	 * It should go in console_init() once we have a proper kernel memory/page management.
	 */
	#include <fnx/console.h>
	for(n = 1; n <= NR_VCONSOLES; n++) {
		vc_screen[n] = (short int *)_last_data_addr;
		_last_data_addr += (video.columns * video.lines * 2);
	}
	/*
	 * FIXME: this is ugly!
	 * It should go in console_init() once we have a proper kernel memory/page management.
	 */
	vcbuf = (short int *)_last_data_addr;
	_last_data_addr += (video.columns * video.lines * SCREENS_LOG * 2 * sizeof(short int));

#ifdef CONFIG_KEXEC
	if(kexec_size > 0) {
		bios_map_reserve(KEXEC_BOOT_ADDR, KEXEC_BOOT_ADDR + (PAGE_SIZE * 2));
		ramdisk_minors++;
		if(last_ramdisk < ramdisk_minors) {
			if(!is_addr_in_bios_map(V2P(_last_data_addr) + (kexec_size * 1024))) {
				kexec_size = 0;
				ramdisk_minors--;
				printk("WARNING: RAMdisk drive for kexec disabled (not enough physical memory).\n");
			} else {
				ramdisk_table[last_ramdisk].addr = (char *)_last_data_addr;
				ramdisk_table[last_ramdisk].size = kexec_size;
				_last_data_addr += kexec_size * 1024;
			}
		}
	}
#endif /* CONFIG_KEXEC */

	/* the last one must be the page_table structure */
	n = (kstat.physical_pages * PAGE_HASH_PER_10K) / 10000;
	n = MAX(n, 1);	/* 1 page for the hash table as minimum */
	n = MIN(n, MAX_PAGES_HASH);
	page_hash_table_size = n * PAGE_SIZE;
	if(!is_addr_in_bios_map(V2P(_last_data_addr) + page_hash_table_size)) {
		PANIC("Not enough memory for page_hash_table.\n");
	}
	page_hash_table = (struct page **)_last_data_addr;
	_last_data_addr += page_hash_table_size;

	/* sized by the TOP of usable memory, not by the count of usable
	 * pages: page_table[] is indexed by physical page number, and the
	 * loader's map has holes (firmware areas, the kernel image), so a
	 * usable page can sit above the count */
	page_table_size =
		PAGE_ALIGN(kstat.physical_pages_top * sizeof(struct page));
	if(!is_addr_in_bios_map(V2P(_last_data_addr) + page_table_size)) {
		PANIC("Not enough memory for page_table.\n");
	}
	page_table = (struct page *)_last_data_addr;
	_last_data_addr += page_table_size;

	page_init(kstat.physical_pages);
	buddy_low_init();
#ifdef __x86_64__
	/* FNX (pivot): the static tables are carved; hand the usable pages
	 * above them back to the single 64-bit bitmap allocator. */
	extern void mm64_postmem_init(void);
	mm64_postmem_init();
#endif /* __x86_64__ */
}

void mem_stats(void)
{
	kstat.kernel_reserved <<= 2;
	kstat.physical_reserved <<= 2;

	printk("\n");
	printk("memory: total=%dKB, user=%dKB, kernel=%dKB, reserved=%dKB\n",
		kstat.physical_pages << 2,
		kstat.total_mem_pages << 2,
		kstat.kernel_reserved, kstat.physical_reserved);
	printk("tables: procs=%d (%dKB), opens=%d (%dKB), pages=%dKB, inodes=%d\n",
		NR_PROCS, proc_table_size / 1024,
		NR_OPENS, fd_table_size / 1024,
		page_table_size / 1024,
		kstat.max_inodes);
	printk("hash tables: buffers=%d (%dKB), inodes=%d (%dKB), pages=%d (%dKB)\n",
		buffer_hash_table_size / sizeof(struct buffer *), buffer_hash_table_size / 1024,
		inode_hash_table_size / sizeof(struct inode *), inode_hash_table_size / 1024,
		page_hash_table_size / sizeof(struct page *), page_hash_table_size / 1024);
	printk("kernel: text=%dKB, data=%dKB, bss=%dKB\n\n",
		KERNEL_TEXT_SIZE / 1024, KERNEL_DATA_SIZE / 1024, KERNEL_BSS_SIZE / 1024);
}
