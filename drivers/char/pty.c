/*
 * fnx/drivers/char/pty.c
 *
 * Copyright 2025, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/devices.h>
#include <fnx/errno.h>
#include <fnx/tty.h>
#include <fnx/pty.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devpts.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
#include <fnx/ioctl.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#ifdef CONFIG_UNIX98_PTYS
extern struct devpts_files *devpts_list;

static struct fs_operations pty_master_driver_fsop = {
	0,
	0,

	tty_open,
	tty_close,
	pty_read,
	pty_write,
	tty_ioctl,
	tty_llseek,
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	pty_select,

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

static struct fs_operations pty_slave_driver_fsop = {
	0,
	0,

	tty_open,
	tty_close,
	tty_read,
	pty_write,	/* slave writes must land in read_q/cooked_q, not
			 * write_q - the master's pty_read drains cooked_q */
	tty_ioctl,
	tty_llseek,
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	tty_select,

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

static struct device pty_master_device = {
	"ptmx",
	PTY_MASTER_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&pty_master_driver_fsop,
	NULL,
	NULL,
	NULL
};

static struct device pty_slave_device = {
	"pts",
	PTY_SLAVE_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&pty_slave_driver_fsop,
	NULL,
	NULL,
	NULL
};

void pty_wakeup_read(struct tty *tty)
{
	wakeup(&pty_read);
	wakeup(&do_select);
}

int pty_open(struct tty *tty)
{
	struct filesystems *fs;
	struct inode *i;
	int n, minor;


	if(tty->flags & TTY_PTY_LOCK) {
		return -EIO;
	}
	if(MAJOR(tty->dev) == PTY_SLAVE_MAJOR) {
		return 0;
	}
	minor = MINOR(tty->dev);
	if(!TEST_MINOR(pty_master_device.minors, minor)) {
		return -ENXIO;
	}

	if(!(fs = get_filesystem("devpts"))) {
		printk("WARNING: %s(): devpts filesystem is not registered!\n", __FUNCTION__);
		return -EINVAL;
	}
	if(!(i = ialloc(&fs->mp->sb, S_IFCHR))) {
		return -EINVAL;
	}
	for(n = 0; n < NR_PTYS; n++) {
		if(devpts_list[n].inode == i) {
			break;
		}
	}
	tty->driver_data = (void *)&devpts_list[n];
	i->i_uid = current->uid;
	i->i_gid = current->gid;
	minor = i->inode - (DEVPTS_ROOT_INO + 1);
	i->rdev = MKDEV(PTY_SLAVE_MAJOR, minor);
	SET_MINOR(pty_slave_device.minors, minor);
	register_device(CHR_DEV, &pty_slave_device);
	tty->flags &= ~TTY_OTHER_CLOSED;
	return 0;
}

int pty_close(struct tty *tty)
{
	struct devpts_files *dp;
	struct inode *i;
	int minor;

	if(MAJOR(tty->dev) == PTY_SLAVE_MAJOR) {
		if(tty->count > 2) {
			return 0;
		}
	}

	tty->flags |= TTY_OTHER_CLOSED;
	wakeup(&tty->read_q);
	wakeup(&pty_read);
	wakeup(&do_select);
	if(MAJOR(tty->dev) == PTY_SLAVE_MAJOR) {
		minor = MINOR(tty->dev);
		CLEAR_MINOR(pty_slave_device.minors, minor);
		unregister_device(CHR_DEV, &pty_slave_device);
		devfs_remove_node(MKDEV(PTY_SLAVE_MAJOR, minor));
		dp = (struct devpts_files *)tty->driver_data;
		i = (struct inode *)dp->inode;
		if(tty->count < 2) {
			if(tty->link) {
				tty->link->count--;	/* /dev/ptmx */
			}
			unregister_tty(tty);
			i->i_nlink--;
			iput(i);
		}
	}
	return 0;
}

/* FNX: both ends of a pty pair share the slave tty as f->private_data
 * (set in tty_open's PTMX_DEV block), so ALL data - master writes AND
 * slave writes - lands in that tty's read_q and is cooked into cooked_q
 * by do_cook. Reading must therefore consume cooked_q (like tty_read),
 * not write_q, or a read on either end never sees the other's data. */
