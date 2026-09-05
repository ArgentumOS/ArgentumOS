/*
 * fnx/drivers/char/ac97.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Intel 82801AA AC97 Audio (8086:2415) with the OSS /dev/dsp API.
 *
 * Two I/O BARs: BAR0 = NAM (native audio mixer, the codec registers, 16-bit
 * accesses), BAR1 = NABMB (native audio bus mastering). Playback runs on the
 * PO (play out) channel: the driver writes a list of 8-byte buffer
 * descriptors (BD) into guest memory (BD = phys addr + ctl_len; ctl_len's
 * low 16 bits are the length in 16-bit samples, bit 31 = IOC - interrupt on
 * completion), points PO_BDBAR at it, sets PO_LVI and starts the channel
 * with PO_CR's CR_RPBM. QEMU DMA-reads each BD buffer and plays it; the
 * interrupt (GLOB_STA's GS_POINT + the PO status) wakes the blocked writer.
 * The codec is always 16-bit stereo; the rate is set via the NAM's
 * extended-audio control (EACS_VRA) + PCM front DAC rate register.
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

#define AC97_VENDOR_ID			0x8086
#define AC97_DEVICE_ID			0x2415	/* Intel 82801AA */

#define DSP_MAJOR			14
#define DSP_MINOR			3

/* NAM (BAR0) - codec registers (16-bit) */
#define NAM_EXT_AUDIO_CTRL		0x2a
#define NAM_PCM_FRONT_DAC_RATE		0x2c
#define EACS_VRA			0x0001

/* NABMB (BAR1) - bus mastering */
#define NABMB_PO_BDBAR			0x10
#define NABMB_PO_LVI			0x15
#define NABMB_PO_SR			0x16
#define NABMB_PO_CR			0x1b
#define NABMB_GLOB_STA			0x30

#define SR_BCIS				0x0008
#define SR_LVBCI			0x0004
#define SR_CELV				0x0002
#define SR_DCH				0x0001

#define CR_IOCE				0x0010
#define CR_RPBM				0x0001

#define GS_POINT			(1 << 6)

#define BD_IOC				(1 << 31)

/* the OSS ioctls (4Front soundcard.h) */
#define SNDCTL_DSP_RESET		0x00005000
#define SNDCTL_DSP_SPEED		0xc0045002
#define SNDCTL_DSP_STEREO		0x80045003
#define SNDCTL_DSP_GETBLKSIZE		0x80045004
#define SNDCTL_DSP_SETFMT		0xc0045005
#define SNDCTL_DSP_CHANNELS		0xc0045006

#define AFMT_U8				0x00000008
#define AFMT_S16_LE			0x00000010

#define DSP_BUF_SIZE			16384	/* bytes; 4 pages */

struct ac97_state {
	int present;
	unsigned short nam;		/* BAR0 */
	unsigned short nabm;		/* BAR1 */
	unsigned char irq;
	unsigned int afmt;		/* AFMT_* */
	unsigned int speed;
	unsigned char *buf;		/* the DMA audio buffer (kernel VA) */
	unsigned int buf_phys;
	unsigned char *bdl;		/* the BD list (kernel VA) */
	unsigned int bdl_phys;
	unsigned int buf_len;		/* currently armed length (bytes) */
	int playing;			/* a BD is in flight */
};

static struct ac97_state ac97;

static void ac97_nam_w(unsigned int off, unsigned short val)
{
	outport_w(ac97.nam + off, val);
}

static void ac97_nabm_b(unsigned int off, unsigned char val)
{
	outport_b(ac97.nabm + off, val);
}

static unsigned char ac97_nabm_rb(unsigned int off)
{
	return inport_b(ac97.nabm + off);
}

static unsigned short ac97_nabm_rw(unsigned int off)
{
	return inport_w(ac97.nabm + off);
}

static unsigned int ac97_nabm_rl(unsigned int off)
{
	return inport_l(ac97.nabm + off);
}

static void ac97_irq_handler(int num, struct sigcontext *sc)
{
	unsigned short sr;
	unsigned int gs;

	gs = ac97_nabm_rl(NABMB_GLOB_STA);
	if(!(gs & GS_POINT)) {
		return;
	}
	sr = ac97_nabm_rw(NABMB_PO_SR);
	/* clear the PO status (BCIS/LVBCI are write-1-to-clear); QEMU's
	 * update_sr deasserts the interrupt when the mask empties */
	outport_w(ac97.nabm + NABMB_PO_SR, sr & (SR_BCIS | SR_LVBCI));
	ac97.playing = 0;
	wakeup(&ac97.playing);
}

/* build one BD in the descriptor list + arm the PO channel */
static void ac97_arm(unsigned int len)
{
	unsigned int *bd = (unsigned int *)ac97.bdl;
	unsigned int samples = len >> 1;	/* the BD length is in 16-bit samples */

	bd[0] = ac97.buf_phys;
	bd[1] = samples | BD_IOC;		/* IOC: interrupt on completion */

	ac97_nabm_b(NABMB_PO_LVI, 0);		/* last valid BD = BD 0 */
	outport_l(ac97.nabm + NABMB_PO_BDBAR, ac97.bdl_phys);
	/* clear any leftover status then run: RPBM starts the channel, IOCE
	 * enables the completion interrupt */
	outport_w(ac97.nabm + NABMB_PO_SR, SR_BCIS | SR_LVBCI);
	ac97_nabm_b(NABMB_PO_CR, CR_IOCE | CR_RPBM);
}

