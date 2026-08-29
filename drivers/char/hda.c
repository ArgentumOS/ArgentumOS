/*
 * fnx/drivers/char/hda.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Intel HD Audio (ICH6 "intel-hda" 8086:2668 / ICH9 "ich9-intel-hda"
 * 8086:293e) with the OSS /dev/dsp API, against QEMU's hda-output codec.
 *
 * The controller is a BAR0 MMIO device: GCTL reset, then two DMA rings in
 * guest memory carry codec commands and responses (CORB: 4-byte verbs,
 * RIRB: 8-byte responses). The verb word is cad<<28 | nid<<20 | verb<<8 |
 * payload; responses are polled (the controller processes the CORB
 * synchronously on the CORBWP write, and stalls once rirb_count reaches
 * RINTCNT until RIRBSTS is cleared). The output stream is SDO0 at
 * 0x100: SD_CBL/SD_LVI/SD_FORMAT/SD_BDLP describe a 16-byte BDL entry
 * {u64 addr; u32 len; u32 flags}; SD_CTL reset then DMA_START+tag starts
 * it and QEMU parses the BDL; when an entry with IOC (flags bit 0)
 * completes, the controller raises the stream completion interrupt
 * (SD_STS bit 2, INTSTS bit 4, INTx). The writer re-arms each chunk by
 * updating the BDL entry and restarting the stream.
 */

#include <fnx/kernel.h>
#include <fnx/fs.h>
#include <fnx/devices.h>
#include <fnx/pci.h>
#include <fnx/asm.h>
#include <fnx/irq.h>
#include <fnx/pic.h>
#include <fnx/sleep.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/string.h>
#include <fnx/stdio.h>

#define HDA_VENDOR_ID			0x8086
#define HDA_DEVICE_ICH6			0x2668	/* intel-hda */
#define HDA_DEVICE_ICH9			0x293e	/* ich9-intel-hda */

#define DSP_MAJOR			14
#define DSP_MINOR			3

#define HDA_MMIO_VA			0xFFFFBE1000000000UL	/* pml4[383] */
#define HDA_MMIO_SIZE			0x4000

/* controller registers */
#define HDA_REG_GCTL			0x08
#define   HDA_GCTL_RESET		(1 << 0)
#define   HDA_GCTL_FCNTRL		(1 << 1)
#define HDA_REG_WAKEEN			0x0c
#define HDA_REG_STATESTS		0x0e
#define HDA_REG_INTCTL			0x20
#define   HDA_INT_GLOBAL_EN		(1U << 31)
#define   HDA_INT_CTRL_EN		(1 << 30)
#define HDA_REG_INTSTS			0x24
#define HDA_REG_CORBLBASE		0x40
#define HDA_REG_CORBUBASE		0x44
#define HDA_REG_CORBWP			0x48
#define HDA_REG_CORBRP			0x4a
#define   HDA_CORBRP_RST		(1 << 15)
#define HDA_REG_CORBCTL			0x4c
#define   HDA_CORBCTL_RUN		(1 << 1)
#define HDA_REG_CORBSTS			0x4d
#define HDA_REG_RIRBLBASE		0x50
#define HDA_REG_RIRBUBASE		0x54
#define HDA_REG_RIRBWP			0x58
#define   HDA_RIRBWP_RST		(1 << 15)
#define HDA_REG_RINTCNT			0x5a
#define HDA_REG_RIRBCTL			0x5c
#define   HDA_RBCTL_IRQ_EN		(1 << 0)
#define   HDA_RBCTL_DMA_EN		(1 << 1)
#define HDA_REG_RIRBSTS			0x5d

/* stream registers (SDO0 = 0x100) */
#define HDA_SD_BASE(n)			(0x80 + (n) * 0x20)
#define HDA_REG_SD_CTL			0x00
#define   HDA_SD_CTL_RESET		(1 << 0)
#define   HDA_SD_CTL_DMA_START		(1 << 1)
#define   HDA_SD_CTL_STREAM_TAG(x)	((x) << 20)
#define HDA_REG_SD_STS			0x03
#define   HDA_SD_INT_COMPLETE		0x04
#define   HDA_SD_STS_FIFO_READY		0x20
#define HDA_REG_SD_CBL			0x08
#define HDA_REG_SD_LVI			0x0c
#define HDA_REG_SD_FORMAT		0x12
#define HDA_REG_SD_BDLPL		0x18
#define HDA_REG_SD_BDLPU		0x1c

