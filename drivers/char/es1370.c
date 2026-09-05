/*
 * fnx/drivers/char/es1370.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * ENSONIQ AudioPCI ES1370 (1274:1371) with the OSS /dev/dsp API.
 *
 * The ES1370 exposes one 256-byte I/O BAR. Playback runs on the DAC1
 * ("wave-table") channel: a fixed-size DMA frame (frame_addr/frame_cnt
 * under the 0x0c mempage) is played at one of four sample rates
 * (5512/11025/22050/44100, CTRL_WTSRSEL). The driver arms a 16KB frame,
 * starts the channel and blocks the writer until the DAC1 interrupt
 * (STAT_DAC1) fires; QEMU clears the interrupt when SCTRL_P1INTEN drops,
 * so the ISR also pauses the channel (SCTRL_P1PAUSE) to stop the loop.
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

#define ES1370_VENDOR_ID		0x1274
#define ES1370_DEVICE_ID		0x5000	/* the ES1370 (0x1371 is the ES1371) */

#define DSP_MAJOR			14	/* the classic OSS audio major */
#define DSP_MINOR			3	/* /dev/dsp */

#define ES1370_REG_CONTROL		0x00
#define ES1370_REG_STATUS		0x04
#define ES1370_REG_MEMPAGE		0x0c
#define ES1370_REG_SERIAL_CONTROL	0x20
#define ES1370_REG_DAC1_SCOUNT		0x24
#define ES1370_REG_DAC1_FRAMEADR		0x30
#define ES1370_REG_DAC1_FRAMECNT		0x34
#define ES1370_MEMPAGE_DAC1		0x0c

#define CTRL_CDC_EN			0x00000002
#define CTRL_DAC1_EN			0x00000040
#define CTRL_WTSRSEL			0x00003000
#define CTRL_SH_WTSRSEL			12

#define STAT_INTR			0x80000000
#define STAT_DAC1			0x00000004

#define SCTRL_P1PAUSE			0x00000040
#define SCTRL_P1INTEN			0x00000100
#define SCTRL_P1FMT			0x00000003

/* the OSS ioctls (4Front soundcard.h) */
#define SNDCTL_DSP_RESET		0x00005000
#define SNDCTL_DSP_SPEED		0xc0045002
#define SNDCTL_DSP_STEREO		0x80045003
#define SNDCTL_DSP_GETBLKSIZE		0x80045004
#define SNDCTL_DSP_SETFMT		0xc0045005
#define SNDCTL_DSP_CHANNELS		0xc0045006

#define AFMT_U8				0x00000008
#define AFMT_S16_LE			0x00000010

#define DSP_BUF_SIZE			16384	/* bytes; 4 contiguous pages */

/* DAC1's four fixed sample rates (the CTRL_WTSRSEL index) */
static const unsigned int es1370_rates[4] = { 5512, 11025, 22050, 44100 };

struct es1370_state {
	int present;
	unsigned short iobase;
	unsigned char irq;
	unsigned int speed_index;	/* CTRL_WTSRSEL (0..3) */
	unsigned int afmt;		/* AFMT_U8 / AFMT_S16_LE */
	unsigned int channels;		/* 1 or 2 */
	unsigned char *buf;		/* DMA buffer (kernel VA) */
	unsigned int buf_phys;
	unsigned int buf_len;		/* currently armed length (bytes) */
	int playing;			/* a DMA frame is in flight */
};

static struct es1370_state es1370;

static void es1370_reg_w(unsigned int off, unsigned int val)
{
	outport_l(es1370.iobase + off, val);
}

static unsigned int es1370_reg_r(unsigned int off)
{
	return inport_l(es1370.iobase + off);
}

/* the SCTRL_P1FMT value for the current format: 0=8M, 1=8S, 2=16M, 3=16S */
static unsigned int es1370_fmt_value(void)
{
	return ((es1370.afmt == AFMT_S16_LE) ? 2 : 0) | ((es1370.channels == 2) ? 1 : 0);
}

