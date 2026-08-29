/*
 * fnx/drivers/char/sb16.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Sound Blaster 16 (ISA) with the OSS /dev/dsp API, against QEMU's
 * 8-bit DMA playback path.
 *
 * The SB16 is a fixed-I/O ISA card: DSP reset at 0x226, command/data at
 * 0x22C/0x22A, status at 0x22E (8-bit IRQ ACK), 16-bit ACK at 0x22F,
 * mixer at 0x224/0x225 (defaults: IRQ 5, 8-bit DMA 1, 16-bit DMA 5).
 *
 * QEMU QUIRK: the 16-bit DMA DAC command 0xB8 has bit 3 set, and QEMU's
 * sb16 routes every 0xAF-0xCF command with bit 3 to its (unimplemented)
 * ADC branch - so 16-bit playback is BROKEN and only the 8-bit DMA DAC
 * (0xD1, unsigned 8-bit samples) works. The driver therefore converts
 * the S16 tone to U8 (high byte ^ 0x80) and plays it on the 8-bit DMA
 * channel 1: page 0x83, byte address/count via 0x02/0x03 and the 0x0C
 * flip-flop clear, mode 0x0B, command 0x08, mask 0x0A. The rate is set
 * with DSP command 0x41 (QEMU treats 0xB1 as a DMA command); stereo for
 * the 8-bit path comes from mixer register 0x0e bit 1. On completion the
 * card raises IRQ 5 (mixer 0x82 bit 0); reading 0x22E acknowledges it.
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

#define DSP_MAJOR			14
#define DSP_MINOR			3

#define SB16_IOBASE			0x220
#define SB16_DSP_RESET			(SB16_IOBASE + 0x06)
#define SB16_DSP_READ			(SB16_IOBASE + 0x0a)
#define SB16_DSP_WRITE			(SB16_IOBASE + 0x0c)
#define SB16_DSP_STATUS			(SB16_IOBASE + 0x0e)
#define SB16_MIXER_ADDR			(SB16_IOBASE + 0x04)
#define SB16_MIXER_DATA			(SB16_IOBASE + 0x05)

#define SB16_IRQ			5
#define SB16_DMA			1	/* 8-bit DMA channel */

/* the 8237 DMA controller (8-bit channels at 0x00-0x0F, byte-addressed) */
#define DMA_PAGE_CH1			0x83
#define DMA_ADDR_CH1			0x02
#define DMA_COUNT_CH1			0x03
#define DMA_MODE			0x0B
#define DMA_COMMAND			0x08
#define DMA_MASK			0x0A
#define DMA_FF				0x0C

#define DMA_MODE_SINGLE			0x01
#define DMA_MODE_READ			0x04	/* memory -> I/O (playback) */
#define DMA_MODE_INCREMENT		0x00

/* mixer: 8-bit stereo flag lives in register 0x0e bit 1 */
#define SB16_MIXER_STEREO		0x0e
#define SB16_STEREO_FLAG		0x02

/* the OSS ioctls (4Front soundcard.h) */
#define SNDCTL_DSP_RESET		0x00005000
#define SNDCTL_DSP_SPEED		0xc0045002
#define SNDCTL_DSP_STEREO		0x80045003
#define SNDCTL_DSP_GETBLKSIZE		0x80045004
#define SNDCTL_DSP_SETFMT		0xc0045005
#define SNDCTL_DSP_CHANNELS		0xc0045006

#define AFMT_S16_LE			0x00000010

#define DSP_BUF_SIZE			16384	/* bytes (fits the DMA buffer) */

struct sb16_device {
	int present;
	unsigned char irq;
	unsigned long buf_phys;
	unsigned char *buf;		/* the DMA audio buffer */
	unsigned int buf_len;		/* armed length (bytes) */
	int playing;
	unsigned int afmt, speed, channels;
};

static struct sb16_device sb16;

