/*
 * fnx/kernel64/serial64.h
 *
 * Shared serial-console and small asm helpers for the FNX EFI stub
 * (kernel64 sources). Static (one private copy per TU, marked unused so
 * -Wall stays quiet in TUs that use only a subset). COM1, 115200 8N1.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _SERIAL64_H
#define _SERIAL64_H

#include <fnx/efi.h>

#define COM1		0x3F8

#define COM1_THR	(COM1 + 0)	/* transmit holding register */
#define COM1_RBR	(COM1 + 0)	/* receive buffer register */
#define COM1_IER	(COM1 + 1)	/* interrupt enable register */
#define COM1_FCR	(COM1 + 2)	/* FIFO control register */
#define COM1_LCR	(COM1 + 3)	/* line control register */
#define COM1_MCR	(COM1 + 4)	/* modem control register */
#define COM1_LSR	(COM1 + 5)	/* line status register */

#define LSR_THRE	0x20		/* transmitter holding register empty */

static void __attribute__((unused)) outb(unsigned short port, unsigned char val)
{
	__asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}

static unsigned char __attribute__((unused)) inb(unsigned short port)
{
	unsigned char val;

	__asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static void __attribute__((unused)) serial_init(void)
{
	outb(COM1_IER, 0x00);		/* disable all interrupts */
	outb(COM1_LCR, 0x80);		/* DLAB = 1 */
	outb(COM1_THR, 0x03);		/* divisor 3 = 115200 baud */
	outb(COM1_IER, 0x00);		/* divisor high byte */
	outb(COM1_LCR, 0x03);		/* 8N1, DLAB = 0 */
	outb(COM1_FCR, 0xC7);		/* enable + clear FIFOs, 14-byte threshold */
	outb(COM1_MCR, 0x0B);		/* DTR + RTS + OUT2 (IRQ enable) */
}

static void __attribute__((unused)) serial_putc(char c)
{
	while(!(inb(COM1_LSR) & LSR_THRE)) {
		/* wait for the transmitter to be ready */
	}
	outb(COM1_THR, c);
}

static void __attribute__((unused)) serial_puts(const char *s)
{
	while(*s) {
		if(*s == '\n') {
			serial_putc('\r');
		}
		serial_putc(*(s++));
	}
}

static void __attribute__((unused)) serial_puts16(CHAR16 *s)
{
	while(*s) {
		if(*s == '\n') {
			serial_putc('\r');
		}
		serial_putc((char)(*s & 0xFF));
		s++;
	}
}

/* 16 hex digits, "0x" prefix */
static void __attribute__((unused)) serial_hex(UINT64 v)
{
	static const char digits[] = "0123456789abcdef";
	char buf[17];
	int n;

	buf[16] = 0;
	for(n = 15; n >= 0; n--) {
		buf[n] = digits[v & 0x0F];
		v >>= 4;
	}
	serial_puts("0x");
	serial_puts(buf);
}

/* 8 hex digits, "0x" prefix */
static void __attribute__((unused)) puthex32(unsigned int v)
{
	static const char digits[] = "0123456789abcdef";
	char buf[9];
	int n;

	buf[8] = 0;
	for(n = 7; n >= 0; n--) {
		buf[n] = digits[v & 0x0F];
		v >>= 4;
	}
	serial_puts("0x");
	serial_puts(buf);
}

static void __attribute__((unused)) putdec64(UINT64 v)
{
	char buf[21];
	int n;

	n = 20;
	buf[n] = 0;
	do {
		buf[--n] = '0' + (v % 10);
		v /= 10;
	} while(v);
	serial_puts(&buf[n]);
}

/*
 * Runtime address of this instruction (lea 0(%%rip) is position-independent,
 * so it works from both the identity mapping and the high-half alias; see
 * docs/port-longmode-uefi.txt M1 STATUS for why &function cannot be used).
 */
static unsigned long __attribute__((unused)) get_rip(void)
{
	unsigned long addr;

	__asm__ __volatile__("lea 0(%%rip), %0" : "=r"(addr));
	return addr;
}

static unsigned long __attribute__((unused)) get_rsp(void)
{
	unsigned long addr;

	__asm__ __volatile__("mov %%rsp, %0" : "=r"(addr));
	return addr;
}

#endif /* _SERIAL64_H */
