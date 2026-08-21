/*
 * fiwix/kernel64/switch64.S
 *
 * Fiwix64 M3 (phase B): 64-bit kernel context switch.
 *
 * switch_to64(unsigned long *old_rsp, unsigned long new_rsp)
 *   - saves the current kernel stack pointer into *old_rsp,
 *   - switches RSP to new_rsp and resumes the target context.
 *
 * Frame layout (top-down): [r15][r14][r13][r12][rbp][rbx][return addr].
 * A freshly created thread's stack is prepared with six zero slots and the
 * thread entry function as the return address, so the first switch pops
 * the zeros and "returns" into the thread body.
 *
 * This is a naked function: it must not be called with the red zone or
 * with the compiler assuming a standard prologue (compiled -mno-red-zone,
 * and the assembly contains its own full save/restore).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

.text
.globl switch_to64
.type switch_to64, @function
switch_to64:
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	movq	%rsp, (%rdi)		/* *old_rsp = saved RSP (r15 slot) */
	movq	%rsi, %rsp		/* switch to the new stack */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	ret
.size switch_to64, .-switch_to64

/*
 * do_switch(addr_t *prev_esp, addr_t *prev_eip, addr_t next_esp,
 *           addr_t next_eip, addr_t next_cr3, unsigned short int tss)
 *   rdi=&prev->tss.esp, rsi=&prev->tss.eip, rdx=next_esp, rcx=next_eip,
 *   r8=next_cr3, r9=tss selector.
 *
 * Fiwix64 (M5): 64-bit replacement for the real kernel's 32-bit do_switch
 * (kernel/core386.S, which uses pusha/pushfl and cannot run in long mode).
 *
 * Saves the current kernel stack pointer + a resume label into the prev
 * process's tss, switches RSP to the next process's saved stack, pushes
 * the next eip and returns into it. When the process is later re-scheduled
 * the saved esp/eip resume at label 1: and the code returns to the caller
 * of do_switch() (the scheduler).
 *
 * The TSS selector is deliberately IGNORED: the single TSS loaded by
 * gdt64_init()/load_tr() is shared. Fiwix64 (M6-next): next_cr3 is the
 * next process's per-process 4-level pml4 (physical) and IS loaded into
 * CR3; the kernel high half is mapped in every pml4 so the switch itself
 * is always safe. Kernel processes use the kernel's own pml4.
 * The kernel process stacks are the kmalloc'd PAGE_SIZE stacks set up by
 * kernel_process().
 */
.globl do_switch
.type do_switch, @function
do_switch:
	/* Save the FULL callee-saved register set on the current (prev) stack,
	 * like switch_to64() and the 32-bit core386.S do_switch (pusha). The
	 * 64-bit SysV ABI only obliges a *function call* to preserve
	 * rbx/rbp/r12-r15; a coroutine switch is NOT a function call, so
	 * relying on the compiler to have spilled these in the caller's frame
	 * is wrong: when the process is later resumed, do_switch must restore
	 * them itself, otherwise the resumed process inherits whatever the
	 * last-running process left in r12-r15 (the A3 bug: sys_wait4's
	 * `status` local, kept in r13/r15 across the sleep(), came back as
	 * do_sched's `&current` and the zombie branch then wrote the child's
	 * exit code into the `current` pointer -> current = 0x...00000400). */
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	movq	%rsp, (%rdi)		/* prev->tss.esp = current RSP (at the r15 slot) */
	leaq	1f(%rip), %rax
	movq	%rax, (%rsi)		/* prev->tss.eip = resume label */
	movq	%rdx, %rsp		/* switch to the next stack */
	movq	%r8, %cr3		/* Fiwix64 (M6-next): per-process pml4 */
	pushq	%rcx			/* the next eip (return target) */
	ret
1:
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	ret
.size do_switch, .-do_switch

