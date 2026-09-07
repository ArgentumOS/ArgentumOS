/*
 * fnx/kernel/main.c
 *
 * Copyright 2018-2023, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/limits.h>
#include <fnx/kparms.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/system.h>
#include <fnx/version.h>
#include <fnx/utsname.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/video.h>
#include <fnx/console.h>
#include <fnx/serial.h>
#ifdef __x86_64__
#include <fnx/gop.h>
#include <fnx/bootconf.h>

extern void kernel_conf_apply(void);	/* kernel/multiboot.c */
#endif
#include <fnx/pci.h>
#include <fnx/pic.h>
#include <fnx/irq.h>
#include <fnx/msix.h>
#include <fnx/usb.h>
#include <fnx/segments.h>
#include <fnx/devices.h>
#include <fnx/buffer.h>
#include <fnx/cpu.h>
#include <fnx/timer.h>
#include <fnx/sleep.h>
#include <fnx/locks.h>
#include <fnx/ps2.h>
#include <fnx/keyboard.h>
#include <fnx/sched.h>
#include <fnx/mm.h>
#include <fnx/kexec.h>
#include <fnx/sysconsole.h>

struct kernel_params kparms;
struct kernel_stat kstat;
#ifdef __x86_64__
addr_t _last_data_addr;		/* FNX: also holds the high-half alias */
#else
unsigned int _last_data_addr;
#endif

struct new_utsname sys_utsname = {
	UTS_SYSNAME,
	UTS_NODENAME,
	UTS_RELEASE,
	UTS_VERSION,
	"",
	UTS_DOMAINNAME,
};

static void set_default_values(void)
{
	/* no rootfstype default: mount_root() probes the disk filesystems
	 * (minix -> ext2 -> iso9660 -> xbfs) when rootfstype= is absent */

	/* console defaults to /dev/tty0, but virtual consoles are disabled
	 * in this build (the session compositor owns the display and the
	 * console is the serial), so a boot without console= falls back
	 * to the first serial port (ttyS0) */
	if(!kparms.syscondev) {
		kparms.syscondev = MKDEV(SERIAL_MAJOR, 1 << SERIAL_MSF);
		add_sysconsoledev(kparms.syscondev);
	}
}

#ifdef __x86_64__
/* FNX: populate the video console from the UEFI GOP framebuffer that
 * the EFI stub captured before ExitBootServices. Called after multiboot()
 * (which, with no multiboot VBE info, falls back to VGA text) and before
 * video_init(), so the console renders through fbcon instead of vgacon. */
static void gop_video_init(void)
{
	int bpp, pixelwidth;

	if(!fnx_gop_fb.phys_base) {
		return;	/* no framebuffer (headless or no graphics device) */
	}

	/* QEMU's GOP reports 32bpp BGRX (PixelBlueGreenRedReserved...);
	 * fbcon's 32bpp set_color() writes 0x00RRGGBB, which lands in memory
	 * as B,G,R,0 - matching that layout byte-for-byte. */
	bpp = 32;
	pixelwidth = bpp / 8;

	video.flags = VPF_VESAFB;
	video.fb_phys = (unsigned int)fnx_gop_fb.phys_base;
	video.port = 0;
	video.memsize = (int)fnx_gop_fb.size;
	video_map_framebuffer(video.fb_phys, video.memsize);
	video.fb_version = 0;
	/* shared field computation (also used by runtime mode switches) */
	video_gop_geometry((unsigned int)fnx_gop_fb.width,
			   (unsigned int)fnx_gop_fb.height, bpp,
			   (unsigned int)fnx_gop_fb.pixels_per_scanline *
			   pixelwidth);
	strcpy((char *)video.signature, "UEFI GOP");

	/* Clear the OVMF boot graphics out of the framebuffer before the
	 * console first draws, so it starts on a black screen instead of
	 * showing the firmware's leftover splash/logo. (The "clobbers the
	 * kernel image" suspicion was disproven - the image stays intact
	 * through the whole boot.) */
	memset_b((void *)video.address, 0, video.memsize);
}
#endif /* __x86_64__ */

