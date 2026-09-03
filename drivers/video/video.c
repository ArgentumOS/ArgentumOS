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
	/* GOP-only display: the UEFI GOP framebuffer (set up by
	 * gop_video_init in main.c) is the only display path. The native
	 * adapter drivers (vmware-svga, ATI Rage XL, Bochs BGA) were
	 * removed: none could scan its LFB out under QEMU (vmsvga swaps
	 * its VRAM/FIFO BARs and never runs FIFO commands; qemu's ati-vga
	 * ignores the CRTC2 mode-set; BGA needs a boot param and a second
	 * mode switch), so only the firmware-set GOP framebuffer is
	 * reliable. The session compositor owns /dev/fb0 (fb_init);
	 * fbcon_init is NOT called - no kernel text console draws on the
	 * display (the serial port is the system console). */
	if(video.flags & VPF_VGA) {
		vgacon_init();
		return;
	}

	if(video.flags & VPF_VESAFB) {
		fb_init();
	}
}
