/*
 * fnx/fs/fatfs/exwrite.c — exFAT write support (M2b).
 *
 * exFAT free space lives in the ALLOCATION BITMAP (bit i = cluster i+2);
 * the FAT records chains only (full 32-bit entries, EOC 0xFFFFFFFF,
 * never masked). Newly written streams are FAT-LINKED (the contiguous
 * flag is never set): readers (FatFs included) accept both forms, and a
 * FAT chain is always extendable without moving data. Directory growth
 * allocates + FAT-links one cluster and bumps the directory's own stream
 * entry (DataLength/ValidDataLength) inside its parent set; the root
 * directory (no parent stream) just grows.
 *
 * Entry sets: File 0x85 (SecondaryCount +1, SetChecksum word at +2,
 * excluded from the sum), Stream 0xC0 (GenFlags +1, NameLength +3,
 * NameHash +4, FirstCluster +20, DataLength +24, ValidDataLength +8),
 * FileName 0xC1 (15 UTF-16 units at +2). Set allocation = a run of
 * (2 + ceil(len/15)) slots whose type bytes have bit 7 clear (deleted
 * sets or never-used space).
 *
 * ASCII name generation only (the write.c FAT32 limitation); NameHash
 * uses ASCII upcasing, byte-identical to FatFs for ASCII names.
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
#include <fnx/sched.h>
#include <fnx/fcntl.h>
#include <fnx/mm.h>
#include "fat.h"

#define EX_EOC		0xFFFFFFFFu

/* ================= allocation bitmap ================= */

static __blk_t ex_bit_sector(struct superblock *sb, __u32 cluster)
{
	struct fatfs_sb_info *f = &sb->u.fatfs;
	__u32 bit = cluster - 2;

	return (__blk_t)(f->data_sector +
			 ((__u64)(f->bitmap_cluster - 2) *
			  f->sects_per_cluster) + bit / 8 / 512);
}

static int ex_bit_set(struct superblock *sb, __u32 cluster, int val)
{
	struct buffer *buf;
	unsigned int byte, mask;
	__u32 bit = cluster - 2;

	if(!sb->u.fatfs.bitmap_cluster) {
		return -EIO;
	}
	if(!(buf = bread(sb->dev, ex_bit_sector(sb, cluster), 512))) {
		return -EIO;
	}
	byte = (bit / 8) % 512;
	mask = 1u << (bit % 8);
	if(val) {
		((unsigned char *)buf->data)[byte] |= mask;
	} else {
		((unsigned char *)buf->data)[byte] &= ~mask;
	}
	bwrite(buf);
	return 0;
}

static int ex_bit_test(struct superblock *sb, __u32 cluster, int *val)
{
	struct buffer *buf;
	unsigned int byte, mask;
	__u32 bit = cluster - 2;

	if(!(buf = bread(sb->dev, ex_bit_sector(sb, cluster), 512))) {
		return -EIO;
	}
	byte = (bit / 8) % 512;
	mask = 1u << (bit % 8);
	*val = (((unsigned char *)buf->data)[byte] & mask) ? 1 : 0;
	brelse(buf);
	return 0;
}

/* find a free cluster (scan the bitmap) and mark it used */
int ex_alloc_cluster(struct superblock *sb, __u32 *out)
{
	__u32 max = sb->u.fatfs.fat_n_fatent - 1;
	__u32 cl;

	for(cl = 2; cl <= max; cl++) {
		int v;

		if(ex_bit_test(sb, cl, &v)) {
			return -EIO;
		}
		if(!v) {
			ex_bit_set(sb, cl, 1);
			*out = cl;
			return 0;
		}
	}
	return -ENOSPC;
}

/* free a FAT-linked exFAT chain via the bitmap (FAT entries remain) */
int ex_free_chain(struct superblock *sb, __u32 first)
{
	__u32 cl = first, next;
	struct buffer *buf;

	if(!cl || cl < 2) {
		return 0;
	}
	while(cl >= 2 && cl < sb->u.fatfs.fat_n_fatent) {
		__blk_t sect = (__blk_t)sb->u.fatfs.fat_sector + cl / 128;

		if(!(buf = bread(sb->dev, sect, 512))) {
			return -EIO;
		}
		next = ((__u32 *)buf->data)[cl % 128];
		brelse(buf);
		ex_bit_set(sb, cl, 0);
		if(next >= sb->u.fatfs.fat_n_fatent || next == EX_EOC) {
			break;
		}
		cl = next;
	}
	return 0;
}

