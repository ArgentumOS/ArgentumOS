/*
 * fnx/kernel/boot64/msix64.c
 *
 * M6-G: MSI-X dispatch glue. The 64-bit IDT hands IDT vectors
 * 0x30-0x3F (MSIX_VEC_BASE +) to msix64_handler(); it forwards to the
 * real kernel's msix_handler() (kernel/msix.c) and then runs bottom
 * halves, mirroring what irq64_handler() does for the PIC IRQs.
 *
 * There is NO 8259 EOI here: MSI-X messages are delivered by the
 * local APIC, and kernel/msix.c writes the APIC EOI register itself.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/efi.h>
#include <fnx/msix.h>
#include <fnx/sigcontext.h>
#include <fnx/string.h>
#include "serial64.h"

void msix64_handler(unsigned long vector)
{
	extern void msix_handler(int, struct sigcontext);
	extern void do_bh(struct sigcontext);
	struct sigcontext sc;
	int num;

	num = (int)(vector - MSIX_VEC_BASE);
	memset_b(&sc, 0, sizeof(sc));
	sc.cs = 0x08;	/* KERNEL_CS, like the PIC IRQ path */
	msix_handler(num, sc);
	do_bh(sc);
}
