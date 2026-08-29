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

/* the idx-th visible child entry of a devfs dir inode (0-based, children
 * only - "." and ".." are handled by the callers). The root yields the
 * first path components of every node (deduplicated); a nested dir node
 * (e.g. /dev/disk) yields the next components of the names under its own
 * prefix ("disk/by-id" -> "by-id"). *disp receives the child's display
 * name and the returned node carries its identity (the full node, e.g.
 * "disk/by-id", for the ino/lookup consistency). */
static struct devfs_node *devfs_child_at(struct inode *dir, unsigned int idx, char *disp)
{
	struct devfs_node *n, *m;
	const char *rest, *slash;
	char prefix[64];
	int plen, dlen;
	unsigned int count;
	int dup;

	if(dir->inode == DEVFS_ROOT_INO) {
		plen = 0;
	} else if(dir->u.devfs.node) {
		plen = strlen(dir->u.devfs.node->name);
		if(plen >= (int)sizeof(prefix) - 2) {
			return NULL;
		}
		memcpy_b(prefix, dir->u.devfs.node->name, plen);
		prefix[plen++] = '/';
		prefix[plen] = '\0';
	} else {
		return NULL;
	}

	count = 0;
	for(n = devfs_nodes; n; n = n->next) {
		if(strncmp(n->name, prefix, plen)) {
			continue;
		}
		rest = n->name + plen;
		slash = strchr(rest, '/');
		dlen = slash ? (int)(slash - rest) : (int)strlen(rest);
		dup = 0;
		for(m = devfs_nodes; m != n; m = m->next) {
			if(!strncmp(m->name, prefix, plen) && !strncmp(m->name + plen, rest, dlen) &&
			   (m->name[plen + dlen] == '\0' || m->name[plen + dlen] == '/')) {
				dup = 1;
				break;
			}
		}
		if(!dup && count++ == idx) {
			if(disp) {
				memcpy_b(disp, rest, dlen);
				disp[dlen] = '\0';
			}
			return n;
		}
	}
	return NULL;
}

static unsigned int devfs_child_count(struct inode *dir)
{
	unsigned int count;
	int i;

	for(count = 0; devfs_child_at(dir, count, NULL); count++) {
		;
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

	char disp[32];

	offset = f->offset;
	total_read = 0;
	ncount = devfs_child_count(i);

	while(offset < (ncount + 2) && count > 0) {
		if(offset == 0) {
			name = ".";
			ino = DEVFS_ROOT_INO;
		} else if(offset == 1) {
			name = "..";
			ino = DEVFS_ROOT_INO;
		} else {
			if(!(n = devfs_child_at(i, offset - 2, disp))) {
				break;
			}
			name = disp;
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

	char disp[32];

	offset = f->offset;
	total_read = 0;
	ncount = devfs_child_count(i);

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
			if(!(n = devfs_child_at(i, offset - 2, disp))) {
				break;
			}
			name = disp;
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
