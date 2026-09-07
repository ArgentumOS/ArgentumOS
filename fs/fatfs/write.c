/*
 * fnx/fs/fatfs/write.c — FNX-native FAT32 write support (M1).
 *
 * FAT chain allocation, directory entry creation/removal (8.3 + LFN),
 * truncate, unlink/rmdir/rename, inode writeback (size/cluster/times to
 * the parent directory slot) and the file write path.
 *
 * Serialization: every metadata mutation runs under superblock_lock(sb)
 * (whole-volume atomicity: FAT allocation, dir scans and entry writes
 * are short). Content writes follow the house minix pattern (bread at
 * s_blocksize, bwrite, INODE_DIRTY) under inode_lock(i).
 *
 * Name generation is ASCII-only for now (FNX writes are ASCII);
 * non-ASCII characters degrade to '_' in the stored names.
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

extern int file_read(struct inode *, struct fd *, char *, __size_t);

/* ---- FAT entry read/write (32-bit FATs; fs_type == 32 in M1) ---- */

int fat_read_entry(struct superblock *sb, __u32 cl, __u32 *val)
{
	struct buffer *buf;
	__blk_t sect;

	sect = (__blk_t)sb->u.fatfs.fat_sector + cl / 128;
	if(!(buf = bread(sb->dev, sect, 512))) {
		return -EIO;
	}
	*val = ((__u32 *)buf->data)[cl % 128] & 0x0FFFFFFF;
	brelse(buf);
	return 0;
}

static int fat_write_entry(struct superblock *sb, __u32 cl, __u32 val)
{
	struct buffer *buf;
	__blk_t sect;

	sect = (__blk_t)sb->u.fatfs.fat_sector + cl / 128;
	if(!(buf = bread(sb->dev, sect, 512))) {
		return -EIO;
	}
	((__u32 *)buf->data)[cl % 128] =
		(((__u32 *)buf->data)[cl % 128] & 0xF0000000) |
		(val & 0x0FFFFFFF);
	bwrite(buf);
	return 0;
}

#define FAT_EOC		0x0FFFFFFF

/* scan the FAT for a free cluster and mark it EOC */
int fat_alloc_cluster(struct superblock *sb, __u32 *cluster)
{
	struct fatfs_sb_info *f = &sb->u.fatfs;
	__u32 max_cluster, cl;
	struct buffer *buf = NULL;
	__blk_t sect;
	__u32 start = 2;

	max_cluster = (f->total_sectors - f->data_sector) /
		      f->sects_per_cluster;
	cl = start;
	for(;;) {
		if(!(cl & 127)) {
			if(buf) {
				brelse(buf);
			}
			sect = (__blk_t)f->fat_sector + cl / 128;
			if(!(buf = bread(sb->dev, sect, 512))) {
				return -EIO;
			}
		}
		if(buf && (((__u32 *)buf->data)[cl % 128] & 0x0FFFFFFF) == 0) {
			((__u32 *)buf->data)[cl % 128] =
				(((__u32 *)buf->data)[cl % 128] &
				 0xF0000000) | FAT_EOC;
			bwrite(buf);
			brelse(buf);
			*cluster = cl;
			return 0;
		}
		if(++cl > max_cluster) {
			if(buf) {
				brelse(buf);
			}
			return -ENOSPC;
		}
	}
}

void fat_set_eoc(struct superblock *sb, __u32 cluster)
{
	fat_write_entry(sb, cluster, FAT_EOC);
}

static int fat_link(struct superblock *sb, __u32 from, __u32 to)
{
	return fat_write_entry(sb, from, to);
}

/* free a cluster chain ('first' inclusive) */
int fat_free_chain(struct superblock *sb, __u32 first)
{
	__u32 cl = first, next;

	if(!cl || cl < 2) {
		return 0;
	}
	while(cl >= 2 && cl < FAT_CLUST_LAST) {
		if(fat_read_entry(sb, cl, &next)) {
			return -EIO;
		}
		if(fat_write_entry(sb, cl, 0)) {
			return -EIO;
		}
		if(next >= FAT_CLUST_LAST) {
			break;
		}
		cl = next;
	}
	return 0;
}

/* ---- DOS timestamps (epoch -> DOS date/time) ---- */

static void fat_dos_time(__u32 epoch, unsigned char *t2, unsigned char *d2)
{
	__u32 days = epoch / 86400, secs = epoch % 86400;
	__u32 z, era, doe, yoe, doy, mp;
	int y, m, d, hh, mm, ss;
	__u16 t, dd;

	z = days + 719468;
	era = z / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	d = (int)(doy - (153 * mp + 2) / 5 + 1);
	m = (int)(mp < 10 ? mp + 3 : mp - 9);
	y = (int)(yoe + era * 400) + (m <= 2 ? 1 : 0);
	if(y < 1980) {
		y = 1980;
	}
	if(y > 2107) {
		y = 2107;
	}
	hh = (int)(secs / 3600);
	mm = (int)((secs % 3600) / 60);
	ss = (int)(secs % 60);
	t = (__u16)((hh << 11) | (mm << 5) | (ss >> 1));
	dd = (__u16)(((y - 1980) << 9) | (m << 5) | d);
	*t2 = t & 0xFF;
	t2[1] = t >> 8;
	*d2 = dd & 0xFF;
	d2[1] = dd >> 8;
}