static void es1370_irq_handler(int num, struct sigcontext *sc)
{
	unsigned int sctl;

	if(!(es1370_reg_r(ES1370_REG_STATUS) & STAT_DAC1)) {
		return;
	}
	es1370.playing = 0;
	wakeup(&es1370.playing);
	/* QEMU lowers the DAC1 interrupt when SCTRL_P1INTEN drops; pause the
	 * channel too so the frame does not loop before the next write. */
	sctl = es1370_reg_r(ES1370_REG_SERIAL_CONTROL);
	es1370_reg_w(ES1370_REG_SERIAL_CONTROL, (sctl & ~SCTRL_P1INTEN) | SCTRL_P1PAUSE);
}

/* arm and start one DMA frame of len bytes (the frame regs live at 0x30/0x34
 * under the 0x0c mempage; the frame count is in 4-byte units) */
static void es1370_arm(unsigned int len)
{
	unsigned int dwords = len >> 2;
	unsigned int samples;

	/* the per-batch sample count (the SCOUNT reload value, 16-bit) */
	samples = len / ((es1370.afmt == AFMT_S16_LE ? 2 : 1) * es1370.channels);

	es1370_reg_w(ES1370_REG_MEMPAGE, ES1370_MEMPAGE_DAC1);
	es1370_reg_w(ES1370_REG_DAC1_FRAMEADR, es1370.buf_phys);
	es1370_reg_w(ES1370_REG_DAC1_FRAMECNT, (dwords << 16) | dwords);
	es1370_reg_w(ES1370_REG_DAC1_SCOUNT, ((samples - 1) << 16) | (samples - 1));
	/* start the channel: clear the pause + raise P1INTEN */
	es1370_reg_w(ES1370_REG_SERIAL_CONTROL, es1370_fmt_value() | SCTRL_P1INTEN);
	/* the voice is only (re)activated when the enable bit changes in the
	 * CONTROL register (QEMU's es1370_update_voices compares the diff) -
	 * drop DAC1_EN first, then raise it so a format change earlier in the
	 * ioctl path cannot leave the voice inactive. */
	es1370_reg_w(ES1370_REG_CONTROL, CTRL_CDC_EN |
		(es1370.speed_index << CTRL_SH_WTSRSEL));
	es1370_reg_w(ES1370_REG_CONTROL, CTRL_CDC_EN | CTRL_DAC1_EN |
		(es1370.speed_index << CTRL_SH_WTSRSEL));
}

int es1370_open(struct inode *i, struct fd *f)
{
	es1370.playing = 0;
	return 0;
}

int es1370_close(struct inode *i, struct fd *f)
{
	/* stop the channel: pause + drop the interrupt enable */
	es1370_reg_w(ES1370_REG_SERIAL_CONTROL,
		es1370_reg_r(ES1370_REG_SERIAL_CONTROL) | SCTRL_P1PAUSE);
	es1370.playing = 0;
	return 0;
}

int es1370_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	unsigned int done = 0, chunk;

	while(count > 0) {
		chunk = (count > DSP_BUF_SIZE) ? DSP_BUF_SIZE : count;
		/* wait for the previous frame to finish playing */
		while(es1370.playing) {
			sleep(&es1370.playing, PROC_UNINTERRUPTIBLE);
		}
		memcpy_b(es1370.buf, buffer + done, chunk);
		es1370.buf_len = chunk;
		es1370.playing = 1;
		es1370_arm(chunk);
		done += chunk;
		count -= chunk;
	}
	return done;
}