/* ================= FAT entry write ================= */

int ex_fat_write(struct superblock *sb, __u32 cl, __u32 val)
{
	struct buffer *buf;
	__blk_t sect = (__blk_t)sb->u.fatfs.fat_sector + cl / 128;

	if(!(buf = bread(sb->dev, sect, 512))) {
		return -EIO;
	}
	((__u32 *)buf->data)[cl % 128] = val;
	bwrite(buf);
	return 0;
}

/* ================= names / checksums ================= */

static int ex_name_units(const char *name, unsigned char *u16, int cap)
{
	int n = 0;

	while(*name && n < cap) {
		unsigned char c = (unsigned char)*name++;
		__u16 uc;

		if(c < 0x80) {
			uc = c;
		} else if((c & 0xE0) == 0xC0 && *name) {
			uc = (__u16)(((c & 0x1F) << 6) |
				     ((unsigned char)*name++ & 0x3F));
		} else {
			uc = '_';
			if((c & 0xF0) == 0xE0 && name[1]) {
				name += 2;
			}
		}
		u16[n * 2] = (unsigned char)(uc & 0xFF);
		u16[n * 2 + 1] = (unsigned char)(uc >> 8);
		n++;
	}
	return n;
}

static unsigned short ex_name_hash(const unsigned char *u16, int units)
{
	unsigned short sum = 0;
	int i;

	for(i = 0; i < units; i++) {
		__u16 uc = u16[i * 2] | (u16[i * 2 + 1] << 8);

		if(uc >= 'a' && uc <= 'z') {
			uc -= 32;
		}
		sum = ((sum & 1) ? 0x8000 : 0) + (sum >> 1) + (uc & 0xFF);
		sum = ((sum & 1) ? 0x8000 : 0) + (sum >> 1) + (uc >> 8);
	}
	return sum;
}

/* set checksum over 'count' 32-byte entries, skipping set[0] bytes 2-3 */
static unsigned short ex_set_sum(unsigned char (*set)[32], int count)
{
	unsigned short sum = 0;
	int i, j;

	for(i = 0; i < count; i++) {
		for(j = 0; j < 32; j++) {
			if(i == 0 && (j == 2 || j == 3)) {
				continue;
			}
			sum = ((sum & 1) ? 0x8000 : 0) + (sum >> 1) + set[i][j];
		}
	}
	return sum;
}

/* pack a full entry set; returns the total entry count */
static int ex_pack_set(unsigned char (*set)[32], const char *name,
		       int is_dir, __u32 cluster, __u64 dlen, __u32 epoch)
{
	unsigned char u16[NAME_MAX * 2];
	int units, count, i, j, ci;

	units = ex_name_units(name, u16, NAME_MAX);
	count = 2 + (units + 14) / 15;
	if(count > 18) {
		count = 18;
	}
	/* File 0x85 */
	memset_b(set[0], 0, 32);
	set[0][0] = 0x85;
	set[0][1] = (unsigned char)(count - 1);	/* SecondaryCount */
	set[0][4] = is_dir ? 0x10 : 0x20;
	{
		/* DOS-style create/modify/access timestamps (FAT bit
		 * layout, exactly as FatFs stores them in exFAT) */
		__u32 days = epoch / 86400, secs = epoch % 86400;
		__u32 z = days + 719468;
		__u32 era = z / 146097;
		__u32 doe = z - era * 146097;
		__u32 yoe = (doe - doe / 1460 + doe / 36524 -
			     doe / 146096) / 365;
		__u32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		__u32 mp = (5 * doy + 2) / 153;
		int d = (int)(doy - (153 * mp + 2) / 5 + 1);
		int m = (int)(mp < 10 ? mp + 3 : mp - 9);
		int y = (int)(yoe + era * 400) + (m <= 2 ? 1 : 0);
		int hh = (int)(secs / 3600);
		int mm = (int)((secs % 3600) / 60);
		int ss = (int)(secs % 60);
		__u16 t, dd;
		int off;

		if(y < 1980) {
			y = 1980;
		}
		if(y > 2107) {
			y = 2107;
		}
		t = (__u16)((hh << 11) | (mm << 5) | (ss >> 1));
		dd = (__u16)(((y - 1980) << 9) | (m << 5) | d);
		for(off = 0; off < 3; off++) {	/* create, modify, access */
			set[0][8 + off * 4] = (unsigned char)(t & 0xFF);
			set[0][9 + off * 4] = (unsigned char)(t >> 8);
			set[0][10 + off * 4] = (unsigned char)(dd & 0xFF);
			set[0][11 + off * 4] = (unsigned char)(dd >> 8);
		}
	}
	/* Stream 0xC0 */
	memset_b(set[1], 0, 32);
	set[1][0] = 0xC0;
	set[1][1] = 0x00;	/* FAT-linked */
	set[1][3] = (unsigned char)units;
	{
		unsigned short h = ex_name_hash(u16, units);

		set[1][4] = (unsigned char)(h & 0xFF);
		set[1][5] = (unsigned char)(h >> 8);
	}
	for(i = 0; i < 4; i++) {
		set[1][20 + i] = (unsigned char)(cluster >> (i * 8));
	}
	for(i = 0; i < 8; i++) {
		set[1][8 + i] = (unsigned char)(dlen >> (i * 8));	/* VDL */
		set[1][24 + i] = (unsigned char)(dlen >> (i * 8));	/* DL */
	}
	/* FileName 0xC1 entries */
	ci = 0;
	for(i = 2; i < count; i++) {
		memset_b(set[i], 0, 32);
		set[i][0] = 0xC1;
		for(j = 0; j < 15 && ci < units; j++, ci++) {
			set[i][2 + j * 2] = u16[ci * 2];
			set[i][3 + j * 2] = u16[ci * 2 + 1];
		}
	}
	{
		unsigned short c = ex_set_sum(set, count);

		set[0][2] = (unsigned char)(c & 0xFF);
		set[0][3] = (unsigned char)(c >> 8);
	}
	return count;
}