/* ---- directory slot machinery ---- */

/* buffer holding dir-slot 'slot' of the chain at 'dir_cluster' */
static struct buffer *fat_slot_buffer(struct superblock *sb,
				      __u32 dir_cluster, unsigned long slot)
{
	struct fatfs_sb_info *f = &sb->u.fatfs;
	unsigned int per_cluster = f->sects_per_cluster * 16;
	unsigned long step = slot / per_cluster;
	unsigned int within = (unsigned int)(slot % per_cluster);
	__u32 cl = dir_cluster;
	unsigned long n;

	for(n = 0; n < step; n++) {
		__u32 next;

		if(fat_read_entry(sb, cl, &next)) {
			return NULL;
		}
		if(next < 2 || next >= FAT_CLUST_LAST) {
			return NULL;
		}
		cl = next;
	}
	return bread(sb->dev, fat_cluster_sector(sb, cl, within / 16), 512);
}

/* append a zeroed cluster to the end of the dir chain */
static __u32 fat_dir_grow(struct superblock *sb, __u32 dir_cluster)
{
	struct fatfs_sb_info *f = &sb->u.fatfs;
	struct buffer *buf;
	__u32 tail = dir_cluster, next, nc;
	unsigned int i;

	for(;;) {
		if(fat_read_entry(sb, tail, &next)) {
			return 0;
		}
		if(next < 2 || next >= FAT_CLUST_LAST) {
			break;
		}
		tail = next;
	}
	if(fat_alloc_cluster(sb, &nc)) {
		return 0;
	}
	if(fat_link(sb, tail, nc)) {
		fat_write_entry(sb, nc, 0);
		return 0;
	}
	for(i = 0; i < f->sects_per_cluster; i++) {
		if(!(buf = bread(sb->dev, fat_cluster_sector(sb, nc, i), 512))) {
			return 0;
		}
		memset_b(buf->data, 0, 512);
		bwrite(buf);
	}
	return nc;
}

/* Return the slot index of the directory's first 0x00 end marker (the
 * logical end of the directory; everything from there is free space).
 * Grows the chain so that 'need' slots fit from that point. Deleted
 * (0xE5) entries before the marker are left for future compaction - a
 * writer must never place entries past a live end marker, or compliant
 * readers (including this driver) will never see them. */
static unsigned long fat_find_run(struct inode *dir, int need,
				  unsigned long *last)
{
	struct fatfs_sb_info *f = &dir->sb->u.fatfs;
	__u32 cl = dir->u.fatfs.cluster;
	unsigned int si = 0, ei = 0;
	unsigned long slot = 0, term = 0;
	unsigned long cap = 0;
	struct buffer *buf = NULL;
	int have_term = 0;
	__u32 lastcl = 0;

	/* first pass: find the end marker / chain capacity */
	for(;;) {
		unsigned char *e;

		if(!buf) {
			if(!(buf = bread(dir->dev,
					 fat_cluster_sector(dir->sb, cl, si),
					 512))) {
				return (unsigned long)-1;
			}
		}
		e = (unsigned char *)buf->data + ei * 32;
		if(!have_term && e[0] == 0x00) {
			term = slot;
			have_term = 1;
		}
		slot++;
		cap++;
		ei++;
		if(ei < 16) {
			continue;
		}
		ei = 0;
		si++;
		if(si < f->sects_per_cluster) {
			brelse(buf);
			buf = NULL;
			continue;
		}
		si = 0;
		{
			__u32 next;

			if(fat_read_entry(dir->sb, cl, &next)) {
				brelse(buf);
				return (unsigned long)-1;
			}
			if(next >= 2 && next < FAT_CLUST_LAST) {
				lastcl = cl;
				cl = next;
				brelse(buf);
				buf = NULL;
				continue;
			}
		}
		lastcl = cl;
		break;		/* end of chain */
	}
	if(buf) {
		brelse(buf);
	}
	if(!have_term) {
		term = cap;	/* append after the last live entry */
	}
	/* ensure the chain covers term + need slots (grow whole clusters) */
	while(term + need > cap) {
		__u32 nc = fat_dir_grow(dir->sb, dir->u.fatfs.cluster);

		if(!nc) {
			return (unsigned long)-1;
		}
		cap += (unsigned long)f->sects_per_cluster * 16;
		lastcl = nc;
		(void)lastcl;
	}
	*last = term + need - 1;
	return term;
}
/* ---- short entry / LFN packing ---- */

