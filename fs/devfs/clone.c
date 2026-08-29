/*
 * fnx/fs/devfs/clone.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * devfs clone devices (the /dev/ptmx -> /dev/pts/N pattern). A clone node
 * carries a clone_fn; opening it first runs the normal device open (for
 * ptmx: chr_dev_open -> the pty master tty -> pty_open allocates the
 * slave), then calls clone_fn to materialize the runtime node (pts/N) in
 * the devfs registry. The runtime node is a plain char node whose rdev
 * dispatches to the freshly allocated device; the driver removes it at
 * close (pty_close -> devfs_remove_node).
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devfs.h>
#include <fnx/devices.h>
#include <fnx/stat.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

int devfs_clone_open(struct inode *i, struct fd *f);
int devfs_clone_close(struct inode *i, struct fd *f);

struct fs_operations devfs_clone_fsop = {
	0,
	0,

	devfs_clone_open,
	devfs_clone_close,
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* ioctl */
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

int devfs_clone_open(struct inode *i, struct fd *f)
{
	struct devfs_node *n;
	int errno;

	if(!(n = i->u.devfs.node)) {
		return -EINVAL;
	}

	/* the underlying open first: for ptmx this allocates the pty slave
	 * (the fresh minor). */
	if((errno = chr_dev_open(i, f))) {
		return errno;
	}

	/* then materialize the runtime node (pts/N) via the clone_fn. */
	if(n->clone_fn) {
		if((errno = n->clone_fn(i->rdev))) {
			return errno;
		}
		n->clone_count++;
	}
	return 0;
}

int devfs_clone_close(struct inode *i, struct fd *f)
{
	struct devfs_node *n;

	if((n = i->u.devfs.node) && n->clone_count > 0) {
		n->clone_count--;
	}
	return 0;
}