/* ================= dir slot access ================= */

/* bread the buffer holding dir-slot 'slot' of the dir at 'dir_cluster';
 * entry = data + (slot % 16) * 32 */
static struct buffer *ex_slot_buf(struct inode *dir, unsigned long slot)
{
	struct fatfs_sb_info *f = &dir->sb->u.fatfs;
	unsigned long per_cluster = f->sects_per_cluster * 16;
	unsigned long step = slot / per_cluster;
	unsigned long within = slot % per_cluster;
	__u32 cl = dir->u.fatfs.cluster;
	unsigned long n;

	for(n = 0; n < step; n++) {
		__u32 next = fat_next_cluster(dir->sb, cl);

		if(next < 2 || next >= FAT_CLUST_LAST) {
			return NULL;
		}
		cl = next;
	}
	return bread(dir->dev, fat_cluster_sector(dir->sb, cl, within / 16),
		     512);
}

/* append a zeroed cluster to the dir's FAT chain */
static __u32 ex_dir_grow(struct inode *dir)
{
	struct fatfs_sb_info *f = &dir->sb->u.fatfs;
	__u32 tail, next, nc;
	unsigned int i;
	struct buffer *buf;

	tail = dir->u.fatfs.cluster;
	for(;;) {
		__blk_t sect = (__blk_t)f->fat_sector + tail / 128;

		if(!(buf = bread(dir->sb->dev, sect, 512))) {
			return 0;
		}
		next = ((__u32 *)buf->data)[tail % 128];
		brelse(buf);
		if(next >= f->fat_n_fatent || next == EX_EOC) {
			break;
		}
		tail = next;
	}
	if(ex_alloc_cluster(dir->sb, &nc)) {
		return 0;
	}
	ex_fat_write(dir->sb, nc, EX_EOC);
	ex_fat_write(dir->sb, tail, nc);
	for(i = 0; i < f->sects_per_cluster; i++) {
		if(!(buf = bread(dir->dev,
				 fat_cluster_sector(dir->sb, nc, i), 512))) {
			return 0;
		}
		memset_b(buf->data, 0, 512);
		bwrite(buf);
	}
	return nc;
}

/* find 'count' consecutive free slots (type bit 7 clear) in a dir,
 * growing the chain once when needed; returns the start slot or -1 */
