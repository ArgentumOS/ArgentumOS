/*
 * fnx/drivers/char/kbdaux.c
 *
 * /dev/kbd: the GUI keyboard device. keyboard.c's pipeline emits
 * normalized key-press events here (4-byte records: key, mods, state,
 * pad) whenever a reader has the device open, and the console tty
 * emission is skipped while it is grabbed - the session compositor
 * owns the keyboard, exactly as it owns the pointer via the native
 * mouse device (/System/Devices/mouse, drivers/char/mousedev.c).
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
#include <fnx/kbdaux.h>
#include <fnx/sched.h>
#include <fnx/sleep.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#ifdef CONFIG_KBDAUX
static struct fs_operations kbdaux_driver_fsop = {
	0,
	0,

	kbdaux_open,
	kbdaux_close,
	kbdaux_read,
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	kbdaux_select,

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

static struct device kbdaux_device = {
	"kbd",
	KBD_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&kbdaux_driver_fsop,
	NULL,
	NULL,
	NULL
};

struct kbdaux *kbdaux_table;	/* heap: sleep/wakeup key + state (same
					 * rule as psaux_table/tty_table) */

int kbdaux_open(struct inode *i, struct fd *f)
{
	int minor;

	(void)f;
	minor = MINOR(i->rdev);
	if(!TEST_MINOR(kbdaux_device.minors, minor)) {
		return -ENXIO;
	}
	if(kbdaux_table->count == 0) {
		/* first reader: the GUI takes over the keyboard */
	}
	kbdaux_table->count++;
	return 0;
}

int kbdaux_close(struct inode *i, struct fd *f)
{
	int minor;

	(void)f;
	minor = MINOR(i->rdev);
	if(!TEST_MINOR(kbdaux_device.minors, minor)) {
		return -ENXIO;
	}
	if(kbdaux_table->count > 0) {
		kbdaux_table->count--;
	}
	return 0;
}

int kbdaux_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	int minor, bytes_read;
	unsigned char ch;

	minor = MINOR(i->rdev);
	if(!TEST_MINOR(kbdaux_device.minors, minor)) {
		return -ENXIO;
	}

	while(!kbdaux_table->read_q.count) {
		if(f->flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		if(sleep(&kbdaux_table->read_q, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
	}
	bytes_read = 0;
	while(bytes_read < count) {
		if(kbdaux_table->read_q.count) {
			ch = charq_getchar(&kbdaux_table->read_q);
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

int kbdaux_select(struct inode *i, struct fd *f, int flag)
{
	int minor;

	(void)f;
	minor = MINOR(i->rdev);
	if(!TEST_MINOR(kbdaux_device.minors, minor)) {
		return -ENXIO;
	}

	switch(flag) {
		case SEL_R:
			if(kbdaux_table->read_q.count) {
				return 1;
			}
			break;
	}
	return 0;
}

/* queue one key event when the GUI has the device open */
void kbdaux_event(int key, int mods, int state)
{
	unsigned char rec[4];

	if(!kbdaux_table || !kbdaux_table->count) {
		return;
	}
	rec[0] = (unsigned char)key;
	rec[1] = (unsigned char)mods;
	rec[2] = (unsigned char)state;
	rec[3] = 0;
	charq_putchar(&kbdaux_table->read_q, rec[0]);
	charq_putchar(&kbdaux_table->read_q, rec[1]);
	charq_putchar(&kbdaux_table->read_q, rec[2]);
	charq_putchar(&kbdaux_table->read_q, rec[3]);
	wakeup(&kbdaux_table->read_q);
	wakeup(&do_select);
}

int kbdaux_active(void)
{
	return kbdaux_table && kbdaux_table->count > 0;
}

void kbdaux_init(void)
{
	if(!(kbdaux_table = (struct kbdaux *)kmalloc(sizeof(struct kbdaux)))) {
		printk("kbd: no memory\n");
		return;
	}
	memset_b(kbdaux_table, 0, sizeof(struct kbdaux));
	SET_MINOR(kbdaux_device.minors, KBD_MINOR);
	if(register_device(CHR_DEV, &kbdaux_device)) {
		printk("WARNING: %s(): unable to register kbd device.\n", __FUNCTION__);
	}
	devfs_make_node("PS2/Keyboard", MKDEV(KBD_MAJOR, KBD_MINOR), S_IFCHR | S_IRUSR | S_IWUSR);
	printk("kbdaux    /dev/kbd        -\tGUI keyboard device\n");
}
#endif /* CONFIG_KBDAUX */
