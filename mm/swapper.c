/*
 * fnx/mm/swapper.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/config.h>
#include <fnx/process.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/ipc.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/memdev.h>
#include <fnx/serial.h>
#include <fnx/lp.h>
#include <fnx/ramdisk.h>
#include <fnx/floppy.h>
#include <fnx/ata.h>
#include <fnx/buffer.h>
#include <fnx/mm.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/pty.h>
#include <fnx/es1370.h>
#include <fnx/ac97.h>
#include <fnx/virtio_snd.h>
#include <fnx/hda.h>

#include <fnx/net.h>
#include <fnx/stdio.h>

/* kswapd continues the kernel initialization */
int kswapd(void)
{
	STI();

#ifdef CONFIG_SYSVIPC
	ipc_init();
#endif /* CONFIG_SYSVIPC */

	/* char devices */
	memdev_init();
	serial_init();
	lp_init();
#ifdef CONFIG_UNIX98_PTYS
	pty_init();
	es1370_init();
	ac97_init();
	virtio_snd_init();
	hda_init();
#endif /* CONFIG_UNIX98_PTYS */

	/* network */
#ifdef CONFIG_NET
	net_init();
#endif /* CONFIG_NET */

	/* block devices */
	ramdisk_init();
	floppy_init();
	ata_init();
	ahci_init();
	pvscsi_init();
	nvme_init();

	/* starting system */
	mem_stats();
	fs_init();
	mount_root();
	devfs_boot_mount();
	init_init();

	/* make sure interrupts are enabled after initializing devices */
	STI();

	for(;;) {
		sleep(&kswapd, PROC_INTERRUPTIBLE);
		if((kstat.pages_reclaimed = reclaim_buffers())) {
			continue;
		}
		wakeup(&get_free_page);
	}
}
