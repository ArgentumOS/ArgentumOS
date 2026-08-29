/*
 * fnx/fs/devfs/super.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
#include <fnx/mm.h>
#include <fnx/sched.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

struct fs_operations devfs_fsop = {
	0,
	DEVFS_DEV,

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

	devfs_read_inode,
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	devfs_statfs,
	devfs_read_superblock,
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

void devfs_statfs(struct superblock *sb, struct statfs *buf)
{
	buf->f_type = DEVFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = 0;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_namelen = 16;
}

int devfs_read_superblock(__dev_t dev, struct superblock *sb)
{
	superblock_lock(sb);
	sb->dev = dev;
	sb->fsop = &devfs_fsop;
	sb->s_blocksize = PAGE_SIZE;

	if(!(sb->root = iget(sb, DEVFS_ROOT_INO))) {
		printk("WARNING: %s(): unable to get root inode.\n", __FUNCTION__);
		superblock_unlock(sb);
		return -EINVAL;
	}

	superblock_unlock(sb);
	return 0;
}

int devfs_init(void)
{
	return register_filesystem("devfs", &devfs_fsop);
}

/* Kernel-side mount of devfs on the rootfs /dev directory, called right
 * after mount_root() in the kswapd boot flow (before init runs), so the
 * init trampoline's open("/dev/console") resolves through devfs. Mirrors
 * sys_mount()'s steps but runs in kernel context. Fails soft (warn, no
 * panic): an initrd root has no /dev directory and simply boots without
 * devfs. */
int devfs_boot_mount(void)
{
	struct inode *i_target;
	struct mount *mp;
	struct filesystems *fs;
	int errno;

	if(!(fs = get_filesystem("devfs"))) {
		printk("devfs: filesystem not registered.\n");
		return -ENODEV;
	}

	if((errno = namei("/dev", &i_target, NULL, FOLLOW_LINKS))) {
		printk("devfs: cannot find /dev on the root filesystem (%d).\n", errno);
		return errno;
	}
	if(!S_ISDIR(i_target->i_mode)) {
		iput(i_target);
		return -ENOTDIR;
	}

	if(!(mp = add_mount_point(DEVFS_DEV, "devfs", "/dev"))) {
		iput(i_target);
		return -EBUSY;
	}

	if(fs->fsop->read_superblock(DEVFS_DEV, &mp->sb)) {
		iput(i_target);
		del_mount_point(mp);
		return -EINVAL;
	}

	mp->sb.dir = i_target;
	mp->fs = fs;
	fs->mp = mp;
	i_target->mount_point = mp->sb.root;
	/* hold a reference on the mounted-on inode: mp->sb.dir keeps it
	 * alive, otherwise the free-list recycle could clear its dev/ino
	 * while mp->sb.dir and the stale mount_point still point at it */
	mp->sb.dir->count++;
	iput(i_target);

	{
		struct devfs_node *n;
		int count = 0;
		for(n = devfs_nodes; n; n = n->next) {
			count++;
		}
		printk("devfs mounted on /dev. (%d nodes)\n", count);
	}
	return 0;
}
