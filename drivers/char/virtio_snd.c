/*
 * fnx/drivers/char/virtio_snd.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * virtio-snd (virtio sound, VIRTIO_ID_SOUND = 25) with the OSS /dev/dsp
 * API, driven over the MODERN (virtio-1) PCI transport.
 *
 * The device (QEMU's virtio-sound-pci, 1af4:1059) is modern-only: it has
 * no legacy BAR0 I/O block, so this driver implements the virtio-1 PCI
 * transport from scratch: the VENDOR capabilities in the PCI config space
 * locate the COMMON_CFG / NOTIFY_CFG / ISR_CFG / DEVICE_CFG regions inside
 * BAR4 (mapped into a fixed kernel VA), queue setup goes through the
 * 64-bit queue_desc/queue_avail/queue_used + queue_enable handshake, and
 * notifications are MMIO writes to the notify region (queue index * the
 * notify_off_multiplier).
 *
 * Wire protocol (virtio 1.2 spec section 5.14, QEMU hw/audio/virtio-snd.c):
 * - config { u32 jacks; u32 streams; u32 chmaps; u32 controls; } - QEMU
 *   exposes 2 streams (1 output + 1 input); the driver uses stream 0.
 * - queue 0 = control, queue 2 = tx (each a 64-entry split vring).
 * - control messages: an OUT buffer (the request, code + payload) chained
 *   with an IN buffer (the 4-byte status); the device pushes the element
 *   back to the used ring with the status written into the IN buffer.
 *   Stream setup = SET_PARAMS(0x0101) -> PREPARE(0x0102) -> START(0x0104),
 *   teardown = STOP(0x0105) -> RELEASE(0x0103); status OK = 0x8000.
 * - tx buffers: an OUT buffer whose first 4 bytes are the stream id (0),
 *   followed by the raw PCM; the device plays it and returns it via the
 *   used ring. The writer blocks on the used ring (poll + timed sleep,
 *   mirroring virtio_net: the INTx line does not reliably reach the PIC
 *   on this QEMU, so the IRQ handler only ACKs and wakes sleepers).
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

#define VIRTIO_PCI_VENDOR		0x1AF4
#define VIRTIO_PCI_DEVICE_SOUND		0x1059	/* modern: 0x1040 + 25 */

#define DSP_MAJOR			14
#define DSP_MINOR			3

/* BAR4 of the modern virtio device: common cfg @0, ISR @0x1000,
 * device cfg @0x2000, notify @0x3000 (all located via the caps) */
#define VSOUND_MMIO_VA			0xFFFFBE0800000000UL	/* pml4[382] */
#define VSOUND_MMIO_SIZE		0x4000

#define PCI_CAP_ID_VNDR			0x09

/* virtio_pci_cap.cfg_type values */
#define VPCI_CAP_COMMON_CFG		1
#define VPCI_CAP_NOTIFY_CFG		2
#define VPCI_CAP_ISR_CFG		3
#define VPCI_CAP_DEVICE_CFG		4

/* virtio_pci_common_cfg offsets (32-bit granularity) */
#define CFG_DEVICE_FEATURE_SEL		0x00
#define CFG_DEVICE_FEATURE		0x04
#define CFG_GUEST_FEATURE_SEL		0x08
#define CFG_GUEST_FEATURE		0x0c
#define CFG_MSIX_CONFIG			0x10
#define CFG_NUM_QUEUES			0x12
#define CFG_DEVICE_STATUS		0x14
#define CFG_CONFIG_GENERATION		0x15
#define CFG_QUEUE_SELECT		0x16
#define CFG_QUEUE_SIZE			0x18
#define CFG_QUEUE_MSIX			0x1a
#define CFG_QUEUE_ENABLE		0x1c
#define CFG_QUEUE_NOTIFY_OFF		0x1e
#define CFG_QUEUE_DESC_LO		0x20
#define CFG_QUEUE_DESC_HI		0x24
#define CFG_QUEUE_AVAIL_LO		0x28
#define CFG_QUEUE_AVAIL_HI		0x2c
#define CFG_QUEUE_USED_LO		0x30
#define CFG_QUEUE_USED_HI		0x34

/* device_status bits */
#define VSTATUS_ACKNOWLEDGE		0x01
#define VSTATUS_DRIVER			0x02
#define VSTATUS_DRIVER_OK		0x04
#define VSTATUS_FEATURES_OK		0x08
#define VSTATUS_FAILED			0x80

