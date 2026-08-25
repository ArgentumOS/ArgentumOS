/*
 * fnx/fs/devpts/dir.c
 *
 * Copyright 2025, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devpts.h>
#include <fnx/dirent.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

#ifdef CONFIG_UNIX98_PTYS
int devpts_readdir64(struct inode *, struct fd *, struct dirent64 *, __size_t);

struct fs_operations devpts_dir_fsop = {
	0,
	0,

	devpts_dir_open,
	devpts_dir_close,
	devpts_dir_read,
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	devpts_readdir,
	devpts_readdir64,
	NULL,			/* mmap */
	NULL,			/* select */

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	devpts_lookup,
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

int devpts_dir_open(struct inode *i, struct fd *f)
{
	f->offset = 0;
	return 0;
}

int devpts_dir_close(struct inode *i, struct fd *f)
{
	return 0;
}

int devpts_dir_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	return -EISDIR;
}

int devpts_readdir(struct inode *i, struct fd *f, struct dirent *dirent, __size_t count)
{
	unsigned int offset;
	int rec_len, name_len;
	__size_t total_read;
	int base_dirent_len;
	char *name, numstr[10 + 1];

	base_dirent_len = sizeof(dirent->d_ino) + sizeof(dirent->d_off) + sizeof(dirent->d_reclen);

	offset = f->offset;
	total_read = 0;
	memset_b(numstr, 0, sizeof(numstr));

	while(offset < NR_PTYS && count > 0) {
		if(offset == 0) {
			name = ".";
			dirent->d_ino = DEVPTS_ROOT_INO;
		} else if(offset == 1) {
			name = "..";
			dirent->d_ino = DEVPTS_ROOT_INO;
		} else {
			if(devpts_list[offset - 2].count) {
				dirent->d_ino = devpts_list[offset - 2].inode->inode;
				sprintk(numstr, "%d", offset - 2);
				name = numstr;
			} else {
				offset++;
				continue;
			}
		}
		name_len = strlen(name);
		rec_len = (base_dirent_len + (name_len + 1)) + 3;
		rec_len &= ~3;	/* round up */
		if(total_read + rec_len < count) {
			dirent->d_off = offset;
			dirent->d_reclen = rec_len;
			memcpy_b(dirent->d_name, name, name_len);
			dirent->d_name[name_len] = 0;
			dirent = (struct dirent *)((char *)dirent + rec_len);
			total_read += rec_len;
			count -= rec_len;
		} else {
			count = 0;
		}
		offset++;
	}
	f->offset = offset;
	return total_read;
}
/* FNX: getdents64 view of /dev/pts (the 32-bit devpts_readdir above is
 * only used by the i386 ABI; native x86-64 userspace needs dirent64). */
int devpts_readdir64(struct inode *i, struct fd *f, struct dirent64 *dirent, __size_t count)
{
	unsigned int offset;
	int rec_len, name_len, type;
	__size_t total_read;
	int base_dirent_len;
	char *name, numstr[10 + 1];

	base_dirent_len = sizeof(dirent->d_ino) + sizeof(dirent->d_off) +
		sizeof(dirent->d_reclen) + sizeof(dirent->d_type);

	offset = f->offset;
	total_read = 0;
	memset_b(numstr, 0, sizeof(numstr));

	while(offset < NR_PTYS && count > 0) {
		if(offset == 0) {
			name = ".";
			dirent->d_ino = DEVPTS_ROOT_INO;
		} else if(offset == 1) {
			name = "..";
			dirent->d_ino = DEVPTS_ROOT_INO;
		} else {
			if(devpts_list[offset - 2].count) {
				dirent->d_ino = devpts_list[offset - 2].inode->inode;
				sprintk(numstr, "%d", offset - 2);
				name = numstr;
			} else {
				offset++;
				continue;
			}
		}
		name_len = strlen(name);
		rec_len = (base_dirent_len + (name_len + 1)) + 3;
		rec_len &= ~3;	/* round up */
		if(total_read + rec_len < count) {
			dirent->d_off = offset;
			dirent->d_reclen = rec_len;
			type = (offset < 2) ? DT_DIR : DT_CHR;
			dirent->d_type = type;
			memcpy_b(dirent->d_name, name, name_len);
			dirent->d_name[name_len] = 0;
			dirent = (struct dirent64 *)((char *)dirent + rec_len);
			total_read += rec_len;
			count -= rec_len;
		} else {
			count = 0;
		}
		offset++;
	}
	f->offset = offset;
	return total_read;
}
#endif /* CONFIG_UNIX98_PTYS */
