/*
 * fnx/kernel64/irq64.c
 *
 * FNX M3 (phase A): 8259 PIC remap, PIT timer, IRQ dispatch.
 *
 * The PICs are remapped so IRQ0-7 -> vectors 0x20-0x27 and IRQ8-15 ->
 * 0x28-0x2F, and only IRQ0 (the PIT) is unmasked. The PIT ticks at 100 Hz.
 * irq64_handler() is called from isr64_dispatch() (kernel64/idt64.c) for
 * vectors 32-47; it sends the EOI and bumps the tick counter for IRQ0.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/efi.h>
#include <fnx/sigcontext.h>
#include "serial64.h"

static unsigned long ticks64;

void sched64_tick(void);

unsigned long get_ticks64(void)
{
	return ticks64;
}

void irq64_handler(unsigned long vector)
{
	/* EOI to the 8259 PICs (slave first, then master) */
	if(vector >= 0x28) {
		outb(0xA0, 0x20);
	}
	outb(0x20, 0x20);

	if(vector >= 0x20 && vector <= 0x2F) {
		/* M5: dispatch EVERY hardware IRQ to the real kernel's
		 * irq_handler(), not just IRQ0. Without this the serial IRQ
		 * (IRQ4) never reaches irq_serial, so the tty write_q never
		 * drains and flush_log_buf() spins forever on -EAGAIN after
		 * the first post-ttyS0 printk. irq_handler() runs the
		 * registered driver handler (irq_timer for IRQ0, irq_serial
		 * for IRQ4, ...) and re-enables the IRQ; the real kernel's
		 * ack_pic_irq() sends a second (idempotent) EOI, matching the
		 * 32-bit core386.S IRQ entry. do_bh() runs the pending bottom
		 * halves exactly like BOTTOM_HALVES in the 32-bit path. */
		extern void irq_handler(int, struct sigcontext);
		extern void do_bh(struct sigcontext);
		struct sigcontext sc;
		int irq;

		irq = (int)(vector - 0x20);
		memset_b(&sc, 0, sizeof(sc));
		sc.cs = 0x08;	/* KERNEL_CS: the irq_timer_bh's user check */
		irq_handler(irq, sc);
		do_bh(sc);
	}

	if(vector == 0x20) {	/* IRQ0: PIT tick */
		ticks64++;
		sched64_tick();
	}
}

void irq64_init(void)
{
	/* remap: IRQ0-7 -> 0x20-0x27, IRQ8-15 -> 0x28-0x2F */
	outb(0x20, 0x11);	/* ICW1: init, edge triggered */
	outb(0xA0, 0x11);
	outb(0x21, 0x20);	/* ICW2: master base 0x20 */
	outb(0xA1, 0x28);	/* ICW2: slave base 0x28 */
	outb(0x21, 0x04);	/* ICW3: slave on master IRQ2 */
	outb(0xA1, 0x02);	/* ICW3: cascade identity */
	outb(0x21, 0x01);	/* ICW4: 8086 mode */
	outb(0xA1, 0x01);
	outb(0x21, 0xFE);	/* mask: only IRQ0 (timer) unmasked */
	outb(0xA1, 0xFF);

	/* PIT channel 0: 100 Hz square wave (divisor 11931 = 1193182/100) */
	outb(0x43, 0x36);	/* ch0, lobyte/hibyte, mode 3, binary */
	outb(0x40, 0x9B);	/* divisor low */
	outb(0x40, 0x2E);	/* divisor high */

	serial_puts("[M3-A] PIC remapped (IRQ0=0x20..IRQ15=0x2F), PIT 100 Hz, IRQ0 unmasked\n");
}
