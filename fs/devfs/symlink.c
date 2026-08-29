/*
 * fnx/fs/devfs/symlink.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * devfs symlink operations. A symlink node carries its target in
 * devfs_node.target; followlink() resolves it within devfs via parse_namei.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devfs.h>
#include <fnx/mm.h>
#include <fnx/stat.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

struct fs_operations devfs_symlink_fsop = {
	0,
	0,

	NULL,			/* open */
	NULL,			/* close */
	NULL,			/* read */
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */

	devfs_readlink,
	devfs_followlink,
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

int devfs_readlink(struct inode *i, char *buffer, __size_t count)
{
	struct devfs_node *n;
	int size_read;

	if(!(n = i->u.devfs.node) || !n->target) {
		return -EINVAL;
	}
	size_read = strlen(n->target);
	if(size_read > count) {
		size_read = count;
	}
	memcpy_b(buffer, n->target, size_read);
	return size_read;
}

int devfs_followlink(struct inode *dir, struct inode *i, struct inode **i_res)
{
	struct devfs_node *n;
	__ino_t errno;

	if(!i) {
		return -ENOENT;
	}
	if(!(S_ISLNK(i->i_mode))) {
		printk("%s(): Oops, inode '%d' is not a symlink (!?).\n", __FUNCTION__, i->inode);
		return 0;
	}
	if(!(n = i->u.devfs.node) || !n->target) {
		iput(i);
		return -EINVAL;
	}

	/* resolve the target within devfs (targets are devfs-relative) */
	iput(i);
	if((errno = parse_namei(n->target, dir->sb->root, i_res, NULL, FOLLOW_LINKS))) {
		return errno;
	}
	return 0;
}
