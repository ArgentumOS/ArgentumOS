/*
 * fnx/fs/fatfs/super.c — FNX-native FAT filesystem driver: registration,
 * read_superblock (BPB parse -> sb geometry), read_inode (identity via
 * the driver-side cluster cache), FAT-chain helper.
 *
 * M0: FAT32 read-only (docs/design/fatfs-driver-plan.md). fstype "fat"
 * autodetects the variant; exFAT + FAT12/16 land in M2/M3.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/string.h>
#include <fnx/buffer.h>
#include <fnx/kernel.h>
#include <fnx/stdio.h>
#include <fnx/devices.h>
#include <fnx/filesystems.h>
#include <fnx/fcntl.h>
#include <fnx/mm.h>
#include <fnx/sched.h>
#include "fat.h"

extern int file_read(struct inode *, struct fd *, char *, __size_t);

#define FAT_CACHE_INIT	64	/* dir-cache entries per mount */
#define FAT_CACHE_MAX	65536

/* DOS 8.3 name helpers (dir.c uses these too) */
#define FAT_NAMELEN	8
#define FAT_EXTLEN	3

/* ---- driver-side cluster cache ---- */

static struct fatfs_cache *fat_cache(struct superblock *sb)
{
	return (struct fatfs_cache *)sb->u.fatfs.cache;
}

int fatfs_ent_add(struct superblock *sb, __ino_t ino, __u32 cluster,
		  __u32 size, unsigned char is_dir, __u32 parent,
		  unsigned long slot, unsigned char contiguous)
{
	struct fatfs_cache *c = fat_cache(sb);
	struct fatfs_ent *ne;

	if(!c || !ino) {
		return 0;
	}
	if(fatfs_ent_find(sb, ino, NULL)) {
		return 0;	/* already known */
	}
	if(c->used == c->cap) {
		unsigned int ncap = c->cap ? c->cap * 2 : FAT_CACHE_INIT;
		struct fatfs_ent *na;

		if(ncap > FAT_CACHE_MAX) {
			return -ENOMEM;
		}
		if(!(na = (struct fatfs_ent *)
		     kmalloc(ncap * sizeof(struct fatfs_ent)))) {
			return -ENOMEM;
		}
		if(c->ents) {
			memcpy_b(na, c->ents, c->used * sizeof(struct fatfs_ent));
			kfree((addr_t)c->ents);
		}
		c->ents = na;
		c->cap = ncap;
	}
	ne = &c->ents[c->used++];
	ne->ino = ino;
	ne->cluster = cluster;
	ne->size = size;
	ne->parent = parent;
	ne->slot = slot;
	ne->is_dir = is_dir;
	ne->contiguous = contiguous;
	ne->used = 1;
	return 0;
}

int fatfs_ent_find(struct superblock *sb, __ino_t ino, struct fatfs_ent **out)
{
	struct fatfs_cache *c = fat_cache(sb);
	unsigned int n;

	if(!c) {
		return 0;
	}
	for(n = 0; n < c->used; n++) {
		if(c->ents[n].used && c->ents[n].ino == ino) {
			if(out) {
				*out = &c->ents[n];
			}
			return 1;
		}
	}
	return 0;
}

/* ---- FAT table access: next cluster of a chain ---- */

__u32 fat_next_cluster(struct superblock *sb, __u32 cluster)
{
	struct fatfs_sb_info *f = &sb->u.fatfs;
	struct buffer *buf;
	__blk_t sect;
	__u32 val;

	switch(f->fs_type) {
	case 32:
		sect = (__blk_t)f->fat_sector + cluster / 128;
		if(!(buf = bread(sb->dev, sect, 512))) {
			return FAT_CLUST_LAST;	/* treat I/O error as EOC */
		}
		val = ((__u32 *)buf->data)[cluster % 128] & 0x0FFFFFFF;
		brelse(buf);
		return val;
	case FAT_EXFAT:
		/* exFAT FAT entries are full 32-bit (no mask); the chain
		 * terminates at 0xFFFFFFFF or an out-of-range entry */
		sect = (__blk_t)f->fat_sector + cluster / 128;
		if(!(buf = bread(sb->dev, sect, 512))) {
			return FAT_CLUST_LAST;
		}
		val = ((__u32 *)buf->data)[cluster % 128];
		brelse(buf);
		if(val >= f->fat_n_fatent) {
			return 0xFFFFFFFF;	/* EOC (free entries are 0) */
		}
		return val;
	default:
		return FAT_CLUST_LAST;
	}
}