static long ex_find_run(struct inode *dir, int count, int *grew_out)
{
	struct buffer *buf = NULL;
	__u32 cl = dir->u.fatfs.cluster;
	unsigned int sector = 0, within = 0;
	unsigned long slot = 0;
	int run = 0, grew = 0;
	unsigned long start = 0;

	for(;;) {
		unsigned char *e;

		if(!buf) {
			if(!(buf = bread(dir->dev,
					 fat_cluster_sector(dir->sb, cl,
							    sector), 512))) {
				return -1;
			}
		}
		e = (unsigned char *)buf->data + within * 32;
		if(!(e[0] & 0x80)) {	/* free */
			if(!run) {
				start = slot;
			}
			if(++run >= count) {
				brelse(buf);
				*grew_out = grew;
				return (long)start;
			}
		} else {
			run = 0;
		}
		slot++;
		within++;
		if(within < 16) {
			continue;
		}
		within = 0;
		sector++;
		if(sector < dir->sb->u.fatfs.sects_per_cluster) {
			brelse(buf);
			buf = NULL;
			continue;
		}
		sector = 0;
		{
			__u32 next = fat_next_cluster(dir->sb, cl);

			if(next >= 2 && next < FAT_CLUST_LAST) {
				cl = next;
				brelse(buf);
				buf = NULL;
				continue;
			}
		}
		/* chain ended: grow once */
		if(!grew) {
			__u32 nc = ex_dir_grow(dir);

			if(!nc) {
				brelse(buf);
				return -1;
			}
			grew = 1;
			cl = nc;
			if(buf) {
				brelse(buf);
			}
			buf = NULL;
			continue;
		}
		brelse(buf);
		return -1;
	}
}

/* write 'count' slots starting at 'start' */
static int ex_write_set(struct inode *dir, unsigned long start,
			unsigned char (*set)[32], int count)
{
	unsigned long i;

	for(i = 0; i < (unsigned long)count; i++) {
		struct buffer *buf;
		unsigned long slot = start + i;

		if(!(buf = ex_slot_buf(dir, slot))) {
			return -EIO;
		}
		memcpy_b((unsigned char *)buf->data + (slot % 16) * 32,
			 set[i], 32);
		bwrite(buf);
		brelse(buf);
	}
	return 0;
}

/* ---- set tweak + checksum refresh (after a stream entry change) ---- */

/* update the stream entry of the set whose 0x85 is at 'slot' in 'dir':
 * set FirstCluster + DataLength + VDL. When bump is set, dlen is a delta
 * added to the current DataLength. */
static int ex_update_stream(struct inode *dir, unsigned long slot,
			    __u32 cluster, __u64 dlen, int bump)
{
	struct buffer *buf;
	unsigned char *e, *s;
	unsigned char set[18][32];
	int nsec, k;
	unsigned short c;

	if(!(buf = ex_slot_buf(dir, slot))) {
		return -EIO;
	}
	e = (unsigned char *)buf->data + (slot % 16) * 32;
	if(e[0] != 0x85) {
		/* deleted set (0x05): the object is gone, nothing to update */
		brelse(buf);
		return e[0] == 0x05 ? 0 : -EIO;
	}
	nsec = e[1];
	if(nsec < 1 || nsec > 17) {
		brelse(buf);
		return -EIO;
	}
	memcpy_b(set[0], e, 32);
	brelse(buf);
	for(k = 1; k <= nsec; k++) {
		unsigned long s2 = slot + k;

		if(!(buf = ex_slot_buf(dir, s2))) {
			return -EIO;
		}
		memcpy_b(set[k], (unsigned char *)buf->data +
			 (s2 % 16) * 32, 32);
		brelse(buf);
	}
	if(set[1][0] != 0xC0) {
		return -EIO;
	}
	s = set[1];
	if(bump) {
		__u64 cur = 0;

		for(k = 0; k < 8; k++) {
			cur |= (__u64)s[24 + k] << (k * 8);
		}
		dlen = cur + dlen;
	}
	for(k = 0; k < 4; k++) {
		s[20 + k] = (unsigned char)(cluster >> (k * 8));
	}
	for(k = 0; k < 8; k++) {
		s[8 + k] = (unsigned char)(dlen >> (k * 8));
		s[24 + k] = (unsigned char)(dlen >> (k * 8));
	}
	c = ex_set_sum(set, nsec + 1);
	/* write the STREAM entry back (FirstCluster/DataLength/VDL live at
	 * slot+1) - the RAM copy above is worthless until it reaches the
	 * stream's own buffer */
	if(!(buf = ex_slot_buf(dir, slot + 1))) {
		return -EIO;
	}
	e = (unsigned char *)buf->data + ((slot + 1) % 16) * 32;
	if(e[0] != 0xC0) {
		brelse(buf);
		return -EIO;
	}
	memcpy_b(e, set[1], 32);
	bwrite(buf);
	brelse(buf);
	/* then the checksum over the whole (now updated) set */
	if(!(buf = ex_slot_buf(dir, slot))) {
		return -EIO;
	}
	e = (unsigned char *)buf->data + (slot % 16) * 32;
	e[2] = (unsigned char)(c & 0xFF);
	e[3] = (unsigned char)(c >> 8);
	bwrite(buf);
	brelse(buf);
	return 0;
}

