/* Fiwix64 (M6): the INIT process's user-mode entry + int 0x80 syscall
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
#include <fiwix/efi.h>
#include <fiwix/errno.h>
#include <fiwix/kernel.h>
#include <fiwix/process.h>
#include <fiwix/sigcontext.h>
#include <fiwix/stdio.h>
#include <fiwix/unistd.h>
#include <fiwix/string.h>

#define PAGE_OFFSET64	0xFFFFFFFF80000000ULL

#define UCODE32_SEL	0x18
#define UDATA32_SEL	0x20
#define UCODE64_SEL	0x48

/* x86-64 syscall numbers for the native ABI (the kernel's SYS_* constants
 * in unistd.h are the i386 values, used by the compat syscall_table) */
#define SYS64_execve	59
#define SYS64_mmap	9
#define SYS64_brk	12

extern void *syscall_table[];
#ifdef __x86_64__
extern void *syscall_table64[];
#define NR_SYSCALLS64	232	/* x86_64 table: highest implemented nr + 1 */
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
 * at the 15 saved GPRs (gprs[0] = rax, [1] = rcx, [2] = rdx, [3] = rbx,
 * [5] = rsi, [6] = rdi, ...) with the CPU frame right below. */
void syscall80_handler(unsigned long *gprs)
{
	struct x86_frame64 *f;
	struct sigcontext sc;
	int was_exec;
	long ret;

	f = (struct x86_frame64 *)((char *)gprs + (15 * 8));
	if(f->vector != 0x80) {
		return;
	}

	memset_b(&sc, 0, sizeof(sc));
	/* isr_common64 pushes in order rax,rcx,rdx,rbx,rbp,rsi,rdi,r8..r15, so
	 * the LAST push (r15) is at gprs[0] and rax is at gprs[14] */
	sc.eax = (unsigned int)gprs[14];	/* rax: syscall number */
	sc.ebx = (int)gprs[11];			/* rbx: arg1 */
	sc.ecx = (int)gprs[13];			/* rcx: arg2 */
	sc.edx = (int)gprs[12];			/* rdx: arg3 */
	sc.esi = (int)gprs[9];			/* rsi: arg4 */
	sc.edi = (int)gprs[8];			/* rdi: arg5 */
	sc.ebp = (unsigned int)gprs[10];	/* rbp: arg6 (sys_ipc/msgrcv, mmap2 offset) */
	sc.eip = (unsigned int)f->rip;
	sc.cs = (unsigned int)f->cs;
	sc.eflags = (unsigned int)f->rflags;
	sc.esp = (unsigned int)f->rsp;
	sc.oldesp = (unsigned int)f->rsp;
	sc.oldss = (unsigned int)f->ss;
	/* Fiwix64 (canonical amd64 split): the full 64-bit user RIP/RSP of
	 * the syscall frame - fork() children iretq back to these, and they
	 * can be anywhere in the 128TB user half (not truncated to 32 bits
	 * like the i386-compat eip/oldesp fields). */
	sc.rip = f->rip;
	sc.rsp = f->rsp;

	was_exec = (sc.eax == ((current->flags & PF_ELF64) ? SYS64_execve : SYS_execve));
	{
		/* Fiwix64 (native port): a native 64-bit process (PF_ELF64) uses
		 * the x86-64 syscall ABI (rdi/rsi/rdx/rcx/r8/r9 args, rax = nr)
		 * and the x86_64-numbered syscall_table64; the compat path keeps
		 * the i386 ABI (ebx/ecx/edx/esi/edi/ebp) and syscall_table. */
		void **tbl = (current->flags & PF_ELF64) ? syscall_table64 : syscall_table;
		unsigned long bound = (current->flags & PF_ELF64) ?
			NR_SYSCALLS64 : NR_SYSCALLS_BOUND;
		long a1, a2, a3, a4, a5;

		if(sc.eax >= bound || !tbl[sc.eax]) {
			gprs[14] = -ENOSYS;
			return;
		}

		if(current->flags & PF_ELF64) {
			/* x86-64 syscall ABI: arg1=rdi arg2=rsi arg3=rdx
			 * arg4=r10 arg5=r8. The syscall_entry64 frame saves
			 * (gprs[14]=rax,[13]=rcx,[12]=rdx,[11]=rbx,[10]=rbp,
			 * [9]=rsi,[8]=rdi,[7]=r8,[6]=r9,[5]=r10,[4]=r11,[3]=r12,
			 * [2]=r13,[1]=r14,[0]=r15). NOTE rcx is the syscall
			 * return RIP and is NOT an argument; the 4th arg is r10.
			 * The 6th arg (r9, gprs[6]) is stashed in sc.ebp for
			 * mmap/select-style syscalls. */
			a1 = (long)gprs[8];		/* rdi */
			a2 = (long)gprs[9];		/* rsi */
			a3 = (long)gprs[12];		/* rdx */
			a4 = (long)gprs[5];		/* r10 */
			a5 = (long)gprs[7];		/* r8 */
			sc.ebp = (unsigned int)gprs[6];	/* r9 (6th arg) */
		} else {
			/* The INIT trampoline's USER_SYSCALL macro passes its
			 * arguments as 32-bit values (lea (%rax),%ebx), so
			 * kernel-data pointers arrive truncated (0x8xxxxxxx = low
			 * 32 bits of 0xffffffff8xxxxxxx). Before the process has
			 * any vma (the INIT case) reconstruct them; once the
			 * program is exec'd its real 32-bit user pointers (bit 31
			 * clear) pass through untouched. */
			a1 = sc.ebx;
			a2 = sc.ecx;
			a3 = sc.edx;
			a4 = sc.esi;
			a5 = sc.edi;
			if(!current->vma_table) {
				if(a1 & 0x80000000L) a1 = 0xFFFFFFFF80000000UL | (a1 & 0x7FFFFFFF);
				if(a2 & 0x80000000L) a2 = 0xFFFFFFFF80000000UL | (a2 & 0x7FFFFFFF);
				if(a3 & 0x80000000L) a3 = 0xFFFFFFFF80000000UL | (a3 & 0x7FFFFFFF);
				if(a4 & 0x80000000L) a4 = 0xFFFFFFFF80000000UL | (a4 & 0x7FFFFFFF);
				if(a5 & 0x80000000L) a5 = 0xFFFFFFFF80000000UL | (a5 & 0x7FFFFFFF);
			}
		}

		/* same dispatch as the 32-bit do_syscall(): the table entry gets
		 * the 5 ABI args + a pointer to the (patched on exec) sigcontext */
		current->sp = (addr_t)&sc;
		/* SysV x86-64 ABI: every syscall except mmap/brk returns an int
		 * (RAX = EAX, upper bits unspecified) - read it as int and
		 * sign-extend so -EINVAL etc. reach the caller as negative.
		 * mmap/brk return 64-bit addresses (e.g. 0x400000000000 in the
		 * canonical 128TB user half) and are declared long. */
		if(sc.eax == SYS64_mmap || sc.eax == SYS64_brk) {
			ret = ((long (*)(long, long, long, long, long, struct sigcontext *))
				tbl[sc.eax])(a1, a2, a3, a4, a5, &sc);
		} else {
			ret = (long)(int)((int (*)(long, long, long, long, long, struct sigcontext *))
				tbl[sc.eax])(a1, a2, a3, a4, a5, &sc);
		}
	}

	if(was_exec && !ret && (current->flags & PF_PEXEC)) {
		/* sys_execve()/elf_load() patched sc.eip/oldesp with the new
		 * program entry and stack. iretq into it in COMPAT mode
		 * (UCODE32|RPL3, UDATA32|RPL3) for a 32-bit binary, or in
		 * native 64-bit user mode (UCODE64|RPL3) for an ELF64 binary
		 * (the Fiwix64 port). The frame is built on the current kernel
		 * stack and iretq never returns to the isr epilogue. */
		unsigned long long xrip, xrsp;

		xrip = (current->flags & PF_ELF64) ? sc.rip : sc.eip;
		xrsp = (current->flags & PF_ELF64) ? sc.rsp : sc.oldesp;
		__asm__ __volatile__(
			"movw $0x23, %%ax\n\t"
			"movw %%ax, %%ds\n\t"
			"movw %%ax, %%es\n\t"
			"movw %%ax, %%fs\n\t"
			"movw %%ax, %%gs\n\t"
			"pushq %0\n\t"		/* SS  = UDATA32 | RPL3 */
			"pushq %1\n\t"		/* ESP/RSP = new user stack */
			"pushq %2\n\t"		/* EFLAGS/RFLAGS */
			"pushq %3\n\t"		/* CS  = UCODE32 or UCODE64 | RPL3 */
			"pushq %4\n\t"		/* EIP/RIP = new program entry */
			"iretq\n\t"
			:: "r"((unsigned long)(UDATA32_SEL | 3)),
			   "r"(xrsp),
			   "r"((unsigned long)sc.eflags),
			   "r"((unsigned long)((current->flags & PF_ELF64) ?
				(UCODE64_SEL | 3) : (UCODE32_SEL | 3))),
			   "r"(xrip)
			: "rax", "memory");
	}

	/* plain syscall return: store the result in eax (gprs[14] = the rax
	 * slot; gprs[0] is r15); the isr64 epilogue iretq's back to the user
	 * frame (64-bit for the trampoline, compat for a 32-bit program - the
	 * frame's CS selects the mode). Full 64-bit: mmap() returns addresses
	 * in the 128TB user half (e.g. 0x400000000000) whose low 32 bits are
	 * 0 - a 32-bit store would make the caller see a NULL mapping. */
	gprs[14] = (unsigned long)ret;
}