/* ---- read_inode: rebuild an inode from the cache ---- */

static int fat_read_inode(struct inode *inode)
{
	struct fatfs_ent *e;

	if(inode->inode == FAT_ROOT_INO) {
		inode->i_mode = S_IFDIR | 0755;
		inode->i_uid = 0;
		inode->i_size = 0;
		inode->i_gid = 0;
		inode->i_nlink = 2;
		inode->i_blocks = 0;
		inode->u.fatfs.cluster = inode->sb->u.fatfs.root_cluster;
		inode->u.fatfs.is_dir = 1;
		inode->u.fatfs.contiguous = 0;
		inode->fsop = &fatfs_fsop;
		inode->count = 1;
		return 0;
	}
	if(!fatfs_ent_find(inode->sb, inode->inode, &e)) {
		return -ENOENT;	/* never scanned: not a known FAT entry */
	}
	if(e->is_dir) {
		inode->i_mode = S_IFDIR | 0755;
		inode->i_nlink = 2;
	} else {
		inode->i_mode = S_IFREG | 0644;
		inode->i_nlink = 1;
	}
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_size = e->size;
	inode->i_blocks = (e->size + 511) / 512;
	inode->u.fatfs.cluster = e->cluster;
	inode->u.fatfs.is_dir = e->is_dir;
	inode->u.fatfs.contiguous = e->contiguous;
	inode->fsop = e->is_dir ? &fatfs_fsop : &fatfs_file_fsop;
	inode->count = 1;
	return 0;
}