static void fat_pack_entry(unsigned char *e, const unsigned char name11[11],
			   unsigned char attr, __u32 cluster, __u32 size,
			   __u32 epoch)
{
	unsigned char t2[2], d2[2];

	memset_b(e, 0, 32);
	memcpy_b(e, name11, 11);
	e[11] = attr;
	fat_dos_time(epoch, t2, d2);
	e[14] = t2[0]; e[15] = t2[1];
	e[16] = d2[0]; e[17] = d2[1];
	e[18] = d2[0]; e[19] = d2[1];
	e[20] = (unsigned char)((cluster >> 16) & 0xFF);
	e[21] = (unsigned char)((cluster >> 24) & 0xFF);
	e[22] = t2[0]; e[23] = t2[1];
	e[24] = d2[0]; e[25] = d2[1];
	e[26] = (unsigned char)(cluster & 0xFF);
	e[27] = (unsigned char)((cluster >> 8) & 0xFF);
	e[28] = size & 0xFF;
	e[29] = (size >> 8) & 0xFF;
	e[30] = (size >> 16) & 0xFF;
	e[31] = (size >> 24) & 0xFF;
}

static void fat_pack_lfn(unsigned char *e, int part, int nparts,
			 const unsigned char *u16, int name_units,
			 unsigned char checksum)
{
	int bo, j, first = (part - 1) * 13;

	memset_b(e, 0, 32);
	e[0] = (unsigned char)((part == nparts ? 0x40 : 0) | part);
	e[11] = FAT_ATTR_LFN;
	e[13] = checksum;
	for(j = 0; j < 13; j++) {
		int at = first + j;
		__u16 uc = at < name_units ?
			   (__u16)(u16[at * 2] | (u16[at * 2 + 1] << 8)) :
			   (at == name_units ? 0x0000 : 0xFFFF);

		bo = (j < 5) ? 1 + j * 2 :
		     (j < 11) ? 14 + (j - 5) * 2 :
				28 + (j - 11) * 2;
		e[bo] = (unsigned char)(uc & 0xFF);
		e[bo + 1] = (unsigned char)(uc >> 8);
	}
}

static unsigned char fat_checksum(const unsigned char name11[11])
{
	unsigned char sum = 0;
	int i;

	for(i = 0; i < 11; i++) {
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i];
	}
	return sum;
}

/* UTF-8 name -> UTF-16LE units (ASCII fast path); returns unit count */
static int fat_utf8_to_u16(const char *name, unsigned char *u16, int cap)
{
	int n = 0;

	while(*name) {
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
		if(n >= cap - 1) {
			break;
		}
		u16[n * 2] = (unsigned char)(uc & 0xFF);
		u16[n * 2 + 1] = (unsigned char)(uc >> 8);
		n++;
	}
	return n;
}

/* build the 11-byte 8.3 name + whether an LFN is required */
static void fat_build_sfn(const char *name, unsigned char name11[11],
			  int *need_lfn)
{
	char base[9];
	const char *dot = NULL, *p;
	int blen = 0, elen = 0, i, lc = 0;

	*need_lfn = 0;
	for(i = 0; name[i]; i++) {
		if(name[i] == '.') {
			dot = name + i;
		}
		if(name[i] >= 'a' && name[i] <= 'z') {
			lc = 1;
		}
	}
	/* an extension is only the tail after the last '.' when it is
	 * 1-3 chars and the part before fits 8 (else the whole name is
	 * base-only and the dot forces an LFN) */
	if(dot) {
		p = dot + 1;
		for(elen = 0; p[elen] && p[elen] != '.'; elen++) {
			;
		}
		if(!elen || elen > 3 || p[elen]) {
			dot = NULL;	/* no usable extension */
		}
	}
	for(i = 0; ; i++) {
		char c;

		if(dot && name + i == dot) {
			break;
		}
		c = name[i];
		if(!c || c == '.') {
			break;
		}
		if(c >= 'a' && c <= 'z') {
			c -= 32;
		}
		if(c < 0x20 || c == '/' || c == '\\' || c == ':' ||
		   c == '*' || c == '?' || c == '"' || c == '<' ||
		   c == '>' || c == '|') {
			c = '_';
		}
		if(blen < 8) {
			base[blen++] = c;
		} else {
			*need_lfn = 1;
		}
	}
	if(lc || blen > 8 || (dot && elen > 3)) {
		*need_lfn = 1;
	}
	if(!dot && lc == 0 && blen <= 8 && name[blen] == '\0') {
		/* plain upper short name */
	}
	memset_b(name11, ' ', 11);
	memcpy_b(name11, base, blen);
	if(dot) {
		for(i = 0; i < elen && i < 3; i++) {
			char c = dot[1 + i];

			name11[8 + i] = (c >= 'a' && c <= 'z') ? c - 32 : c;
		}
	}
	if(!blen) {
		*need_lfn = 1;
		name11[0] = '_';	/* empty base -> placeholders */
	}
}

/* ---- name-existence + SFN uniqueness scans (raw dir walk) ----
 * Exposed from dir.c: does the dir contain an entry named 'name'
 * (case-insensitive LFN or 8.3 match)? */
extern int fat_dir_has_name(struct inode *, const char *);