/* virtio-1: write 0xffff to the msix config/vector to leave MSI-X off */
#define VIRTIO_MSI_NO_VECTOR		0xffff

#define VRING_DESC_F_NEXT		1
#define VRING_DESC_F_WRITE		2

#define VSND_VQ_SIZE			64	/* QEMU: 4 queues of 64 */

#define VQ_CONTROL			0
#define VQ_EVENT			1
#define VQ_TX				2
#define VQ_RX				3

/* control request codes */
#define R_PCM_SET_PARAMS		0x0101
#define R_PCM_PREPARE			0x0102
#define R_PCM_RELEASE			0x0103
#define R_PCM_START			0x0104
#define R_PCM_STOP			0x0105

/* status codes */
#define S_OK				0x8000
#define S_BAD_MSG			0x8001
#define S_NOT_SUPP			0x8002
#define S_IO_ERR			0x8003

/* PCM format/rate indices (virtio_snd_pcm_set_params) */
#define FMT_S16				5
#define RATE_44100			6

/* device config: { u32 jacks; u32 streams; u32 chmaps; u32 controls; } */
#define DEVCFG_STREAMS			4

/* the OSS ioctls (4Front soundcard.h) */
#define SNDCTL_DSP_RESET		0x00005000
#define SNDCTL_DSP_SPEED		0xc0045002
#define SNDCTL_DSP_STEREO		0x80045003
#define SNDCTL_DSP_GETBLKSIZE		0x80045004
#define SNDCTL_DSP_SETFMT		0xc0045005
#define SNDCTL_DSP_CHANNELS		0xc0045006

#define AFMT_S16_LE			0x00000010

#define DSP_BUF_SIZE			16380	/* bytes (4 header + payload in a 16KB DMA page) */

/* ---------------- split vring (same layout as virtio_net) ------------- */

struct virtq_desc {
	__u64 addr;
	__u32 len;
	__u16 flags;
	__u16 next;
};

struct virtq_avail {
	__u16 flags;
	__u16 idx;
	__u16 ring[VSND_VQ_SIZE];
};

struct virtq_used_elem {
	__u32 id;
	__u32 len;
};

struct virtq_used {
	__u16 flags;
	__u16 idx;
	struct virtq_used_elem ring[VSND_VQ_SIZE];
};

/* the used ring is 4K-aligned at 16*qsz inside the (3-page) queue block */
struct virtq_page {
	__u8 raw[12288];
};

#define VQ_DESC(qp, qsz)	((struct virtq_desc *)((char *)(qp) + 0))
#define VQ_AVAIL(qp, qsz)	((struct virtq_avail *)((char *)(qp) + 16 * (qsz)))
#define VQ_USED(qp, qsz)	((struct virtq_used *)((char *)(qp) + \
				(((16 * (qsz)) + (6 + 2 * (qsz)) + 4095) & ~4095)))

struct virtq {
	struct virtq_page *page;	/* kernel VA of the queue pages */
	unsigned long page_phys;	/* physical address for the cfg */
	unsigned int size;		/* ring size (VSND_VQ_SIZE) */
	unsigned int next_free;		/* next free desc index */
	unsigned int avail_idx;		/* next available-ring index */
	unsigned int used_consumed;	/* last consumed used index */
	unsigned int noff;		/* queue_notify_off (from the cfg) */
};

struct vsnd_device {
	int present;
	unsigned long mmio;		/* BAR4 kernel VA */
	unsigned int notify_off;	/* notify region offset in BAR4 */
	unsigned int notify_mult;	/* notify_off_multiplier */
	unsigned int isr_off;
	unsigned int dev_off;
	unsigned int common_off;
	unsigned char irq;
	unsigned int streams;		/* from the device config */
	struct virtq cq, tq;		/* control + tx queues */
	unsigned char *ctrl_req;	/* control request (out part) */
	unsigned char *ctrl_resp;	/* control response (in part) */
	unsigned char *txbuf;		/* the fixed DMA audio buffer (VA) */
	unsigned long txbuf_phys;
	int playing;			/* a tx buffer is in flight */
	int started;			/* PREPARE + START were sent */
	unsigned int afmt, speed, channels;
};

static struct vsnd_device vsnd;

static int vsnd_tx_send(const void *data, unsigned int len);

/* ---------------- modern transport helpers ------------- */