/* ================= exported operations ================= */

/* bump the size fields of a subdir's own stream after it grew by
 * 'delta' bytes (delta = one cluster). The root directory has no stream
 * entry and needs no bump. */
static int ex_bump_dir(struct inode *dir, __u32 delta)
{
	struct fatfs_ent *e;
	struct inode *pd;
	int errno;

	if(dir->inode == FAT_ROOT_INO) {
		return 0;
	}
	if(!fatfs_ent_find(dir->sb, dir->inode, &e) || !e->used) {
		return 0;
	}
	if(e->parent == dir->sb->u.fatfs.root_cluster) {
		pd = iget(dir->sb, FAT_ROOT_INO);
	} else {
		pd = iget(dir->sb, (__ino_t)e->parent);
	}
	if(!pd) {
		return -EIO;
	}
	superblock_lock(dir->sb);
	errno = ex_update_stream(pd, e->slot, dir->u.fatfs.cluster,
				 (__u64)delta, 1);
	superblock_unlock(dir->sb);
	iput(pd);
	return errno;
}

/* exFAT file/dir create */
static int ex_create_common(struct inode *dir, char *name, __mode_t mode,
			    int is_dir, struct inode **i_res)
{
	unsigned char set[18][32];
	__u32 cluster = 0;
	__u64 dlen = 0;
	__ino_t ino;
	int count, errno = 0;
	long start;
	struct inode *i;
	struct fatfs_sb_info *f;

	*i_res = NULL;
	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}
	if(!S_ISDIR(dir->i_mode)) {
		return -ENOTDIR;
	}
	if(strlen(name) > NAME_MAX || strlen(name) > 255) {
		return -ENAMETOOLONG;
	}
	f = &dir->sb->u.fatfs;
	superblock_lock(dir->sb);
	if(fat_dir_has_name(dir, name)) {
		superblock_unlock(dir->sb);
		return -EEXIST;
	}
	if(is_dir) {
		/* allocate the directory's first cluster (zeroed) */
		if(ex_alloc_cluster(dir->sb, &cluster)) {
			superblock_unlock(dir->sb);
			return -ENOSPC;
		}
		ex_fat_write(dir->sb, cluster, EX_EOC);
		{
			struct buffer *b;
			unsigned int i2;

			for(i2 = 0; i2 < f->sects_per_cluster; i2++) {
				if(!(b = bread(dir->dev,
					 fat_cluster_sector(dir->sb, cluster,
							    i2), 512))) {
					ex_free_chain(dir->sb, cluster);
					superblock_unlock(dir->sb);
					return -EIO;
				}
				memset_b(b->data, 0, 512);
				bwrite(b);
			}
		}
		dlen = (__u64)f->sects_per_cluster * 512;
	}
	count = ex_pack_set(set, name, is_dir, cluster, dlen, CURRENT_TIME);
	start = ex_find_run(dir, count, &errno);
	if(start < 0) {
		if(is_dir && cluster) {
			ex_free_chain(dir->sb, cluster);
		}
		superblock_unlock(dir->sb);
		return -ENOSPC;
	}
	if((errno = ex_write_set(dir, (unsigned long)start, set, count))) {
		if(is_dir && cluster) {
			ex_free_chain(dir->sb, cluster);
		}
		superblock_unlock(dir->sb);
		return errno;
	}
	if(is_dir && ex_bump_dir(dir, 0)) {
		/* dir growth above already bumped via find_run's grow */
	}
	ino = is_dir ? cluster :
	      (cluster ? cluster : (0x80000000u | (unsigned int)start));
	if(!ino) {
		ino = 2;
	}
	{
		/* 'start' is the 0x85 slot of the new set */
		unsigned char isd = is_dir ? 1 : 0;

		fatfs_ent_add(dir->sb, ino, cluster, (__u32)dlen, isd,
			      dir->u.fatfs.cluster, (unsigned long)start, 0);
	}
	superblock_unlock(dir->sb);
	if(!(i = iget(dir->sb, ino))) {
		return -EIO;
	}
	i->count = 1;
	i->i_mode = (mode & ~current->umask & 0777) |
		    (is_dir ? S_IFDIR : S_IFREG);
	i->i_uid = current->euid;
	i->i_gid = current->egid;
	i->i_nlink = is_dir ? 2 : 1;
	i->i_size = (__off_t)dlen;
	i->state |= INODE_DIRTY;
	*i_res = i;
	return 0;
}

