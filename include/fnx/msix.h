/*
 * fnx/include/fnx/msix.h
 *
 * MSI-X (Message Signaled Interrupts - eXtended) support.
 *
 * The local APIC delivers MSI-X messages as IDT vectors 0x30-0x3F
 * (MSIX_VEC_BASE .. +NR_MSIX_VECS-1), bypassing the 8259 PIC. The
 * driver side registers a handler here exactly like register_irq(),
 * and the kernel64 dispatcher (kernel64/msix64.c) routes vectors in
 * that range to msix_handler(). The PIC's INTx path is untouched.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_MSIX_H
#define _FNX_MSIX_H

#include <fnx/irq.h>
#include <fnx/sigcontext.h>

#define NR_MSIX_VECS	16	/* MSI-X vectors 0x30-0x3F */
#define MSIX_VEC_BASE	0x30	/* first IDT vector used by MSI-X */

struct pci_device;

/* the APIC's fixed physical address (local APIC MMIO page) */
#define APIC_PHYS_BASE	0xFEE00000UL
#define APIC_EOI	0x000B0	/* EOI register offset (write 0) */
#define APIC_SVR	0x000F0	/* spurious vector register */
#define APIC_SV_ENABLE	0x00100	/* APIC software enable bit in SVR */
#define APIC_LVT0	0x00350	/* LVT entry 0 (LINT0): PIC ExtINT route */
#define APIC_LVT0_EXTINT	0x00700	/* ExtINT delivery, edge, unmasked */

/* same layout as the PIC interrupt struct (see fnx/irq.h) */
extern struct interrupt *msix_table[NR_MSIX_VECS];

int register_msix(int, struct interrupt *);
int unregister_msix(int, const struct interrupt *);
void msix_handler(int, struct sigcontext);
void msix_init(void);

/* kernel64 glue: called from idt64 dispatch for vectors 0x30-0x3F */
void msix64_handler(unsigned long);

/* drivers/pci/msix.c: program a device's MSI-X table (vector 0) so it
 * delivers messages on the given IDT vector (>= MSIX_VEC_BASE). */
int msix_pci_setup(struct pci_device *, int vector);

#endif /* _FNX_MSIX_H */
