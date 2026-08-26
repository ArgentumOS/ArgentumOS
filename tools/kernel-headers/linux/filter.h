/*
 * fnx/tools/kernel-headers/linux/filter.h
 *
 * Minimal Linux-uapi-compatible <linux/filter.h> for the FNX userland
 * builds. FNX's musl toolchain ships no kernel headers; this subset
 * provides the classic (cBPF) socket filter ABI - struct sock_filter,
 * struct sock_fprog and the BPF_* instruction encodings - with the same
 * names and values as the real Linux uapi header (bpf_common.h +
 * filter.h). toybox's dhcp attaches such a filter to its PF_PACKET
 * socket (SO_ATTACH_FILTER; the FNX kernel ignores it).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _LINUX_FILTER_H
#define _LINUX_FILTER_H

/* instruction classes */
#define BPF_LD		0x00
#define BPF_LDX		0x01
#define BPF_ST		0x02
#define BPF_STX		0x03
#define BPF_ALU		0x04
#define BPF_JMP		0x05
#define BPF_RET		0x06
#define BPF_MISC	0x07

/* ld/ldx fields */
#define BPF_W		0x00
#define BPF_H		0x08
#define BPF_B		0x10
#define BPF_ABS		0x20
#define BPF_IND		0x40
#define BPF_MSH		0x80

/* alu/jmp fields */
#define BPF_ADD		0x00
#define BPF_SUB		0x10
#define BPF_MUL		0x20
#define BPF_DIV		0x30
#define BPF_OR		0x40
#define BPF_AND		0x50
#define BPF_LSH		0x60
#define BPF_RSH		0x70
#define BPF_NEG		0x80
#define BPF_MOD		0x90
#define BPF_XOR		0xa0

#define BPF_K		0x00
#define BPF_X		0x08
#define BPF_A		0x10

/* jmp fields */
#define BPF_JA		0x00
#define BPF_JEQ		0x10
#define BPF_JGT		0x20
#define BPF_JGE		0x30
#define BPF_JSET	0x40

/* store/load ret/ret fields */
#define BPF_MISC_MISC	0x00
#define BPF_RET_K	0x00
#define BPF_RET_A	0x10

#define BPF_TAX		0x00
#define BPF_TXA		0x80

/* struct sock_filter: (u16)code, (u8)jt, (u8)jf, (u32)k */
struct sock_filter {
	unsigned short	code;
	unsigned char	jt;
	unsigned char	jf;
	unsigned int	k;
};

struct sock_fprog {
	unsigned short	len;
	struct sock_filter *filter;
};

#define BPF_STMT(code, k)	{ (unsigned short)(code), 0, 0, (unsigned int)(k) }
#define BPF_JUMP(code, k, jt, jf)	{ (unsigned short)(code), \
					  (unsigned char)(jt), \
					  (unsigned char)(jf), \
					  (unsigned int)(k) }

#endif /* _LINUX_FILTER_H */
