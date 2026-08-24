/*
 * fnx/kernel64/mm64.c
 *
 * FNX M2 (phase B): physical page allocator + dynamic 4KB-page mapping.
 *
 * The page allocator is a first-fit bitmap over usable RAM below 1GB
 * (EfiConventionalMemory / EfiBootServicesCode / EfiBootServicesData, all
 * free after ExitBootServices). Everything else - reserved/ACPI/MMIO/RT
 * regions, the low 1MB, the loader image span (LoaderCode + LoaderData and
 * the gaps between them, which hold the stub's .bss page tables), and the
 * current stack window - is marked used. Allocation granularity: 4KB pages.
 *
 * map_page64()/unmap_page64()/virt_to_phys64() walk the 4-level tables
 * installed by paging64.c (PML4 -> PDPT -> PD -> PT), allocating
 * intermediate table pages from the allocator on demand.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/efi.h>
#include <fnx/kernel.h>
#include "serial64.h"

#define PAGE_OFFSET64	0xFFFFFFFF80000000ULL
#define PAGE_SIZE64	4096
#define PAGE_SHIFT64	12
#define PAGE_MASK64	(~(PAGE_SIZE64 - 1))

#define LOW_LIMIT	0x40000000ULL		/* allocator covers phys < 1GB */
#define LOW_1MB		0x100000ULL
#define MAX_PAGES	(LOW_LIMIT >> PAGE_SHIFT64)	/* 262144 */
#define BITMAP_BYTES	(MAX_PAGES / 8)			/* 32768 */

#define PML4_INDEX(a)	(((unsigned long)(a) >> 39) & 0x1FF)
#define PDPT_INDEX(a)	(((unsigned long)(a) >> 30) & 0x1FF)
#define PD_INDEX(a)	(((unsigned long)(a) >> 21) & 0x1FF)
#define PT_INDEX(a)	(((unsigned long)(a) >> 12) & 0x1FF)

#define X86_PTE_P	0x001ULL	/* present */
#define X86_PTE_RW	0x002ULL	/* read/write */
#define X86_PTE_US	0x004ULL	/* user */
#define X86_PTE_PS	0x080ULL	/* 2MB page (PD entry) */

#define P2V64(a)	(((unsigned long)(a) < PAGE_OFFSET64) ? \
				((unsigned long)(a) + PAGE_OFFSET64) : (unsigned long)(a))
#define V2P64(a)	((unsigned long)(a) - PAGE_OFFSET64)

unsigned long paging64_pml4(void);
unsigned long paging64_pml4_phys(void);
void tlb_flush64(void);

static unsigned char page_bitmap[BITMAP_BYTES];
static unsigned long free_pages_count;
static unsigned long total_pages_count;

/* FNX (pivot): the bitmap is now the SINGLE physical allocator for the
 * whole 64-bit kernel (page tables, user pages, kernel stacks, kmalloc).
 * mm64_init runs BEFORE the real kernel's mem_init(), which later carves
 * its static tables (kpage_dir, page_table[], buffer/inode caches) right
 * after the image and computes _last_data_addr. Those carved addresses are
 * unknown here, so mm64_init conservatively marks everything from the
 * loader span to LOW_LIMIT used; mem_init() then calls mm64_postmem_init()
 * to release the region above the static tables back to the bitmap. The
 * usable spans from the EFI map are remembered so postmem can free exactly
 * the pages mem_init() left alone. */
#define MAX_USABLE_RANGES 32
static unsigned long usable_ranges[MAX_USABLE_RANGES][2];	/* start, end */
static int usable_count;

static void bit_set(unsigned long p)
{
	page_bitmap[p >> 3] |= (unsigned char)(1 << (p & 7));
}

static void bit_clear(unsigned long p)
{
	page_bitmap[p >> 3] &= (unsigned char)~(1 << (p & 7));
}

static int bit_test(unsigned long p)
{
	return (page_bitmap[p >> 3] & (unsigned char)(1 << (p & 7))) ? 1 : 0;
}

/* mark [start, end) used (pages outside the bitmap range are ignored) */
static void mark_used(unsigned long start, unsigned long end)
{
	unsigned long p, first, last;

	if(start >= end) {
		return;
	}
	first = start >> PAGE_SHIFT64;
	last = (end - 1) >> PAGE_SHIFT64;
	if(last >= MAX_PAGES) {
		last = MAX_PAGES - 1;
	}
	for(p = first; p <= last; p++) {
		bit_set(p);
	}
}