/* does the dir contain a short entry with these exact 11 name bytes? */
static int fat_sfn_exists(struct inode *dir, const unsigned char want[11])
{
	struct fatfs_sb_info *f = &dir->sb->u.fatfs;
	__u32 cl = dir->u.fatfs.cluster;
	struct buffer *buf = NULL;
	unsigned int si = 0, ei = 0;

	for(;;) {
		unsigned char *e;

		if(!buf) {
			if(!(buf = bread(dir->dev,
					 fat_cluster_sector(dir->sb, cl, si),
					 512))) {
				return 1;	/* assume exists on error */
			}
		}
		e = (unsigned char *)buf->data + ei * 32;
		if(e[0] == 0x00) {
			brelse(buf);
			return 0;	/* rest of the dir is free */
		}
		if(e[0] != FAT_ENTRY_DELETED && e[11] != FAT_ATTR_LFN &&
		   !memcmp(e, want, 11)) {
			brelse(buf);
			return 1;
		}
		ei++;
		if(ei < 16) {
			continue;
		}
		ei = 0;
		si++;
		if(si < f->sects_per_cluster) {
			brelse(buf);
			buf = NULL;
			continue;
		}
		si = 0;
		{
			__u32 next;

			if(fat_read_entry(dir->sb, cl, &next)) {
				brelse(buf);
				return 1;
			}
			if(next >= 2 && next < FAT_CLUST_LAST) {
				cl = next;
				brelse(buf);
				buf = NULL;
				continue;
			}
		}
		brelse(buf);
		return 0;
	}
}

/* uniquify an SFN with a '~N' tail: base must already be truncated to
 * 6 chars when need_lfn; fills the tilde digits. */
static void fat_unique_sfn(struct inode *dir, unsigned char name11[11],
			   int need_lfn)
{
	int d;

	if(!need_lfn) {
		return;
	}
	/* name11: base 8 + ext 3; move base 8 -> 6 + ~ + digit */
	for(d = 1; d < 10; d++) {
		unsigned char try[11];

		memcpy_b(try, name11, 11);
		if(try[0] == ' ') {
			try[0] = '_';
		}
		/* shift base chars 6..7 out */
		memcpy_b(try + 6, try + 7, 2);
		memcpy_b(try + 6, try, 6);
		/* recompute properly below */
		memset_b(try, ' ', 8);
		memcpy_b(try, name11, 6);	/* first 6 base chars */
		try[6] = '~';
		try[7] = (unsigned char)('0' + d);
		if(!fat_sfn_exists(dir, try)) {
			memcpy_b(name11, try, 11);
			return;
		}
	}
	/* all digits taken: keep the plain 8.3 of the truncated base */
}

/* write a run of LFN + one SFN entry starting at slot 'start' */
static int fat_write_record(struct inode *dir, unsigned long start,
			    const unsigned char name11[11],
			    const unsigned char *u16, int units,
			    unsigned char attr, __u32 cluster, __u32 size,
			    __u32 epoch)
{
	int nparts = (units + 12) / 13;
	unsigned char csum = fat_checksum(name11);
	struct buffer *buf;
	int p;

	if(!nparts) {
		nparts = 0;
	}
	for(p = 0; p < nparts; p++) {
		unsigned long slot = start + p;

		if(!(buf = fat_slot_buffer(dir->sb, dir->u.fatfs.cluster,
					   slot))) {
			return -EIO;
		}
		fat_pack_lfn((unsigned char *)buf->data + (slot % 16) * 32, p + 1, nparts,
			     u16, units, csum);
		bwrite(buf);
		brelse(buf);
	}
	{
		unsigned long slot = start + nparts;

		if(!(buf = fat_slot_buffer(dir->sb, dir->u.fatfs.cluster,
					   slot))) {
			return -EIO;
		}
		fat_pack_entry((unsigned char *)buf->data + (slot % 16) * 32, name11, attr,
			       cluster, size, epoch);
		bwrite(buf);
		brelse(buf);
	}
	return 0;
}

/* ---- create / mkdir ---- */

