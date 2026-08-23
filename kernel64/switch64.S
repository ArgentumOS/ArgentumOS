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
	.align 8
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
 * to current->tss.esp = the init user stack top by do_switch; INIT's
 * per-process pml4 is already in CR3, with the bootstrap trampoline copied
 * at user VA 0x100000 by init_init()).
 *
 * 1. user_mode_prep() maps the INIT user stack page as user-accessible in
 *    the ACTIVE (INIT's) page tables.
 * 2. Load the user data segments and iretq into the trampoline at its user
 *    VA 0x100000 with UCODE64|RPL3 (64-bit user mode). The trampoline's
 *    first 'syscall' then traps to syscall80_handler() in user64.c.
 */
.globl switch_to_user_mode
.type switch_to_user_mode, @function
switch_to_user_mode:
	/* RSP is the KERNEL stack (init->tss.esp0, set by do_switch). The
	 * user stack is a fixed user-half page (0x101000, mapped by
	 * user_mode_prep); the trampoline code is at 0x100000. */
	movq	$0x102000, %r12		/* r12 = INIT user stack top */
	movq	%rsp, %r13		/* save kernel stack */
	andq	$-16, %rsp		/* align for the C call */
	call	user_mode_prep
	movq	%r13, %rsp		/* restore kernel stack */
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
	movq	$0x100000, %rax		/* RIP = the trampoline's USER VA */
	pushq	%rax
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


/*
 * syscall_entry64: Fiwix64 native 64-bit syscall entry - the 'syscall'
 * instruction used by musl x86_64. The CPU sets RCX = return RIP and
 * R11 = return RFLAGS but does NOT switch stacks (unlike an interrupt
 * gate), so we save the user RSP in a global scratch, switch to the
 * current process's kernel stack (fiwix64_rsp0, kept in sync with the
 * TSS RSP0 by gdt64_set_rsp0), build the SAME frame layout as
 * isr_common64 (7-word x86_frame64 + 15 GPRs) and tail into
 * syscall80_handler(); the iretq epilogue restores the user frame
 * (RCX/R11/RSP were saved in the frame). Single-CPU kernel, so the
 * global scratch is safe - a nested interrupt returns before we
 * re-enter (it nests on the kernel stack above our frame).
 */
.globl syscall_entry64
.type syscall_entry64, @function
syscall_entry64:
	movq	%rsp, fiwix64_syscall_userrsp(%rip)	/* save the user RSP */
	movq	fiwix64_rsp0(%rip), %rsp		/* switch to the kernel stack */
	/* x86_frame64 (7 words, so [gprs + 15*8] = vector like the ISR path) */
	pushq	$0x23					/* ss  = UDATA32 | RPL3 */
	pushq	fiwix64_syscall_userrsp(%rip)		/* rsp (user) */
	pushq	%r11					/* rflags */
	pushq	$0x4b					/* cs  = UCODE64 | RPL3 */
	pushq	%rcx					/* rip (the return address) */
	pushq	$0					/* error */
	pushq	$0x80					/* vector */
	/* 15 GPRs, isr_common64 order (last push = gprs[0] = r15) */
	pushq	%rax
	pushq	%rcx
	pushq	%rdx
	pushq	%rbx
	pushq	%rbp
	pushq	%rsi
	pushq	%rdi
	pushq	%r8
	pushq	%r9
	pushq	%r10
	pushq	%r11
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	movq	%rsp, %rdi
	call	syscall80_handler
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%r11
	popq	%r10
	popq	%r9
	popq	%r8
	popq	%rdi
	popq	%rsi
	popq	%rbp
	popq	%rbx
	popq	%rdx
	popq	%rcx
	popq	%rax
	addq	$16, %rsp				/* drop vector+error, keep rip/cs/rflags/rsp/ss for the iretq */
	iretq
.size syscall_entry64, .-syscall_entry64

/*
 * return_from_syscall64: Fiwix64 (native port) - the fork() child's
 * 64-bit-mode return, mirroring return_from_syscall for the compat path.
 * The child's kernel stack holds the copied sigcontext at RSP; reload the
 * user data segments and iretq into the 64-bit user program (UCODE64|
 * RPL3, UDATA32|RPL3) at sc->eip/sc->oldesp with eax=0 (set by fork.c).
 */
.globl return_from_syscall64
.type return_from_syscall64, @function
return_from_syscall64:
	movq	%rsp, %r11		/* r11 = &sigcontext (stable base) */
	movl	$0x23, %r10d		/* UDATA32 | RPL3 */
	movw	%r10w, %ds
	movw	%r10w, %es
	/* NOT fs/gs: a 64-bit process's FS is its TLS selector (inherited
	 * by the fork child); clobbering it kills the child in
	 * musl's __post_Fork (%fs:0 read). GS stays inherited too. */
	pushq	$0x23			/* SS */
	/* Fiwix64 (canonical amd64 split): native 64-bit processes use the
	 * 64-bit rip/rsp sigcontext fields (the 32-bit eip/oldesp are the
	 * i386-compat layout and truncate addresses in the 128TB user
	 * half). */
	movq	80(%r11), %r9		/* RSP = sc->rsp (64-bit) */
	pushq	%r9
	movl	60(%r11), %r9d		/* RFLAGS = sc->eflags (32-bit: zero-extend) */
	pushq	%r9
	pushq	$0x4b			/* CS = UCODE64 | RPL3 */
	movq	72(%r11), %r9		/* RIP = sc->rip (64-bit) */
	pushq	%r9
	movl	44(%r11), %eax		/* child's RAX = sc->eax (the fork returns 0) */
	iretq
.size return_from_syscall64, .-return_from_syscall64