#define HDA_STREAM_OUT0			4	/* SDO0 */

/* codec verbs */
#define AC_VERB_PARAMETERS		0x0f00
#define AC_VERB_SET_CHANNEL_STREAMID	0x706
#define AC_VERB_SET_STREAM_FORMAT	0x200
#define AC_VERB_SET_PIN_WIDGET_CONTROL	0x707

/* codec nodes (hda-output) */
#define NID_ROOT			0x00
#define NID_DAC				0x02
#define NID_OUT_PIN			0x03

#define AC_PAR_VENDOR_ID		0x00
#define AC_PINCTL_OUT_EN		0x40

/* the HDA stream format word (tone: S16 stereo @ 44100) */
#define AC_FMT_CHAN(x)			((x) - 1)
#define AC_FMT_BITS_16			(1 << 4)
#define AC_FMT_BASE_44K			(1 << 14)
#define HDA_FMT_S16_44K_STEREO		(AC_FMT_CHAN(2) | AC_FMT_BITS_16 | AC_FMT_BASE_44K)

/* the OSS ioctls (4Front soundcard.h) */
#define SNDCTL_DSP_RESET		0x00005000
#define SNDCTL_DSP_SPEED		0xc0045002
#define SNDCTL_DSP_STEREO		0x80045003
#define SNDCTL_DSP_GETBLKSIZE		0x80045004
#define SNDCTL_DSP_SETFMT		0xc0045005
#define SNDCTL_DSP_CHANNELS		0xc0045006

#define AFMT_S16_LE			0x00000010

#define DSP_BUF_SIZE			16380	/* bytes (fits the 16KB DMA buffer) */

struct hda_device {
	int present;
	unsigned long mmio;
	unsigned char irq;
	/* DMA rings (guest phys + kernel VA) */
	unsigned long corb_phys;
	unsigned int *corb;		/* 256 x 4-byte commands */
	unsigned long rirb_phys;
	unsigned int *rirb;		/* 256 x 8-byte responses */
	unsigned long buf_phys;
	unsigned char *buf;		/* the 16KB audio buffer */
	unsigned long bdl_phys;
	unsigned int *bdl;		/* one 16-byte BDL entry */
	unsigned char corb_wp;
	unsigned char rirb_rp;		/* last consumed RIRB write pointer */
	int playing;
	int started;
	unsigned int afmt, speed, channels;
};

static struct hda_device hda;

/* ---------------- mmio accessors ---------------- */

static __u32 hda_r32(unsigned int off)
{
	return *(volatile __u32 *)(hda.mmio + off);
}

static void hda_w32(unsigned int off, __u32 val)
{
	*(volatile __u32 *)(hda.mmio + off) = val;
}

static __u16 hda_r16(unsigned int off)
{
	return *(volatile __u16 *)(hda.mmio + off);
}

static void hda_w16(unsigned int off, __u16 val)
{
	*(volatile __u16 *)(hda.mmio + off) = val;
}

static __u8 hda_r8(unsigned int off)
{
	return *(volatile __u8 *)(hda.mmio + off);
}

static void hda_w8(unsigned int off, __u8 val)
{
	*(volatile __u8 *)(hda.mmio + off) = val;
}

/* ---------------- controller + codec plumbing ---------------- */

static void hda_ctrl_reset(void)
{
	/* assert + release the controller reset */
	hda_w32(HDA_REG_GCTL, HDA_GCTL_RESET);
	__asm__ __volatile__("" ::: "memory");
	hda_w32(HDA_REG_GCTL, 0);
	hda_w32(HDA_REG_GCTL, HDA_GCTL_FCNTRL);
	/* codec present: STATESTS bit 0 (QEMU: hda-output on cad 0) */
}