static int fat_create_common(struct inode *dir, char *name, __mode_t mode,
			     unsigned char is_dir, struct inode **i_res)
{
	struct inode *i;
	unsigned char name11[11];
	unsigned char u16[NAME_MAX * 2];
	unsigned long last, start;
	int need_lfn, units, errno = 0;
	__u32 cluster = 0;

	*i_res = NULL;
	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}
	if(dir->sb->u.fatfs.fs_type == FAT_EXFAT) {
		return -EROFS;	/* exFAT writes are M2b */
	}
	if(!S_ISDIR(dir->i_mode)) {
		return -ENOTDIR;
	}
	if(strlen(name) > NAME_MAX) {
		return -ENAMETOOLONG;
	}
	superblock_lock(dir->sb);
	if(fat_dir_has_name(dir, name)) {
		superblock_unlock(dir->sb);
		return -EEXIST;
	}
	fat_build_sfn(name, name11, &need_lfn);
	if(need_lfn) {
		units = fat_utf8_to_u16(name, u16, NAME_MAX * 2);
	} else {
		units = 0;
	}
	if(is_dir) {
		if(fat_alloc_cluster(dir->sb, &cluster)) {
			superblock_unlock(dir->sb);
			return -ENOSPC;
		}
	}
	fat_unique_sfn(dir, name11, need_lfn);
	{
		int runlen = 1 + (need_lfn ? (units + 12) / 13 : 0);

		if(runlen > 1) {
			/* uniquify may have changed nothing; recompute */
		}
		start = fat_find_run(dir, runlen, &last);
		if(start < 0) {
			if(is_dir && cluster) {
				fat_write_entry(dir->sb, cluster, 0);
			}
			superblock_unlock(dir->sb);
			return -ENOSPC;
		}
		errno = fat_write_record(dir, (unsigned long)start, name11,
					 u16, units,
					 is_dir ? FAT_ATTR_DIRECTORY |
						  FAT_ATTR_ARCHIVE :
						  FAT_ATTR_ARCHIVE,
					 cluster, 0, CURRENT_TIME);
	}
	if(errno) {
		if(is_dir && cluster) {
			fat_write_entry(dir->sb, cluster, 0);
		}
		superblock_unlock(dir->sb);
		return errno;
	}
	/* build the inode via the cache (directory entry is on disk now) */
	{
		__ino_t ino = is_dir ? cluster :
			      (0x80000000u | (unsigned int)(start +
			      (need_lfn ? (units + 12) / 13 : 0)));

		if(!ino) {
			ino = 2;
		}
		fatfs_ent_add(dir->sb, ino, cluster, 0, is_dir,
			      dir->u.fatfs.cluster,
			      start + (need_lfn ? (units + 12) / 13 : 0), 0);
		superblock_unlock(dir->sb);
		if(!(i = iget(dir->sb, ino))) {
			return -EIO;
		}
	}
	i->count = 1;
	i->i_mode = (mode & ~current->umask & 0777) |
		    (is_dir ? S_IFDIR : S_IFREG);
	i->i_uid = current->euid;
	i->i_gid = current->egid;
	i->i_nlink = is_dir ? 2 : 1;
	i->i_size = 0;
	i->state |= INODE_DIRTY;
	*i_res = i;
	return 0;
}

int fat_create(struct inode *dir, char *name, int flags, __mode_t mode,
	       struct inode **i_res)
{
	return fat_create_common(dir, name, mode, 0, i_res);
}

int fat_mkdir(struct inode *dir, char *name, __mode_t mode)
{
	struct inode *i;
	unsigned char name11[11];
	unsigned char u16[NAME_MAX * 2];
	struct buffer *buf;
	unsigned long last, start;
	int need_lfn, units;
	__u32 cluster = 0;
	int errno;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}
	superblock_lock(dir->sb);
	if(fat_dir_has_name(dir, name)) {
		superblock_unlock(dir->sb);
		return -EEXIST;
	}
	fat_build_sfn(name, name11, &need_lfn);
	units = need_lfn ? fat_utf8_to_u16(name, u16, NAME_MAX * 2) : 0;
	if(fat_alloc_cluster(dir->sb, &cluster)) {
		superblock_unlock(dir->sb);
		return -ENOSPC;
	}
	fat_unique_sfn(dir, name11, need_lfn);
	{
		int runlen = 1 + (need_lfn ? (units + 12) / 13 : 0);

		start = fat_find_run(dir, runlen, &last);
		if(start < 0) {
			fat_write_entry(dir->sb, cluster, 0);
			superblock_unlock(dir->sb);
			return -ENOSPC;
		}
		errno = fat_write_record(dir, (unsigned long)start, name11,
					 u16, units,
					 FAT_ATTR_DIRECTORY |
					 FAT_ATTR_ARCHIVE,
					 cluster, 0, CURRENT_TIME);
		if(errno) {
			fat_write_entry(dir->sb, cluster, 0);
			superblock_unlock(dir->sb);
			return errno;
		}
	}
	/* write "." and ".." into the new directory */
	if(!(buf = bread(dir->dev,
			 fat_cluster_sector(dir->sb, cluster, 0), 512))) {
		superblock_unlock(dir->sb);
		return -EIO;
	}
	fat_pack_entry((unsigned char *)buf->data, (const unsigned char *)".          ", 0x10,
		       cluster, 0, CURRENT_TIME);
	fat_pack_entry((unsigned char *)buf->data + 32, (const unsigned char *)"..         ",
		       0x10, dir->u.fatfs.cluster, 0, CURRENT_TIME);
	bwrite(buf);
	brelse(buf);
	superblock_unlock(dir->sb);

	/* materialize the dir inode through the cache */
	{
		__ino_t ino = cluster ? cluster : 2;
		unsigned long slot = start +
				    (need_lfn ? (units + 12) / 13 : 0);

		fatfs_ent_add(dir->sb, ino, cluster, 0, 1,
			      dir->u.fatfs.cluster, slot, 0);
		if(!(i = iget(dir->sb, ino))) {
			return -EIO;
		}
	}
	i->count = 1;
	i->i_mode = (mode & ~current->umask & 0777) | S_IFDIR;
	i->i_uid = current->euid;
	i->i_gid = current->egid;
	i->i_nlink = 2;
	i->i_size = 0;
	iput(i);	/* dirs are reached by path, not kept */
	return 0;
}

/* ---- record deletion ---- */

