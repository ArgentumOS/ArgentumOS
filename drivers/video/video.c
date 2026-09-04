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
#include <fnx/errno.h>
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

/*
 * Bochs dispi (VBE) register interface.
 *
 * The UEFI GOP framebuffer can only be changed through the firmware
 * while boot services are alive; after ExitBootServices the kernel has
 * to program the display controller itself. Under QEMU the device
 * behind OVMF's GOP is the standard VGA, a Bochs-compatible (dispi)
 * controller whose registers sit at I/O ports 0x01CE/0x01CF. The
 * linear framebuffer is the controller's PCI BAR - the firmware fixed
 * its physical address (video.fb_phys) and a mode change never moves
 * it, it only changes geometry/pitch. Programming it here mirrors what
 * seabios/OVMF's own VBE SetMode does on this hardware.
 *
 * On any non-Bochs controller the ID probe fails and the call returns
 * -EOPNOTSUPP.
 */
#define VBE_DISPI_IOPORT_INDEX		0x01CE
#define VBE_DISPI_IOPORT_DATA		0x01CF
#define VBE_DISPI_INDEX_ID		0x0
#define VBE_DISPI_INDEX_XRES		0x1
#define VBE_DISPI_INDEX_YRES		0x2
#define VBE_DISPI_INDEX_BPP		0x3
#define VBE_DISPI_INDEX_ENABLE		0x4
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K	0xa
#define VBE_DISPI_ID0			0xB0C0
#define VBE_DISPI_ENABLED		0x01
#define VBE_DISPI_LFB_ENABLED		0x40
#define VBE_DISPI_MAX_XRES		16000
#define VBE_DISPI_MAX_YRES		12000
#define VBE_DISPI_MAX_BPP		32

#define VBE_DISPI_MIN_XRES		320
#define VBE_DISPI_MIN_YRES		200

static void vbe_write(unsigned int index, unsigned int val)
{
	outport_w(VBE_DISPI_IOPORT_INDEX, index);
	outport_w(VBE_DISPI_IOPORT_DATA, val);
}

static unsigned int vbe_read(unsigned int index)
{
	outport_w(VBE_DISPI_IOPORT_INDEX, index);
	return inport_w(VBE_DISPI_IOPORT_DATA);
}

/*
 * Recompute every derived geometry field for a linear mode. Shared by
 * gop_video_init() (boot, from the firmware-captured GOP info) and
 * video_gop_set_mode() (runtime mode switch) so there is one source of
 * truth for the field formulas.
 */
void video_gop_geometry(unsigned int width, unsigned int height,
			unsigned int bpp, unsigned int pitch)
{
	video.fb_width = width;
	video.fb_height = height;
	video.fb_bpp = bpp;
	video.fb_pixelwidth = bpp / 8;
	video.fb_pitch = pitch;
	video.fb_char_width = 8;
	video.fb_char_height = 16;
	video.fb_linesize = video.fb_pitch * video.fb_char_height;
	video.fb_size = width * height * video.fb_pixelwidth;
	video.columns = width / video.fb_char_width;
	video.lines = height / video.fb_char_height;
	video.fb_vsize = video.lines * video.fb_pitch * video.fb_char_height;
}

/*
 * Switch the GOP framebuffer to a new resolution/depth at runtime.
 * Only works when the firmware's GOP sits on a Bochs-compatible
 * controller (QEMU's standard VGA - the FNX reference platform).
 *
 * width/height/bpp: requested geometry. The controller only supports
 * x-resolutions that are multiples of 8; the actual mode is rounded
 * down and reported back through the video fields (callers re-read the
 * geometry after a successful call). bpp accepts 16, 24 and 32.
 */
int video_gop_set_mode(unsigned int width, unsigned int height,
		       unsigned int bpp)
{
	unsigned int pixelwidth, pitch, linelength, vram, memsize;

	if(!video.fb_phys) {
		return -ENODEV;
	}

	/* is there a Bochs dispi controller behind the GOP? */
	vbe_write(VBE_DISPI_INDEX_ID, VBE_DISPI_ID0);
	if(vbe_read(VBE_DISPI_INDEX_ID) != VBE_DISPI_ID0) {
		return -EOPNOTSUPP;
	}

	switch(bpp) {
		case 16:
		case 24:
		case 32:
			break;
		default:
			return -EINVAL;
	}

	/* VBE rounds the x-resolution down to a multiple of 8 */
	width &= ~7;
	if(width < VBE_DISPI_MIN_XRES || width > VBE_DISPI_MAX_XRES ||
	   height < VBE_DISPI_MIN_YRES || height > VBE_DISPI_MAX_YRES) {
		return -EINVAL;
	}

	pixelwidth = bpp / 8;
	linelength = width * pixelwidth;
	pitch = width * pixelwidth;

	/* the controller silently clamps a too-tall mode to its VRAM,
	 * so reject it up front instead of reporting a different mode */
	vram = vbe_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K) * 65536;
	if(!vram) {
		vram = 16 * 1024 * 1024;	/* fallback if unreadable */
	}
	if(height > vram / linelength) {
		return -EINVAL;
	}

	/* disable the display, program the geometry, re-enable with the
	 * linear framebuffer bit. On the enable transition QEMU resets
	 * the virtual width to the x-resolution (linear layout) and
	 * clears the top yres*linelength of the VRAM. */
	vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_LFB_ENABLED);
	vbe_write(VBE_DISPI_INDEX_XRES, width);
	vbe_write(VBE_DISPI_INDEX_YRES, height);
	vbe_write(VBE_DISPI_INDEX_BPP, bpp);
	vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

	/* grow the kernel's linear map if the new mode needs more than
	 * the firmware mode mapped at boot (idempotent re-map). Do this
	 * before committing the new geometry so a failure leaves the
	 * kernel's view consistent with what the controller shows. */
	memsize = width * height * pixelwidth;
	if(video_map_framebuffer(video.fb_phys, memsize)) {
		return -EFAULT;
	}

	/* update the kernel's view of the framebuffer */
	video_gop_geometry(width, height, bpp, pitch);
	video.memsize = memsize;
	video.fb_version = 0;

	/* make sure the screen starts black even if the controller did
	 * not clear it on the mode switch */
	memset_b((void *)video.address, 0, memsize);

	printk("fb0       %dx%dx%d pitch=%d memsize=%d\n",
		video.fb_width, video.fb_height, video.fb_bpp,
		video.fb_pitch, video.memsize);

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