void mm64_init(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size, UINTN desc_size)
{
	EFI_MEMORY_DESCRIPTOR *d;
	unsigned long start, end, loader_min, loader_max, rsp, p;
	UINTN n, count;
	int usable;

	loader_min = ~0UL;
	loader_max = 0;
	count = map_size / desc_size;

	/* default: every page below 1GB is used */
	for(n = 0; n < BITMAP_BYTES; n++) {
		page_bitmap[n] = 0xFF;
	}

	/* usable pages below 1GB become free; track the loader image span */
	usable_count = 0;
	for(n = 0; n < count; n++) {
		d = (EFI_MEMORY_DESCRIPTOR *)((char *)map + (n * desc_size));
		start = (unsigned long)d->PhysicalStart;
		if(start >= LOW_LIMIT) {
			continue;
		}
		end = start + ((unsigned long)d->NumberOfPages << PAGE_SHIFT64);
		usable = (d->Type == EfiConventionalMemory ||
			  d->Type == EfiBootServicesCode ||
			  d->Type == EfiBootServicesData);
		if(usable) {
			for(p = start >> PAGE_SHIFT64; p < (end >> PAGE_SHIFT64); p++) {
				if(p < MAX_PAGES) {
					bit_clear(p);
				}
			}
			/* remember the span so mm64_postmem_init() can free the
			 * part above the kernel's static tables later */
			if(usable_count < MAX_USABLE_RANGES) {
				usable_ranges[usable_count][0] = start;
				usable_ranges[usable_count][1] = end;
				usable_count++;
			}
		} else if(d->Type == EfiLoaderCode || d->Type == EfiLoaderData) {
			if(start < loader_min) {
				loader_min = start;
			}
			if(end > loader_max) {
				loader_max = end;
			}
		}
	}

	/* subtract the low 1MB, the loader image span, and the stack window */
	mark_used(0, LOW_1MB);
	if(loader_max > loader_min) {
		mark_used(loader_min, loader_max);
	}
	/* FNX (M6-H): cap the bitmap's allocatable range to BELOW the
	 * kernel image. The real kernel's mem_init() places its static tables
	 * (kpage_dir, page_table[], buffer/inode caches) right after the image
	 * and its allocator reserves everything from 0x100000 to _last_data_addr
	 * (>= loader_max), so granting bitmap pages up there would collide with
	 * live kernel structures (observed: the page-table array's refcounts got
	 * clobbered by a 4-level table grant). Below the image the FNX
	 * allocator never hands anything out, so the two allocators are
	 * disjoint. */
	mark_used(loader_min, LOW_LIMIT);
	rsp = get_rsp();
	mark_used(rsp - 0x20000, rsp + 0x1000);

	free_pages_count = 0;
	total_pages_count = 0;
	for(p = 0; p < MAX_PAGES; p++) {
		if(!bit_test(p)) {
			free_pages_count++;
		}
	}
	/* recount usable pages for stats */
	for(n = 0; n < count; n++) {
		d = (EFI_MEMORY_DESCRIPTOR *)((char *)map + (n * desc_size));
		start = (unsigned long)d->PhysicalStart;
		if(start >= LOW_LIMIT) {
			continue;
		}
		end = start + ((unsigned long)d->NumberOfPages << PAGE_SHIFT64);
		if(d->Type == EfiConventionalMemory ||
		   d->Type == EfiBootServicesCode ||
		   d->Type == EfiBootServicesData) {
			total_pages_count += (end - start) >> PAGE_SHIFT64;
		}
	}
}

/* FNX (pivot): called from mem_init() AFTER the kernel has carved its
 * static tables (kpage_dir, page_table[], buffer/inode caches...) right
 * after the image and computed _last_data_addr. The bitmap is the single
 * physical allocator, so the usable pages ABOVE the static tables - the
 * region the FNX free-list used to manage - are handed back to it here.
 * mm64_init() had conservatively marked [loader_min, LOW_LIMIT) used
 * because _last_data_addr was not known yet. */