static void vsnd_cfg_w32(unsigned int off, __u32 val)
{
	*(volatile __u32 *)(vsnd.mmio + vsnd.common_off + off) = val;
}

static __u16 vsnd_cfg_r16(unsigned int off)
{
	return *(volatile __u16 *)(vsnd.mmio + vsnd.common_off + off);
}

static void vsnd_cfg_w16(unsigned int off, __u16 val)
{
	*(volatile __u16 *)(vsnd.mmio + vsnd.common_off + off) = val;
}

static __u8 vsnd_cfg_r8(unsigned int off)
{
	return *(volatile __u8 *)(vsnd.mmio + vsnd.common_off + off);
}

static void vsnd_cfg_w8(unsigned int off, __u8 val)
{
	*(volatile __u8 *)(vsnd.mmio + vsnd.common_off + off) = val;
}

static __u32 vsnd_dev_r32(unsigned int off)
{
	return *(volatile __u32 *)(vsnd.mmio + vsnd.dev_off + off);
}

static void vsnd_set_status(__u8 st)
{
	vsnd_cfg_w8(CFG_DEVICE_STATUS, st);
	__asm__ __volatile__("" ::: "memory");
}

/* find a virtio VENDOR capability in the PCI config space */
static int vsnd_find_cap(struct pci_device *pd, unsigned int type,
			 unsigned int *bar, unsigned int *offset,
			 unsigned int *mult)
{
	unsigned char ptr, cap;

	ptr = pci_read_char(pd, 0x34) & 0xFC;	/* capabilities pointer */
	while(ptr) {
		cap = pci_read_char(pd, ptr);
		if(cap == PCI_CAP_ID_VNDR) {
			unsigned char cfg_type = pci_read_char(pd, ptr + 3);
			if(cfg_type == type) {
				*bar = pci_read_char(pd, ptr + 4);
				*offset = pci_read_long(pd, ptr + 8);
				if(mult) {
					*mult = pci_read_long(pd, ptr + 0x10);
				}
				return 0;
			}
		}
		ptr = pci_read_char(pd, ptr + 1) & 0xFC;
	}
	return -ENODEV;
}

/* allocate the queue's 3 contiguous pages (like virtio_net) */
static unsigned long vsnd_alloc_page(addr_t *phys)
{
	extern unsigned long alloc_pages64(int);
	extern void free_pages64(unsigned long, int);
	unsigned long a, b, c;
	int n;

	for(n = 0; n < 32; n++) {
		a = alloc_pages64(1);
		if(!a) {
			return 0;
		}
		if(a < 0x100000 || a >= 0x8000000) {
			continue;
		}
		b = alloc_pages64(1);
		if(!b) {
			return 0;
		}
		if(b < 0x100000 || b >= 0x8000000) {
			free_pages64(b, 1);
			continue;
		}
		c = alloc_pages64(1);
		if(!c) {
			return 0;
		}
		if(c < 0x100000 || c >= 0x8000000) {
			free_pages64(c, 1);
			continue;
		}
		if(b == a + 0x1000 && c == a + 0x2000) {
			*phys = a;
			return P2V(a);
		}
		free_pages64(b, 1);
		free_pages64(c, 1);
	}
	return 0;
}

static int vsnd_queue_setup(struct virtq *q, unsigned int qidx)
{
	vsnd_cfg_w16(CFG_QUEUE_SELECT, qidx);
	vsnd_cfg_w16(CFG_QUEUE_SIZE, VSND_VQ_SIZE);
	vsnd_cfg_w16(CFG_QUEUE_MSIX, VIRTIO_MSI_NO_VECTOR);
	q->size = vsnd_cfg_r16(CFG_QUEUE_SIZE);
	if(!q->size) {
		return -ENODEV;
	}
	q->page = (struct virtq_page *)vsnd_alloc_page(&q->page_phys);
	if(!q->page) {
		return -ENOMEM;
	}
	q->next_free = 0;
	q->avail_idx = 0;
	q->used_consumed = 0;
	q->noff = vsnd_cfg_r16(CFG_QUEUE_NOTIFY_OFF);

	/* modern virtio: the desc/avail/used addresses are INDEPENDENT (no
	 * legacy single base + in-block offsets); write each ring's actual
	 * phys address (same layout the VQ_* macros use in the page block) */
	vsnd_cfg_w32(CFG_QUEUE_DESC_LO, (__u32)q->page_phys);
	vsnd_cfg_w32(CFG_QUEUE_DESC_HI, (__u32)(q->page_phys >> 32));
	vsnd_cfg_w32(CFG_QUEUE_AVAIL_LO,
		     (__u32)(q->page_phys + 16 * q->size));
	vsnd_cfg_w32(CFG_QUEUE_AVAIL_HI,
		     (__u32)((q->page_phys + 16 * q->size) >> 32));
	vsnd_cfg_w32(CFG_QUEUE_USED_LO,
		     (__u32)(q->page_phys + (((16 * q->size) + (6 + 2 * q->size) + 4095) & ~4095)));
	vsnd_cfg_w32(CFG_QUEUE_USED_HI,
		     (__u32)((q->page_phys + (((16 * q->size) + (6 + 2 * q->size) + 4095) & ~4095)) >> 32));
	vsnd_cfg_w16(CFG_QUEUE_ENABLE, 1);
	return 0;
}

