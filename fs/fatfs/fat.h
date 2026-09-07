/*
 * fnx/fs/fatfs/fat.h — driver-private definitions for the FNX-native
 * FAT12/16/32 + exFAT driver (docs/design/fatfs-driver-plan.md).
 *
 * Only fs/fatfs sources include this. The c89-visible sb/inode info lives in
 * include/fnx/fs_fat.h. All on-disk fields are little-endian.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FATFS_FAT_H
#define _FATFS_FAT_H

/* FAT32 directory-entry attributes */
#define FAT_ATTR_READONLY	0x01
#define FAT_ATTR_HIDDEN		0x02
#define FAT_ATTR_SYSTEM		0x04
#define FAT_ATTR_VOLUME_ID	0x08
#define FAT_ATTR_DIRECTORY	0x10
#define FAT_ATTR_ARCHIVE	0x20
#define FAT_ATTR_LFN		0x0F	/* LFN entry marker */

#define FAT_ENTRY_DELETED	0xE5	/* first name byte: deleted entry */
#define FAT_ENTRY_END		0x00	/* first name byte: end of dir */

/* cluster marks (FAT32 EOC high-water) */
#define FAT_CLUST_LAST		0x0FFFFFF8
#define FAT_CLUST_BAD		0x0FFFFFF7

/* per-inode cache entry: rebuilds evicted inodes (cluster -> identity).
 * Populated by the dir scanner before iget, kept for the mount. parent +
 * slot locate the entry's directory slot so write_inode/rename/unlink can
 * rewrite or remove it. */
struct fatfs_ent {
	__ino_t ino;			/* entry inode number */
	__u32 cluster;			/* first cluster (0 = empty file) */
	__u32 size;			/* file size */
	__u32 parent;			/* parent dir first cluster (0 = n/a) */
	unsigned long slot;		/* 0-based slot of the short entry */
	unsigned char is_dir;
	unsigned char used;
};

/* driver-private superblock view (sb->u.fatfs.cache points here) */
struct fatfs_cache {
	struct fatfs_ent *ents;
	unsigned int used;
	unsigned int cap;
};

/* cluster -> device block helper (512-byte blocks, partition-relative) */
static __blk_t fat_cluster_sector(struct superblock *sb, __u32 cluster,
				  unsigned int within)
{
	struct fatfs_sb_info *f = &sb->u.fatfs;
	return (__blk_t)(f->data_sector +
			 ((__u64)(cluster - 2) * f->sects_per_cluster) +
			 within);
}

/* fsop tables (dir/file inodes get different tables) */
extern struct fs_operations fatfs_fsop;		/* dir + sb operations */
extern struct fs_operations fatfs_file_fsop;	/* regular file ops */

/* super.c */
int fatfs_init(void);
int fatfs_ent_find(struct superblock *sb, __ino_t ino, struct fatfs_ent **out);
int fatfs_ent_add(struct superblock *sb, __ino_t ino, __u32 cluster,
		  __u32 size, unsigned char is_dir, __u32 parent,
		  unsigned long slot);
__u32 fat_next_cluster(struct superblock *sb, __u32 cluster);
int fat_alloc_cluster(struct superblock *sb, __u32 *cluster);
int fat_read_entry(struct superblock *sb, __u32 cl, __u32 *val);
void fat_set_eoc(struct superblock *sb, __u32 cluster);
void fatfs_ent_remove(struct superblock *sb, __ino_t ino);
int fat_free_chain(struct superblock *sb, __u32 first);

/* write.c */
int fat_write(struct inode *, struct fd *, const char *, __size_t);
int fat_write_inode(struct inode *);
int fat_truncate(struct inode *, __off_t);
int fat_create(struct inode *, char *, int, __mode_t, struct inode **);
int fat_mkdir(struct inode *, char *, __mode_t);
int fat_unlink(struct inode *, struct inode *, char *);
int fat_rmdir(struct inode *, struct inode *);
int fat_rename(struct inode *i, struct inode *dir, struct inode *i_new,
	       struct inode *dir_new, char *oldname, char *newname);

/* dir.c */
int fat_readdir(struct inode *, struct fd *, struct dirent *, __size_t);
int fat_readdir64(struct inode *, struct fd *, struct dirent64 *, __size_t);
int fat_lookup(const char *, struct inode *, struct inode **);
int fat_dir_has_name(struct inode *, const char *);

/* file.c */
__blk_t fat_bmap(struct inode *, __off_t, int);
__loff_t fat_file_llseek(struct inode *, __loff_t);

#endif /* _FATFS_FAT_H */
