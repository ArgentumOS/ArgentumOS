/* FNX (M6): the INIT process's user-mode entry + int 0x80 syscall
 * dispatch into the real kernel's syscall table.
 *
 * The init_trampoline() in kernel/init.c is compiled as 64-bit code and
 * runs in 64-bit user mode (UCODE64 0x48 added by gdt64_init, at its own
 * VMA so its RIP-relative references to init_argv/init_envp stay valid).
 * Its int 0x80 traps land here with a 64-bit frame; the exec'd /sbin/init
 * is a 32-bit ELF and traps with a COMPAT frame. Both are handled by this
 * same code: the 32-bit ABI registers live in the low halves of the GPRs,
 * and the saved user frame is 64-bit or compat depending on the CS.
 *
 * The INIT process has no vma_table, so verify_address()/check_user_area()
 * accept its kernel-address arguments (init_argv, the "/dev/console"
 * string, ...) - the same contract as the 32-bit kernel. */
#include <fnx/efi.h>
#include <fnx/errno.h>
#include <fnx/kernel.h>
#include <fnx/process.h>
#include <fnx/sigcontext.h>
#include <fnx/stdio.h>
#include <fnx/unistd.h>
#include <fnx/string.h>

#define PAGE_OFFSET64	0xFFFFFFFF80000000ULL

#define UCODE32_SEL	0x18
#define UDATA32_SEL	0x20
#define UCODE64_SEL	0x48

/* x86-64 syscall numbers for the native ABI (the kernel's SYS_* constants
 * in unistd.h are the i386 values, used by the compat syscall_table) */
#define SYS64_execve	59
#define SYS64_rt_sigreturn 15
#define SYS64_mmap	9
#define SYS64_mremap	25
#define SYS64_brk	12
#define SYS64_shmat	30

extern void *syscall_table[];
#ifdef __x86_64__
extern void *syscall_table64[];
#define NR_SYSCALLS64	319	/* x86_64 table: highest implemented nr + 1 */
#endif /* __x86_64__ */
extern void init_trampoline(void);
extern int map_user_page64(unsigned long, unsigned long, unsigned long);

/* kernel/syscalls.c: void *syscall_table[] has 275 entries */
#define NR_SYSCALLS_BOUND	275

/* kernel64/idt64.c: the frame the CPU pushed below the 15 saved GPRs */
struct x86_frame64 {
	unsigned long vector;
	unsigned long error;
	unsigned long rip;
	unsigned long cs;
	unsigned long rflags;
	unsigned long rsp;
	unsigned long ss;
};

/* Map the INIT user stack page (fixed user VA 0x101000, the page above
 * the 0x100000 trampoline) as user-accessible in INIT's OWN pml4 (the one
 * in CR3 after do_switch). Called with RSP = current->tss.esp0 (the
 * kernel stack), right before the iretq into user mode. */
void user_mode_prep(void)
{
	unsigned long stack_va;
	unsigned long pml4;

	extern unsigned long paging64_pml4_phys(void);
	extern int map_user_page64_in(unsigned long, unsigned long, unsigned long, unsigned long);

	pml4 = current->cr3_64 ? current->cr3_64 : paging64_pml4_phys();
	stack_va = 0x101000;
	map_user_page64_in(pml4, stack_va, stack_va - PAGE_OFFSET64, 0x003);	/* P|RW: user stack */
}

/* Real int 0x80 dispatcher (replaces the M4-A demo handler). gprs points
 * at the 15 saved GPRs (gprs[0] = r15, [1] = r14, ..., [14] = rax) with the
 * CPU frame right below. FNX: native x86-64 syscalls only (no 32-bit
 * compatibility), dispatched via syscall_table64. */
