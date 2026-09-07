/*
 * fnx/include/fnx/fs_fat.h
 *
 * FNX native FAT12/16/32 + exFAT filesystem driver (fs/fatfs/),
 * docs/design/fatfs-driver-plan.md.
 *
 * c89-safe on purpose: this header is included from the core include/fnx/
 * fs.h and must not drag in foreign types. The driver is FNX-native; the
 * vendored FatFs (third_party/fatfs) is kept only as an on-disk-format
 * reference and is never linked or compiled into the kernel.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_FS_FAT_H
#define _FNX_FS_FAT_H

/* FAT inode identity: i_ino = the entry's first cluster (the natural
 * stable FAT identity - a file keeps its cluster chain across renames).
 * FAT_ROOT_INO = 1 stands for the volume root. */
#define FAT_ROOT_INO		1
#define FAT_MAX_CLUSTER		0x0FFFFFF5	/* FAT32 high water mark */

/* per-superblock info (struct superblock's u union): BPB-derived volume
 * geometry + the driver-side dirent table used to rebuild evicted
 * inodes (see the plan doc). */
struct fatfs_sb_info {
	/* geometry from the boot sector / BPB */
	__u32 total_sectors;		/* volume size in sectors */
	__u32 fat_sectors;		/* sectors per FAT */
	__u32 root_cluster;		/* FAT32/16/12 root dir start cluster
					   (0 = fixed root area) */
	__u32 data_sector;		/* first data sector */
	__u16 bytes_per_sector;		/* 512 */
	__u16 root_dir_sectors;		/* FAT12/16 fixed root dir size */
	unsigned char sects_per_cluster;
	unsigned char n_fats;
	unsigned char fs_type;		/* 12/16/32/64( =exFAT) */
	/* driver-side cluster->entry cache (readdir/lookup register
	 * entries before iget; read_inode rebuilds evicted inodes). */
	void *cache;			/* struct fatfs_dir_cache * */
};

/* per-inode info (struct inode's u union) */
struct fatfs_i_info {
	__u32 cluster;			/* first cluster (root: 0) */
	unsigned char is_dir;		/* directory inode */
};

#endif /* _FNX_FS_FAT_H */
