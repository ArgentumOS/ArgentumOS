/*
 * fnx/drivers/char/mousedev.c
 *
 * The native mouse device (/System/Devices/mouse): normalized pointer
 * events, the mouse counterpart of kbdaux's /dev/kbd. Producers decode
 * their own wire format and call mousedev_event():
 *
 *   - the PS/2 8042 aux port  -> mousedev_ps2_byte() frames 3-byte PS/2
 *     packets and decodes them (called from irq_psaux);
 *   - USB HID                 -> usb-mouse decodes the report directly.
 *
 * No consumer ever parses a PS/2 byte stream, so no framing heuristic
 * lives in userspace and a dropped/duplicated byte cannot desync a
 * reader into fabricated button edges.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/devices.h>
#include <fnx/fs.h>
#include <fnx/fs_devfs.h>
#include <fnx/mm.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/fcntl.h>
#include <fnx/mousedev.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#ifdef CONFIG_MOUSEDEV

static struct fs_operations mousedev_driver_fsop = {
	0,
	0,

	mousedev_open,
	mousedev_close,
	mousedev_read,
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	mousedev_select,

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	NULL,			/* lockup */
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

static struct device mousedev_device = {
	"mouse",
	MOUSE_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&mousedev_driver_fsop,
	NULL,
	NULL,
	NULL
};

struct mousedev *mousedev_table;	/* heap: sleep/wakeup key + state
					   (same rule as psaux/kbdaux) */

int mousedev_open(struct inode *i, struct fd *f)
{
	int minor;

	(void)f;
	minor = MINOR(i->rdev);
	if(!TEST_MINOR(mousedev_device.minors, minor)) {
		return -ENXIO;
	}
	mousedev_table->count++;
	return 0;
}

int mousedev_close(struct inode *i, struct fd *f)
{
	int minor;

	(void)f;
	minor = MINOR(i->rdev);
	if(!TEST_MINOR(mousedev_device.minors, minor)) {
		return -ENXIO;
	}
	if(mousedev_table->count > 0) {
		mousedev_table->count--;
	}
	return 0;
}