int ac97_open(struct inode *i, struct fd *f)
{
	ac97.playing = 0;
	return 0;
}

int ac97_close(struct inode *i, struct fd *f)
{
	ac97_nabm_b(NABMB_PO_CR, 0);		/* pause */
	ac97.playing = 0;
	return 0;
}

int ac97_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	unsigned int done = 0, chunk;

	while(count > 0) {
		chunk = (count > DSP_BUF_SIZE) ? DSP_BUF_SIZE : count;
		while(ac97.playing) {
			sleep(&ac97.playing, PROC_UNINTERRUPTIBLE);
		}
		memcpy_b(ac97.buf, buffer + done, chunk);
		ac97.buf_len = chunk;
		ac97.playing = 1;
		ac97_arm(chunk);
		done += chunk;
		count -= chunk;
	}
	return done;
}

int ac97_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	unsigned int *uval = (unsigned int *)arg;

	switch(cmd) {
		case SNDCTL_DSP_SETFMT:
			if(*uval != AFMT_S16_LE) {
				*uval = AFMT_S16_LE;	/* the codec is always 16-bit */
			}
			ac97.afmt = *uval;
			break;
		case SNDCTL_DSP_SPEED:
			ac97.speed = *uval;
			ac97_nam_w(NAM_EXT_AUDIO_CTRL, EACS_VRA);
			ac97_nam_w(NAM_PCM_FRONT_DAC_RATE, (unsigned short)*uval);
			break;
		case SNDCTL_DSP_CHANNELS:
			if(*uval != 2) {
				*uval = 2;	/* the codec is always stereo */
			}
			break;
		case SNDCTL_DSP_STEREO:
			*uval = 1;
			break;
		case SNDCTL_DSP_GETBLKSIZE:
			*uval = DSP_BUF_SIZE;
			break;
		case SNDCTL_DSP_RESET:
			ac97_nabm_b(NABMB_PO_CR, 0);
			ac97.playing = 0;
			wakeup(&ac97.playing);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static struct fs_operations ac97_driver_fsop = {
	0,
	0,

	ac97_open,
	ac97_close,
	NULL,			/* read */
	ac97_write,
	ac97_ioctl,
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

static struct device ac97_device = {
	"dsp",
	DSP_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&ac97_driver_fsop,
	NULL,
	NULL,
	NULL
};

int ac97_init(void)
{
	struct pci_device *pd;
	unsigned short nam, nabm;
	extern unsigned long alloc_pages64(int);

	ac97.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == AC97_VENDOR_ID && pd->device_id == AC97_DEVICE_ID) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}
	nam = (unsigned short)(pd->bar[0] & 0xFFFC);
	nabm = (unsigned short)(pd->bar[1] & 0xFFFC);
	if(!nam || !nabm) {
		return -ENODEV;
	}
	ac97.nam = nam;
	ac97.nabm = nabm;
	ac97.irq = pd->irq;
	pci_write_short(pd, 0x04, 0x0001 | 0x0004);	/* command: I/O space + bus master */

	/* the DMA audio buffer + the BD list (one page holds 32 BDs) */
	ac97.buf_phys = (unsigned int)alloc_pages64(4);
	if(!ac97.buf_phys) {
		return -ENOMEM;
	}
	ac97.buf = (unsigned char *)P2V(ac97.buf_phys);
	ac97.bdl_phys = (unsigned int)alloc_pages64(1);
	if(!ac97.bdl_phys) {
		return -ENOMEM;
	}
	ac97.bdl = (unsigned char *)P2V(ac97.bdl_phys);
	ac97.afmt = AFMT_S16_LE;
	ac97.speed = 44100;

	SET_MINOR(ac97_device.minors, DSP_MINOR);
	if(register_device(CHR_DEV, &ac97_device)) {
		printk("WARNING: %s(): unable to register 'dsp' device.\n", __FUNCTION__);
		return -EINVAL;
	}
	devfs_make_node("Audio/dsp", MKDEV(DSP_MAJOR, DSP_MINOR), S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(ac97.irq) {
		static struct interrupt irq_config_ac97 = { 0, "ac97", &ac97_irq_handler, NULL };
		register_irq(ac97.irq, &irq_config_ac97);
		enable_irq(ac97.irq);
	}

	/* codec: enable VRA (variable-rate audio) + set the front DAC rate */
	ac97_nam_w(NAM_EXT_AUDIO_CTRL, EACS_VRA);
	ac97_nam_w(NAM_PCM_FRONT_DAC_RATE, (unsigned short)ac97.speed);

	ac97.present = 1;
	printk("dsp       0x%04x-0x%04x    %d\ttype=AC97 (Intel 82801AA), OSS /dev/dsp (%u Hz S16_LE stereo)\n",
		nam, nabm + 0xff, ac97.irq, ac97.speed);
	return 0;
}
