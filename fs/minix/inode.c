/*
 * fnx/fs/minix/inode.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_minix.h>
#include <fnx/fs_pipe.h>
#include <fnx/statfs.h>
#include <fnx/sleep.h>
#include <fnx/stat.h>
#include <fnx/sched.h>
#include <fnx/buffer.h>
#include <fnx/process.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#ifdef CONFIG_FS_MINIX
int minix_read_inode(struct inode *i)
{
	if(i->sb->u.minix.version == 1) {
		return v1_minix_read_inode(i);
	}

	return v2_minix_read_inode(i);
}

int minix_write_inode(struct inode *i)
{
	if(i->sb->u.minix.version == 1) {
		return v1_minix_write_inode(i);
	}

	return v2_minix_write_inode(i);
}

int minix_ialloc(struct inode *i, int mode)
{
	if(i->sb->u.minix.version == 1) {
		return v1_minix_ialloc(i, mode);
	}

	return v2_minix_ialloc(i, mode);
}

void minix_ifree(struct inode *i)
{
	if(i->sb->u.minix.version == 1) {
		return v1_minix_ifree(i);
	}

	return v2_minix_ifree(i);
}

int minix_bmap(struct inode *i, __off_t offset, int mode)
{
	if(i->sb->u.minix.version == 1) {
		return v1_minix_bmap(i, offset, mode);
	}

	return v2_minix_bmap(i, offset, mode);
}

int minix_truncate(struct inode *i, __off_t length)
{
	if(i->sb->u.minix.version == 1) {
		return v1_minix_truncate(i, length);
	}

	return v2_minix_truncate(i, length);
}
#endif /* CONFIG_FS_MINIX */