static void vsnd_notify(unsigned int qidx, unsigned int noff)
{
	/* the modern notify: a 16-bit write of the queue index to
	 * notify_offset + queue_notify_off * notify_off_multiplier */
	*(volatile __u16 *)(vsnd.mmio + vsnd.notify_off + noff * vsnd.notify_mult) = (__u16)qidx;
	__asm__ __volatile__("" ::: "memory");
}

/* publish one descriptor in the avail ring and bump its index */
static void vsnd_avail_push(struct virtq *q, unsigned int d)
{
	unsigned int a = q->avail_idx++ % q->size;

	VQ_AVAIL(q->page, q->size)->ring[a] = d;
	__asm__ __volatile__("" ::: "memory");
	VQ_AVAIL(q->page, q->size)->idx = q->avail_idx;
}

/* drain completed TX buffers from the used ring and clear ->playing on
 * any completion (the writer owns this ring, so no IRQ/context race) */
static void vsnd_tx_poll(void)
{
	struct virtq *q = &vsnd.tq;
	struct virtq_used *u = VQ_USED(q->page, q->size);
	unsigned int any = 0;

	if(!q->page || !q->size) {
		return;
	}
	for(;;) {
		unsigned int cur = *(volatile __u16 *)&u->idx;

		if((q->used_consumed & 0xFFFF) == cur) {
			break;
		}
		q->used_consumed++;
		any = 1;
	}
	if(any) {
		vsnd.playing = 0;
	}
}

/* send one control message: code + payload in the OUT part, 4-byte
 * status in the IN part; busy-polls the control used ring (the device
 * processes the queue synchronously on notify under TCG) */
static int vsnd_ctrl_send(unsigned int code, const void *payload,
			  unsigned int plen)
{
	struct virtq *q = &vsnd.cq;
	struct virtq_used *u = VQ_USED(q->page, q->size);
	unsigned int d, d2, spin;

	if(plen > 32) {
		return -EINVAL;
	}
	*(volatile __u32 *)(vsnd.ctrl_req) = code;
	if(plen) {
		memcpy_b(vsnd.ctrl_req + 4, payload, plen);
	}

	d = q->next_free++ % q->size;
	d2 = q->next_free++ % q->size;

	VQ_DESC(q->page, q->size)[d].addr = V2P((addr_t)vsnd.ctrl_req);
	VQ_DESC(q->page, q->size)[d].len = 4 + plen;
	VQ_DESC(q->page, q->size)[d].flags = VRING_DESC_F_NEXT;
	VQ_DESC(q->page, q->size)[d].next = d2;
	VQ_DESC(q->page, q->size)[d2].addr = V2P((addr_t)vsnd.ctrl_resp);
	VQ_DESC(q->page, q->size)[d2].len = 4;
	VQ_DESC(q->page, q->size)[d2].flags = VRING_DESC_F_WRITE;
	VQ_DESC(q->page, q->size)[d2].next = 0;
	vsnd_avail_push(q, d);
	vsnd_notify(VQ_CONTROL, q->noff);

	for(spin = 0; spin < 1000000; spin++) {
		unsigned int cur = *(volatile __u16 *)&u->idx;

		while((q->used_consumed & 0xFFFF) != cur) {
			/* drain entries until OUR element comes back (a previous
			 * timed-out message may still be pending) */
			unsigned int i = q->used_consumed % q->size;

			q->used_consumed++;
			if(u->ring[i].id == d) {
				return *(volatile __u32 *)(vsnd.ctrl_resp);
			}
			cur = *(volatile __u16 *)&u->idx;
		}
		__asm__ __volatile__("pause" ::: "memory");
	}
	return -EIO;
}