static void hda_corb_rirb_init(void)
{
	/* CORB: 256 entries x 4 bytes, ring at corb_phys */
	hda_w32(HDA_REG_CORBLBASE, (__u32)hda.corb_phys);
	hda_w32(HDA_REG_CORBUBASE, (__u32)(hda.corb_phys >> 32));
	hda_w16(HDA_REG_CORBRP, HDA_CORBRP_RST);	/* reset read ptr */
	hda_w16(HDA_REG_CORBRP, 0);
	hda.corb_wp = 0;
	hda_w8(HDA_REG_CORBSTS, 0x01);			/* clear CMEI */
	hda_w8(HDA_REG_CORBCTL, HDA_CORBCTL_RUN);

	/* RIRB: 256 entries x 8 bytes, ring at rirb_phys */
	hda_w32(HDA_REG_RIRBLBASE, (__u32)hda.rirb_phys);
	hda_w32(HDA_REG_RIRBUBASE, (__u32)(hda.rirb_phys >> 32));
	hda_w16(HDA_REG_RIRBWP, HDA_RIRBWP_RST);	/* reset write ptr */
	hda_w16(HDA_REG_RIRBWP, 0);
	hda.rirb_rp = 0;
	hda_w16(HDA_REG_RINTCNT, 1);
	hda_w8(HDA_REG_RIRBSTS, 0x07);			/* clear IRQ/OVERRUN */
	/* IRQ_EN is REQUIRED: the controller resets rirb_count (and thus
	 * un-stalls the CORB) only when RIRBSTS.IRQ goes 1->0, and the IRQ
	 * bit is only set when IRQ_EN is on */
	hda_w8(HDA_REG_RIRBCTL, HDA_RBCTL_IRQ_EN | HDA_RBCTL_DMA_EN);
}

/* send one codec verb and busy-poll the RIRB for the response */
static unsigned int hda_codec_verb(unsigned int cad, unsigned int nid,
				   unsigned int verb, unsigned int payload)
{
	unsigned int word = (cad << 28) | (nid << 20) | (verb << 8) | payload;
	unsigned int wp, spin;

	wp = (hda.corb_wp + 1) & 0xff;
	hda.corb[wp] = word;
	__asm__ __volatile__("" ::: "memory");
	hda.corb_wp = wp;
	hda_w16(HDA_REG_CORBWP, wp);

	/* the controller processes the CORB synchronously on the WP write;
	 * the response lands at rirb[wp+1] and RIRBWP advances */
	for(spin = 0; spin < 1000000; spin++) {
		unsigned int rp = hda_r16(HDA_REG_RIRBWP) & 0xff;

		if(rp != hda.rirb_rp) {
			unsigned int resp = hda.rirb[2 * ((hda.rirb_rp + 1) & 0xff)];

			hda.rirb_rp = rp;
			/* clear the response IRQ so rirb_count resets and the
			 * CORB keeps processing subsequent verbs */
			hda_w8(HDA_REG_RIRBSTS, 0x07);
			return resp;
		}
		__asm__ __volatile__("pause" ::: "memory");
	}
	return 0;
}

/* bring the codec up: bind the DAC to the stream tag + set the format */
static void hda_codec_init(unsigned int tag)
{
	unsigned int fmt;

	/* vendor id sanity (QEMU hda-output: 0x1af40011) */
	(void)hda_codec_verb(0, NID_ROOT, AC_VERB_PARAMETERS, AC_PAR_VENDOR_ID);

	/* the format word for the requested rate/channels */
	fmt = AC_FMT_BASE_44K | AC_FMT_BITS_16 | AC_FMT_CHAN(hda.channels);
	hda_codec_verb(0, NID_DAC, AC_VERB_SET_CHANNEL_STREAMID,
		       (tag << 4) | 0);
	hda_codec_verb(0, NID_DAC, AC_VERB_SET_STREAM_FORMAT, fmt);
	hda_codec_verb(0, NID_OUT_PIN, AC_VERB_SET_PIN_WIDGET_CONTROL,
		       AC_PINCTL_OUT_EN);
}

