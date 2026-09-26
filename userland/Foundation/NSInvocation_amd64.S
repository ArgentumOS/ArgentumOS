/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInvocation_amd64.S — the two things the x86-64 SysV ABI needs and C cannot say:
 * capturing the register file of a call that is being forwarded, and issuing a
 * call from a stored register file.
 *
 * The IMAGE LAYOUT is the contract with userland/Foundation/NSInvocation.h; the
 * offsets below and the struct there are a pair, and both sides say so.
 *
 * WHY NOT C. A variadic C function's prologue saves the SSE registers only when
 * the caller's `al` says they are used, and `al` is UNDEFINED for a non-variadic
 * prototype — which is exactly what a forwarded method has. So the capture must
 * save xmm0-7 unconditionally, and only assembler can say that.
 *
 * AND WHY EVERY FUNCTION HERE CARRIES ITS OWN `.cfi_*` DIRECTIVES (§62.22, MEASURED): a
 * hand-written frame with no DWARF had NO FDE, so the unwinder could not walk through
 * it — and an exception raised by a method called through either trampoline therefore
 * ABORTED the process inside `objc_exception_throw`, with no `@try` anywhere above it
 * able to catch. Found by the `NSInvocationOperation` probe, whose contract is that
 * `-result` re-raises what the run raised: the backtrace put `fn_call_image` between
 * the target method and `-[NSInvocation invokeWithTarget:]`, and four directives per
 * function are the whole fix. The frames were always rbp-based, so the CFA rules below
 * say exactly what the prologue does rather than describing a different shape.
 */

#define FN_GPR		0
#define FN_SSE		48
#define FN_STACK	176
#define FN_RET_INT	184
#define FN_RET_FP	192
#define FN_SSE_COUNT	200

	.text

/*
 * The IMP __objc_msg_forward2 answers with, so the RUNTIME calls it with the
 * forwarded method's own arguments — which is where they are captured. It fills
 * fn_forward_regs, calls the C handler (which copies the image out, builds the
 * invocation and delivers it), then returns whatever the handler left in the
 * image's return slots.
 *
 * At the call instruction rsp is 16-byte aligned, as the ABI requires: the entry
 * has rsp = 8 (mod 16), and `pushq %rbp` makes it 0.
 */
	.globl	fn_forward_entry
	.hidden	fn_forward_entry	/* one shared object, so bind it HERE: that is what
					 * makes the PC-relative access to the image
					 * below legal, with or without -fPIC */
	.type	fn_forward_entry, @function
fn_forward_entry:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset	16
	.cfi_offset	%rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register	%rbp
	movq	%rdi, fn_forward_regs+FN_GPR+0(%rip)
	movq	%rsi, fn_forward_regs+FN_GPR+8(%rip)
	movq	%rdx, fn_forward_regs+FN_GPR+16(%rip)
	movq	%rcx, fn_forward_regs+FN_GPR+24(%rip)
	movq	%r8,  fn_forward_regs+FN_GPR+32(%rip)
	movq	%r9,  fn_forward_regs+FN_GPR+40(%rip)
	movups	%xmm0, fn_forward_regs+FN_SSE+0(%rip)
	movups	%xmm1, fn_forward_regs+FN_SSE+16(%rip)
	movups	%xmm2, fn_forward_regs+FN_SSE+32(%rip)
	movups	%xmm3, fn_forward_regs+FN_SSE+48(%rip)
	movups	%xmm4, fn_forward_regs+FN_SSE+64(%rip)
	movups	%xmm5, fn_forward_regs+FN_SSE+80(%rip)
	movups	%xmm6, fn_forward_regs+FN_SSE+96(%rip)
	movups	%xmm7, fn_forward_regs+FN_SSE+112(%rip)
	leaq	16(%rbp), %rax		/* the stack arguments: past the return address */
	movq	%rax, fn_forward_regs+FN_STACK(%rip)
	call	fn_forward_handler
	movq	fn_forward_regs+FN_RET_INT(%rip), %rax
	movsd	fn_forward_regs+FN_RET_FP(%rip), %xmm0
	leave
	.cfi_def_cfa	%rsp, 8
	ret
	.cfi_endproc
	.size	fn_forward_entry, .-fn_forward_entry

/*
 * void fn_call_image(IMP function, fn_regs_t *regs);
 *
 * The inverse: load the register file from `regs`, call `function`, and store the
 * result back into `regs`. STACK arguments are deliberately NOT placed — v1
 * supports register-only signatures, and NSInvocation refuses the rest loudly.
 *
 * The image pointer is parked in a local slot because the callee may clobber
 * r10/r11; the stack is realigned for the call with the same reservation.
 */
	.globl	fn_call_image
	.hidden	fn_call_image
	.type	fn_call_image, @function
fn_call_image:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset	16
	.cfi_offset	%rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register	%rbp
	subq	$16, %rsp
	movq	%rdi, %r11		/* the function to call */
	movq	%rsi, -8(%rbp)		/* the image, parked across the call */
	movq	%rsi, %r10
	movq	FN_GPR+0(%r10), %rdi
	movq	FN_GPR+8(%r10), %rsi
	movq	FN_GPR+16(%r10), %rdx
	movq	FN_GPR+24(%r10), %rcx
	movq	FN_GPR+32(%r10), %r8
	movq	FN_GPR+40(%r10), %r9
	movups	FN_SSE+0(%r10), %xmm0
	movups	FN_SSE+16(%r10), %xmm1
	movups	FN_SSE+32(%r10), %xmm2
	movups	FN_SSE+48(%r10), %xmm3
	movups	FN_SSE+64(%r10), %xmm4
	movups	FN_SSE+80(%r10), %xmm5
	movups	FN_SSE+96(%r10), %xmm6
	movups	FN_SSE+112(%r10), %xmm7
	movq	FN_SSE_COUNT(%r10), %rax	/* al: the SSE registers in use */
	call	*%r11
	movq	-8(%rbp), %r10
	movq	%rax, FN_RET_INT(%r10)
	movsd	%xmm0, FN_RET_FP(%r10)
	leave
	.cfi_def_cfa	%rsp, 8
	ret
	.cfi_endproc
	.size	fn_call_image, .-fn_call_image

	.data
	.align	16
	.globl	fn_forward_regs
	.hidden	fn_forward_regs
	.type	fn_forward_regs, @object
	.size	fn_forward_regs, 224
fn_forward_regs:
	.zero	224
