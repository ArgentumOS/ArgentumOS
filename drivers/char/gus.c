/*
 * fnx/drivers/char/gus.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Gravis Ultrasound (ISA) with the OSS /dev/dsp API, against QEMU's
 * gusemu emulation (default iobase 0x240).
 *
 * Register map (physical ports; the gusemu masks port & 0xff0f):
 *   0x240 (0x200) mixer control, 0x342 (0x302) VoiceSel,
 *   0x343 (0x303) FunkSel (function select), 0x344/0x345 (0x304/0x305)
 *   the 16-bit data register, 0x347 (0x307) DRAM byte access.
 *
 * Playback (single voice 0): the reset register (function 0x4c) must be
 * written with bit 0 set or the voice registers and the mixer are
 * ignored; load the PCM chunk into the GUS DRAM via the DRAM position
 * functions (0x43 low 16 bits, 0x44 high bits) + the 0x347 byte port;
 * then set the voice: wVSRControl (0x80) = 0x400 (16-bit) without the
 * 0x100 "stopped" bit, wVSRFreq (0x81) = 512 (1:1 at 44100 with
 * NumVoices = 6), loop start/end (0x82-0x85, 23.9 fixed point),
 * CurrPos (0x8A/0x8B), CurrVol (0x89 = 0xFF00), Panning (0x8C = 0x0700).
 * The gusemu advances the position while 0x100 is clear and sets it at
 * the loop-end boundary without a loop, so the driver polls wVSRControl
 * bit 0x100 for the chunk-done signal, then re-arms.
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

#define GUS_IOBASE			0x240
#define GUS_VOICE_SEL			(GUS_IOBASE + 0x102)	/* 0x342 */
#define GUS_FUNK_SEL			(GUS_IOBASE + 0x103)	/* 0x343 */
#define GUS_DATA_LO			(GUS_IOBASE + 0x104)	/* 0x344 */
#define GUS_DATA_HI			(GUS_IOBASE + 0x105)	/* 0x345 */
#define GUS_DRAM_DATA			(GUS_IOBASE + 0x107)	/* 0x347 */
#define GUS_MIXER_CTRL			(GUS_IOBASE + 0x000)	/* 0x240 */
#define GUS_IRQ				7

/* functions (FunkSel values). The gusemu WRITE path only matches the
 * LOW voice-function values 0x00-0x0d (its cases are 0x00..0x0d) while
 * the READ path only matches 0x80-0x8d - so writes and reads use
 * different function numbers for the same voice register. */
#define GUS_F_DRAMADDR_LO		0x43
#define GUS_F_DRAMADDR_HI		0x44
#define GUS_F_RESET			0x4c
#define GUS_F_NUMVOICES			0x0e
#define GUS_VW_VOICECTRL		0x00	/* wVSRControl (write funk) */
#define GUS_VW_FREQ			0x01	/* wVSRFreq */
#define GUS_VW_LOOPSTARTHI		0x02
#define GUS_VW_LOOPSTARTLO		0x03
#define GUS_VW_LOOPENDHI		0x04
#define GUS_VW_LOOPENDLO		0x05
#define GUS_VW_CURRVOL			0x09
#define GUS_VW_CURRPOSHI		0x0a
#define GUS_VW_CURRPOSLO		0x0b
#define GUS_VW_PANNING			0x0c
#define GUS_VR_VOICECTRL		0x80	/* wVSRControl (read funk) */
#define GUS_VR_FREQ			0x81
#define GUS_VR_CURRPOSLO		0x8b
#define GUS_VR_NUMVOICES		0x8e

/* wVSRControl bits */
#define GUS_VCTRL_STOP			0x0200	/* stop request */
#define GUS_VCTRL_STOPPED		0x0100	/* the position is parked */
#define GUS_VCTRL_16BIT			0x0400
#define GUS_VCTRL_BIDIR			0x2000	/* bidir loop: wavetable IRQ at the end */
#define GUS_FUNK_IRQACK			0x8f	/* funk-sel write = IRQ FIFO ACK */

/* the OSS ioctls (4Front soundcard.h) */
#define SNDCTL_DSP_RESET		0x00005000
#define SNDCTL_DSP_SPEED		0xc0045002
#define SNDCTL_DSP_STEREO		0x80045003
#define SNDCTL_DSP_GETBLKSIZE		0x80045004
#define SNDCTL_DSP_SETFMT		0xc0045005
#define SNDCTL_DSP_CHANNELS		0xc0045006

