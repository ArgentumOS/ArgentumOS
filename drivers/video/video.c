/*
 * fnx/drivers/video/video.c
 *
 * Copyright 2021-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/config.h>
#include <fnx/vgacon.h>
#include <fnx/fb.h>
#include <fnx/fbcon.h>
#include <fnx/bga.h>
#include <fnx/ati.h>
#include <fnx/console.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/console.h>
#include <fnx/video.h>
#include <fnx/mm.h>

/* Map the physical framebuffer at a kernel-high VA so that every
 * process context (which shares the kernel's upper half) can read and
 * write it - the raw phys address is only identity-mapped for the boot
 * pml4 and faults in a user process's page tables. */
int video_map_framebuffer(unsigned int phys, unsigned int memsize)
{
	extern int map_page64(unsigned long, unsigned long, unsigned long);
	unsigned int i, pages = (memsize + 4095) >> 12;

	for(i = 0; i < pages; i++) {
		if(map_page64(FB_MMIO_VA + i * 4096, phys + i * 4096, 0x003)) {
			return -1;
		}
	}
	video.fb_phys = phys;
	video.address = (unsigned int *)FB_MMIO_VA;
	return 0;
}

void video_init(void)
{
	/* Native display adapters first: they provide a linear framebuffer
	 * even when the firmware's GOP could not (e.g. the ATI Rage XL,
	 * which OVMF has no driver for - the boot would otherwise fall
	 * back to the VGA text console with no /dev/fb0 at all). They
	 * probe for their specific PCI device and leave the GOP setup
	 * untouched when absent. (The vmware-svga driver was removed:
	 * QEMU's vmsvga swaps the VRAM/FIFO BARs vs the SVGA spec and
	 * never executes FIFO commands, so its LFB could not be scanned
	 * out.) fbcon_init is NOT called below: the session compositor
	 * owns the LFB and the serial port is the system console, so no
	 * kernel text console draws on the display. */
#ifdef CONFIG_PCI
	if(ati_init()) {
		video.flags = (video.flags & ~VPF_VGA) | VPF_VESAFB;
	}
#endif /* CONFIG_PCI */

	if(video.flags & VPF_VGA) {
		vgacon_init();
		return;
	}

#ifdef CONFIG_PCI
#ifdef CONFIG_BGA
	if(video.flags & VPF_VESAFB) {
		bga_init();
	}
#endif /* CONFIG_BGA */
#endif /* CONFIG_PCI */

	if(video.flags & VPF_VESAFB) {
		fb_init();
	}
}