/* ---------------- the 8237 DMA + DSP plumbing ---------------- */

static void dma8_program(unsigned long phys, unsigned int bytes)
{
	outport_b(DMA_PAGE_CH1, (unsigned char)(phys >> 16));

	outport_b(DMA_FF, 0);		/* clear the low/high flip-flop */
	outport_b(DMA_ADDR_CH1, phys & 0xFF);
	outport_b(DMA_ADDR_CH1, (phys >> 8) & 0xFF);

	outport_b(DMA_FF, 0);
	outport_b(DMA_COUNT_CH1, (bytes - 1) & 0xFF);
	outport_b(DMA_COUNT_CH1, ((bytes - 1) >> 8) & 0xFF);

	/* mode: channel 1, single, increment, read */
	outport_b(DMA_MODE, (1 << 6) | DMA_MODE_SINGLE | DMA_MODE_READ);
	outport_b(DMA_COMMAND, 0x00);	/* controller enabled, normal timing */
	outport_b(DMA_MASK, 1);		/* unmask channel 1 */
}

static void dsp_write(unsigned char val)
{
	outport_b(SB16_DSP_WRITE, val);
}

static unsigned char dsp_read(void)
{
	unsigned int spin;

	for(spin = 0; spin < 100000; spin++) {
		if(inport_b(SB16_DSP_STATUS) & 0x80) {
			return inport_b(SB16_DSP_READ);
		}
	}
	return 0;
}

static int dsp_reset(void)
{
	outport_b(SB16_DSP_RESET, 1);
	outport_b(SB16_DSP_RESET, 0);
	return dsp_read() == 0xAA ? 0 : -EIO;
}

static void dsp_set_rate(unsigned int rate)
{
	dsp_write(0x41);		/* set output sample rate (8-bit cmd) */
	dsp_write(rate & 0xFF);
	dsp_write((rate >> 8) & 0xFF);
}

static void dsp_play8(unsigned int bytes)
{
	unsigned int len = bytes - 1;

	dsp_write(0xD0);		/* halt 8-bit DMA (stop any in-flight block) */
	dsp_write(0x48);		/* set the DMA block size (bytes - 1) */
	dsp_write(len & 0xFF);
	dsp_write((len >> 8) & 0xFF);
	dsp_write(0x90);		/* auto-init 8-bit DMA DAC */
}

/* convert a S16_LE stereo buffer to the SB16's unsigned 8-bit MONO */
static void s16_to_u8(const unsigned char *src, unsigned char *dst,
		      unsigned int sbytes)
{
	unsigned int k;

	/* take the L channel (every other sample): high byte, sign-flipped */
	for(k = 0; k < sbytes; k += 4) {
		dst[k >> 2] = src[k + 1] ^ 0x80;
	}
}

/* ---------------- the OSS /dev/dsp interface ---------------- */

int sb16_open(struct inode *i, struct fd *f)
{
	sb16.playing = 0;
	return 0;
}

int sb16_close(struct inode *i, struct fd *f)
{
	dsp_write(0xD0);		/* halt the 8-bit DMA voice */
	sb16.playing = 0;
	return 0;
}

int sb16_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	unsigned int done = 0, chunk;

	if(!sb16.present) {
		return -ENODEV;
	}
	if(!sb16.speed) {
		sb16.speed = 44100;
	}
	if(!sb16.channels) {
		sb16.channels = 2;
	}
	sb16.afmt = AFMT_S16_LE;
	dsp_set_rate(sb16.speed);

	while(count > 0) {
		chunk = (count > DSP_BUF_SIZE * 2) ? DSP_BUF_SIZE * 2 : count;
		s16_to_u8((const unsigned char *)buffer + done, sb16.buf, chunk);
		dma8_program(sb16.buf_phys, chunk >> 2);
		sb16.playing = 1;
		dsp_play8(chunk >> 2);

		while(sb16.playing) {
			extern unsigned int tv2ticks(const struct timeval *);
			struct timeval tv;
			int woken;

			tv.tv_sec = 0;
			tv.tv_usec = 50000;	/* 50 ms */
			current->timeout = tv2ticks(&tv);
			woken = sleep(&sb16.playing, PROC_INTERRUPTIBLE);
			if(!current->timeout) {
				current->timeout = 0;
				continue;	/* timed out: poll again */
			}
			current->timeout = 0;
			if(woken) {
				return (done) ? (int)done : -EINTR;
			}
		}
		done += chunk;
		count -= chunk;
	}
	return done;
}

