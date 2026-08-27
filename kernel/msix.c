/*
 * fnx/kernel/msix.c
 *
 * MSI-X handler table, mirroring the PIC IRQ machinery in irq.c but
 * for the IDT vectors 0x30-0x3F that the local APIC delivers MSI-X
 * messages on. There is no 8259 involvement: the EOI for an edge-
 * triggered MSI message is a write to the local APIC's EOI register.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/errno.h>
#include <fnx/msix.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

struct interrupt *msix_table[NR_MSIX_VECS];

static unsigned long apic_va;	/* kernel VA of the APIC MMIO page */

extern int map_page64(unsigned long, unsigned long, unsigned long);

static void msix_apic_eoi(void)
{
	if(apic_va) {
		*(volatile unsigned int *)(apic_va + APIC_EOI) = 0;
	}
}

/* make sure the local APIC is enabled and mapped so MSI-X messages
 * (edge-triggered, IDT vector delivery) are accepted */
void msix_init(void)
{
	unsigned int lo, hi;
	unsigned long apicbase;

	memset_b(msix_table, 0, sizeof(msix_table));

	__asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"((unsigned long)0x1B));
	apicbase = ((unsigned long)hi << 32) | lo;

	if(!(apicbase & (1UL << 11))) {
		/* local APIC disabled: enable it (keep BSP bit 8) */
		apicbase |= (1UL << 11);
		__asm__ __volatile__("wrmsr" ::
			"c"((unsigned long)0x1B), "a"((unsigned int)apicbase),
			"d"((unsigned int)(apicbase >> 32)));
	}

	if(!apic_va) {
		/* map the APIC MMIO page (0xFEE00000) into a kernel VA */
		if(map_page64(0xFFFFBE0000000000UL, APIC_PHYS_BASE, 0x003)) {
			printk("msix: unable to map the local APIC!\n");
			return;
		}
		apic_va = 0xFFFFBE0000000000UL;
	}

	/* enable the APIC's software-enable bit in SVR if firmware
	 * (OVMF) did not already */
	if(!(*(volatile unsigned int *)(apic_va + APIC_SVR) & APIC_SV_ENABLE)) {
		*(volatile unsigned int *)(apic_va + APIC_SVR) =
			(*(volatile unsigned int *)(apic_va + APIC_SVR) & 0xFF) |
			APIC_SV_ENABLE;
	}

	/* If we just brought the APIC up ourselves, keep the 8259 PIC
	 * routed through LINT0 (ExtINT): QEMU drops PIC IRQs once the
	 * APIC is enabled unless LVT0 is unmasked, which would silently
	 * kill the timer. OVMF normally leaves this set. */
	if(*(volatile unsigned int *)(apic_va + APIC_LVT0) & 0x10000) {
		*(volatile unsigned int *)(apic_va + APIC_LVT0) = APIC_LVT0_EXTINT;
	}
}

int register_msix(int num, struct interrupt *new_irq)
{
	struct interrupt **irq;

	if(num < 0 || num >= NR_MSIX_VECS) {
		printk("WARNING: %s(): MSI-X vector %d is out of range (%d)!\n",
			__FUNCTION__, num, NR_MSIX_VECS);
		return -EINVAL;
	}

	irq = &msix_table[num];

	while(*irq) {
		if(*irq == new_irq) {
			printk("WARNING: %s(): MSI-X vector %d already registered!\n",
				__FUNCTION__, num);
			return -EINVAL;
		}
		irq = &(*irq)->next;
	}
	*irq = new_irq;
	new_irq->ticks = 0;
	return 0;
}

int unregister_msix(int num, const struct interrupt *old_irq)
{
	struct interrupt **irq, *prev_irq;

	if(num < 0 || num >= NR_MSIX_VECS) {
		return -EINVAL;
	}

	irq = &msix_table[num];
	prev_irq = NULL;

	while(*irq) {
		if(*irq == old_irq) {
			if((*irq)->next) {
				printk("WARNING: %s(): cannot unregister MSI-X vector %d.\n",
					__FUNCTION__, num);
				return -EINVAL;
			}
			*irq = NULL;
			if(prev_irq) {
				prev_irq->next = NULL;
			}
			break;
		}
		prev_irq = *irq;
		irq = &(*irq)->next;
	}
	return 0;
}

/* called from kernel64/msix64.c with interrupts disabled */
void msix_handler(int num, struct sigcontext sc)
{
	struct interrupt *irq;

	irq = msix_table[num];

	if(!irq) {
		printk("Unknown MSI-X vector %d received!\n", num);
		goto end;
	}

	irq->ticks++;
	do {
		irq->handler(num, &sc);
		irq = irq->next;
	} while(irq);

end:
	msix_apic_eoi();
}
