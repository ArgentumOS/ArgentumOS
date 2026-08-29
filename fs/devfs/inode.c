/*
 * fnx/fs/devfs/inode.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * devfs inode synthesis. The inode number carries the identity
 * (device number + char/block type), so read_inode() can rebuild the
 * inode from the number alone. Device inodes get the generic
 * def_chr_fsop/def_blk_fsop: chr_dev_open()/blk_dev_open() dispatch to the
 * real driver fsop via get_device() using i->rdev.
 */

#include <fnx/kernel.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>

int devfs_read_inode(struct inode *i)
{
	struct devfs_node *n;
	__dev_t raw, dev;
	__mode_t mode;
	__nlink_t nlink;

	if(i->inode == DEVFS_ROOT_INO) {
		mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		nlink = 2;
		i->fsop = &devfs_dir_fsop;
	} else {
		raw = (__dev_t)(i->inode - DEVFS_INO_BASE);
		dev = raw >> 1;
		if(!(n = devfs_find_node_dev(dev))) {
			return -ENOENT;
		}
		mode = n->mode;
		nlink = 1;
		i->u.devfs.node = n;
		if(S_ISLNK(mode)) {
			i->fsop = &devfs_symlink_fsop;
		} else if(n->flags & DEVFS_NODE_CLONE) {
			i->fsop = &devfs_clone_fsop;
		} else if(S_ISDIR(mode)) {
			/* directory node (e.g. /dev/pts): an empty dir until a
			 * filesystem (devpts) is mounted on it */
			i->fsop = &devfs_dir_fsop;
		} else if(S_ISBLK(mode)) {
			i->fsop = &def_blk_fsop;
		} else {
			i->fsop = &def_chr_fsop;
		}
	}

	i->i_mode = mode;
	i->i_uid = 0;
	i->i_size = 0;
	i->i_atime = CURRENT_TIME;
	i->i_ctime = CURRENT_TIME;
	i->i_mtime = CURRENT_TIME;
	i->i_gid = 0;
	i->i_nlink = nlink;
	i->i_blocks = 0;
	i->i_flags = 0;
	i->state = INODE_LOCKED;
	i->count = 1;
	i->rdev = (i->inode == DEVFS_ROOT_INO) ? 0 : (raw >> 1);
	return 0;
}
