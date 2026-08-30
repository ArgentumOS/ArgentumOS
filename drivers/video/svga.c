/*
 * fnx/drivers/video/svga.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * VMware SVGA II (QEMU -device vmware-svga, PCI 15ad:0405).
 *
 * The SVGA II is a linear-framebuffer display: BAR0 is an I/O pair of
 * index/value registers (index at BAR0+0, value at BAR0+4), BAR2 is the
 * framebuffer. The mode is set with a handful of register writes:
 *
 *   ID (0) = 2, WIDTH (2) = X, HEIGHT (3) = Y,
 *   BITS_PER_PIXEL (7) = 32, CONFIG_DONE (20) = 1, ENABLE (1) = 1
 *
 * then the LFB at BAR2 shows the mode and the console renders straight
 * into it (via video_map_framebuffer's kernel-high alias).
 */

#include <fnx/asm.h>
#include <fnx/config.h>
#include <fnx/pci.h>
#include <fnx/console.h>
#include <fnx/video.h>
#include <fnx/string.h>
#include <fnx/stdio.h>

#define SVGA_PCI_VENDOR		0x15ad
#define SVGA_PCI_DEVICE		0x0405

/* the SVGA II (SVGA_VERSION_2) uses byte-wide index/value ports:
 * the index at BAR0+0, the value at BAR0+1, the BIOS at +2 */
#define SVGA_IO_MUL		1
#define SVGA_INDEX_PORT		0
#define SVGA_VALUE_PORT		1

#define SVGA_REG_ID		0
#define SVGA_REG_ENABLE		1
#define SVGA_REG_WIDTH		2
#define SVGA_REG_HEIGHT		3
#define SVGA_REG_BITS_PER_PIXEL	7
#define SVGA_REG_CONFIG_DONE	20
#define SVGA_REG_MEM_START	18
#define SVGA_REG_SYNC		21

#define SVGA_ID_2		0x90000002	/* SVGA_MAKE_ID(2) */

#define SVGA_CMD_UPDATE		1
#define SVGA_FIFO_MIN		32		/* after the 16-byte header */
#define SVGA_FIFO_MAX		0x10000

struct svga_fifo {
	unsigned long base;	/* the kernel VA of the FIFO BAR */
	unsigned int next;	/* the guest write position */
};

static struct svga_fifo fifo;

static void svga_write_reg(unsigned int iobase, unsigned int reg,
			   unsigned int value)
{
	outport_l(iobase + SVGA_IO_MUL * SVGA_INDEX_PORT, reg);
	outport_l(iobase + SVGA_IO_MUL * SVGA_VALUE_PORT, value);
}

static unsigned int svga_read_reg(unsigned int iobase, unsigned int reg)
{
	outport_l(iobase + SVGA_IO_MUL * SVGA_INDEX_PORT, reg);
	return inport_l(iobase + SVGA_IO_MUL * SVGA_VALUE_PORT);
}

/* The SVGA II in its SVGA mode does not dirty-track the LFB: the guest
 * must post SVGA_CMD_UPDATE rect commands into the FIFO ring in the VRAM
 * and then write the SYNC register, which makes QEMU's fifo_run() flush
 * those rects to the display. The console calls this after every draw. */
static void svga_flush(void)
{
	unsigned int *head = (unsigned int *)fifo.base;
	unsigned int next = fifo.next;
	unsigned int *cmd;

	if(!video.flush || !fifo.base) {
		return;
	}
	if(next + 5 * 4 > SVGA_FIFO_MAX) {
		next = SVGA_FIFO_MIN;	/* never straddle the ring end */
	}
	cmd = (unsigned int *)((char *)fifo.base + next);
	cmd[0] = SVGA_CMD_UPDATE;
	cmd[1] = 0;
	cmd[2] = 0;
	cmd[3] = video.fb_width;
	cmd[4] = video.fb_height;
	next += 5 * 4;
	if(next >= SVGA_FIFO_MAX) {
		next = SVGA_FIFO_MIN;
	}
	fifo.next = next;
	head[3] = next;		/* SVGA_FIFO_NEXT */
	svga_write_reg(video.port, SVGA_REG_SYNC, 1);
}

static int setup_svga_device(struct pci_device *pci_dev)
{
	unsigned short int cmd;
	unsigned int iobase, lfb;
	unsigned int width = 1024, height = 768, bpp = 32;

	/* the LFB and the I/O BARs */
	lfb = pci_dev->bar[2] & 0xFFFFFFF0;
	iobase = pci_dev->bar[0] & 0xFFFFFFF0;

	/* enable I/O space and memory space */
	cmd = (pci_dev->command | PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	pci_write_short(pci_dev, PCI_COMMAND, cmd);

	/* the SVGA II mode-set */
	svga_write_reg(iobase, SVGA_REG_ID, SVGA_ID_2);
	svga_write_reg(iobase, SVGA_REG_WIDTH, width);
	svga_write_reg(iobase, SVGA_REG_HEIGHT, height);
	svga_write_reg(iobase, SVGA_REG_BITS_PER_PIXEL, bpp);
	svga_write_reg(iobase, SVGA_REG_CONFIG_DONE, 1);
	svga_write_reg(iobase, SVGA_REG_ENABLE, 1);


	video.pci_dev = pci_dev;
	video.port = iobase;
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
	video.memsize = 32 * 1024 * 1024;	/* the SVGA II has 32MB of VRAM */

	strcpy((char *)video.signature, "VMWARE SVGA");
	video.fb_version = 2;

	video_map_framebuffer(lfb, video.memsize);

	/* set up the FIFO ring for the display updates. The FIFO is its own
	 * BAR (BAR1, 64KB): the guest writes SVGA_CMD_UPDATE rect commands
	 * into the ring and then pokes SYNC, which makes QEMU's fifo_run()
	 * flush those rects to the display. */
	{
		extern int map_page64(unsigned long, unsigned long, unsigned long);
		unsigned int fifo_bar = pci_dev->bar[1] & 0xFFFFFFF0;
		unsigned int *head;
		int i;

		for(i = 0; i < (64 * 1024) / 4096; i++) {
			map_page64(FB_MMIO_VA + 0x10000000 + i * 4096,
				  fifo_bar + i * 4096, 0x003);
		}
		fifo.base = FB_MMIO_VA + 0x10000000;
		fifo.next = SVGA_FIFO_MIN;
		head = (unsigned int *)fifo.base;
		head[0] = SVGA_FIFO_MIN;		/* SVGA_FIFO_MIN */
		head[1] = SVGA_FIFO_MAX;		/* SVGA_FIFO_MAX */
		head[2] = SVGA_FIFO_MIN;		/* SVGA_FIFO_STOP */
		head[3] = SVGA_FIFO_MIN;		/* SVGA_FIFO_NEXT */
	}
	video.flush = svga_flush;

	printk("svga: VMware SVGA II framebuffer 1024x768x32 at 0x%x (io=0x%x)\n", lfb, iobase);
	return 1;
}

int svga_init(void)
{
	struct pci_device *pci_dev;

	pci_dev = pci_device_table;

	while(pci_dev) {
		if(pci_dev->vendor_id == SVGA_PCI_VENDOR &&
		   pci_dev->device_id == SVGA_PCI_DEVICE) {
			return setup_svga_device(pci_dev);
		}
		pci_dev = pci_dev->next;
	}
	return 0;
}
