/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * fninvoke.h — the PRIVATE seam between the ObjC side (ninvocation.m) and the two
 * assembler trampolines (ninvoke_amd64.S). NOT installed: the public headers are
 * audited against Cocoa, and this is not Cocoa's business.
 *
 * THE REGISTER IMAGE, one layout for both directions — CAPTURE (the runtime calls
 * us with a forwarded method's own arguments) and INVOKE (we call a method from a
 * stored image). The assembler writes it and this struct reads it, so THE OFFSETS
 * ARE A CONTRACT: change one side and both must move together.
 *
 *   +0    6 x 8   the integer/pointer registers, in ABI order (rdi rsi rdx rcx r8 r9)
 *   +48   8 x 16  the SSE registers (xmm0..xmm7)
 *   +176  8       the caller's stack-argument pointer (capture sets this)
 *   +184  8       the RETURN value, integer/pointer (rax)
 *   +192  8       the RETURN value, floating point (xmm0)
 *   +200  8       the SSE count the callee is told about (al), set before invoke
 *   =224 bytes
 *
 * THE ABI SUBSET, stated once and enforced loudly: arguments and the return value
 * live in the REGISTERS — integers/pointers classified up to six, floats/doubles
 * up to eight, with self and _cmd taking the first two integer registers as they
 * do for any method. An argument that needs the STACK, a struct passed by value,
 * or a long double is REFUSED by NSInvocation rather than half-supported (see
 * ninvocation.m's -setArgument:atIndex: and -invoke).
 */
#ifndef FOUNDATION_FNINVOKE_H
#define FOUNDATION_FNINVOKE_H

#include <stdint.h>
#include <objc/runtime.h>

#define FN_IMAGE_SIZE		224
#define FN_OFF_GPR		0
#define FN_OFF_SSE		48
#define FN_OFF_STACK		176
#define FN_OFF_RETURN_INT	184
#define FN_OFF_RETURN_FP	192
#define FN_OFF_SSE_COUNT	200

typedef struct fn_regs {
	uint64_t gpr[6];		/* +0   */
	uint64_t sse[8][2];		/* +48  */
	uint64_t stack;			/* +176 */
	uint64_t return_int;		/* +184 */
	uint64_t return_fp;		/* +192 */
	uint64_t sse_count;		/* +200 */
	uint64_t reserved[2];		/* to 224 */
} fn_regs_t;

/* The trampolines. fn_forward_entry is never called by name — the runtime calls it
 * through __objc_msg_forward2, with the forwarded method's arguments. */
extern void fn_forward_entry(void);
extern void fn_call_image(IMP function, fn_regs_t *regs);

/* The C half of the capture: with fn_forward_regs filled, find the receiver, build
 * the invocation, deliver it, and leave the return value in the image. */
extern void fn_forward_handler(void);

/* The image the trampoline fills. ONE is enough, and that is a deliberate
 * argument: the handler copies it out before it calls anything, so a nested
 * forwarding call cannot overwrite an image still in use. */
extern fn_regs_t fn_forward_regs;

#endif /* FOUNDATION_FNINVOKE_H */