int ex_create(struct inode *dir, char *name, int flags, __mode_t mode,
	      struct inode **i_res)
{
	return ex_create_common(dir, name, mode, 0, i_res);
}

int ex_mkdir(struct inode *dir, char *name, __mode_t mode)
{
	struct inode *i;
	int errno;

	if((errno = ex_create_common(dir, name, mode, 1, &i))) {
		return errno;
	}
	iput(i);
	return 0;
}

/* delete a set (clear the in-use bits) + free the object chain */
static int ex_remove(struct inode *dir, struct inode *child)
{
	struct fatfs_ent *e;
	unsigned char *ent;
	struct buffer *buf;
	int nsec, k;

	if(!fatfs_ent_find(dir->sb, child->inode, &e) || !e->used) {
		return -ENOENT;
	}
	/* free the chain (file or dir) */
	if(e->cluster) {
		ex_free_chain(dir->sb, e->cluster);
	}
	/* clear bit 7 on every entry of the set */
	if(!(buf = ex_slot_buf(dir, e->slot))) {
		return -EIO;
	}
	ent = (unsigned char *)buf->data + (e->slot % 16) * 32;
	nsec = ent[1];
	if(ent[0] == 0x85) {
		ent[0] = 0x05;
		bwrite(buf);
	} else {
		brelse(buf);
		return -EIO;
	}
	brelse(buf);
	for(k = 1; k <= nsec; k++) {
		unsigned long s2 = e->slot + k;

		if(!(buf = ex_slot_buf(dir, s2))) {
			return -EIO;
		}
		ent = (unsigned char *)buf->data + (s2 % 16) * 32;
		ent[0] &= 0x7F;	/* clear the in-use bit */
		bwrite(buf);
		brelse(buf);
	}
	{
		__ino_t packed = 0x80000000u | (__ino_t)e->slot;

		fatfs_ent_remove(dir->sb, child->inode);
		if(packed != child->inode) {
			fatfs_ent_remove(dir->sb, packed);
		}
	}
	child->i_nlink = 0;	/* iput() then drops dirty instead of
				 * writeback of the deleted object */
	return 0;
}

int ex_unlink(struct inode *dir, struct inode *child, char *name)
{
	int errno;

	(void)name;
	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}
	superblock_lock(dir->sb);
	errno = ex_remove(dir, child);
	superblock_unlock(dir->sb);
	return errno;
}

int ex_rmdir(struct inode *dir, struct inode *child)
{
	extern int fat_dir_empty(struct inode *);

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}
	if(child->inode == FAT_ROOT_INO) {
		return -EBUSY;
	}
	superblock_lock(dir->sb);
	/* an exFAT dir is empty when no live 0x85 set exists in it */
	if(!fat_dir_empty(child)) {
		superblock_unlock(dir->sb);
		return -ENOTEMPTY;
	}
	{
		int errno = ex_remove(dir, child);

		superblock_unlock(dir->sb);
		return errno;
	}
}

int ex_rename(struct inode *i, struct inode *dir, struct inode *i_new,
	      struct inode *dir_new, char *oldname, char *newname)
{
	unsigned char set[18][32];
	struct fatfs_ent *e;
	unsigned char *ent;
	struct buffer *buf;
	int count, errno = 0;
	long start;
	__u32 cluster;

