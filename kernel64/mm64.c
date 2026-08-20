/*
 * fiwix/kernel64/mm64.c
 *
 * Fiwix64 M2 (phase B): physical page allocator + dynamic 4KB-page mapping.
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
 * intermediate table pages from the allocator on demand. The demo in
 * mm64_demo() maps a scratch region at 0xFFFFFFFFC0000000 (PDPT entry 511,
 * beyond the fixed 1GB high-half map) with 4KB pages.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fiwix/efi.h>
#include "serial64.h"

#define PAGE_OFFSET64	0xFFFFFFFF80000000ULL
#define PAGE_SIZE64	4096
#define PAGE_SHIFT64	12
#define PAGE_MASK64	(~(PAGE_SIZE64 - 1))

#define LOW_LIMIT	0x40000000ULL		/* allocator covers phys < 1GB */
#define LOW_1MB		0x100000ULL
#define MAX_PAGES	(LOW_LIMIT >> PAGE_SHIFT64)	/* 262144 */
#define BITMAP_BYTES	(MAX_PAGES / 8)			/* 32768 */

/* demo scratch region: PDPT[511] (0xFFFFFFFFC0000000..0xFFFFFFFFDFFFFFFF) */
#define SCRATCH_VA	0xFFFFFFFFC0000000ULL

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
void tlb_flush64(void);

static unsigned char page_bitmap[BITMAP_BYTES];
static unsigned long free_pages_count;
static unsigned long total_pages_count;

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

	phys = alloc_pages64(1);
	if(phys) {
		clear_page(phys);
	}
	return phys;
}

/* map one 4KB page at vaddr -> paddr; allocates intermediate tables on
 * demand and splits a 2MB huge page if the target PD entry has PS set.
 * Fiwix64 (M6-next): the walk starts at the GIVEN pml4 (per-process page
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
		/* split the 2MB huge page: cover the same phys with a 4KB PT */
		base = *entry & ~0x1FFFFFUL;
		phys = alloc_table_page();
		if(!phys) {
			return 1;
		}
		pt = (unsigned long *)P2V64(phys);
		for(i = 0; i < 512; i++) {
			pt[i] = (base + ((unsigned long)i << 12)) |
				((*entry & 0xFFFUL) & ~X86_PTE_PS) | X86_PTE_P;
		}
		*entry = phys | X86_PTE_P | X86_PTE_RW;
		split = 1;
	}
	lvl = (unsigned long *)P2V64(*entry & PAGE_MASK64);
	lvl[PT_INDEX(vaddr)] = (paddr & PAGE_MASK64) | (flags & 0xFFFUL) | X86_PTE_P;
	if(split) {
		tlb_flush64();	/* drop the stale 2MB TLB entry */
	}
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

/* Fiwix64 (M6-next): per-process page tables. Each user process gets its
 * own 4-level tables: the low-4GB identity/user hierarchy is deep-copied
 * (private PDPT + the 4 PD pages so splits never touch the kernel's or
 * another process's tables), while PML4[511] (the kernel high half) stays
 * SHARED with the kernel. Returns the new pml4's PHYSICAL address (0 on
 * failure). */
unsigned long create_pml4_64(void)
{
	unsigned long *kml4, *pml4, *kpdpt, *pdpt, *kpd, *pd;
	unsigned long pml4_phys, pdpt_phys, pd_phys;
	int i, j;

	kml4 = (unsigned long *)P2V64(paging64_pml4());

	pml4_phys = alloc_table_page();
	if(!pml4_phys) {
		return 0;
	}
	pml4 = (unsigned long *)P2V64(pml4_phys);
	pml4[0] = 0;

	/* deep-copy the low-4GB hierarchy (kernel PML4[0] -> PDPT -> PDs) */
	if(kml4[0] & X86_PTE_P) {
		pdpt_phys = alloc_table_page();
		if(!pdpt_phys) {
			free_pages64(pml4_phys, 1);
			return 0;
		}
		pdpt = (unsigned long *)P2V64(pdpt_phys);
		kpdpt = (unsigned long *)P2V64(kml4[0] & PAGE_MASK64);
		for(i = 0; i < 512; i++) {
			pdpt[i] = kpdpt[i];
		}
		/* the low 4GB (PDPT[0..3]) gets PRIVATE PD pages */
		for(i = 0; i < 4; i++) {
			if(!(pdpt[i] & X86_PTE_P)) {
				continue;
			}
			pd_phys = alloc_table_page();
			if(!pd_phys) {
				free_pages64(pdpt_phys, 1);
				free_pages64(pml4_phys, 1);
				return 0;
			}
			pd = (unsigned long *)P2V64(pd_phys);
			kpd = (unsigned long *)P2V64(pdpt[i] & PAGE_MASK64);
			for(j = 0; j < 512; j++) {
				pd[j] = kpd[j];
			}
			pdpt[i] = pd_phys | (pdpt[i] & 0xFFFUL);
		}
		pml4[0] = pdpt_phys | (kml4[0] & 0xFFFUL);
	}
	/* everything else (kernel high half etc.) stays shared */
	for(i = 1; i < 512; i++) {
		pml4[i] = kml4[i];
	}
	return pml4_phys;
}

