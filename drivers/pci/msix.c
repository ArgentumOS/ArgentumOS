/*
 * fnx/drivers/pci/msix.c
 *
 * MSI-X capability setup: locate the MSI-X capability on a PCI
 * device, map its MSI-X table BAR into a fixed kernel VA, program
 * table entry 0 so the device delivers an edge-triggered message to
 * the BSP's local APIC on the requested IDT vector, and enable the
 * MSI-X function.
 *
 * The kernel side (kernel/msix.c + kernel64/idt64.c) must have been
 * initialized so vectors 0x30-0x3F dispatch to msix_handler().
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/msix.h>
#include <fnx/pci.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#define PCI_CAP_ID_MSIX		0x11

#define MSIX_CAP_MC		2	/* message control (16 bits) */
#define MSIX_MC_TABLE_SIZE	0x07FF	/* table size field: N-1 */
#define MSIX_MC_FUNCMASK	0x4000	/* function mask */
#define MSIX_MC_ENABLE		0x8000	/* MSI-X enable */
#define MSIX_CAP_TABLE		4	/* table offset/BIR (32 bits) */
#define MSIX_CAP_PBA		8	/* PBA offset/BIR (32 bits) */
#define MSIX_TABLE_BIR		0x7	/* BAR index (bits 0-2) */
#define MSIX_TABLE_OFFSET	~0x7	/* offset into the BAR */

/* fixed kernel VA for the MSI-X table BAR (pml4[506]) */
#define MSIX_TABLE_VA		0xFFFFBD0000000000UL

extern int map_page64(unsigned long, unsigned long, unsigned long);

static unsigned int msix_read_table(struct pci_device *pd, int bir,
				    unsigned long *pa)
{
	unsigned long base;

	base = pd->bar[bir];
	if(pd->flags[bir] & PCI_F_ADDR_MEM_64) {
		base |= (unsigned long)pd->bar[bir + 1] << 32;
	}
	*pa = base;
	return pd->size[bir];
}

int msix_pci_setup(struct pci_device *pd, int vector)
{
	unsigned int mc, table;
	unsigned long table_pa, table_size;
	volatile unsigned int *tbl;
	int bir, cap, n;

	if(vector < MSIX_VEC_BASE || vector >= MSIX_VEC_BASE + NR_MSIX_VECS) {
		return -EINVAL;
	}

	/* walk the capability list looking for MSI-X (0x11) */
	cap = pci_read_char(pd, 0x34) & 0xFC;
	while(cap) {
		if(pci_read_char(pd, cap) == PCI_CAP_ID_MSIX) {
			break;
		}
		cap = pci_read_char(pd, cap + 1) & 0xFC;
	}
	if(!cap) {
		return -ENODEV;	/* no MSI-X capability */
	}

	mc = pci_read_short(pd, cap + MSIX_CAP_MC);
	table = pci_read_long(pd, cap + MSIX_CAP_TABLE);

	bir = table & MSIX_TABLE_BIR;
	table_size = ((mc & MSIX_MC_TABLE_SIZE) + 1) * 16; /* entries x 16B */
	msix_read_table(pd, bir, &table_pa);
	table_pa += (table & MSIX_TABLE_OFFSET);

	if(!table_pa) {
		return -ENODEV;
	}

	/* map the table page(s) into a fixed kernel VA (entries may cross
	 * a page boundary; the table offset is 8-byte aligned) */
	for(n = 0; n * 4096 < table_size + ((table & MSIX_TABLE_OFFSET) & 0xFFF) &&
	    n < 16; n++) {
		if(map_page64(MSIX_TABLE_VA + n * 4096,
			      table_pa + n * 4096, 0x003)) {
			printk("msix: unable to map the MSI-X table BAR!\n");
			return -ENOMEM;
		}
	}
	tbl = (volatile unsigned int *)(MSIX_TABLE_VA +
		((table & MSIX_TABLE_OFFSET) & 0xFFF));

	/* entry 0: address = local APIC base, dest = BSP (0), fixed,
	 * edge-triggered; data = the IDT vector. Unmasked. */
	tbl[0] = (unsigned int)APIC_PHYS_BASE;	/* message addr lo */
	tbl[1] = 0;				/* message addr hi */
	tbl[2] = vector;			/* message data */
	tbl[3] = 0;				/* vector control: unmask */

	/* enable MSI-X and clear the function mask */
	mc |= MSIX_MC_ENABLE;
	mc &= ~MSIX_MC_FUNCMASK;
	pci_write_short(pd, cap + MSIX_CAP_MC, mc);

	return 0;
}