void syscall80_handler(unsigned long *gprs)
{
	struct x86_frame64 *f;
	struct sigcontext sc;
	int was_exec, was_sigreturn;
	long ret;

	f = (struct x86_frame64 *)((char *)gprs + (15 * 8));
	if(f->vector != 0x80) {
		return;
	}
	memset_b(&sc, 0, sizeof(sc));
	/* 64-bit-native sigcontext: err, rip, cs, rflags, rsp, ss, then the 15
	 * GPRs in isr_common64 push order (gprs[0]=r15 ... gprs[14]=rax). */
	sc.err = f->error;
	sc.rip = f->rip;
	sc.cs = f->cs;
	sc.rflags = f->rflags;
	sc.rsp = f->rsp;
	sc.ss = f->ss;
	sc.r15 = gprs[0];
	sc.r14 = gprs[1];
	sc.r13 = gprs[2];
	sc.r12 = gprs[3];
	sc.r11 = gprs[4];
	sc.r10 = gprs[5];
	sc.r9 = gprs[6];
	sc.r8 = gprs[7];
	sc.rdi = gprs[8];
	sc.rsi = gprs[9];
	sc.rbp = gprs[10];
	sc.rbx = gprs[11];
	sc.rdx = gprs[12];
	sc.rcx = gprs[13];
	sc.rax = gprs[14];


	was_exec = (sc.rax == SYS64_execve);
	was_sigreturn = (sc.rax == SYS64_rt_sigreturn);
	{
		/* FNX (native port): x86-64 syscall ABI (rdi/rsi/rdx/r10/r8
		 * args, rax = nr) and the x86_64-numbered syscall_table64. */
		long a1, a2, a3, a4, a5;

		if(sc.rax >= NR_SYSCALLS64 || !syscall_table64[sc.rax]) {
			gprs[14] = -ENOSYS;
			return;
		}

		/* x86-64 syscall ABI: arg1=rdi arg2=rsi arg3=rdx
		 * arg4=r10 arg5=r8. The syscall_entry64 frame saves
		 * (gprs[14]=rax,[13]=rcx,[12]=rdx,[11]=rbx,[10]=rbp,
		 * [9]=rsi,[8]=rdi,[7]=r8,[6]=r9,[5]=r10,[4]=r11,[3]=r12,
		 * [2]=r13,[1]=r14,[0]=r15). NOTE rcx is the syscall
		 * return RIP and is NOT an argument; the 4th arg is r10.
		 * The 6th arg (r9, gprs[6]) is stashed in sc.r9 for
		 * mmap/select-style syscalls. */
		a1 = (long)gprs[8];		/* rdi */
		a2 = (long)gprs[9];		/* rsi */
		a3 = (long)gprs[12];		/* rdx */
		a4 = (long)gprs[5];		/* r10 */
		a5 = (long)gprs[7];		/* r8 */
		sc.r9 = (unsigned long)gprs[6];	/* r9 (6th arg) */

		/* same dispatch as the 32-bit do_syscall(): the table entry gets
		 * the 5 ABI args + a pointer to the (patched on exec) sigcontext */
		current->sp = (addr_t)&sc;
		/* SysV x86-64 ABI: every syscall except mmap/brk returns an int
		 * (RAX = EAX, upper bits unspecified) - read it as int and
		 * sign-extend so -EINVAL etc. reach the caller as negative.
		 * mmap/brk return 64-bit addresses (e.g. 0x400000000000 in the
		 * canonical 128TB user half) and are declared long; shmat
		 * likewise returns the mapped address (musl: (void *)ret). */
		if(sc.rax == SYS64_mmap || sc.rax == SYS64_mremap || sc.rax == SYS64_brk || sc.rax == SYS64_shmat) {
			ret = ((long (*)(long, long, long, long, long, struct sigcontext *))
				syscall_table64[sc.rax])(a1, a2, a3, a4, a5, &sc);
		} else {
			ret = (long)(int)((int (*)(long, long, long, long, long, struct sigcontext *))
				syscall_table64[sc.rax])(a1, a2, a3, a4, a5, &sc);
		}
	}

	if(was_exec && !ret && (current->flags & PF_PEXEC)) {
		/* sys_execve()/elf_load() patched sc.rip/sc.rsp with the new
		 * program entry and stack. iretq into native 64-bit user mode
		 * (UCODE64|RPL3, UDATA32|RPL3). The frame is built on the
		 * current kernel stack and iretq never returns to the isr
		 * epilogue. */
		__asm__ __volatile__(
			"movw $0x23, %%ax\n\t"
			"movw %%ax, %%ds\n\t"
			"movw %%ax, %%es\n\t"
			"movw %%ax, %%fs\n\t"
			"movw %%ax, %%gs\n\t"
			"pushq %0\n\t"		/* SS  = UDATA32 | RPL3 */
			"pushq %1\n\t"		/* RSP = new user stack */
			"pushq %2\n\t"		/* RFLAGS */
			"pushq %3\n\t"		/* CS  = UCODE64 | RPL3 */
			"pushq %4\n\t"		/* RIP = new program entry */
			"iretq\n\t"
			:: "r"((unsigned long)(UDATA32_SEL | 3)),
			   "r"(sc.rsp),
			   "r"(sc.rflags),
			   "r"((unsigned long)(UCODE64_SEL | 3)),
			   "r"(sc.rip)
			: "rax", "memory");
	}

	/* plain syscall return: store the result in eax (gprs[14] = the rax
	 * slot; gprs[0] is r15); the isr64 epilogue iretq's back to the user
	 * frame. Full 64-bit: mmap() returns addresses in the 128TB user half
	 * (e.g. 0x400000000000) whose low 32 bits are 0 - a 32-bit store
	 * would make the caller see a NULL mapping. */
	if(was_sigreturn) {
		/* sys_rt_sigreturn() copied the saved (pre-signal) sigcontext
		 * into sc; write it all back into the iretq frame + gprs so
		 * the isr64 epilogue resumes the interrupted user state. */
		f->rip = sc.rip;
		f->cs = sc.cs;
		f->rflags = sc.rflags;
		f->rsp = sc.rsp;
		f->ss = sc.ss;
		gprs[0] = sc.r15;
		gprs[1] = sc.r14;
		gprs[2] = sc.r13;
		gprs[3] = sc.r12;
		gprs[4] = sc.r11;
		gprs[5] = sc.r10;
		gprs[6] = sc.r9;
		gprs[7] = sc.r8;
		gprs[8] = sc.rdi;
		gprs[9] = sc.rsi;
		gprs[10] = sc.rbp;
		gprs[11] = sc.rbx;
		gprs[12] = sc.rdx;
		gprs[13] = sc.rcx;
		gprs[14] = sc.rax;
		return;
	}
	gprs[14] = (unsigned long)ret;
}