/* ---------------- the virtio-snd PCM control sequence ------------- */

static int vsnd_pcm_cmd(unsigned int code)
{
	__u32 stream_id = 0;

	return vsnd_ctrl_send(code, &stream_id, 4);
}

static int vsnd_set_params(void)
{
	unsigned char msg[20];
	__u32 stream_id = 0;
	__u32 buffer_bytes = DSP_BUF_SIZE;
	__u32 period_bytes = DSP_BUF_SIZE;
	__u32 features = 0;

	/* virtio_snd_pcm_set_params: { stream_id, buffer_bytes,
	 * period_bytes, features, channels, format, rate, pad } */
	memcpy_b(msg, &stream_id, 4);
	memcpy_b(msg + 4, &buffer_bytes, 4);
	memcpy_b(msg + 8, &period_bytes, 4);
	memcpy_b(msg + 12, &features, 4);
	msg[16] = (unsigned char)vsnd.channels;
	msg[17] = FMT_S16;
	msg[18] = RATE_44100;
	msg[19] = 0;

	return vsnd_ctrl_send(R_PCM_SET_PARAMS, msg, 20);
}

static int vsnd_stream_start(void)
{
	int st;

	/* format/rate/channels come from the ioctls (defaults = tone) */
	vsnd.afmt = AFMT_S16_LE;
	if(!vsnd.speed) {
		vsnd.speed = 44100;
	}
	if(!vsnd.channels) {
		vsnd.channels = 2;
	}

	st = vsnd_set_params();
	if(st != S_OK) {
		printk("virtio-snd: SET_PARAMS status %x\n", st);
		return -EIO;
	}
	st = vsnd_pcm_cmd(R_PCM_PREPARE);
	if(st != S_OK) {
		printk("virtio-snd: PREPARE status %x\n", st);
		return -EIO;
	}
	st = vsnd_pcm_cmd(R_PCM_START);
	if(st != S_OK) {
		printk("virtio-snd: START status %x\n", st);
		return -EIO;
	}
	vsnd.started = 1;
	return 0;
}

/* ---------------- the OSS /dev/dsp interface ------------- */

int vsnd_open(struct inode *i, struct fd *f)
{
	/* a previous session may have left an element in flight on the fixed
	 * txbuf: collect the completion before clearing the gate, or the next
	 * submit would memcpy over data the device is still reading */
	{
		unsigned int spin;

		for(spin = 0; spin < 100000 && vsnd.playing; spin++) {
			vsnd_tx_poll();
		}
	}
	vsnd.playing = 0;
	vsnd.started = 0;
	return 0;
}

int vsnd_close(struct inode *i, struct fd *f)
{
	if(vsnd.started) {
		vsnd_pcm_cmd(R_PCM_STOP);
		vsnd_pcm_cmd(R_PCM_RELEASE);
		vsnd.started = 0;
	}
	vsnd.playing = 0;
	return 0;
}

int vsnd_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	unsigned int done = 0, chunk;
	int ret;

	if(!vsnd.present) {
		return -ENODEV;
	}
	if(!vsnd.started) {
		if(vsnd_stream_start()) {
			return -EIO;
		}
	}
	while(count > 0) {
		chunk = (count > DSP_BUF_SIZE) ? DSP_BUF_SIZE : count;
		while(vsnd.playing) {
			unsigned int spin;

			vsnd_tx_poll();
			if(!vsnd.playing) {
				break;
			}
			/* brief busy-wait for a completion that just landed */
			for(spin = 0; spin < 10000 && vsnd.playing; spin++) {
				vsnd_tx_poll();
			}
			if(!vsnd.playing) {
				break;
			}
			/* sleep with a timeout: the INTx wakeup is best-effort
			 * (it never reaches the PIC on this QEMU), so the
			 * timeout lets us poll again */
			{
				extern unsigned int tv2ticks(const struct timeval *);
				struct timeval tv;
				int woken;

				tv.tv_sec = 0;
				tv.tv_usec = 50000;	/* 50 ms */
				current->timeout = tv2ticks(&tv);
				woken = sleep(&vsnd.playing, PROC_INTERRUPTIBLE);
				if(!current->timeout) {
					current->timeout = 0;
					continue;	/* timed out: poll again */
				}
				current->timeout = 0;
				if(woken) {
					return (done) ? (int)done : -EINTR;
				}
			}
		}
		ret = vsnd_tx_send(buffer + done, chunk);
		if(ret < 0) {
			return (done) ? (int)done : ret;
		}
		done += chunk;
		count -= chunk;
	}
	return done;
}

