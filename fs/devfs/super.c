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

/* boot-time alias table (rules-lite): symlinks that reference other
 * devfs nodes. Targets are devfs-relative and resolved at access time via
 * followlink(), so the target nodes need not exist yet. */
static void devfs_aliases(void)
{
	/* Q3 topology skeleton (docs/devfs-topology.md): real nodes live
	 * under bus/role dirs; the flat /dev names survive as top-level
	 * symlinks (role aliases) so kernel + userland keep resolving
	 * while consumers migrate to the topology paths. */
	devfs_make_dir("Memory");
	devfs_make_dir("TTY");
	devfs_make_dir("Serial");
	devfs_make_dir("PTS");
	devfs_make_dir("PS2");
	devfs_make_dir("USB");
	devfs_make_dir("Display");
	devfs_make_dir("Audio");
	devfs_make_dir("Disk");
	devfs_make_dir("Disk/IDE");
	devfs_make_dir("Disk/AHCI");
	devfs_make_dir("Disk/SCSI");
	devfs_make_dir("Disk/USB");
	devfs_make_dir("Disk/NVMe");
	devfs_make_dir("Disk/Floppy");
	devfs_make_dir("Disk/RAM");
	devfs_make_dir("Disk/by-identity");

	/* role aliases: fixed-name symlinks into the topology. The block
	 * unit symlinks (hda.., sda.., fd0, ram0, nvme0n1) are created by
	 * the per-bus block registration (probe-time mapping). */
	devfs_make_symlink("console", "TTY/console", 0777);
	devfs_make_symlink("tty", "TTY/tty", 0777);
	devfs_make_symlink("ptmx", "PTS/ptmx", 0777);
	devfs_make_symlink("pts", "PTS/pts", 0777);
	devfs_make_symlink("kbd", "PS2/Keyboard", 0777);
	devfs_make_symlink("mouse", "PS2/Mouse", 0777);
	devfs_make_symlink("psaux", "PS2/Mouse", 0777);
	devfs_make_symlink("fb0", "Display/fb0", 0777);
	devfs_make_symlink("dsp", "Audio/dsp", 0777);
	devfs_make_symlink("mem", "Memory/mem", 0777);
	devfs_make_symlink("kmem", "Memory/kmem", 0777);
	devfs_make_symlink("null", "Memory/null", 0777);
	devfs_make_symlink("port", "Memory/port", 0777);
	devfs_make_symlink("zero", "Memory/zero", 0777);
	devfs_make_symlink("full", "Memory/full", 0777);
	devfs_make_symlink("random", "Memory/random", 0777);
	devfs_make_symlink("urandom", "Memory/urandom", 0777);
}

int devfs_init(void)
{
	int errno;

	if((errno = register_filesystem("devfs", &devfs_fsop))) {
		return errno;
	}
	devfs_aliases();
	return 0;
}

/* Kernel-side mount of devfs on the rootfs /System/Devices directory,
 * called right after mount_root() in the kswapd boot flow (before init
 * runs), so the init trampoline's open("/System/Devices/console")
 * resolves through devfs. Mirrors sys_mount()'s steps but runs in
 * kernel context. Fails soft (warn, no panic): a root without
 * /System/Devices simply boots without devfs. */
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

	if((errno = namei("/System/Devices", &i_target, NULL, FOLLOW_LINKS))) {
		printk("devfs: cannot find /System/Devices on the root filesystem (%d).\n", errno);
		return errno;
	}
	if(!S_ISDIR(i_target->i_mode)) {
		iput(i_target);
		return -ENOTDIR;
	}

	if(!(mp = add_mount_point(DEVFS_DEV, "devfs", "/System/Devices"))) {
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
		printk("devfs mounted on /System/Devices. (%d nodes)\n", count);
	}
	return 0;
}