	(void)oldname;
	if(IS_RDONLY_FS(dir) || IS_RDONLY_FS(dir_new)) {
		return -EROFS;
	}
	if(!fatfs_ent_find(dir->sb, i->inode, &e) || !e->used) {
		return -ENOENT;
	}
	superblock_lock(dir->sb);
	if(i_new) {
		struct fatfs_ent *te;

		if(!fatfs_ent_find(dir_new->sb, i_new->inode, &te) ||
		   !te->used) {
			superblock_unlock(dir->sb);
			return -ENOENT;
		}
		if(te->is_dir) {
			superblock_unlock(dir->sb);
			return -ENOTEMPTY;
		}
		if((errno = ex_remove(dir_new, i_new))) {
			superblock_unlock(dir->sb);
			return errno;
		}
	}
	cluster = e->cluster;
	if(!(buf = ex_slot_buf(dir, e->slot))) {
		superblock_unlock(dir->sb);
		return -EIO;
	}
	ent = (unsigned char *)buf->data + (e->slot % 16) * 32;
	/* the moved set's 0x85 attr byte lives at +4; keep dir-ness */
	{
		int is_dir = (ent[4] & 0x10) ? 1 : 0;

		count = ex_pack_set(set, newname, is_dir, cluster, e->size,
				    CURRENT_TIME);
	}
	brelse(buf);
	start = ex_find_run(dir_new, count, &errno);
	if(start < 0) {
		superblock_unlock(dir->sb);
		return -ENOSPC;
	}
	if((errno = ex_write_set(dir_new, (unsigned long)start, set, count))) {
		superblock_unlock(dir->sb);
		return errno;
	}
	/* clear the old set + drop the old cache entry */
	{
		struct fatfs_ent *oe;
		unsigned char *oe2;
		struct buffer *ob;
		int nsec, k;

		if(fatfs_ent_find(dir->sb, i->inode, &oe) && oe->used &&
		   (ob = ex_slot_buf(dir, oe->slot))) {
			oe2 = (unsigned char *)ob->data + (oe->slot % 16) * 32;
			nsec = oe2[1];
			if(oe2[0] == 0x85) {
				oe2[0] = 0x05;
				bwrite(ob);
			}
			brelse(ob);
			for(k = 1; k <= nsec; k++) {
				unsigned long s2 = oe->slot + k;

				if((ob = ex_slot_buf(dir, s2))) {
					oe2 = (unsigned char *)ob->data +
					      (s2 % 16) * 32;
					oe2[0] &= 0x7F;
					bwrite(ob);
					brelse(ob);
				}
			}
			fatfs_ent_remove(dir->sb, i->inode);
		}
	}
	/* register under the new parent */
	{
		__ino_t ino = cluster ? cluster :
			      (0x80000000u | (unsigned int)start);
		int is_dir = e->is_dir;

		if(!ino) {
			ino = 2;
		}
		fatfs_ent_add(dir_new->sb, ino, cluster, e->size, is_dir,
			      dir_new->u.fatfs.cluster, (unsigned long)start,
			      0);
		if(e->cluster && !fatfs_ent_find(dir_new->sb, ino, NULL)) {
			/* n/a: added above */
		}
	}
	superblock_unlock(dir->sb);
	return 0;
}

/* extend a FAT-linked exFAT chain so it covers the cluster holding
 * 'offset' (allocation under superblock_lock); returns the first cluster */
static __u32 ex_ensure_chain(struct inode *i, __off_t offset)
{
	struct fatfs_sb_info *f = &i->sb->u.fatfs;
	unsigned int cluster_bytes = f->sects_per_cluster * 512;
	unsigned int want = (unsigned int)(offset / cluster_bytes);
	__u32 cl = i->u.fatfs.cluster;
	unsigned int n;

	if(!cl) {
		if(ex_alloc_cluster(i->sb, &cl)) {
			return 0;
		}
		ex_fat_write(i->sb, cl, EX_EOC);
		i->u.fatfs.cluster = cl;
	}
	for(n = 0; n < want; n++) {
		struct buffer *b;
		__blk_t sect = (__blk_t)f->fat_sector + cl / 128;
		__u32 next;

		if(!(b = bread(i->sb->dev, sect, 512))) {
			return 0;
		}
		next = ((__u32 *)b->data)[cl % 128];
		brelse(b);
		if(next >= 2 && next < f->fat_n_fatent && next != EX_EOC) {
			cl = next;
			continue;
		}
		{
			__u32 nc;

			if(ex_alloc_cluster(i->sb, &nc)) {
				return 0;
			}
			ex_fat_write(i->sb, nc, EX_EOC);
			ex_fat_write(i->sb, cl, nc);
			cl = nc;
		}
	}
	return cl;
}

/* bmap FOR_WRITING for exFAT files: 512-byte block containing 'offset' */
int ex_fat_bmap_write(struct inode *i, __off_t offset)
{
	struct fatfs_sb_info *f = &i->sb->u.fatfs;
	unsigned int cluster_bytes = f->sects_per_cluster * 512;
	__u32 cl;

	if(!i->u.fatfs.cluster && offset == 0) {
		/* first block of a new file: allocate on the first write */
	}
	superblock_lock(i->sb);
	cl = ex_ensure_chain(i, offset);
	superblock_unlock(i->sb);
	if(!cl) {
		return -ENOSPC;
	}
	return (int)((__blk_t)f->data_sector +
		     ((__u64)(cl - 2) * f->sects_per_cluster) +
		     ((unsigned int)(offset % cluster_bytes) / 512));
}