int mousedev_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	int minor, bytes_read;
	unsigned char ch;

	minor = MINOR(i->rdev);
	if(!TEST_MINOR(mousedev_device.minors, minor)) {
		return -ENXIO;
	}

	while(!mousedev_table->read_q.count) {
		if(f->flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		if(sleep(&mousedev_table->read_q, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
	}
	bytes_read = 0;
	while(bytes_read < count) {
		if(mousedev_table->read_q.count) {
			ch = charq_getchar(&mousedev_table->read_q);
			buffer[bytes_read++] = ch;
			continue;
		}
		break;
	}
	if(bytes_read) {
		i->i_atime = CURRENT_TIME;
	}
	return bytes_read;
}

int mousedev_select(struct inode *i, struct fd *f, int flag)
{
	int minor;

	(void)f;
	minor = MINOR(i->rdev);
	if(!TEST_MINOR(mousedev_device.minors, minor)) {
		return -ENXIO;
	}

	switch(flag) {
		case SEL_R:
			if(mousedev_table->read_q.count) {
				return 1;
			}
			break;
	}
	return 0;
}

/* queue one normalized pointer event when the GUI has the device open.
 * dx/dy are relative screen-convention deltas (positive y = down), the
 * same convention both producers already deliver. */
void mousedev_event(int buttons, int wheel, int dx, int dy, int hwheel)
{
	unsigned char rec[MOUSE_EVENT_SIZE];

	if(!mousedev_table || !mousedev_table->count) {
		return;
	}
	if(dx < -32768) dx = -32768;
	if(dx > 32767) dx = 32767;
	if(dy < -32768) dy = -32768;
	if(dy > 32767) dy = 32767;

	rec[0] = (unsigned char)(buttons & 0xFF);
	rec[1] = (unsigned char)(wheel & 0xFF);
	rec[2] = (unsigned char)(dx & 0xFF);
	rec[3] = (unsigned char)((dx >> 8) & 0xFF);
	rec[4] = (unsigned char)(dy & 0xFF);
	rec[5] = (unsigned char)((dy >> 8) & 0xFF);
	rec[6] = (unsigned char)(hwheel & 0xFF);
	rec[7] = 0;

	{
		int n;
		int edge;
		unsigned int need;

		/* A record must enter the queue whole.  charq_putchar()
		 * returns -EAGAIN when the queue is full, so inserting the
		 * eight bytes blindly can leave a *partial* record in the
		 * stream: every later 8-byte record is then shifted and the
		 * reader decodes garbage - fabricated button edges and wild
		 * deltas (exactly the "button released while held", "window
		 * jumps" symptoms).  Drop the whole record instead.
		 *
		 * A dropped record must never be one carrying a button or
		 * wheel *transition*: lost motion only shortens a pointer
		 * step, but a lost release leaves the WM - and every client
		 * - believing the button is still held, so the window keeps
		 * following the pointer after the user let go.  Motion may
		 * therefore fill the queue only up to MOUSE_EDGE_RESERVE;
		 * edge records may use all of it. */
		edge = (buttons != (int)mousedev_table->queued_buttons) ||
			wheel || hwheel;
		need = MOUSE_EVENT_SIZE + (edge ? 0 : MOUSE_EDGE_RESERVE);
		if(charq_room(&mousedev_table->read_q) < need) {
			if(edge) {
				/* the reserve should make this impossible for any
				 * realistic input; say so loudly if it is not */
				mousedev_table->edge_dropped++;
				printk("mouse: button/wheel edge dropped (queue full, total %d)\n",
					mousedev_table->edge_dropped);
			} else {
				mousedev_table->dropped++;
				if(mousedev_table->dropped == 1 || !(mousedev_table->dropped % 128)) {
					printk("mouse: record dropped (queue full, total %d)\n",
						mousedev_table->dropped);
				}
			}
			return;
		}
		for(n = 0; n < MOUSE_EVENT_SIZE; n++) {
			charq_putchar(&mousedev_table->read_q, rec[n]);
		}
		mousedev_table->queued_buttons = (unsigned char)(buttons & 0xFF);
	}
	wakeup(&mousedev_table->read_q);
	wakeup(&do_select);
}

int mousedev_active(void)
{
	return mousedev_table && mousedev_table->count > 0;
}

/* --- PS/2 (8042 aux port) producer --------------------------------
 *
 * Frame the byte stream into 3-byte PS/2 packets and decode them. The
 * first byte of a packet always has bit 3 set, which is the only
 * reliable resync anchor; a stray or lost byte is dropped here rather
 * than corrupting a whole packet downstream (the old userspace parser
 * lived with exactly this risk). dy passes through as each byte's own
 * two's-complement value, the convention the pipeline has always
 * carried (bit 7, not the packet's 0x10/0x20 sign hints). */
static unsigned char ps2_pkt[3];
static int ps2_pkt_n;

void mousedev_ps2_byte(unsigned char b)
{
	int b0, dx, dy, buttons;

	if(ps2_pkt_n == 0) {
		if(!(b & 0x08)) {
			return;
		}
		ps2_pkt[0] = b;
		ps2_pkt_n = 1;
		return;
	}
	if(ps2_pkt_n == 1) {
		ps2_pkt[1] = b;
		ps2_pkt_n = 2;
		return;
	}
	ps2_pkt[2] = b;
	ps2_pkt_n = 0;

	b0 = ps2_pkt[0];
	dx = (int)(signed char)ps2_pkt[1];
	dy = (int)(signed char)ps2_pkt[2];
	buttons = b0 & 0x07;

	mousedev_event(buttons, 0, dx, dy, 0);
}

void mousedev_init(void)
{
	if(!(mousedev_table = (struct mousedev *)kmalloc(sizeof(struct mousedev)))) {
		printk("mouse: no memory\n");
		return;
	}
	memset_b(mousedev_table, 0, sizeof(struct mousedev));
	SET_MINOR(mousedev_device.minors, MOUSE_MINOR);
	if(register_device(CHR_DEV, &mousedev_device)) {
		printk("WARNING: %s(): unable to register mouse device.\n", __FUNCTION__);
	}
	printk("mouse     /dev/mouse      -\tnative pointer device\n");
}

#endif /* CONFIG_MOUSEDEV */