void mm64_postmem_init(void)
{
	extern unsigned long _last_data_addr;	/* kernel memory.c */
	extern struct kernel_stat kstat;
	unsigned long last_data_phys, start, end;
	int n;

	last_data_phys = (unsigned long)_last_data_addr;
	if(last_data_phys >= PAGE_OFFSET64) {
		last_data_phys -= PAGE_OFFSET64;	/* V2P */
	}
	for(n = 0; n < usable_count; n++) {
		start = usable_ranges[n][0];
		end = usable_ranges[n][1];
		if(end <= last_data_phys) {
			continue;
		}
		if(start < last_data_phys) {
			start = last_data_phys;
		}
		if(start < LOW_LIMIT && end > start) {
			/* re-free this span (mm64_init marked it used) */
			unsigned long p;
			for(p = start >> PAGE_SHIFT64; p < (end >> PAGE_SHIFT64); p++) {
				if(p < MAX_PAGES && bit_test(p)) {
					bit_clear(p);
					free_pages_count++;
				}
			}
		}
	}
	/* the page_init() free-list is not built in the 64-bit build; keep
	 * the kstat pool stats in sync with the single bitmap allocator */
	kstat.total_mem_pages = (int)total_pages_count;
	kstat.free_pages = (int)free_pages_count;
	kstat.min_free_pages = (kstat.total_mem_pages * 20) / 100;
}

/* first-fit allocation of n consecutive pages; returns the physical address */
unsigned long alloc_pages64(int n)
{
	unsigned long p, i, run;

	if(n <= 0) {
		return 0;
	}
	for(p = 0; p < MAX_PAGES; p++) {
		if(bit_test(p)) {
			continue;
		}
		run = 0;
		for(i = p; i < MAX_PAGES && run < n; i++) {
			if(bit_test(i)) {
				break;
			}
			run++;
		}
		if(run >= n) {
			for(i = p; i < (p + n); i++) {
				bit_set(i);
			}
			free_pages_count -= n;
#ifdef M6_BITMAP_DEBUG
			printk("[bitmap] alloc_pages64(%d) = phys 0x%lx\n", n, p << PAGE_SHIFT64);
#endif /* M6_BITMAP_DEBUG */
			return (p << PAGE_SHIFT64);
		}
		p = i;
	}
	return 0;
}

void free_pages64(unsigned long phys, int n)
{
	unsigned long p, first;

	first = phys >> PAGE_SHIFT64;
	for(p = first; p < (first + n); p++) {
		bit_clear(p);
	}
	free_pages_count += n;
}

unsigned long pages_free64(void)
{
	return free_pages_count;
}

unsigned long pages_total64(void)
{
	return total_pages_count;
}

/* zero a page through its kernel-virtual (high-half) alias */
static void clear_page(unsigned long phys)
{
	unsigned char *p;
	int i;

	p = (unsigned char *)P2V64(phys);
	for(i = 0; i < PAGE_SIZE64; i++) {
		p[i] = 0;
	}
}

/* allocate a zeroed table page, return its physical address (0 = failure) */
static unsigned long alloc_table_page(void)
{
	unsigned long phys;

	/* Allocated from the 64-bit bitmap, whose allocatable range is capped
	 * BELOW the kernel image (see mm64_init) so it never collides with the
	 * real kernel's kmalloc pages (static tables + user pages). */
	phys = alloc_pages64(1);
	if(phys) {
		clear_page(phys);
	}
	return phys;
}

/* map one 4KB page at vaddr -> paddr; allocates intermediate tables on
 * demand and splits a 2MB huge page if the target PD entry has PS set.
 * FNX (M6-next): the walk starts at the GIVEN pml4 (per-process page
 * tables), not the kernel's shared one. */