void start_kernel(unsigned int magic, unsigned int info, unsigned long last_boot_addr)
{
	struct proc *init;

	_last_data_addr = last_boot_addr - PAGE_OFFSET;
	memset_b(&kstat, 0, sizeof(kstat));
	sysconsole_init();

#ifdef CONFIG_QEMU_DEBUGCON
#ifdef __x86_64__
	/* FNX: the 0xE9 read-back probe is unreliable under QEMU's
	 * -debugcon; the port is present whenever the device is wired up,
	 * and writes to it are harmless otherwise. */
	kstat.flags |= KF_HAS_DEBUGCON;
#else
	if(inport_b(QEMU_DEBUG_PORT) == QEMU_DEBUG_PORT) {
		kstat.flags |= KF_HAS_DEBUGCON;
	}
#endif /* __x86_64__ */
#endif /* CONFIG_QEMU_DEBUGCON */

	printk("                       FNX v%s for x86_64 architecture\n", UTS_RELEASE);
	printk("                     Copyright (c) 2018-2025, Jordi Sanfeliu\n");
	printk("\n");
#ifdef __TINYC__
	printk("             (built on %s with tcc)\n", UTS_VERSION);
#else
	printk("             (built on %s with GCC %s)\n", UTS_VERSION, __VERSION__);
#endif
	printk("\n");
	printk("DEVICE    ADDRESS         IRQ   COMMENT\n");
	printk("--------------------------------------------------------------------------------\n");

	cpu_init();
	multiboot(magic, info);
	set_default_values();
	/* kernel.conf (ESP boot config, M1): applied after the compiled-in
	 * cmdline (default layer) and before mount_root. A future real
	 * firmware cmdline will be parsed after this and win over both. */
	kernel_conf_apply();
#ifdef __x86_64__
	gop_video_init();
#endif
	pic_init();
	irq_init();
	msix_init();
	dev_init();
	tty_init();
	mem_init();

#ifdef CONFIG_PCI
	pci_init();
	usb_init();
#endif /* CONFIG_PCI */

	video_init();
	console_init();
	timer_init();
	ps2_init();
	proc_init();
	sleep_init();
	buffer_init();
	sched_init();
	inode_init();
	fd_init();

	/*
	 * IDLE is now the current process (created manually as PID 0),
	 * it won't be placed in the running queue.
	 */
	current = get_proc_free();
	proc_slot_init(current);
	set_tss(current);
	load_tr(TSS);
	current->tss.cr3 = V2P((addr_t)kpage_dir);
#ifdef __x86_64__
	{
		extern unsigned long paging64_pml4_phys(void);
		current->cr3_64 = paging64_pml4_phys();	/* boot process uses the kernel pml4 */
	}
#endif /* __x86_64__ */
	current->flags |= PF_KPROC;
	sprintk(current->argv0, "%s", "idle");

	/* PID 1 is for the INIT process */
	init = get_proc_free();
	proc_slot_init(init);
	init->pid = get_unused_pid();

	kernel_process("kswapd", kswapd);	/* PID 2 */
	kernel_process("kbdflushd", kbdflushd);	/* PID 3 */

	/* kswapd will take over the rest of the kernel initialization */
	need_resched = 1;

	STI();		/* let's rock! */
	cpu_idle();
}

void stop_kernel(void)
{
	struct proc *p, *next;
	int n;

	/* put all processes to sleep and reset all pending signals */
	FOR_EACH_PROCESS_RUNNING(p) {
		next = p->next_run;
		not_runnable(p, PROC_SLEEPING);
		p->sigpending = 0;
		p = next;
	}

	/* flush all dirty buffers, inodes and superblocks to disk so that a
	 * process's un-synced writes survive a power-off */
	sync_superblocks(0);
	sync_inodes(0);
	sync_buffers(0);

#ifdef CONFIG_KEXEC
	if(!(kstat.flags & KF_HAS_PANICKED)) {
		if(kexec_size > 0) {
			switch(kexec_proto) {
				case KEXEC_MULTIBOOT1:
					kexec_multiboot1();
					break;
				case KEXEC_LINUX:
					kexec_linux();
					break;
			}
		}
	}
#endif /* CONFIG_KEXEC */

	printk("\n");
	printk("**    Safe to Power Off    **\n");
	printk("            -or-\n");
	printk("** Press Any Key to Reboot **\n");
	any_key_to_reboot = 1;

	/* stop and disable all interrupts! */
	CLI();
	for(n = 0; n < NR_IRQS; n++) {
		disable_irq(n);
	}

	/* enable keyboard only */
	enable_irq(KEYBOARD_IRQ);
	STI();

	cpu_idle();
}

void cpu_idle(void)
{
	for(;;) {
		if(need_resched) {
			do_sched();
		}
		HLT();
	}
}