#define AFMT_S16_LE			0x00000010

#define DSP_BUF_SIZE			16384	/* bytes (fits the GUS DRAM) */

#define GUS_MAX_BUF			131072	/* 128 KB: the whole tone fits */

struct gus_device {
	int present;
	unsigned char irq;
	unsigned int afmt, speed, channels;
	int playing;
	unsigned char *buf;		/* the S16 playback buffer */
	unsigned int buf_len;
};

static struct gus_device gus;

int gus_play_buffer(void);

/* ---------------- the GUS register plumbing ---------------- */

static void gus_voice_sel(unsigned char v)
{
	outport_b(GUS_VOICE_SEL, v);
}

static void gus_funk(unsigned char f)
{
	outport_b(GUS_FUNK_SEL, f);
}

static void gus_data_w(unsigned short val)
{
	outport_b(GUS_DATA_LO, val & 0xFF);
	outport_b(GUS_DATA_HI, (val >> 8) & 0xFF);
}

/* 8-bit registers (0x4c reset, 0x0e NumVoices, 0x44 DRAM high, ...) are
 * REPLACED with the raw data on every write - writing the high byte of a
 * 16-bit write would clobber them with 0 - so they need a single-byte
 * write through the low data port only. */
static void gus_data_w8(unsigned char val)
{
	outport_b(GUS_DATA_LO, val);
}

static unsigned short gus_data_r(void)
{
	return inport_b(GUS_DATA_LO) | (inport_b(GUS_DATA_HI) << 8);
}

static void gus_dram_seek(unsigned long pos)
{
	gus_funk(GUS_F_DRAMADDR_LO);
	gus_data_w(pos & 0xFFFF);
	gus_funk(GUS_F_DRAMADDR_HI);
	gus_data_w((pos >> 16) & 0xFF);
}

static void gus_dram_write(unsigned long pos, unsigned char val)
{
	gus_dram_seek(pos);
	outport_b(GUS_DRAM_DATA, val);
}

/* write one voice register (the low byte first, high byte second) */
static void gus_voice_w(unsigned char f, unsigned short val)
{
	gus_funk(f);
	gus_data_w(val);
}

static unsigned short gus_voice_r(unsigned char f)
{
	gus_funk(f);
	return gus_data_r();
}

/* the voice registers: write funk 0x00-0x0d, read funk 0x80-0x8d */
#define GUS_VOICE_W(reg, val)		gus_voice_w(GUS_VW_##reg, (val))
#define GUS_VOICE_R(reg)		gus_voice_r(GUS_VR_##reg)

/* ---------------- the OSS /dev/dsp interface ---------------- */

int gus_open(struct inode *i, struct fd *f)
{
	gus.playing = 0;
	return 0;
}

int gus_close(struct inode *i, struct fd *f)
{
	gus_play_buffer();		/* flush the buffered tone */
	GUS_VOICE_W(VOICECTRL, GUS_VCTRL_16BIT | GUS_VCTRL_STOP);
	gus.playing = 0;
	return 0;
}

int gus_play_buffer(void)
{
	unsigned long loopend;
	unsigned int k, samples;

	if(!gus.present) {
		return -EIO;
	}
	samples = gus.buf_len >> 1;		/* the S16 stereo buffer */
	if(!samples) {
		return 0;
	}

	/* stop the voice, then load the whole buffer into the GUS DRAM */
	GUS_VOICE_W(VOICECTRL, GUS_VCTRL_16BIT | GUS_VCTRL_STOP);
	for(k = 0; k < gus.buf_len; k++) {
		gus_dram_write(k, gus.buf[k]);
	}

	/* voice 0: start at sample 0, end at (samples-1), 1:1 pitch */
	loopend = ((unsigned long)samples - 1) << 9;	/* 23.9 fixed point */
	GUS_VOICE_W(FREQ, 1024);
	GUS_VOICE_W(LOOPSTARTHI, 0);
	GUS_VOICE_W(LOOPSTARTLO, 0);
	GUS_VOICE_W(LOOPENDHI, (unsigned short)(loopend >> 16));
	GUS_VOICE_W(LOOPENDLO, (unsigned short)(loopend & 0xFFFF));
	GUS_VOICE_W(CURRPOSHI, 0);
	GUS_VOICE_W(CURRPOSLO, 0);
	GUS_VOICE_W(CURRVOL, 0xFF00);
	GUS_VOICE_W(PANNING, 0x0700);
	GUS_VOICE_W(VOICECTRL, GUS_VCTRL_16BIT | GUS_VCTRL_BIDIR);

	/* sleep until the wavetable IRQ (no timeout: the ISR wakes us) */
	gus.playing = 1;
	{
		int signum;

		signum = sleep(&gus.playing, PROC_UNINTERRUPTIBLE);
		if(signum) {
			return -EINTR;
		}
	}
	GUS_VOICE_W(VOICECTRL, GUS_VCTRL_16BIT | GUS_VCTRL_STOP);
	gus.playing = 0;
	return 0;
}

