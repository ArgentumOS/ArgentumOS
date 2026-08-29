/*
 * fnx/include/fnx/fs_devfs.h
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_FS_DEVFS_H
#define _FNX_FS_DEVFS_H

#include <fnx/types.h>

#define DEVFS_ROOT_INO		1	/* root inode */
#define DEVFS_SUPER_MAGIC	0x5DE5	/* "devfs" */

/* Inode-number encoding: a devfs inode carries its whole identity in the
 * inode number (device number + char/block type), so read_inode() can
 * re-synthesize it on demand and no devfs inode is ever cached (pseudo-fs
 * inodes are dropped after the last iput). Device numbers are 16-bit
 * (major << 8 | minor), so: ino = BASE + (dev << 1) + is_block. */
#define DEVFS_INO_BASE		0x60000000
#define DEVFS_INO(dev, is_blk)	(DEVFS_INO_BASE + ((__dev_t)(dev) << 1) + ((is_blk) ? 1 : 0))

/* devfs node registry - the make_dev() analog. Drivers call
 * devfs_make_node() at probe time for every device node they own; devfs
 * synthesizes /dev from this list. */
struct devfs_node {
	char name[16];			/* node name ("null", "ttyS0", ...) */
	__dev_t dev;			/* device number (major << 8 | minor) */
	__mode_t mode;			/* S_IFCHR|0600, S_IFBLK|0600, ... */
	struct devfs_node *next;
};

extern struct devfs_node *devfs_nodes;
extern struct fs_operations devfs_fsop;
extern struct fs_operations devfs_dir_fsop;

int devfs_make_node(const char *, __dev_t, __mode_t);
void devfs_remove_node(__dev_t);
struct devfs_node *devfs_find_node(const char *);
struct devfs_node *devfs_find_node_dev(__dev_t);

/* devfs filesystem operations */
int devfs_dir_open(struct inode *, struct fd *);
int devfs_dir_close(struct inode *, struct fd *);
int devfs_dir_read(struct inode *, struct fd *, char *, __size_t);
int devfs_readdir(struct inode *, struct fd *, struct dirent *, __size_t);
int devfs_readdir64(struct inode *, struct fd *, struct dirent64 *, __size_t);
int devfs_lookup(const char *, struct inode *, struct inode **);
int devfs_read_inode(struct inode *);
void devfs_statfs(struct superblock *, struct statfs *);
int devfs_read_superblock(__dev_t, struct superblock *);
int devfs_init(void);
int devfs_boot_mount(void);

#endif /* _FNX_FS_DEVFS_H */
