/*
 * fnx/kernel64/init_trampoline64.S
 *
 * FNX (no 32-bit compatibility): the INIT bootstrap trampoline.
 *
 * Copied to user VA 0x100000 at boot (init_init) and executed at CPL3 in
 * 64-bit user mode via switch_to_user_mode. It is fully POSITION-
 * INDEPENDENT: it contains NO kernel symbol addresses. Instead it reads
 * its 4 runtime pointers (console path, argv, envp, INIT_PROGRAM) from a
 * fixed 32-byte table at user VA 0x100000+0x100 (INIT_TRAMP_TABLE), which
 * init_init() fills with the high-half runtime addresses of the C globals
 * in kernel/init.c. The table address (0x100100) is a compile-time
 * constant, so no relocation is needed.
 *
 * Uses the native 'syscall' instruction with x86-64 syscall numbers
 * (open=2, dup=32, execve=59, exit=60).
 *
 * Register clobbers: rax/rcx/r11 (syscall), rdi/rsi/rdx (args).
 */

#define TRAMP_TABLE	0x100100	/* 0x100000 + 0x100: filled by init_init */
#define TRAMP_CONSOLE	0x100100	/* u64: "/dev/console" runtime addr */
#define TRAMP_ARGV	0x100108	/* u64: init_argv runtime addr */
#define TRAMP_ENVP	0x100110	/* u64: init_envp runtime addr */
#define TRAMP_PROGRAM	0x100118	/* u64: INIT_PROGRAM runtime addr */

	.text
	.globl	init_trampoline
	.type	init_trampoline, @function
init_trampoline:
	/* open(console, O_RDWR, 0) -> stdin (fd 0) */
	movabs	$TRAMP_CONSOLE, %rdi
	movq	(%rdi), %rdi
	movl	$2, %esi		/* O_RDWR */
	xorl	%edx, %edx
	movl	$2, %eax		/* sys_open */
	syscall

	/* dup(0) -> stdout (fd 1) */
	xorl	%edi, %edi
	xorl	%esi, %esi
	xorl	%edx, %edx
	movl	$32, %eax		/* sys_dup */
	syscall

	/* dup(0) -> stderr (fd 2) */
	xorl	%edi, %edi
	xorl	%esi, %esi
	xorl	%edx, %edx
	movl	$32, %eax		/* sys_dup */
	syscall

	/* execve(INIT_PROGRAM, init_argv, init_envp) */
	movabs	$TRAMP_PROGRAM, %rdi
	movq	(%rdi), %rdi
	movabs	$TRAMP_ARGV, %rsi
	movq	(%rsi), %rsi
	movabs	$TRAMP_ENVP, %rdx
	movq	(%rdx), %rdx
	movl	$59, %eax		/* sys_execve */
	syscall

	/* exit(0) - only reached if execve fails */
	xorl	%edi, %edi
	xorl	%esi, %esi
	xorl	%edx, %edx
	movl	$60, %eax		/* sys_exit */
	syscall
	hlt
	.size	init_trampoline, .-init_trampoline