/*
 * thread_trampoline64: first-time entry for a freshly created thread.
 * The first switch into a new thread happens inside the timer IRQ handler
 * (IF=0); the trampoline re-enables interrupts before the thread body runs.
 * Stack at entry: [fn] on top (pushed below the switch frame by
 * thread_create64()). Declared extern in sched64.c.
 */
.globl thread_trampoline64
.type thread_trampoline64, @function
thread_trampoline64:
	sti
	popq	%rax
	jmp	*%rax
.size thread_trampoline64, .-thread_trampoline64

/*
 * switch_to_user_mode: Fiwix64 (M6) - the INIT process's transition into
 * 64-bit user mode, entered as the target of do_switch() (RSP has been set
 * to current->tss.esp = the init user stack top by do_switch).
 *
 * 1. user_mode_prep() maps the init_trampoline() code page (executed at its
 *    original VMA) and the user stack page as user-accessible in the
 *    ACTIVE shared page tables.
 * 2. Load the user data segments and iretq into the trampoline with
 *    UCODE64|RPL3 (64-bit user mode). The trampoline's first int 0x80 then
 *    traps to syscall80_handler() in user64.c.
 */
.globl switch_to_user_mode
.type switch_to_user_mode, @function
switch_to_user_mode:
	movq	%rsp, %r12		/* r12 = user RSP (tss.esp) */
	andq	$-16, %rsp		/* align for the C call */
	call	user_mode_prep
	movq	%r12, %rax		/* user stack top */
	movw	$0x23, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	pushq	$0x23			/* SS  = UDATA32 | RPL3 */
	pushq	%r12			/* RSP = user stack top */
	pushq	$0x202			/* RFLAGS: IF = 1 */
	pushq	$0x4B			/* CS  = UCODE64 | RPL3 */
	leaq	init_trampoline(%rip), %rax
	pushq	%rax			/* RIP = the trampoline's VMA */
	iretq
.size switch_to_user_mode, .-switch_to_user_mode

/*
 * return_from_syscall: Fiwix64 (M6-E) - the fork child's first entry,
 * entered as the target of do_switch() with RSP pointing at a 32-bit
 * struct sigcontext (the fork's saved syscall frame) on the child's
 * kernel stack. Reload the user GP registers from the sigcontext and
 * iretq back into the COMPAT-mode user program (eax already 0, set by
 * fork.c). struct sigcontext offsets (all u32):
 *   gs=0 fs=4 es=8 ds=12 edi=16 esi=20 ebp=24 esp=28
 *   ebx=32 edx=36 ecx=40 eax=44 err=48 eip=52 cs=56 eflags=60 oldesp=64 oldss=68
 */
.globl return_from_syscall
.type return_from_syscall, @function
return_from_syscall:
	movq	%rsp, %r11		/* r11 = &sigcontext (stable base) */
	movl	$0x23, %r10d		/* UDATA32 | RPL3 */
	movw	%r10w, %ds
	movw	%r10w, %es
	movw	%r10w, %fs
	/* %gs is the process's TLS selector (0x63), inherited from the parent;
	 * resetting it to UDATA32 here would zero the thread pointer and crash
	 * musl's fork() tail when it reads errno via %gs. */
	movl	$0x63, %r10d		/* TLS_SEL(0x60) | RPL3 */
	movw	%r10w, %gs
	movl	16(%r11), %edi
	movl	20(%r11), %esi
	movl	24(%r11), %ebp
	movl	32(%r11), %ebx
	movl	36(%r11), %edx
	movl	40(%r11), %ecx
	movl	44(%r11), %eax		/* 0 for the fork child */
	pushq	68(%r11)		/* SS  = UDATA32 | RPL3 */
	pushq	64(%r11)		/* RSP = sc->oldesp */
	pushq	60(%r11)		/* RFLAGS */
	pushq	56(%r11)		/* CS  = UCODE32 | RPL3 */
	pushq	52(%r11)		/* RIP = sc->eip */
	iretq
.size return_from_syscall, .-return_from_syscall

