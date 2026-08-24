/*
 * fnx/kernel/pic.c
 *
 * Copyright 2018-2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/config.h>
#include <fnx/pic.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

/* interrupt vector base addresses */
#define IRQ0_ADDR	0x20
#define IRQ8_ADDR	0x28

/*
 * This sends the command OCW3 to PIC (master or slave) to obtain the register
 * values. Slave is chained and represents IRQs 8-15. Master represents IRQs
 * 0-7, with IRQ 2 being the chain.
 */
static unsigned short int pic_get_irq_reg(int ocw3)
{
	outport_b(PIC_MASTER, ocw3);
	outport_b(PIC_SLAVE, ocw3);
	return (inport_b(PIC_SLAVE) << 8) | inport_b(PIC_MASTER);
}

void enable_irq(int irq)
{
	int addr;

	addr = (irq > 7) ? PIC_SLAVE + DATA : PIC_MASTER + DATA;
	irq &= 0x0007;

	outport_b(addr, inport_b(addr) & ~(1 << irq));
}

void disable_irq(int irq)
{
	int addr;

	addr = (irq > 7) ? PIC_SLAVE + DATA : PIC_MASTER + DATA;
	irq &= 0x0007;

	outport_b(addr, inport_b(addr) | (1 << irq));
}

void spurious_interrupt(int irq)
{
	int real;

	/* spurious interrupt treatment */
	real = pic_get_irq_reg(PIC_READ_ISR);
	if(!real) {
		/*
		 * If IRQ was not real and came from slave, then send
		 * an EOI to master because it doesn't know if the IRQ
		 * was a spurious interrupt from slave.
		 */
		if(irq > 7) {
			outport_b(PIC_MASTER, EOI);
		}
		if(kstat.sirqs < MAX_SPU_NOTICES) {
			printk("WARNING: spurious interrupt detected (unregistered IRQ %d).\n", irq);
		} else if(kstat.sirqs == MAX_SPU_NOTICES) {
			printk("WARNING: too many spurious interrupts; not logging any more.\n");
		}
		kstat.sirqs++;
		return;
	}
	ack_pic_irq(irq);
}

void ack_pic_irq(int irq)
{
	if(irq > 7) {
		outport_b(PIC_SLAVE, EOI);
	}
	outport_b(PIC_MASTER, EOI);
}

void pic_init(void)
{
	/* remap interrupts for PIC1 */
	outport_b(PIC_MASTER, ICW1_RESET);
	outport_b(PIC_MASTER + DATA, IRQ0_ADDR);	/* ICW2 */
	outport_b(PIC_MASTER + DATA, 1 << CASCADE_IRQ);	/* ICW3 */
	outport_b(PIC_MASTER + DATA, ICW4_8086EOI);

	/* remap interrupts for PIC2 */
	outport_b(PIC_SLAVE, ICW1_RESET);
	outport_b(PIC_SLAVE + DATA, IRQ8_ADDR);		/* ICW2 */
	outport_b(PIC_SLAVE + DATA, CASCADE_IRQ);	/* ICW3 */
	outport_b(PIC_SLAVE + DATA, ICW4_8086EOI);

	/* mask all IRQs except cascade */
#ifdef __x86_64__
	/* FNX: keep IRQ0 (PIT timer) unmasked too; the IDT64 dispatch
	 * drives the real kernel's timer machinery via irq64_handler(). */
	outport_b(PIC_MASTER + DATA, ~(1 << CASCADE_IRQ) & ~1);
#else
	outport_b(PIC_MASTER + DATA, ~(1 << CASCADE_IRQ));
#endif /* __x86_64__ */

	/* mask all IRQs */
	outport_b(PIC_SLAVE + DATA, OCW1);
}