int map_page64_in(unsigned long pml4, unsigned long vaddr, unsigned long paddr,
		  unsigned long flags)
{
	unsigned long *lvl, phys, *entry, *pt;
	unsigned long base;
	int i, split;

	split = 0;
	lvl = (unsigned long *)P2V64(pml4);
	entry = &lvl[PML4_INDEX(vaddr)];
	if(!(*entry & X86_PTE_P)) {
		phys = alloc_table_page();
		if(!phys) {
			return 1;
		}
		*entry = phys | X86_PTE_P | X86_PTE_RW;
	}
	lvl = (unsigned long *)P2V64(*entry & PAGE_MASK64);
	entry = &lvl[PDPT_INDEX(vaddr)];
	if(!(*entry & X86_PTE_P)) {
		phys = alloc_table_page();
		if(!phys) {
			return 1;
		}
		*entry = phys | X86_PTE_P | X86_PTE_RW;
	}
	lvl = (unsigned long *)P2V64(*entry & PAGE_MASK64);
	entry = &lvl[PD_INDEX(vaddr)];
	if(!(*entry & X86_PTE_P)) {
		phys = alloc_table_page();
		if(!phys) {
			return 1;
		}
		*entry = phys | X86_PTE_P | X86_PTE_RW;
	} else if(*entry & X86_PTE_PS) {
		/* split the 2MB huge page. For a HIGH-HALF (kernel) huge page
		 * (the INIT trampoline/stack live there), the other 511 entries
		 * MUST keep the identity phys - kernel text/data share the same
		 * 2MB page and would vanish. For a LOW-half user huge page
		 * (vaddr < PAGE_OFFSET64), pre-filling identity leaves maps the
		 * phys of pages the bitmap may LATER re-grant as table pages
		 * (e.g. a fork child's PT at 0x400000): the stale leaf then
		 * aliases the table page and file/ELF content overwrites it. So
		 * only the requested 4KB leaf is mapped there; the other 511
		 * entries stay NOT-PRESENT (the PT was zeroed by
		 * alloc_table_page). */
		base = *entry & ~0x1FFFFFUL;
		phys = alloc_table_page();
		if(!phys) {
			return 1;
		}
		pt = (unsigned long *)P2V64(phys);
		if(vaddr >= PAGE_OFFSET64) {
			for(i = 0; i < 512; i++) {
				pt[i] = (base + ((unsigned long)i << 12)) |
					((*entry & 0xFFFUL) & ~X86_PTE_PS) | X86_PTE_P;
			}
		}
		*entry = phys | X86_PTE_P | X86_PTE_RW;
		split = 1;
	}
	lvl = (unsigned long *)P2V64(*entry & PAGE_MASK64);
	lvl[PT_INDEX(vaddr)] = (paddr & PAGE_MASK64) | (flags & 0xFFFUL) | X86_PTE_P;
	/* FNX: flush - the CR3 reload plus an explicit invlpg for the
	 * changed leaf. Some TCGs skip the flush when the CR3 value is
	 * unchanged, leaving the stale 2MB/split entry cached. */
	tlb_flush64();
	__asm__ __volatile__("invlpg (%0)" :: "r"(vaddr) : "memory");
	return 0;
}

int map_page64(unsigned long vaddr, unsigned long paddr, unsigned long flags)
{
	return map_page64_in(paging64_pml4(), vaddr, paddr, flags);
}

/* map a 4KB page readable/writable by user mode (CPL3): the leaf gets
 * U/S and, critically, the U/S bit is propagated up the PML4/PDPT/PD
 * path - the CPU requires U/S at every level of the walk */
int map_user_page64_in(unsigned long pml4, unsigned long vaddr,
		       unsigned long paddr, unsigned long flags)
{
	unsigned long *lvl;

	if(map_page64_in(pml4, vaddr, paddr, (flags & 0xFFFUL) | X86_PTE_US)) {
		return 1;
	}
	lvl = (unsigned long *)P2V64(pml4);
	lvl[PML4_INDEX(vaddr)] |= X86_PTE_US;
	lvl = (unsigned long *)P2V64(lvl[PML4_INDEX(vaddr)] & PAGE_MASK64);
	lvl[PDPT_INDEX(vaddr)] |= X86_PTE_US;
	lvl = (unsigned long *)P2V64(lvl[PDPT_INDEX(vaddr)] & PAGE_MASK64);
	lvl[PD_INDEX(vaddr)] |= X86_PTE_US;
	tlb_flush64();
	return 0;
}

int map_user_page64(unsigned long vaddr, unsigned long paddr, unsigned long flags)
{
	return map_user_page64_in(paging64_pml4(), vaddr, paddr, flags);
}

/* clear the leaf PTE for a user virtual address from the given pml4 - the
 * counterpart of map_user_page64_in. munmap/free_vma_pages use it so a freed
 * mapping doesn't leave a stale present PTE in the ACTIVE tables (which would
 * make a later mmap that reuses the address skip the page fault and read the
 * old content). */