int pty_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	struct tty *tty;
	unsigned char ch;
	int n;

	tty = f->private_data;


	n = 0;
	while(n < count) {
		if(tty->cooked_q.count > 0) {
			ch = charq_getchar(&tty->cooked_q);
			buffer[n++] = ch;
			continue;
		}
		if(n) {
			break;
		}
		if(tty->flags & TTY_OTHER_CLOSED) {
			n = -EIO;
			break;
		}
		if(sleep(&pty_read, PROC_INTERRUPTIBLE)) {
			n = -EINTR;
			break;
		}
	}
	wakeup(&tty->cooked_q);
	wakeup(&do_select);
	return n;
}

int pty_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	struct tty *tty;
	unsigned char ch;
	int n;

	tty = f->private_data;


	n = 0;
	while(n < count) {
		ch = buffer[n++];
		if(charq_room(&tty->read_q) > 0) {
			charq_putchar(&tty->read_q, ch);
		} else {
			break;
		}
	}
	tty->input(tty);
	wakeup(&do_select);
	wakeup(&pty_read);
	return n;
}

int pty_ioctl(struct tty *tty, struct fd *f, int cmd, addr_t arg)
{
	switch(cmd) {
		case TIOCGPTN:
		{
			unsigned int *val = (unsigned int *)arg;
			*val = MINOR(tty->dev);
			break;
		}
		case TIOCSPTLCK:
		{
			int val = *(unsigned int *)arg;
			if(val) {
				tty->flags |= TTY_PTY_LOCK;
			} else {
				tty->flags &= ~TTY_PTY_LOCK;
			}
			break;
		}
		default:
			return -EINVAL;
	}
	return 0;
}

int pty_select(struct inode *i, struct fd *f, int flag)
{
	struct tty *tty;

	tty = f->private_data;

	switch(flag) {
		case SEL_R:
			if(tty->write_q.count) {
				return 1;
			}
			if(tty->flags & TTY_OTHER_CLOSED) {
				return 1;
			}
			break;
		case SEL_W:
			if(!tty->read_q.count) {
				return 1;
			}
			break;
	}
	return 0;
}

/* devfs clone: materialize the /dev/pts/N runtime node for the pty slave
 * the pty_open just allocated. Runs right after chr_dev_open, so the
 * highest set minor in pty_slave_device.minors is the fresh one. The node
 * is a plain devfs char node whose rdev dispatches through the
 * pty_slave_device (chr_dev_open). pty_close removes it again. */
static int pty_devfs_clone(__dev_t dev)
{
	char name[8];
	int minor;

	for(minor = NR_PTYS - 1; minor >= 0; minor--) {
		if(TEST_MINOR(pty_slave_device.minors, minor)) {
			break;
		}
	}
	if(minor < 0) {
		return -ENXIO;
	}
	sprintk(name, "pts/%d", minor);
	devfs_make_node(name, MKDEV(PTY_SLAVE_MAJOR, minor), S_IFCHR | S_IRUSR | S_IWUSR);
	return 0;
}

void pty_init(void)
{
	struct tty *tty;

	SET_MINOR(pty_master_device.minors, PTY_MASTER_MINOR);
	if((tty = register_tty(MKDEV(PTY_MASTER_MAJOR, PTY_MASTER_MINOR)))) {
		tty->open = pty_open;
		tty->close = pty_close;
		if(register_device(CHR_DEV, &pty_master_device)) {
			printk("WARNING: %s(): unable to register '%s' device.\n", __FUNCTION__, pty_master_device.name);
			unregister_tty(tty);
			return;
		}
		devfs_make_clone("ptmx", MKDEV(PTY_MASTER_MAJOR, PTY_MASTER_MINOR), S_IFCHR | S_IRUSR | S_IWUSR, pty_devfs_clone);
		/* devfs directory node that userland init mounts devpts onto */
		devfs_make_node("pts", 0, S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		printk("ptmx      -\t\t    -\ttype=UNIX98, ptys=%d\n", NR_PTYS);
	} else {
		printk("WARNING: %s(): unable to register %s.\n", __FUNCTION__, pty_master_device.name);
	}
}
#endif /* CONFIG_UNIX98_PTYS */