/* arm the output stream with one BDL entry of len bytes + start it */
static void hda_stream_arm(unsigned int len, unsigned int tag)
{
	unsigned int sd = HDA_SD_BASE(HDA_STREAM_OUT0);

	/* the single BDL entry: { addr, len, IOC } */
	hda.bdl[0] = (__u32)hda.buf_phys;
	hda.bdl[1] = (__u32)(hda.buf_phys >> 32);
	hda.bdl[2] = len;
	hda.bdl[3] = 0x01;			/* IOC */
	__asm__ __volatile__("" ::: "memory");

	hda_w32(sd + HDA_REG_SD_CBL, len);
	hda_w16(sd + HDA_REG_SD_LVI, 0);
	hda_w16(sd + HDA_REG_SD_FORMAT,
		AC_FMT_BASE_44K | AC_FMT_BITS_16 | AC_FMT_CHAN(hda.channels));
	hda_w32(sd + HDA_REG_SD_BDLPL, (__u32)hda.bdl_phys);
	hda_w32(sd + HDA_REG_SD_BDLPU, (__u32)(hda.bdl_phys >> 32));

	/* reset then start: the controller parses the BDL on the start */
	hda_w32(sd + HDA_REG_SD_CTL, HDA_SD_CTL_RESET);
	__asm__ __volatile__("" ::: "memory");
	hda_w32(sd + HDA_REG_SD_CTL,
		HDA_SD_CTL_STREAM_TAG(tag) | HDA_SD_CTL_DMA_START);
}

/* wait for the armed chunk to complete: the ISR clears SD_STS and sets
 * playing=0 on the completion interrupt; the SD_STS poll is the fallback
 * for when the INTx line did not reach the PIC */
static int hda_stream_wait(unsigned int tag)
{
	unsigned int sd = HDA_SD_BASE(HDA_STREAM_OUT0);

	for(;;) {
		if(hda_r8(sd + HDA_REG_SD_STS) & HDA_SD_INT_COMPLETE) {
			hda_w8(sd + HDA_REG_SD_STS, 0x1c);	/* clear */
			hda.playing = 0;
			return 0;
		}
		if(!hda.playing) {
			return 0;	/* the ISR completed it */
		}
		{
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&hda.playing, PROC_INTERRUPTIBLE);
			if(!current->timeout) {
				current->timeout = 0;
				continue;	/* timed out: poll again */
			}
			current->timeout = 0;
			if(woken) {
				return -EINTR;
			}
		}
	}
}

static void hda_irq_handler(int num, struct sigcontext *sc)
{
	unsigned int is;

	/* ACK the controller interrupt: the completion (INTSTS bit 4) is
	 * the chunk-done signal - clear SD_STS and wake the blocked writer */
	is = hda_r32(HDA_REG_INTSTS);
	if(is & (1 << HDA_STREAM_OUT0)) {
		hda_w8(HDA_SD_BASE(HDA_STREAM_OUT0) + HDA_REG_SD_STS, 0x1c);
		hda.playing = 0;
	}
	if(is & (1 << 30)) {
		hda_w8(HDA_REG_RIRBSTS, 0x07);
	}
	hda_w32(HDA_REG_INTSTS, 0xc00000ff);
	wakeup(&hda.playing);
}

/* ---------------- the OSS /dev/dsp interface ---------------- */

int hda_open(struct inode *i, struct fd *f)
{
	hda.playing = 0;
	hda.started = 0;
	return 0;
}

int hda_close(struct inode *i, struct fd *f)
{
	hda_w32(HDA_SD_BASE(HDA_STREAM_OUT0) + HDA_REG_SD_CTL,
		HDA_SD_CTL_RESET);
	hda.playing = 0;
	hda.started = 0;
	return 0;
}

int hda_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	unsigned int done = 0, chunk;
	unsigned int tag = 1;
	int ret;

	if(!hda.present) {
		return -ENODEV;
	}
	if(!hda.started) {
		if(!hda.speed) {
			hda.speed = 44100;
		}
		if(!hda.channels) {
			hda.channels = 2;
		}
		hda.afmt = AFMT_S16_LE;
		hda_codec_init(tag);
		hda.started = 1;
	}
	while(count > 0) {
		chunk = (count > DSP_BUF_SIZE) ? DSP_BUF_SIZE : count;
		memcpy_b(hda.buf, buffer + done, chunk);
		hda.playing = 1;
		hda_stream_arm(chunk, tag);
		ret = hda_stream_wait(tag);
		if(ret < 0) {
			hda.playing = 0;
			return (done) ? (int)done : ret;
		}
		hda.playing = 0;
		done += chunk;
		count -= chunk;
	}
	return done;
}

