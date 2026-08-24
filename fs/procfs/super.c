/*
 * fnx/fs/procfs/super.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_proc.h>
#include <fnx/stat.h>
#include <fnx/mm.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

struct fs_operations procfs_fsop = {
	0,
	PROC_DEV,

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

	procfs_read_inode,
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	procfs_statfs,
	procfs_read_superblock,
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

int procfs_read_superblock(__dev_t dev, struct superblock *sb)
{
	superblock_lock(sb);
	sb->dev = dev;
	sb->fsop = &procfs_fsop;
	sb->s_blocksize = PAGE_SIZE;

	if(!(sb->root = iget(sb, PROC_ROOT_INO))) {
		printk("WARNING: %s(): unable to get root inode.\n", __FUNCTION__);
		superblock_unlock(sb);
		return -EINVAL;
	}
	sb->root->u.procfs.i_lev = 0;

	superblock_unlock(sb);
	return 0;
}

int procfs_init(void)
{
	return register_filesystem("proc", &procfs_fsop);
}
