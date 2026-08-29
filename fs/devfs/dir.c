/*
 * fnx/fs/devfs/dir.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * devfs directory operations. The directory is synthesized from the node
 * registry: "." (offset 0), ".." (offset 1), then one entry per registered
 * node. Both the 32-bit (readdir) and 64-bit (readdir64) views are
 * provided; native x86-64 userspace uses getdents64.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devfs.h>
#include <fnx/dirent.h>
#include <fnx/stat.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

struct fs_operations devfs_dir_fsop = {
	0,
	0,

	devfs_dir_open,
	devfs_dir_close,
	devfs_dir_read,
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	devfs_readdir,
	devfs_readdir64,
	NULL,			/* mmap */
	NULL,			/* select */

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	devfs_lookup,
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

int devfs_dir_open(struct inode *i, struct fd *f)
{
	f->offset = 0;
	return 0;
}

int devfs_dir_close(struct inode *i, struct fd *f)
{
	return 0;
}

int devfs_dir_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	return -EISDIR;
}

/* the node at registry index idx (0-based), or NULL */
static struct devfs_node *devfs_node_at(unsigned int idx)
{
	struct devfs_node *n;

	for(n = devfs_nodes; n && idx; n = n->next) {
		idx--;
	}
	return n;
}

static unsigned int devfs_node_count(void)
{
	unsigned int count;
	struct devfs_node *n;

	for(count = 0, n = devfs_nodes; n; n = n->next) {
		count++;
	}
	return count;
}

int devfs_readdir(struct inode *i, struct fd *f, struct dirent *dirent, __size_t count)
{
	unsigned int offset, ncount;
	int rec_len, name_len;
	__size_t total_read;
	int base_dirent_len;
	char *name;
	__ino_t ino;
	struct devfs_node *n;

	base_dirent_len = sizeof(dirent->d_ino) + sizeof(dirent->d_off) + sizeof(dirent->d_reclen);

	offset = f->offset;
	total_read = 0;
	ncount = devfs_node_count();

	/* a non-root dir node (e.g. /dev/pts) is empty until a filesystem
	 * is mounted on it - only "." and ".." */
	if(i->inode != DEVFS_ROOT_INO) {
		ncount = 0;
	}

	while(offset < (ncount + 2) && count > 0) {
		if(offset == 0) {
			name = ".";
			ino = DEVFS_ROOT_INO;
		} else if(offset == 1) {
			name = "..";
			ino = DEVFS_ROOT_INO;
		} else {
			if(!(n = devfs_node_at(offset - 2))) {
				break;
			}
			name = n->name;
			ino = DEVFS_INO(n->dev, S_ISBLK(n->mode));
		}
		name_len = strlen(name);
		rec_len = (base_dirent_len + (name_len + 1)) + 3;
		rec_len &= ~3;	/* round up */
		if(total_read + rec_len <= count) {
			dirent->d_ino = ino;
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

int devfs_readdir64(struct inode *i, struct fd *f, struct dirent64 *dirent, __size_t count)
{
	unsigned int offset, ncount;
	int rec_len, name_len, type;
	__size_t total_read;
	int base_dirent_len;
	char *name;
	__ino_t ino;
	struct devfs_node *n;

	base_dirent_len = sizeof(dirent->d_ino) + sizeof(dirent->d_off) +
		sizeof(dirent->d_reclen) + sizeof(dirent->d_type);

	offset = f->offset;
	total_read = 0;
	ncount = devfs_node_count();

	/* a non-root dir node (e.g. /dev/pts) is empty until a filesystem
	 * is mounted on it - only "." and ".." */
	if(i->inode != DEVFS_ROOT_INO) {
		ncount = 0;
	}

	while(offset < (ncount + 2) && count > 0) {
		if(offset == 0) {
			name = ".";
			ino = DEVFS_ROOT_INO;
			type = DT_DIR;
		} else if(offset == 1) {
			name = "..";
			ino = DEVFS_ROOT_INO;
			type = DT_DIR;
		} else {
			if(!(n = devfs_node_at(offset - 2))) {
				break;
			}
			name = n->name;
			ino = DEVFS_INO(n->dev, S_ISBLK(n->mode));
			type = S_ISDIR(n->mode) ? DT_DIR : (S_ISBLK(n->mode) ? DT_BLK : DT_CHR);
		}
		name_len = strlen(name);
		rec_len = (base_dirent_len + (name_len + 1)) + 3;
		rec_len &= ~3;	/* round up */
		if(total_read + rec_len <= count) {
			dirent->d_ino = ino;
			dirent->d_off = offset;
			dirent->d_reclen = rec_len;
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