int vsnd_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	unsigned int *uval = (unsigned int *)arg;

	switch(cmd) {
		case SNDCTL_DSP_SETFMT:
			if(*uval != AFMT_S16_LE) {
				*uval = AFMT_S16_LE;	/* the device is S16-only */
			}
			vsnd.afmt = *uval;
			break;
		case SNDCTL_DSP_SPEED:
			vsnd.speed = *uval;
			break;
		case SNDCTL_DSP_CHANNELS:
			if(*uval != 2) {
				*uval = 2;	/* the tone is stereo */
			}
			vsnd.channels = *uval;
			break;
		case SNDCTL_DSP_STEREO:
			*uval = 1;
			break;
		case SNDCTL_DSP_GETBLKSIZE:
			*uval = DSP_BUF_SIZE;
			break;
		case SNDCTL_DSP_RESET:
			/* same in-flight drain as open(): don't clear the gate
			 * while the device may still DMA the fixed txbuf */
			{
				unsigned int spin;

				for(spin = 0; spin < 100000 && vsnd.playing; spin++) {
					vsnd_tx_poll();
				}
			}
			vsnd.playing = 0;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static struct fs_operations vsnd_driver_fsop = {
	0,
	0,

	vsnd_open,
	vsnd_close,
	NULL,			/* read */
	vsnd_write,
	vsnd_ioctl,
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

static struct device vsnd_device = {
	"dsp",
	DSP_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&vsnd_driver_fsop,
	NULL,
	NULL,
	NULL
};


static int vsnd_tx_send(const void *data, unsigned int len)
{
	struct virtq *q = &vsnd.tq;
	unsigned int d;

	if(!q->page || !q->size) {
		return -ENODEV;
	}
	if(4 + len > 16384) {
		return -EINVAL;
	}
	*(volatile __u32 *)vsnd.txbuf = 0;	/* stream id 0 (little endian) */
	memcpy_b(vsnd.txbuf + 4, data, len);

	d = q->next_free++ % q->size;
	VQ_DESC(q->page, q->size)[d].addr = vsnd.txbuf_phys;
	VQ_DESC(q->page, q->size)[d].len = 4 + len;
	VQ_DESC(q->page, q->size)[d].flags = 0;
	VQ_DESC(q->page, q->size)[d].next = 0;
	vsnd.playing = 1;
	vsnd_avail_push(q, d);
	vsnd_notify(VQ_TX, q->noff);
	return 0;
}

static void vsnd_irq_handler(int num, struct sigcontext *sc)
{
	/* ACK the interrupt (reading the modern ISR status clears it); the
	 * writer polls the tx used ring itself, so we only wake sleepers */
	(void)*(volatile __u8 *)(vsnd.mmio + vsnd.isr_off);
	wakeup(&vsnd.playing);
}

int virtio_snd_init(void)
{
	struct pci_device *pd;
	extern int map_page64(unsigned long, unsigned long, unsigned long);
	unsigned long mmio;
	unsigned int qsize;
	int i;

	vsnd.present = 0;

	pd = pci_device_table;
	while(pd) {
		if(pd->vendor_id == VIRTIO_PCI_VENDOR &&
		   pd->device_id == VIRTIO_PCI_DEVICE_SOUND) {
			break;
		}
		pd = pd->next;
	}
	if(!pd) {
		return -ENODEV;
	}

	/* the modern transport: BAR4 is a 64-bit MMIO region holding the
	 * common cfg / ISR / device cfg / notify blocks */
	mmio = ((unsigned long)(pd->bar[5] & 0xFFFFFFF0) << 32) |
	       (pd->bar[4] & 0xFFFFFFF0);
	if(!mmio) {
		return -ENODEV;
	}
	for(i = 0; i < VSOUND_MMIO_SIZE / 4096; i++) {
		if(map_page64(VSOUND_MMIO_VA + i * 4096, mmio + i * 4096, 0x003)) {
			return -ENOMEM;
		}
	}
	vsnd.mmio = VSOUND_MMIO_VA;
	vsnd.irq = pd->irq;

	/* locate the modern virtio regions inside BAR4 via the VENDOR caps */
	{
		unsigned int bar, common_off, isr_off, dev_off, notify_off, notify_mult;

		if(vsnd_find_cap(pd, VPCI_CAP_COMMON_CFG, &bar, &common_off, NULL) ||
		   vsnd_find_cap(pd, VPCI_CAP_NOTIFY_CFG, &bar, &notify_off, &notify_mult) ||
		   vsnd_find_cap(pd, VPCI_CAP_ISR_CFG, &bar, &isr_off, NULL) ||
		   vsnd_find_cap(pd, VPCI_CAP_DEVICE_CFG, &bar, &dev_off, NULL) ||
		   bar != 4) {
			return -ENODEV;
		}
		vsnd.notify_off = notify_off;
		vsnd.notify_mult = notify_mult;
		vsnd.isr_off = isr_off;
		vsnd.dev_off = dev_off;
		vsnd.common_off = common_off;
	}

	/* command: MEM | MASTER */
	pci_write_short(pd, 0x04, 0x0006);

	/* reset, then the legacy-style status handshake through the
	 * modern common config */
	vsnd_set_status(0);
	vsnd_set_status(VSTATUS_ACKNOWLEDGE);
	vsnd_set_status(VSTATUS_ACKNOWLEDGE | VSTATUS_DRIVER);

	/* negotiate no features (the device defines none we need) */
	vsnd_cfg_w32(CFG_GUEST_FEATURE_SEL, 0);
	vsnd_cfg_w32(CFG_GUEST_FEATURE, 0);
	vsnd_cfg_w32(CFG_GUEST_FEATURE_SEL, 1);
	vsnd_cfg_w32(CFG_GUEST_FEATURE, 0);

	vsnd_set_status(VSTATUS_ACKNOWLEDGE | VSTATUS_DRIVER | VSTATUS_FEATURES_OK);
	if(!(vsnd_cfg_r8(CFG_DEVICE_STATUS) & VSTATUS_FEATURES_OK)) {
		return -ENODEV;
	}

	/* set up the control + tx queues */
	if(vsnd_queue_setup(&vsnd.cq, VQ_CONTROL) ||
	   vsnd_queue_setup(&vsnd.tq, VQ_TX)) {
		return -ENOMEM;
	}

	vsnd_set_status(VSTATUS_ACKNOWLEDGE | VSTATUS_DRIVER |
			VSTATUS_FEATURES_OK | VSTATUS_DRIVER_OK);

	/* device config: # of PCM streams (QEMU: 2 = 1 out + 1 in) */
	vsnd.streams = vsnd_dev_r32(DEVCFG_STREAMS);

	/* control buffers (tiny: allocate before the big tx buffer) */
	if(!(vsnd.ctrl_req = (unsigned char *)kmalloc(64)) ||
	   !(vsnd.ctrl_resp = (unsigned char *)kmalloc(64))) {
		return -ENOMEM;
	}

	/* the fixed DMA audio buffer: 4 pages (16KB) for the tx payload */
	{
		extern unsigned long alloc_pages64(int);

		vsnd.txbuf_phys = alloc_pages64(4);
		if(!vsnd.txbuf_phys) {
			return -ENOMEM;
		}
		vsnd.txbuf = (unsigned char *)P2V(vsnd.txbuf_phys);
	}

	if(vsnd.irq) {
		static struct interrupt irq_config_vsnd = { 0, "virtio-snd", &vsnd_irq_handler, NULL };
		register_irq(vsnd.irq, &irq_config_vsnd);
		enable_irq(vsnd.irq);
	}

	SET_MINOR(vsnd_device.minors, DSP_MINOR);
	if(register_device(CHR_DEV, &vsnd_device)) {
		printk("WARNING: %s(): unable to register 'dsp' device.\n", __FUNCTION__);
		return -EINVAL;
	}
	devfs_make_node("Audio/dsp", MKDEV(DSP_MAJOR, DSP_MINOR),
			S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH);

	qsize = vsnd.cq.size;
	vsnd.present = 1;
	printk("virtio-snd: 1af4:1059 at 0x%lx, IRQ %d, %u streams, %u-entry vrings, OSS /dev/dsp\n",
		mmio, vsnd.irq, vsnd.streams, qsize);
	return 0;
}