int sb16_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	unsigned int *uval = (unsigned int *)arg;

	switch(cmd) {
		case SNDCTL_DSP_SETFMT:
			if(*uval != AFMT_S16_LE) {
				*uval = AFMT_S16_LE;	/* the SB16 DSP is S16-capable */
			}
			sb16.afmt = *uval;
			break;
		case SNDCTL_DSP_SPEED:
			sb16.speed = *uval;
			break;
		case SNDCTL_DSP_CHANNELS:
			if(*uval != 2) {
				*uval = 2;	/* the tone is stereo */
			}
			sb16.channels = *uval;
			break;
		case SNDCTL_DSP_STEREO:
			*uval = 1;
			break;
		case SNDCTL_DSP_GETBLKSIZE:
			*uval = DSP_BUF_SIZE;
			break;
		case SNDCTL_DSP_RESET:
			dsp_write(0xD0);	/* halt the 8-bit DMA voice */
			sb16.playing = 0;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static void sb16_irq_handler(int num, struct sigcontext *sc)
{
	/* ACK the 8-bit DMA completion interrupt (read 0x22E drops the
	 * line) and wake the blocked writer */
	(void)inport_b(SB16_DSP_STATUS);
	sb16.playing = 0;
	wakeup(&sb16.playing);
}

static struct fs_operations sb16_driver_fsop = {
	0,
	0,

	sb16_open,
	sb16_close,
	NULL,			/* read */
	sb16_write,
	sb16_ioctl,
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

static struct device sb16_device = {
	"dsp",
	DSP_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&sb16_driver_fsop,
	NULL,
	NULL,
	NULL
};

int sb16_init(void)
{
	extern unsigned long alloc_pages64(int);

	sb16.present = 0;

	if(dsp_reset()) {
		return -ENODEV;		/* no SB16 (or DSP busy) */
	}

	/* the DMA audio buffer: 4 pages (16KB). The ISA DMA needs the
	 * buffer below 16 MB (the 8237 page register is 8 bits) */
	sb16.buf_phys = alloc_pages64(4);
	if(!sb16.buf_phys || sb16.buf_phys >= 0x1000000) {
		return -ENOMEM;
	}
	sb16.buf = (unsigned char *)P2V(sb16.buf_phys);
	sb16.irq = SB16_IRQ;

	{
		static struct interrupt irq_config_sb16 = { 0, "sb16", &sb16_irq_handler, NULL };
		register_irq(sb16.irq, &irq_config_sb16);
		enable_irq(sb16.irq);
	}

	SET_MINOR(sb16_device.minors, DSP_MINOR);
	if(register_device(CHR_DEV, &sb16_device)) {
		printk("WARNING: %s(): unable to register 'dsp' device.\n", __FUNCTION__);
		return -EINVAL;
	}
	devfs_make_node("dsp", MKDEV(DSP_MAJOR, DSP_MINOR),
			S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH);

	sb16.afmt = AFMT_S16_LE;
	sb16.speed = 44100;
	sb16.channels = 2;

	sb16.present = 1;
	printk("sb16: Sound Blaster 16 at 0x%x, IRQ %d, DMA %d (8-bit U8, QEMU 16-bit quirk), OSS /dev/dsp\n",
		SB16_IOBASE, sb16.irq, SB16_DMA);
	return 0;
}