static void gus_irq_handler(int num, struct sigcontext *sc)
{
	/* ACK: the funk-sel 0x8f write clears the wavetable IRQ slot + line */
	outport_b(GUS_FUNK_SEL, GUS_FUNK_IRQACK);
	gus.playing = 0;
	wakeup(&gus.playing);
}

int gus_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	if(!gus.present) {
		return -ENODEV;
	}
	if(count > GUS_MAX_BUF - gus.buf_len) {
		count = GUS_MAX_BUF - gus.buf_len;
	}
	memcpy_b(gus.buf + gus.buf_len, buffer, count);
	gus.buf_len += count;
	return count;
}

int gus_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	unsigned int *uval = (unsigned int *)arg;

	switch(cmd) {
		case SNDCTL_DSP_SETFMT:
			if(*uval != AFMT_S16_LE) {
				*uval = AFMT_S16_LE;	/* the GUS is 16-bit */
			}
			gus.afmt = *uval;
			break;
		case SNDCTL_DSP_SPEED:
			gus.speed = *uval;
			break;
		case SNDCTL_DSP_CHANNELS:
			if(*uval != 2) {
				*uval = 2;	/* the tone is stereo */
			}
			gus.channels = *uval;
			break;
		case SNDCTL_DSP_STEREO:
			*uval = 1;
			break;
		case SNDCTL_DSP_GETBLKSIZE:
			*uval = DSP_BUF_SIZE;
			break;
		case SNDCTL_DSP_RESET:
			GUS_VOICE_W(VOICECTRL, GUS_VCTRL_16BIT | GUS_VCTRL_STOP);
			gus.playing = 0;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static struct fs_operations gus_driver_fsop = {
	0,
	0,

	gus_open,
	gus_close,
	NULL,			/* read */
	gus_write,
	gus_ioctl,
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

static struct device gus_device = {
	"dsp",
	DSP_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&gus_driver_fsop,
	NULL,
	NULL,
	NULL
};

int gus_init(void)
{
	extern unsigned long alloc_pages64(int);

	gus.present = 0;

	/* probe: an absent GUS reads 0xFF from the DRAM port, a real one
	 * returns the (calloc'd, hence zeroed) DRAM byte */
	gus_dram_seek(0);
	if(inport_b(GUS_DRAM_DATA) == 0xFF) {
		return -ENODEV;
	}

	/* un-reset: bit 0 enables the mixer + voice registers, bit 1 = DAC,
	 * bit 2 = the synth (wavetable) IRQ enable */
	outport_b(GUS_MIXER_CTRL, 0x40);
	gus_funk(GUS_F_RESET);
	gus_data_w8(0x07);		/* single-byte: the 0x4c reg is 8-bit */
	{
		static struct interrupt irq_config_gus = { 0, "gus", &gus_irq_handler, NULL };
		register_irq(GUS_IRQ, &irq_config_gus);
		enable_irq(GUS_IRQ);
	}
	gus_voice_sel(0);
	gus_funk(GUS_F_NUMVOICES);
	gus_data_w8(6);			/* 7 voices: freq 512 -> 1:1 pitch */
	SET_MINOR(gus_device.minors, DSP_MINOR);
	if(register_device(CHR_DEV, &gus_device)) {
		printk("WARNING: %s(): unable to register 'dsp' device.\n", __FUNCTION__);
		return -EINVAL;
	}
	devfs_make_node("Audio/dsp", MKDEV(DSP_MAJOR, DSP_MINOR),
			S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH);

	gus.buf = (unsigned char *)P2V(alloc_pages64(32));
	if(!gus.buf) {
		return -ENOMEM;
	}
	gus.afmt = AFMT_S16_LE;
	gus.speed = 44100;
	gus.channels = 2;
	gus.present = 1;
	printk("gus: Gravis Ultrasound at 0x%x, OSS /dev/dsp (16-bit voices, buffered playback)\n",
		GUS_IOBASE);
	return 0;
}