/* free a per-process pml4: the private PDPT, the 4 private PDs, any split
 * PT pages under them, and the pml4 itself. Never free the kernel's own
 * pml4 (the caller must not pass it). */
void free_pml4_64(unsigned long pml4_phys)
{
	unsigned long *pml4, *pdpt, *pd, *pt;
	int i, j;

	if(!pml4_phys || pml4_phys == paging64_pml4()) {
		return;
	}
	pml4 = (unsigned long *)P2V64(pml4_phys);
	if(!(pml4[0] & X86_PTE_P)) {
		free_pages64(pml4_phys, 1);
		return;
	}
	pdpt = (unsigned long *)P2V64(pml4[0] & PAGE_MASK64);
	for(i = 0; i < 4; i++) {
		if(!(pdpt[i] & X86_PTE_P)) {
			continue;
		}
		pd = (unsigned long *)P2V64(pdpt[i] & PAGE_MASK64);
		for(j = 0; j < 512; j++) {
			if((pd[j] & X86_PTE_P) && !(pd[j] & X86_PTE_PS)) {
				/* split 4KB table: free the PT page */
				free_pages64(pd[j] & PAGE_MASK64, 1);
			}
		}
		free_pages64(pdpt[i] & PAGE_MASK64, 1);
	}
	free_pages64(pml4[0] & PAGE_MASK64, 1);
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

void kmem_stats64(void)
{
	int b;

	for(b = 0; b < KMEM_BUCKETS; b++) {
		serial_puts("[M2-E] slab class ");
		putdec64((UINT64)(16 << b));
		serial_puts(": pages=");
		putdec64((UINT64)slab_pages_count[b]);
		serial_puts(" free=");
		putdec64((UINT64)slab_free_count[b]);
		serial_puts("\n");
	}
}

/* exercise: allocate 4 pages, map them at SCRATCH_VA, write/read, verify,
 * then unmap and free */
void mm64_demo(void)
{
	unsigned long paddr, vaddr, phys;
	char *p;
	int n, ok;

	serial_puts("\n[M2-B] page allocator: free pages=");
	putdec64((UINT64)free_pages_count);
	serial_puts("\n");

	paddr = alloc_pages64(4);
	if(!paddr) {
		serial_puts("[M2-B] alloc_pages64(4) FAILED\n");
		return;
	}
	vaddr = SCRATCH_VA;
	for(n = 0; n < 4; n++) {
		if(map_page64(vaddr + (n << PAGE_SHIFT64), paddr + (n << PAGE_SHIFT64),
			      X86_PTE_RW)) {
			serial_puts("[M2-B] map_page64 FAILED\n");
			return;
		}
	}

	/* write a pattern through the 4KB mapping and read it back */
	p = (char *)vaddr;
	for(n = 0; n < PAGE_SIZE64; n++) {
		p[n] = (char)(n * 7 + 1);
	}
	ok = 1;
	for(n = 0; n < PAGE_SIZE64; n++) {
		if(p[n] != (char)(n * 7 + 1)) {
			ok = 0;
			break;
		}
	}
	serial_puts("[M2-B] mapped 4 pages at ");
	serial_hex((UINT64)vaddr);
	serial_puts(" -> phys ");
	serial_hex((UINT64)paddr);
	serial_puts(": write/read ");
	serial_puts(ok ? "OK" : "MISMATCH");
	serial_puts("\n");

	phys = virt_to_phys64(vaddr);
	serial_puts("[M2-B] virt_to_phys64(");
	serial_hex((UINT64)vaddr);
	serial_puts(") = ");
	serial_hex((UINT64)phys);
	serial_puts(phys == paddr ? " (match)\n" : " (MISMATCH)\n");

	unmap_page64(vaddr);
	unmap_page64(vaddr + (1 << PAGE_SHIFT64));
	unmap_page64(vaddr + (2 << PAGE_SHIFT64));
	unmap_page64(vaddr + (3 << PAGE_SHIFT64));
	free_pages64(paddr, 4);
	serial_puts("[M2-B] unmapped + freed, free pages=");
	putdec64((UINT64)free_pages_count);
	serial_puts("\n");
}