static int fat_read_superblock(__dev_t dev, struct superblock *sb)
{
	struct fatfs_sb_info *f = &sb->u.fatfs;
	struct fatfs_cache *c;
	struct buffer *buf;
	unsigned char *b;
	__u16 bps, reserved, root_ents, fatsz16;
	__u32 tot32, fatsz32, root_cluster, data_sector;
	__u32 cluster_cnt = 0;
	unsigned char spc, nfats;
	int is_exfat = 0;

	if(!(buf = bread(dev, 0, 512))) {
		return -EIO;
	}
	b = buf->data;
	if(b[510] != 0x55 || b[511] != 0xAA) {
		brelse(buf);
		return -EINVAL;		/* not a FAT volume */
	}
	if(!memcmp(b + 3, "EXFAT   ", 8)) {
		/* ---- exFAT BPB: FatOffset 80, FatLength 84,
		 * ClusterHeapOffset 88, ClusterCount 92,
		 * RootDirCluster 96, shift fields 108/109, n_fats 110 */
		if((b[104] | (b[105] << 8)) != 0x0100) {
			brelse(buf);
			printk("fat: unsupported exFAT revision.\n");
			return -EINVAL;
		}
		if((1 << b[108]) != 512 || !b[109]) {
			brelse(buf);
			return -EINVAL;
		}
		if(b[110] != 1) {
			brelse(buf);
			return -EINVAL;		/* multi-FAT exFAT: refuse */
		}
		tot32 = b[72] | (b[73] << 8) | (b[74] << 16) |
			((__u32)b[75] << 24);
		fatsz32 = b[84] | (b[85] << 8) | (b[86] << 16) |
			  ((__u32)b[87] << 24);
		data_sector = b[88] | (b[89] << 8) | (b[90] << 16) |
			      ((__u32)b[91] << 24);
		cluster_cnt = b[92] | (b[93] << 8) | (b[94] << 16) |
			      ((__u32)b[95] << 24);
		root_cluster = b[96] | (b[97] << 8) | (b[98] << 16) |
			       ((__u32)b[99] << 24);
		bps = 512;
		spc = (unsigned char)(1 << b[109]);
		reserved = (__u16)(b[80] | (b[81] << 8));
		nfats = 1;
		root_ents = 0;
		if(root_cluster < 2 || cluster_cnt < 2 ||
		   data_sector + (__u64)cluster_cnt * spc > tot32) {
			brelse(buf);
			return -EINVAL;
		}
		f->fat_sector = b[80] | (b[81] << 8) | (b[82] << 16) |
				((__u32)b[83] << 24);
		is_exfat = 1;
	} else {
		bps = b[11] | (b[12] << 8);
		spc = b[13];
		reserved = b[14] | (b[15] << 8);
		nfats = b[16];
		root_ents = b[17] | (b[18] << 8);
		fatsz16 = b[22] | (b[23] << 8);
		tot32 = b[32] | (b[33] << 8) | (b[34] << 16) |
			((__u32)b[35] << 24);
		fatsz32 = b[36] | (b[37] << 8) | (b[38] << 16) |
			  ((__u32)b[39] << 24);
		if(bps != 512 || !spc || !nfats || !reserved) {
			brelse(buf);
			return -EINVAL;
		}
		if(!fatsz32) {
			brelse(buf);
			printk("fat: FAT12/16 not yet supported.\n");
			return -EINVAL;
		}
		root_cluster = b[44] | (b[45] << 8) | (b[46] << 16) |
			       ((__u32)b[47] << 24);
		data_sector = reserved + nfats * fatsz32 +
			      (root_ents * 32 + bps - 1) / bps;
		if(tot32 <= data_sector) {
			brelse(buf);
			return -EINVAL;
		}
		cluster_cnt = (tot32 - data_sector) / spc;
		if(cluster_cnt < 65525) {
			brelse(buf);
			return -EINVAL;
		}
		f->fat_sector = reserved;
	}
	f->total_sectors = tot32;
	f->fat_sectors = fatsz32;
	f->root_cluster = root_cluster;
	f->data_sector = data_sector;
	f->bytes_per_sector = bps;
	f->root_dir_sectors = 0;
	f->sects_per_cluster = spc;
	f->n_fats = nfats;
	f->fat_n_fatent = cluster_cnt + 2;
	f->bitmap_cluster = 0;
	f->fs_type = is_exfat ? FAT_EXFAT : 32;
	brelse(buf);

	if(is_exfat) {
		/* locate the allocation bitmap: the root dir's first
		 * cluster holds a 0x81 entry with its FirstCluster */
		struct buffer *rb;
		unsigned char *re;
		int i;

		if((rb = bread(dev, (__blk_t)f->data_sector +
			       (__u64)(root_cluster - 2) * spc, 512))) {
			re = rb->data;
			for(i = 0; i < 16; i++) {
				if(re[i * 32] == 0x00) {
					break;
				}
				if(re[i * 32] == 0x81) {
					f->bitmap_cluster =
						re[i * 32 + 20] |
						(re[i * 32 + 21] << 8) |
						(re[i * 32 + 22] << 16) |
						((__u32)re[i * 32 + 23] << 24);
					break;
				}
			}
			brelse(rb);
		}
	}

	if(!(c = (struct fatfs_cache *)kmalloc(sizeof(struct fatfs_cache)))) {
		return -ENOMEM;
	}
	memset_b(c, 0, sizeof(struct fatfs_cache));
	f->cache = (void *)c;
	sb->dev = dev;
	sb->fsop = &fatfs_fsop;
	sb->s_blocksize = 512;
	if(!(sb->root = iget(sb, FAT_ROOT_INO))) {
		kfree((addr_t)c);
		f->cache = NULL;
		return -EIO;
	}
	printk("fat: %s detected on device %d,%d (%d MB, %u sectors, %u bytes/cluster).\n",
	       is_exfat ? "exFAT" : "FAT32",
	       MAJOR(dev), MINOR(dev), tot32 / 2048, tot32,
	       (unsigned int)spc * bps);
	return 0;
}