void fatfs_ent_remove(struct superblock *sb, __ino_t ino)
{
	struct fatfs_cache *c = (struct fatfs_cache *)sb->u.fatfs.cache;
	unsigned int n;

	if(!c) {
		return;
	}
	for(n = 0; n < c->used; n++) {
		if(c->ents[n].used && c->ents[n].ino == ino) {
			c->ents[n].used = 0;
			return;
		}
	}
}

/* delete the record whose short entry is at 'slot' (LFN run + SFN) */
static int fat_delete_record(struct inode *dir, unsigned long slot)
{
	struct buffer *buf;
	unsigned char *e;
	int n;

	/* walk back over the LFN run */
	for(n = 0; ; n++) {
		unsigned long s = slot - 1 - n;
		struct buffer *b;

		if(!(b = fat_slot_buffer(dir->sb, dir->u.fatfs.cluster, s))) {
			break;
		}
		e = (unsigned char *)b->data + (s % 16) * 32;
		if(e[11] != FAT_ATTR_LFN) {
			brelse(b);
			break;
		}
		e[0] = FAT_ENTRY_DELETED;
		bwrite(b);
		brelse(b);
	}
	if(!(buf = fat_slot_buffer(dir->sb, dir->u.fatfs.cluster, slot))) {
		return -EIO;
	}
	e = (unsigned char *)buf->data + (slot % 16) * 32;
	memset_b(e, 0, 32);
	e[0] = FAT_ENTRY_DELETED;
	bwrite(buf);
	brelse(buf);
	return 0;
}

/* is a directory empty? (only "." / ".." present) */
static int fat_dir_empty(struct inode *dir)
{
	struct fatfs_sb_info *f = &dir->sb->u.fatfs;
	__u32 cl = dir->u.fatfs.cluster;
	struct buffer *buf = NULL;
	unsigned int si = 0, ei = 2;	/* skip "." and ".." */

	if(!cl) {
		return 1;
	}
	/* '.' and '..' occupy slots 0-1 of the first sector */
	for(;;) {
		unsigned char *e;

		if(!buf) {
			if(!(buf = bread(dir->dev,
					 fat_cluster_sector(dir->sb, cl, si),
					 512))) {
				return 0;
			}
		}
		while(ei < 16) {
			e = (unsigned char *)buf->data + ei * 32;
			if(e[0] == 0x00) {
				brelse(buf);
				return 1;	/* end marker: empty */
			}
			if(e[0] != FAT_ENTRY_DELETED) {
				brelse(buf);
				return 0;
			}
			ei++;
		}
		ei = 0;
		si++;
		if(si < f->sects_per_cluster) {
			brelse(buf);
			buf = NULL;
			continue;
		}
		si = 0;
		{
			__u32 next;

			if(fat_read_entry(dir->sb, cl, &next)) {
				brelse(buf);
				return 1;
			}
			if(next >= 2 && next < FAT_CLUST_LAST) {
				cl = next;
				brelse(buf);
				buf = NULL;
				continue;
			}
		}
		brelse(buf);
		return 1;	/* chain ended: empty */
	}
}

/* ---- unlink / rmdir ---- */

static int fat_remove(struct inode *dir, struct inode *child)
{
	struct fatfs_ent *e;
	unsigned long slot;

	if(fatfs_ent_find(dir->sb, child->inode, &e) && e->used) {
		slot = e->slot;
	} else {
		return -ENOENT;
	}
	if(e->cluster && !(e->is_dir)) {
		if(fat_free_chain(dir->sb, e->cluster)) {
			return -EIO;
		}
	}
	fat_delete_record(dir, slot);
	fatfs_ent_remove(dir->sb, child->inode);
	return 0;
}

int fat_unlink(struct inode *dir, struct inode *child, char *name)
{
	struct fatfs_ent *e;
	int errno;

	(void)name;
	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}
	if(dir->sb->u.fatfs.fs_type == FAT_EXFAT) {
		return -EROFS;	/* exFAT writes are M2b */
	}
	if(!fatfs_ent_find(dir->sb, child->inode, &e) || !e->used) {
		return -ENOENT;
	}
	if(e->is_dir) {
		return -EISDIR;		/* use rmdir for directories */
	}
	superblock_lock(dir->sb);
	errno = fat_remove(dir, child);
	superblock_unlock(dir->sb);
	return errno;
}

int fat_rmdir(struct inode *dir, struct inode *child)
{
	struct fatfs_ent *e;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}
	if(dir->sb->u.fatfs.fs_type == FAT_EXFAT) {
		return -EROFS;	/* exFAT writes are M2b */
	}
	if(!fatfs_ent_find(dir->sb, child->inode, &e) || !e->used) {
		return -ENOENT;
	}
	if(!e->is_dir) {
		return -ENOTDIR;
	}
	if(child->inode == FAT_ROOT_INO) {
		return -EBUSY;
	}
	superblock_lock(dir->sb);
	if(!fat_dir_empty(child)) {
		superblock_unlock(dir->sb);
		return -ENOTEMPTY;
	}
	/* free the child's chain (its "." and ".." live there) */
	if(e->cluster && fat_free_chain(dir->sb, e->cluster)) {
		superblock_unlock(dir->sb);
		return -EIO;
	}
	fat_delete_record(dir, e->slot);
	fatfs_ent_remove(dir->sb, child->inode);
	superblock_unlock(dir->sb);
	return 0;
}