int unmap_user_page64_in(unsigned long pml4, unsigned long vaddr)
{
	unsigned long *lvl;

	lvl = (unsigned long *)P2V64(pml4);
	if(!(lvl[PML4_INDEX(vaddr)] & X86_PTE_P)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64(lvl[PML4_INDEX(vaddr)] & PAGE_MASK64);
	if(!(lvl[PDPT_INDEX(vaddr)] & X86_PTE_P)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64(lvl[PDPT_INDEX(vaddr)] & PAGE_MASK64);
	if(!(lvl[PD_INDEX(vaddr)] & X86_PTE_P)) {
		return 0;
	}
	if(lvl[PD_INDEX(vaddr)] & X86_PTE_PS) {
		/* 2MB huge page - never a user demand-mapped page, but clear it
		 * anyway so the address is truly free */
		lvl[PD_INDEX(vaddr)] = 0;
		tlb_flush64();
		return 0;
	}
	lvl = (unsigned long *)P2V64(lvl[PD_INDEX(vaddr)] & PAGE_MASK64);
	lvl[PT_INDEX(vaddr)] = 0;
	tlb_flush64();
	return 0;
}

/* FNX (M6-next): per-process page tables. Each user process gets its
 * own 4-level tables: the low-4GB identity/user hierarchy is deep-copied
 * (private PDPT + the 4 PD pages so splits never touch the kernel's or
 * another process's tables), while PML4[511] (the kernel high half) stays
 * SHARED with the kernel. Returns the new pml4's PHYSICAL address (0 on
 * failure). */
void free_pml4_64(unsigned long pml4_phys);

unsigned long create_pml4_64(unsigned long src_pml4_phys)
{
	unsigned long *src4, *pml4, *spdpt, *pdpt, *spd, *pd, *spt, *pt;
	unsigned long pml4_phys, pdpt_phys, pd_phys, pt_phys;
	unsigned long e;
	extern unsigned long paging64_pml4_phys(void);
	int is_fork, i, j, k;

	src4 = (unsigned long *)P2V64(src_pml4_phys);

	/* Fork passes the PARENT's pml4; INIT/exec pass the shared KERNEL
	 * pml4. Only the fork case may make the source's writable user leaves
	 * read-only (CoW) - the kernel pml4 is SHARED, and its low-4GB may
	 * hold stale user mappings (e.g. the INIT trampoline stack) that the
	 * kernel itself still needs to reach read-write via the high half. */
	is_fork = (src_pml4_phys != paging64_pml4_phys());

	pml4_phys = alloc_table_page();
	if(!pml4_phys) {
		return 0;
	}
	pml4 = (unsigned long *)P2V64(pml4_phys);

	/* FNX (canonical amd64 split): the USER half is pml4[0..255]
	 * (VA 0 .. 0x00007FFFFFFFFFFF, 128TB); the KERNEL half is
	 * pml4[256..511] and is SHARED with the kernel. A FORK child
	 * deep-copies the parent's user half (every present PML4[i] ->
	 * PDPT -> PDs -> PTs) so it inherits all demand-mapped pages
	 * (text/data/stack/TLS - wherever they live in the 128TB user half);
	 * USER leaf pages are mapped read-only (CoW) so a later write faults
	 * into the copy-on-write path. INIT/exec pass the shared KERNEL pml4
	 * (src == kernel): its pml4[0] is the low-1GB identity map, which
	 * must NOT be copied - the process user half starts EMPTY and every
	 * page is demand-mapped fresh. */
	for(i = 0; i < 256; i++) {
		if(!is_fork) {
			continue;	/* exec/INIT: empty user half */
		}
		if(!(src4[i] & X86_PTE_P)) {
			continue;	/* pml4[i] stays 0 (not mapped) */
		}
		pdpt_phys = alloc_table_page();
		if(!pdpt_phys) {
			free_pml4_64(pml4_phys);
			return 0;
		}
		pml4[i] = pdpt_phys | (src4[i] & 0xFFFUL);
		pdpt = (unsigned long *)P2V64(pdpt_phys);
		spdpt = (unsigned long *)P2V64(src4[i] & PAGE_MASK64);
		for(j = 0; j < 512; j++) {
			if(!(spdpt[j] & X86_PTE_P)) {
				continue;	/* pdpt[j] stays 0 (not mapped) */
			}
			pd_phys = alloc_table_page();
			if(!pd_phys) {
				free_pml4_64(pml4_phys);
				return 0;
			}
			pdpt[j] = pd_phys | (spdpt[j] & 0xFFFUL);
			pd = (unsigned long *)P2V64(pd_phys);
			spd = (unsigned long *)P2V64(spdpt[j] & PAGE_MASK64);
			for(k = 0; k < 512; k++) {
				e = spd[k];
				if((e & (X86_PTE_P | X86_PTE_PS)) == X86_PTE_P) {
					/* split 4KB page: private PT, user leaves CoW */
					pt_phys = alloc_table_page();
					if(!pt_phys) {
						free_pml4_64(pml4_phys);
						return 0;
					}
					pd[k] = pt_phys | (e & 0xFFFUL);
					pt = (unsigned long *)P2V64(pt_phys);
					spt = (unsigned long *)P2V64(e & PAGE_MASK64);
					{
						unsigned long *s_pt = spt;
						unsigned long *d_pt = pt;
						int m;
						unsigned long leaf;
						for(m = 0; m < 512; m++) {
							leaf = s_pt[m];
							/* CoW: a writable USER leaf is shared
							 * read-only in BOTH the child (copy)
							 * and the fork source (parent), so the
							 * first write by either side faults
							 * into the copy-on-write path. Only for
							 * MAP_PRIVATE pages (MAP_SHARED pages
							 * stay writable). */
							if((leaf & X86_PTE_US) && (leaf & X86_PTE_RW)) {
								extern int vma_is_shared(unsigned long);
								unsigned long va = ((unsigned long)i << 39)
									| ((unsigned long)j << 30)
									| ((unsigned long)k << 21)
									| ((unsigned long)m << 12);
								if(!vma_is_shared(va)) {
									leaf &= ~X86_PTE_RW;
									if(is_fork) {
										s_pt[m] = leaf;
									}
								}
							}
							d_pt[m] = leaf;
							/* a FORK child inherits a reference to
							 * every user leaf it maps; see the
							 * pivot notes in mm64.c. */
							if(is_fork && (leaf & X86_PTE_US) && (leaf & X86_PTE_P)) {
								extern void page_ref_get(unsigned long);
								page_ref_get(leaf & PAGE_MASK64);
							}
						}
					}
				} else {
					pd[k] = e;
				}
			}
		}
	}
	/* the kernel half (pml4[256..511]) is SHARED with the kernel; no
	 * per-process copy, never freed by free_pml4_64(). The entries are
	 * supervisor (the kernel high half is not user-reachable in the
	 * canonical split; user code no longer lives there). */
	for(i = 256; i < 512; i++) {
		pml4[i] = src4[i];
	}

	if(!is_fork) {
		/* FNX (canonical amd64 split): the PIC kernel image is
		 * loaded by the firmware at load_base and EXECUTES at its
		 * high-half alias, but every function pointer stored in kernel
		 * DATA (syscall_table64, tty->output, IDT gates, file_operations,
		 * ...) holds an IDENTITY address - the firmware's PE base
		 * relocations add (load_base - image_base) to the link VMA, so
		 * e.g. tty->output = load_base + 0x40200. A per-process pml4
		 * must therefore ALSO map the kernel image 1:1 (supervisor 4KB),
		 * or the first indirect call through such a pointer (the very
		 * first printk in the syscall path -> tty->output) faults.
		 * Only the image range is mapped: the rest of the low half stays
		 * empty so user code (trampoline 0x100000, ELF at 0x400000,
		 * mmap at 64TB) never aliases kernel phys. Fork children inherit
		 * these supervisor leaves via the deep copy above. */
		extern unsigned long fnx_load_base, fnx_image_size;
		unsigned long va;

		for(va = fnx_load_base;
		    va < fnx_load_base + fnx_image_size;
		    va += PAGE_SIZE64) {
			if(map_page64_in(pml4_phys, va, va, X86_PTE_P | X86_PTE_RW)) {
				free_pml4_64(pml4_phys);
				return 0;
			}
		}
	}
	return pml4_phys;
}

/* free a per-process pml4: every private user-half PDPT (pml4[0..255]),
 * its PDs and split PT pages, and the pml4 itself. The kernel half
 * (pml4[256..511]) is SHARED with the kernel and never freed. Never free
 * the kernel's own pml4 (the caller must not pass it). */
void free_pml4_64(unsigned long pml4_phys)
{
	unsigned long *pml4, *pdpt, *pd;
	int i, j, m;

	if(!pml4_phys || pml4_phys == paging64_pml4_phys()) {
		return;
	}
	pml4 = (unsigned long *)P2V64(pml4_phys);	for(i = 0; i < 256; i++) {
		if(!(pml4[i] & X86_PTE_P)) {
			continue;
		}
		pdpt = (unsigned long *)P2V64(pml4[i] & PAGE_MASK64);
		for(j = 0; j < 512; j++) {
			if(!(pdpt[j] & X86_PTE_P)) {
				continue;
			}
			if(pdpt[j] & X86_PTE_PS) {
				/* 2MB huge page: no PT page, free just the PD */
				free_pages64(pdpt[j] & PAGE_MASK64, 1);
				continue;
			}
			pd = (unsigned long *)P2V64(pdpt[j] & PAGE_MASK64);
			/* split 4KB tables: free the PT pages; 2MB huge pages
			 * under a PD have no PT page (their phys is user data,
			 * freed via free_vma_pages) */
			for(m = 0; m < 512; m++) {
				if((pd[m] & X86_PTE_P) && !(pd[m] & X86_PTE_PS)) {
					free_pages64(pd[m] & PAGE_MASK64, 1);
				}
			}
			free_pages64(pdpt[j] & PAGE_MASK64, 1);
		}
		free_pages64(pml4[i] & PAGE_MASK64, 1);
	}
	free_pages64(pml4_phys, 1);
}

void unmap_page64(unsigned long vaddr)
{	unsigned long *lvl, *pt;

	lvl = (unsigned long *)P2V64(paging64_pml4());
	if(!(lvl[PML4_INDEX(vaddr)] & X86_PTE_P)) {
		return;
	}
	lvl = (unsigned long *)P2V64(lvl[PML4_INDEX(vaddr)] & PAGE_MASK64);
	if(!(lvl[PDPT_INDEX(vaddr)] & X86_PTE_P)) {
		return;
	}
	lvl = (unsigned long *)P2V64(lvl[PDPT_INDEX(vaddr)] & PAGE_MASK64);
	if(!(lvl[PD_INDEX(vaddr)] & X86_PTE_P)) {
		return;
	}
	if(lvl[PD_INDEX(vaddr)] & X86_PTE_PS) {
		return;
	}
	pt = (unsigned long *)P2V64(lvl[PD_INDEX(vaddr)] & PAGE_MASK64);
	pt[PT_INDEX(vaddr)] = 0;
	tlb_flush64();
}

unsigned long virt_to_phys64(unsigned long vaddr)
{
	unsigned long *lvl, e;

	lvl = (unsigned long *)P2V64(paging64_pml4());
	e = lvl[PML4_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64(e & PAGE_MASK64);
	e = lvl[PDPT_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64(e & PAGE_MASK64);
	e = lvl[PD_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	if(e & X86_PTE_PS) {
		return (e & ~0x1FFFFFULL) + (vaddr & 0x1FFFFF);
	}
	lvl = (unsigned long *)P2V64(e & PAGE_MASK64);
	e = lvl[PT_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	return (e & PAGE_MASK64) + (vaddr & 0xFFF);
}

/*
 * FNX (native-MM): the process's per-process pml4 is the SINGLE source
 * of truth (the 2-level pgdir shadow is gone). user_leaf64_in() returns the
 * physical address of the present USER 4KB leaf covering 'vaddr' in the
 * given pml4, or 0 when the address is NOT user-mapped: absent anywhere in
 * the walk, a 2MB identity/supervisor huge page, or a supervisor leaf.
 * The MM fault path uses this to decide between demand-mapping (0) and
 * copy-on-write / protection handling (non-0).
 */
unsigned long user_leaf64_in(unsigned long pml4, unsigned long vaddr)
{
	unsigned long *lvl, e;

	lvl = (unsigned long *)P2V64(pml4);
	e = lvl[PML4_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64(e & PAGE_MASK64);
	e = lvl[PDPT_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	lvl = (unsigned long *)P2V64(e & PAGE_MASK64);
	e = lvl[PD_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	if(e & X86_PTE_PS) {
		return 0;	/* 2MB identity page - not a user leaf */
	}
	lvl = (unsigned long *)P2V64(e & PAGE_MASK64);
	e = lvl[PT_INDEX(vaddr)];
	if(!(e & X86_PTE_P)) {
		return 0;
	}
	if(!(e & X86_PTE_US)) {
		return 0;	/* supervisor leaf */
	}
	return e & PAGE_MASK64;
}

void tlb_flush64(void)
{
	unsigned long cr3;

	__asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
	__asm__ __volatile__("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

/* ------------------------------------------------------------------ */
/* M2-E: slab kmalloc over the page allocator. Size classes 16..2048;  */
/* larger requests take whole pages. Every allocation has a 16-byte    */
/* header at the start of its page, so kfree64() can recover the size. */
/* ------------------------------------------------------------------ */

struct kmem_hdr {
	unsigned int magic;	/* KMEM_MAGIC */
	unsigned int bucket;	/* size class index, or KMEM_HUGE */
	unsigned int npages;	/* huge allocations only */
	unsigned int pad;
};

#define KMEM_MAGIC	0x4D454D4B	/* 'KMEM' */
#define KMEM_HUGE	8		/* bucket index for > 2048 bytes */
#define KMEM_MAX_SLAB	2048
#define KMEM_BUCKETS	8		/* classes 16,32,...,2048 */

static unsigned long slab_free_head[KMEM_BUCKETS];	/* phys of next free slot */
static unsigned long slab_free_count[KMEM_BUCKETS];
static unsigned long slab_pages_count[KMEM_BUCKETS];

static int slab_bucket(unsigned long size)
{
	int b;

	for(b = 0; b < KMEM_BUCKETS; b++) {
		if(size <= (16UL << b)) {
			return b;
		}
	}
	return KMEM_HUGE;
}

/* carve a fresh page into slots of class 16<<b and link them as free */
static int slab_new_page(int b)
{
	unsigned long class, phys, slot;
	unsigned long n, i;

	class = 16UL << b;
	phys = alloc_pages64(1);
	if(!phys) {
		return 0;
	}
	((struct kmem_hdr *)P2V64(phys))->magic = KMEM_MAGIC;
	((struct kmem_hdr *)P2V64(phys))->bucket = b;
	((struct kmem_hdr *)P2V64(phys))->npages = 1;
	n = (PAGE_SIZE64 - sizeof(struct kmem_hdr)) / class;
	for(i = 0; i < n; i++) {
		slot = phys + sizeof(struct kmem_hdr) + (i * class);
		*(unsigned long *)P2V64(slot) = (i == n - 1) ? 0 : (slot + class);
	}
	slab_free_head[b] = phys + sizeof(struct kmem_hdr);
	slab_free_count[b] = n;
	slab_pages_count[b]++;
	return 1;
}

/* kmalloc64: size in bytes -> kernel-virtual address (high half) */
void *kmalloc64(unsigned long size)
{
	unsigned long phys, npages, slot;
	int b;

	if(size > KMEM_MAX_SLAB) {
		npages = (size + sizeof(struct kmem_hdr) + PAGE_SIZE64 - 1) >> PAGE_SHIFT64;
		phys = alloc_pages64(npages);
		if(!phys) {
			return NULL;
		}
		((struct kmem_hdr *)P2V64(phys))->magic = KMEM_MAGIC;
		((struct kmem_hdr *)P2V64(phys))->bucket = KMEM_HUGE;
		((struct kmem_hdr *)P2V64(phys))->npages = npages;
		return (void *)(P2V64(phys) + sizeof(struct kmem_hdr));
	}

	b = slab_bucket(size);
	slot = slab_free_head[b];
	if(!slot) {
		if(!slab_new_page(b)) {
			return NULL;
		}
		slot = slab_free_head[b];
	}
	slab_free_head[b] = *(unsigned long *)P2V64(slot);	/* pop */
	slab_free_count[b]--;
	return (void *)P2V64(slot);
}

/* kfree64: release a kmalloc64() allocation (size recovered from header) */
void kfree64(void *ptr)
{
	unsigned long phys, page_start;
	struct kmem_hdr *hdr;
	int b;

	if(!ptr) {
		return;
	}
	phys = V2P64((unsigned long)ptr);
	page_start = phys & PAGE_MASK64;
	hdr = (struct kmem_hdr *)P2V64(page_start);
	if(hdr->magic != KMEM_MAGIC) {
		serial_puts("kfree64: bad magic at ");
		serial_hex((UINT64)phys);
		serial_puts("\n");
		for(;;) {
			__asm__ __volatile__("hlt");
		}
	}
	b = hdr->bucket;
	if(b == KMEM_HUGE) {
		free_pages64(page_start, hdr->npages);
		return;
	}
	/* return the slot to its bucket's free list */
	*(unsigned long *)P2V64(phys) = slab_free_head[b];
	slab_free_head[b] = phys;
	slab_free_count[b]++;
}
