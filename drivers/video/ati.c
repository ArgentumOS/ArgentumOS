/*
 * fnx/drivers/video/ati.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * ATI Rage XL / RV100 (QEMU -device ati-vga, PCI 1002:5159).
 *
 * The OVMF has no GOP driver for the ATI, so the framebuffer must be
 * set up natively. BAR0 is the VRAM (the linear framebuffer), BAR2 is
 * the MMIO register block. The "extended mode" is programmed through the
 * CRTC registers (the offsets are from ati_regs.h):
 *
 *   CRTC_GEN_CNTL (0x50)  = CRTC2_EXT_DISP_EN | CRTC2_EN | PIX_WIDTH_32BPP
 *   CRTC_H_TOTAL_DISP (0x200) = ((width/8)-1) << 16
 *   CRTC_V_TOTAL_DISP (0x208) = (height-1) << 16
 *   CRTC_PITCH (0x22c)        = width * bpp / 8 / 8   (stride = pitch*8)
 *   CRTC_OFFSET (0x224)       = 0
 */

#include <fnx/asm.h>
#include <fnx/config.h>
#include <fnx/pci.h>
#include <fnx/console.h>
#include <fnx/video.h>
#include <fnx/string.h>
#include <fnx/stdio.h>

#define ATI_PCI_VENDOR		0x1002
#define ATI_PCI_DEVICE		0x5159	/* Radeon QY (RV100) */

#define ATI_MMIO_VA		0xFFFFBE3000000000UL	/* pml4[386] */

#define CRTC_GEN_CNTL		0x0050
#define CRTC_H_TOTAL_DISP	0x0200
#define CRTC_V_TOTAL_DISP	0x0208
#define CRTC_OFFSET		0x0224
#define CRTC_PITCH		0x022c

#define CRTC2_EXT_DISP_EN	0x01000000
#define CRTC2_EN		0x02000000
#define CRTC_PIX_WIDTH_32BPP	0x00000600

static unsigned long ati_mmio;

static void ati_writel(unsigned int reg, unsigned int value)
{
	*(volatile unsigned int *)(ati_mmio + reg) = value;
}

static int setup_ati_device(struct pci_device *pci_dev)
{
	extern int map_page64(unsigned long, unsigned long, unsigned long);
	unsigned short int cmd;
	unsigned int lfb, mmio, vramsize;
	unsigned int width = 1024, height = 768, bpp = 32;
	int i;

	lfb = pci_dev->bar[0] & 0xFFFFFFF0;
	mmio = pci_dev->bar[2] & 0xFFFFFFF0;

	/* enable memory space */
	cmd = (pci_dev->command | PCI_COMMAND_MEMORY);
	pci_write_short(pci_dev, PCI_COMMAND, cmd);

	/* map the MMIO block (the default ATI VRAM is 16MB, the MMIO is 8KB) */
	for(i = 0; i < 8192 / 4096; i++) {
		map_page64(ATI_MMIO_VA + i * 4096, mmio + i * 4096, 0x003);
	}
	ati_mmio = ATI_MMIO_VA;

	/* the extended-mode CRTC programming (32bpp) */
	ati_writel(CRTC_H_TOTAL_DISP, ((width / 8) - 1) << 16);
	ati_writel(CRTC_V_TOTAL_DISP, (height - 1) << 16);
	ati_writel(CRTC_PITCH, width * bpp / 8 / 8);
	ati_writel(CRTC_OFFSET, 0);
	ati_writel(CRTC_GEN_CNTL, CRTC2_EXT_DISP_EN | CRTC2_EN |
		   CRTC_PIX_WIDTH_32BPP);

	video.pci_dev = pci_dev;
	video.port = mmio;
	video.fb_width = width;
	video.fb_height = height;
	video.fb_bpp = bpp;
	video.fb_pixelwidth = bpp / 8;
	video.fb_pitch = width * video.fb_pixelwidth;
	video.fb_char_width = 8;
	video.fb_char_height = 16;
	video.fb_linesize = video.fb_pitch * video.fb_char_height;
	video.columns = video.fb_width / video.fb_char_width;
	video.lines = video.fb_height / video.fb_char_height;
	video.fb_size = video.fb_width * video.fb_height * video.fb_pixelwidth;
	video.fb_vsize = video.lines * video.fb_pitch * video.fb_char_height;

	/* 16MB of VRAM (BAR0); only the visible area is mapped for the fb */
	vramsize = video.fb_size;
	video.memsize = vramsize;

	strcpy((char *)video.signature, "ATI RAGE XL");
	video.fb_version = 2;

	video_map_framebuffer(lfb, vramsize);

	printk("ati: Rage XL framebuffer 1024x768x32 at 0x%x\n", lfb);
	return 1;
}

int ati_init(void)
{
	struct pci_device *pci_dev;

	pci_dev = pci_device_table;

	while(pci_dev) {
		if(pci_dev->vendor_id == ATI_PCI_VENDOR &&
		   (pci_dev->device_id == ATI_PCI_DEVICE ||
		    pci_dev->device_id == 0x5046)) {
			return setup_ati_device(pci_dev);
		}
		pci_dev = pci_dev->next;
	}
	return 0;
}