/* ---- rename ---- */

int fat_rename(struct inode *i, struct inode *dir, struct inode *i_new,
	       struct inode *dir_new, char *oldname, char *newname)
{
	struct fatfs_ent *e;
	struct buffer *buf;
	unsigned char name11[11];
	unsigned char u16[NAME_MAX * 2];
	unsigned long last, start;
	int need_lfn, units, runlen;
	int errno;

	(void)oldname;
	if(IS_RDONLY_FS(dir) || IS_RDONLY_FS(dir_new)) {
		return -EROFS;
	}
	if(!fatfs_ent_find(dir->sb, i->inode, &e) || !e->used) {
		return -ENOENT;
	}
	superblock_lock(dir->sb);
	if(i_new) {
		/* target exists: only file-over-file overwrite */
		struct fatfs_ent *te;

		if(!fatfs_ent_find(dir_new->sb, i_new->inode, &te) || !te->used) {
			superblock_unlock(dir->sb);
			return -ENOENT;
		}
		if(te->is_dir) {
			superblock_unlock(dir->sb);
			return -ENOTEMPTY;
		}
		if(te->cluster) {
			if(fat_free_chain(dir_new->sb, te->cluster)) {
				superblock_unlock(dir->sb);
				return -EIO;
			}
		}
		if((errno = fat_delete_record(dir_new, te->slot))) {
			superblock_unlock(dir->sb);
			return errno;
		}
		fatfs_ent_remove(dir_new->sb, i_new->inode);
	}
	/* write the new record (cluster + size from the old entry) */
	fat_build_sfn(newname, name11, &need_lfn);
	units = need_lfn ? fat_utf8_to_u16(newname, u16, NAME_MAX * 2) : 0;
	fat_unique_sfn(dir_new, name11, need_lfn);
	runlen = 1 + (need_lfn ? (units + 12) / 13 : 0);
	start = fat_find_run(dir_new, runlen, &last);
	if(start < 0) {
		superblock_unlock(dir->sb);
		return -ENOSPC;
	}
	if((errno = fat_write_record(dir_new, (unsigned long)start, name11,
				      u16, units,
				      e->is_dir ? FAT_ATTR_DIRECTORY |
						  FAT_ATTR_ARCHIVE :
						  FAT_ATTR_ARCHIVE,
				      e->cluster, e->size, CURRENT_TIME))) {
		superblock_unlock(dir->sb);
		return errno;
	}
	/* a moved directory's ".." must point at the new parent */
	if(e->is_dir && e->cluster && dir_new->u.fatfs.cluster != e->parent) {
		if((buf = bread(dir_new->dev,
				 fat_cluster_sector(dir_new->sb, e->cluster, 0),
				 512))) {
			unsigned char *ent = (unsigned char *)buf->data + 32;	/* ".." */

			ent[20] = (unsigned char)((dir_new->u.fatfs.cluster >> 16) & 0xFF);
			ent[21] = (unsigned char)((dir_new->u.fatfs.cluster >> 24) & 0xFF);
			ent[26] = (unsigned char)(dir_new->u.fatfs.cluster & 0xFF);
			ent[27] = (unsigned char)((dir_new->u.fatfs.cluster >> 8) & 0xFF);
			bwrite(buf);
			brelse(buf);
		}
	}
	fat_delete_record(dir, e->slot);
	fatfs_ent_remove(dir->sb, i->inode);
	/* register the moved entry under its new parent/slot */
	{
		__ino_t ino = e->cluster ? e->cluster :
			      (0x80000000u | (unsigned int)(start +
			       (need_lfn ? (units + 12) / 13 : 0)));

		if(!ino) {
			ino = 2;
		}
		fatfs_ent_add(dir_new->sb, ino, e->cluster, e->size,
			      e->is_dir, dir_new->u.fatfs.cluster,
			      start + (need_lfn ? (units + 12) / 13 : 0), 0);
	}
	superblock_unlock(dir->sb);
	return 0;
}

/* ---- truncate ---- */

int fat_truncate(struct inode *i, __off_t length)
{
	struct fatfs_sb_info *f = &i->sb->u.fatfs;
	unsigned int cluster_bytes = f->sects_per_cluster * 512;
	__u32 cl;
	int errno;

	if(IS_RDONLY_FS(i)) {
		return -EROFS;
	}
	if(i->sb->u.fatfs.fs_type == FAT_EXFAT) {
		return -EROFS;	/* exFAT writes are M2b */
	}
	superblock_lock(i->sb);
	cl = i->u.fatfs.cluster;
	if(!cl || length == 0) {
		if(cl) {
			if((errno = fat_free_chain(i->sb, cl))) {
				superblock_unlock(i->sb);
				return errno;
			}
		}
		i->u.fatfs.cluster = 0;
	} else {
		unsigned long need = (unsigned long)(length - 1) /
				     cluster_bytes;
		unsigned long n;
		__u32 prev = cl;

		/* walk to the cluster containing 'length-1' */
		for(n = 0; n < need; n++) {
			__u32 next;

			if(fat_read_entry(i->sb, prev, &next)) {
				superblock_unlock(i->sb);
				return -EIO;
			}
			if(next < 2 || next >= FAT_CLUST_LAST) {
				break;	/* chain shorter than needed */
			}
			prev = next;
		}
		/* free everything after 'prev' */
		{
			__u32 next;

			if(!fat_read_entry(i->sb, prev, &next)) {
				if(next >= 2 && next < FAT_CLUST_LAST) {
					if((errno = fat_free_chain(i->sb, next))) {
						superblock_unlock(i->sb);
						return errno;
					}
					fat_set_eoc(i->sb, prev);
				}
			}
		}
	}
	i->i_size = length;
	i->i_mtime = CURRENT_TIME;
	i->state |= INODE_DIRTY;
	superblock_unlock(i->sb);
	return 0;
}