int es1370_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	unsigned int *uval = (unsigned int *)arg;
	unsigned int idx;

	switch(cmd) {
		case SNDCTL_DSP_SETFMT:
			if(*uval != AFMT_U8 && *uval != AFMT_S16_LE) {
				*uval = AFMT_U8;
			}
			es1370.afmt = *uval;
			es1370_reg_w(ES1370_REG_SERIAL_CONTROL,
				es1370_fmt_value() | SCTRL_P1INTEN);
			break;
		case SNDCTL_DSP_SPEED:
			idx = (*uval <= 5512) ? 0 : (*uval <= 11025) ? 1 :
				(*uval <= 22050) ? 2 : 3;
			es1370.speed_index = idx;
			es1370_reg_w(ES1370_REG_CONTROL, CTRL_CDC_EN | CTRL_DAC1_EN |
				(idx << CTRL_SH_WTSRSEL));
			*uval = es1370_rates[idx];
			break;
		case SNDCTL_DSP_CHANNELS:
			if(*uval == 1 || *uval == 2) {
				es1370.channels = *uval;
			}
			es1370_reg_w(ES1370_REG_SERIAL_CONTROL,
				es1370_fmt_value() | SCTRL_P1INTEN);
			*uval = es1370.channels;
			break;
		case SNDCTL_DSP_STEREO:
			es1370.channels = *uval ? 2 : 1;
			es1370_reg_w(ES1370_REG_SERIAL_CONTROL,
				es1370_fmt_value() | SCTRL_P1INTEN);
			*uval = (es1370.channels == 2);
			break;
		case SNDCTL_DSP_GETBLKSIZE:
			*uval = DSP_BUF_SIZE;
			break;
		case SNDCTL_DSP_RESET:
			es1370.playing = 0;
			wakeup(&es1370.playing);
			es1370_reg_w(ES1370_REG_SERIAL_CONTROL,
				(es1370_reg_r(ES1370_REG_SERIAL_CONTROL) & ~SCTRL_P1INTEN) |
				SCTRL_P1PAUSE);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static struct fs_operations es1370_driver_fsop = {
	0,
	0,

	es1370_open,
	es1370_close,
	NULL,			/* read */
	es1370_write,
	es1370_ioctl,
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

static struct device es1370_device = {
	"dsp",
	DSP_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&es1370_driver_fsop,
	NULL,
	NULL,
	NULL
};

int es1370_init(void)
{
	struct pci_device *pd;
	unsigned short iobase;
	int n;
	extern unsigned long alloc_pages64(int);

	es1370.present = 0;

	/* find the ENSONIQ AudioPCI ES1370 */
	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == ES1370_VENDOR_ID && pd->device_id == ES1370_DEVICE_ID) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}

	iobase = (unsigned short)(pd->bar[0] & 0xFFFC);
	if(!iobase) {
		return -ENODEV;
	}
	es1370.iobase = iobase;
	es1370.irq = pd->irq;

	/* command: I/O space */
	pci_write_short(pd, 0x04, 0x0001 | 0x0004);	/* command: I/O space + bus master */

	/* the DMA buffer: 4 contiguous pages (16KB), phys for the frame */
	es1370.buf_phys = (unsigned int)alloc_pages64(4);
	if(!es1370.buf_phys) {
		return -ENOMEM;
	}
	es1370.buf = (unsigned char *)P2V(es1370.buf_phys);

	es1370.afmt = AFMT_S16_LE;
	es1370.channels = 2;
	es1370.speed_index = 3;		/* 44100 Hz */

	SET_MINOR(es1370_device.minors, DSP_MINOR);
	if(register_device(CHR_DEV, &es1370_device)) {
		printk("WARNING: %s(): unable to register 'dsp' device.\n", __FUNCTION__);
		return -EINVAL;
	}
	devfs_make_node("Audio/dsp", MKDEV(DSP_MAJOR, DSP_MINOR), S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	if(es1370.irq) {
		static struct interrupt irq_config_es1370 = { 0, "es1370", &es1370_irq_handler, NULL };
		register_irq(es1370.irq, &irq_config_es1370);
		enable_irq(es1370.irq);
	}

	/* initial control: codec + DAC1 enabled at 44100 */
	es1370_reg_w(ES1370_REG_CONTROL, CTRL_CDC_EN | CTRL_DAC1_EN |
		(es1370.speed_index << CTRL_SH_WTSRSEL));
	es1370_reg_w(ES1370_REG_SERIAL_CONTROL, es1370_fmt_value() | SCTRL_P1INTEN);

	es1370.present = 1;
	for(n = 0; n < 100000; n++) {
		;	/* small settle so QEMU's audio backend attaches */
	}
	printk("dsp       0x%04x-0x%04x    %d\ttype=ES1370, OSS /dev/dsp (44100 Hz S16_LE)\n",
		iobase, iobase + 0xff, es1370.irq);
	return 0;
}