/* exFAT truncate: free clusters beyond 'length' (via the bitmap) */
int ex_truncate(struct inode *i, __off_t length)
{
	struct fatfs_sb_info *f = &i->sb->u.fatfs;
	unsigned int cluster_bytes = f->sects_per_cluster * 512;

	if(IS_RDONLY_FS(i)) {
		return -EROFS;
	}
	superblock_lock(i->sb);
	if(length == 0) {
		if(i->u.fatfs.cluster) {
			ex_free_chain(i->sb, i->u.fatfs.cluster);
			i->u.fatfs.cluster = 0;
		}
	} else {
		/* find the cluster containing length-1, free the tail */
		unsigned long need = (unsigned long)(length - 1) /
				     cluster_bytes;
		unsigned long n;
		__u32 cl = i->u.fatfs.cluster;

		if(!cl) {
			/* extending an empty file via truncate: allocate */
			if(!(cl = ex_ensure_chain(i, (__off_t)(length - 1)))) {
				superblock_unlock(i->sb);
				return -ENOSPC;
			}
			need = 0;
		}
		for(n = 0; n < need && cl; n++) {
			struct buffer *b;
			__blk_t sect = (__blk_t)f->fat_sector + cl / 128;
			__u32 next;

			if(!(b = bread(i->sb->dev, sect, 512))) {
				superblock_unlock(i->sb);
				return -EIO;
			}
			next = ((__u32 *)b->data)[cl % 128];
			brelse(b);
			if(next >= 2 && next < f->fat_n_fatent &&
			   next != EX_EOC) {
				cl = next;
			} else {
				cl = 0;
			}
		}
		if(cl) {
			struct buffer *b;
			__blk_t sect = (__blk_t)f->fat_sector + cl / 128;
			__u32 next;

			if(!(b = bread(i->sb->dev, sect, 512))) {
				superblock_unlock(i->sb);
				return -EIO;
			}
			next = ((__u32 *)b->data)[cl % 128];
			brelse(b);
			if(next >= 2 && next < f->fat_n_fatent &&
			   next != EX_EOC) {
				ex_free_chain(i->sb, next);
				ex_fat_write(i->sb, cl, EX_EOC);
			}
		}
	}
	i->i_size = length;
	i->i_mtime = CURRENT_TIME;
	i->state |= INODE_DIRTY;
	superblock_unlock(i->sb);
	return 0;
}

/* exFAT write_inode: push FirstCluster/DataLength/VDL into the file's own
 * stream entry inside its parent set */
int ex_write_inode(struct inode *i)
{
	struct fatfs_ent *e;
	int errno;

	if(i->inode == FAT_ROOT_INO) {
		return 0;
	}
	if(!fatfs_ent_find(i->sb, i->inode, &e) || !e->used) {
		return 0;
	}
	if(!e->parent) {
		return 0;
	}
	superblock_lock(i->sb);
	{
		struct superblock *sb = i->sb;
		struct inode *pd;

		/* reconstruct a dir inode for the parent to walk it */
		if(!(pd = iget(sb, (__ino_t)e->parent))) {
			/* the parent may be the volume root: map the root
			 * cluster to FAT_ROOT_INO */
			if(e->parent == sb->u.fatfs.root_cluster) {
				pd = iget(sb, FAT_ROOT_INO);
			}
		}
		if(pd) {
			errno = ex_update_stream(pd, e->slot,
						 i->u.fatfs.cluster,
						 (__u64)i->i_size, 0);
			iput(pd);
		} else {
			errno = -EIO;
		}
	}
	/* keep the cache identity current (first cluster may be new) */
	if(e->cluster != i->u.fatfs.cluster) {
		e->cluster = i->u.fatfs.cluster;
		e->size = (__u32)i->i_size;
		if(i->u.fatfs.cluster &&
		   !fatfs_ent_find(i->sb, (__ino_t)i->u.fatfs.cluster, NULL)) {
			fatfs_ent_add(i->sb, (__ino_t)i->u.fatfs.cluster,
				      i->u.fatfs.cluster, (__u32)i->i_size, 0,
				      e->parent, e->slot, 0);
			/* the packed identity is now stale: drop it so a
			 * later iput/sync cannot rewrite a deleted object */
			if((i->inode & 0x80000000u) == 0) {
				__ino_t packed = 0x80000000u |
						 (__ino_t)e->slot;

				if(packed != i->inode) {
					fatfs_ent_remove(i->sb, packed);
				}
			}
		}
	}
	e->size = (__u32)i->i_size;
	i->state &= ~INODE_DIRTY;
	superblock_unlock(i->sb);
	return errno;
}