/* ---- write_inode: push size/cluster/times to the parent dir entry ---- */

int fat_write_inode(struct inode *i)
{
	struct fatfs_ent *e;
	struct buffer *buf;
	unsigned char *ent;

	if(i->inode == FAT_ROOT_INO) {
		return 0;
	}
	if(!fatfs_ent_find(i->sb, i->inode, &e) || !e->used) {
		/* the inode may have been re-keyed (first cluster alloc):
		 * find any entry carrying the same cluster */
		return 0;
	}
	if(!e->parent) {
		return 0;
	}
	superblock_lock(i->sb);
	if(!(buf = fat_slot_buffer(i->sb, e->parent, e->slot))) {
		superblock_unlock(i->sb);
		return -EIO;
	}
	ent = (unsigned char *)buf->data + (e->slot % 16) * 32;
	if(e->is_dir) {
		ent[20] = (unsigned char)((i->u.fatfs.cluster >> 16) & 0xFF);
		ent[21] = (unsigned char)((i->u.fatfs.cluster >> 24) & 0xFF);
		ent[26] = (unsigned char)(i->u.fatfs.cluster & 0xFF);
		ent[27] = (unsigned char)((i->u.fatfs.cluster >> 8) & 0xFF);
	} else {
		__u32 size = i->i_size;
		__u32 cl = i->u.fatfs.cluster;
		unsigned char t2[2], d2[2];

		ent[20] = (unsigned char)((cl >> 16) & 0xFF);
		ent[21] = (unsigned char)((cl >> 24) & 0xFF);
		ent[26] = (unsigned char)(cl & 0xFF);
		ent[27] = (unsigned char)((cl >> 8) & 0xFF);
		ent[28] = size & 0xFF;
		ent[29] = (size >> 8) & 0xFF;
		ent[30] = (size >> 16) & 0xFF;
		ent[31] = (size >> 24) & 0xFF;
		fat_dos_time(CURRENT_TIME, t2, d2);
		ent[22] = t2[0]; ent[23] = t2[1];
		ent[24] = d2[0]; ent[25] = d2[1];
	}
	bwrite(buf);
	brelse(buf);
	/* keep the cache identity current (cluster may have been added) */
	if(e->cluster != i->u.fatfs.cluster) {
		e->cluster = i->u.fatfs.cluster;
		e->size = i->i_size;
		if(i->u.fatfs.cluster && !fatfs_ent_find(i->sb,
					     (__ino_t)i->u.fatfs.cluster,
					     NULL)) {
			fatfs_ent_add(i->sb, (__ino_t)i->u.fatfs.cluster,
				      i->u.fatfs.cluster, i->i_size, 0,
				      e->parent, e->slot, 0);
		}
	}
	e->size = i->i_size;
	superblock_unlock(i->sb);
	return 0;
}

/* ---- file write (house minix pattern at 512-byte blocks) ---- */

int fat_write(struct inode *i, struct fd *f, const char *buffer,
	      __size_t count)
{
	__blk_t block;
	__size_t total_written = 0;
	unsigned int boffset, bytes;
	struct buffer *buf;

	if(i->sb->u.fatfs.fs_type == FAT_EXFAT) {
		return -EROFS;	/* exFAT writes are M2b */
	}
	inode_lock(i);
	if(f->flags & O_APPEND) {
		f->offset = i->i_size;
	}
	while(total_written < count) {
		boffset = f->offset & 511;
		if((block = fat_bmap(i, f->offset, FOR_WRITING)) < 0) {
			inode_unlock(i);
			return (int)block;
		}
		bytes = 512 - boffset;
		bytes = MIN(bytes, count - total_written);
		if(!(buf = bread(i->dev, block, 512))) {
			inode_unlock(i);
			return -EIO;
		}
		memcpy_b(buf->data + boffset, buffer + total_written, bytes);
		bwrite(buf);
		brelse(buf);
		total_written += bytes;
		f->offset += bytes;
	}
	if(f->offset > i->i_size) {
		i->i_size = f->offset;
	}
	i->i_ctime = CURRENT_TIME;
	i->i_mtime = CURRENT_TIME;
	i->state |= INODE_DIRTY;
	inode_unlock(i);
	return total_written;
}