static void fat_release_superblock(struct superblock *sb)
{
	struct fatfs_cache *c = fat_cache(sb);

	if(c) {
		if(c->ents) {
			kfree((addr_t)c->ents);
		}
		kfree((addr_t)c);
	}
	sb->u.fatfs.cache = NULL;
}

/* ---- open/close stubs ---- */

static int fat_open(struct inode *i, struct fd *f)
{
	f->offset = 0;
	if(S_ISREG(i->i_mode) && (f->flags & O_TRUNC)) {
		if(i->sb->u.fatfs.fs_type == FAT_EXFAT) {
			return -EROFS;	/* exFAT writes are M2b */
		}
		superblock_lock(i->sb);
		if(i->u.fatfs.cluster) {
			fat_free_chain(i->sb, i->u.fatfs.cluster);
			i->u.fatfs.cluster = 0;
		}
		i->i_size = 0;
		i->i_mtime = CURRENT_TIME;
		i->state |= INODE_DIRTY;
		superblock_unlock(i->sb);
	}
	return 0;
}

static int fat_close(struct inode *i, struct fd *f)
{
	return 0;
}

/* ---- fsop tables ---- */

int fatfs_init(void);

/* positional fsop table; trailing xattr/destroy/discard fields stay NULL */
struct fs_operations fatfs_fsop = {
	FSOP_REQUIRES_DEV,	/* flags */
	0,			/* fsdev */
	fat_open,		/* open */
	fat_close,		/* close */
	NULL,			/* read (dirs are not readable) */
	NULL,			/* write */
	NULL,			/* ioctl */
	NULL,			/* llseek */
	fat_readdir,		/* readdir */
	fat_readdir64,		/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */
	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	fat_lookup,		/* lookup */
	fat_rmdir,		/* rmdir */
	NULL,			/* link */
	fat_unlink,		/* unlink */
	NULL,			/* symlink */
	fat_mkdir,		/* mkdir */
	NULL,			/* mknod */
	NULL,			/* truncate */
	fat_create,		/* create */
	fat_rename,		/* rename */
	NULL,			/* read_block */
	NULL,			/* write_block */
	fat_read_inode,		/* read_inode */
	fat_write_inode,	/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	NULL,			/* statfs */
	fat_read_superblock,	/* read_superblock */
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	fat_release_superblock,	/* release_superblock */
	NULL,			/* getxattr */
	NULL,			/* setxattr */
	NULL,			/* listxattr */
	NULL,			/* removexattr */
	NULL,			/* destroy_inode */
	NULL,			/* discard_blocks */
};

/* regular files: the generic page-cache read path + llseek */
struct fs_operations fatfs_file_fsop = {
	FSOP_REQUIRES_DEV,	/* flags */
	0,			/* fsdev */
	fat_open,		/* open */
	fat_close,		/* close */
	file_read,		/* read (generic page-cache path) */
	fat_write,		/* write */
	NULL,			/* ioctl */
	fat_file_llseek,	/* llseek */
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	NULL,			/* mmap */
	NULL,			/* select */
	NULL,			/* readlink */
	NULL,			/* followlink */
	fat_bmap,		/* bmap */
	NULL,			/* lookup */
	NULL,			/* rmdir */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* mknod */
	fat_truncate,		/* truncate */
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
	NULL,			/* release_superblock */
	NULL,			/* getxattr */
	NULL,			/* setxattr */
	NULL,			/* listxattr */
	NULL,			/* removexattr */
	NULL,			/* destroy_inode */
	NULL,			/* discard_blocks */
};

int fatfs_init(void)
{
	return register_filesystem("fat", &fatfs_fsop);
}