int hda_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	unsigned int *uval = (unsigned int *)arg;

	switch(cmd) {
		case SNDCTL_DSP_SETFMT:
			if(*uval != AFMT_S16_LE) {
				*uval = AFMT_S16_LE;	/* the codec is S16-only */
			}
			hda.afmt = *uval;
			break;
		case SNDCTL_DSP_SPEED:
			hda.speed = *uval;
			break;
		case SNDCTL_DSP_CHANNELS:
			if(*uval != 2) {
				*uval = 2;	/* the tone is stereo */
			}
			hda.channels = *uval;
			break;
		case SNDCTL_DSP_STEREO:
			*uval = 1;
			break;
		case SNDCTL_DSP_GETBLKSIZE:
			*uval = DSP_BUF_SIZE;
			break;
		case SNDCTL_DSP_RESET:
			hda_w32(HDA_SD_BASE(HDA_STREAM_OUT0) + HDA_REG_SD_CTL,
				HDA_SD_CTL_RESET);
			hda.playing = 0;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static struct fs_operations hda_driver_fsop = {
	0,
	0,

	hda_open,
	hda_close,
	NULL,			/* read */
	hda_write,
	hda_ioctl,
	NULL,			/* llseek */
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	NULL,			/* lookup */
	NULL,			/* rmdir */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* mknod */
	NULL,			/* truncate */
	NULL,			/* create */
	NULL,			/* rename */

	NULL,			/* read_block */
	NULL,			/* write_block */

	NULL,			/* read_inode */
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	NULL,			/* statfs */
	NULL,			/* read_superblock */
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

static struct device hda_device = {
	"dsp",
	DSP_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&hda_driver_fsop,
	NULL,
	NULL,
	NULL
};

int hda_init(void)
{
	struct pci_device *pd;
	extern int map_page64(unsigned long, unsigned long, unsigned long);
	extern unsigned long alloc_pages64(int);
	unsigned long mmio;
	int i;

	hda.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == HDA_VENDOR_ID &&
		   (pd->device_id == HDA_DEVICE_ICH6 ||
		    pd->device_id == HDA_DEVICE_ICH9)) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}

	mmio = pd->bar[0] & 0xFFFFFFF0;
	if(!mmio) {
		return -ENODEV;
	}
	for(i = 0; i < HDA_MMIO_SIZE / 4096; i++) {
		if(map_page64(HDA_MMIO_VA + i * 4096, mmio + i * 4096, 0x003)) {
			return -ENOMEM;
		}
	}
	hda.mmio = HDA_MMIO_VA;
	hda.irq = pd->irq;

	/* command: MEM | MASTER */
	pci_write_short(pd, 0x04, 0x0006);

	/* DMA buffers: CORB (1 page), RIRB (1 page), audio (4 pages), BDL (1 page) */
	hda.corb_phys = alloc_pages64(1);
	hda.rirb_phys = alloc_pages64(1);
	hda.buf_phys = alloc_pages64(4);
	hda.bdl_phys = alloc_pages64(1);
	if(!hda.corb_phys || !hda.rirb_phys || !hda.buf_phys || !hda.bdl_phys) {
		return -ENOMEM;
	}
	hda.corb = (unsigned int *)P2V(hda.corb_phys);
	hda.rirb = (unsigned int *)P2V(hda.rirb_phys);
	hda.buf = (unsigned char *)P2V(hda.buf_phys);
	hda.bdl = (unsigned int *)P2V(hda.bdl_phys);

	hda_ctrl_reset();
	hda_corb_rirb_init();

	if(hda.irq) {
		static struct interrupt irq_config_hda = { 0, "intel-hda", &hda_irq_handler, NULL };
		register_irq(hda.irq, &irq_config_hda);
		enable_irq(hda.irq);
		/* global + controller + SDO0 completion interrupts */
		hda_w32(HDA_REG_INTCTL,
			HDA_INT_GLOBAL_EN | HDA_INT_CTRL_EN |
			(1 << HDA_STREAM_OUT0));
	}

	SET_MINOR(hda_device.minors, DSP_MINOR);
	if(register_device(CHR_DEV, &hda_device)) {
		printk("WARNING: %s(): unable to register 'dsp' device.\n", __FUNCTION__);
		return -EINVAL;
	}
	devfs_make_node("dsp", MKDEV(DSP_MAJOR, DSP_MINOR),
			S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH);

	hda.afmt = AFMT_S16_LE;
	hda.speed = 44100;
	hda.channels = 2;
	hda.present = 1;
	printk("intel-hda: %x:%x at 0x%lx, IRQ %d, OSS /dev/dsp\n",
		pd->vendor_id, pd->device_id, mmio, hda.irq);
	return 0;
}
