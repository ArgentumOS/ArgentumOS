/*
 * fnx/fs/fatfs/dir.c — FNX-native FAT directory support: the 32-byte
 * slot scanner (shared by readdir and lookup), LFN decoding, and the
 * readdir/readdir64 entry points.
 *
 * FAT32 directories are cluster chains of 32-byte entries. A long file
 * name is stored as a run of 0x0F LFN entries preceding the 8.3 short
 * entry. Reading forward you meet the LFN parts in reverse chunk order:
 * part N (0x40 bit, the name tail) first ... part 1 (the name head,
 * immediately before the short entry) last; each part's seq&0x1F is its
 * chunk index (1 = first 13 chars). Chunks are placed at (seq-1)*13
 * UTF-16 units, so the arrival order needs no reversal.
 *
 * "." and ".." are real entries on FAT32 (with their start clusters), so
 * the generic scanner returns them; only the volume root lacks them.
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
#include <fnx/dirent.h>
#include "fat.h"

/* ---- ASCII helpers ---- */

static int ascii_up(int c)
{
	return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

static int name_ieq(const char *a, const char *b)
{
	while(*a && *b) {
		if(ascii_up(*a) != ascii_up(*b)) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

/* ---- directory slot iterator ----
 * consumed counts 32-byte slots since the chain start; the presented
 * slot's index is consumed-1. The VFS dir position (f->offset) is the
 * number of slots to skip, i.e. the 0-based slot ordinal. */

struct fat_dir_it {
	struct inode *dir;
	__u32 cluster;
	unsigned int sector_off;
	unsigned int ent_off;
	unsigned long consumed;
	struct buffer *buf;
	int ok;
};

static void dir_it_close(struct fat_dir_it *it)
{
	if(it->buf) {
		brelse(it->buf);
		it->buf = NULL;
	}
	it->ok = 0;
}

static int dir_next(struct fat_dir_it *it)
{
	struct fatfs_sb_info *f = &it->dir->sb->u.fatfs;

	if(!it->ok) {
		return 0;
	}
	for(;;) {
		it->ent_off++;
		it->consumed++;
		if(it->ent_off < 16) {
			return 1;
		}
		it->ent_off = 0;
		it->sector_off++;
		if(it->sector_off < f->sects_per_cluster) {
			if(it->buf) {
				brelse(it->buf);
			}
			it->buf = bread(it->dir->dev,
					fat_cluster_sector(it->dir->sb,
							   it->cluster,
							   it->sector_off),
					512);
			return it->buf ? 1 : (dir_it_close(it), 0);
		}
		it->sector_off = 0;
		{
			__u32 next = fat_next_cluster(it->dir->sb, it->cluster);

			if(next < 2 || next >= FAT_CLUST_LAST) {
				dir_it_close(it);
				return 0;
			}
			it->cluster = next;
		}
		if(it->buf) {
			brelse(it->buf);
		}
		it->buf = bread(it->dir->dev,
				fat_cluster_sector(it->dir->sb, it->cluster, 0),
				512);
		if(!it->buf) {
			dir_it_close(it);
			return 0;
		}
	}
}

static unsigned char *dir_ent(struct fat_dir_it *it)
{
	return it->buf ? (unsigned char *)it->buf->data + it->ent_off * 32 : NULL;
}

static void dir_it_open(struct fat_dir_it *it, struct inode *dir)
{
	it->dir = dir;
	it->cluster = dir->u.fatfs.cluster;
	it->sector_off = 0;
	it->ent_off = (unsigned int)-1;
	it->consumed = 0;
	it->buf = NULL;
	it->ok = 0;
	if(!it->cluster) {
		return;		/* FAT12/16 fixed root area (M3) */
	}
	it->buf = bread(dir->dev, fat_cluster_sector(dir->sb, it->cluster, 0),
			512);
	it->ok = it->buf ? 1 : 0;
}

/* skip 'n' slots (a resume offset) */
static int dir_skip(struct fat_dir_it *it, unsigned long n)
{
	while(n--) {
		if(!dir_next(it)) {
			return 0;
		}
	}
	return 1;
}

/* ---- name decode ---- */

static void decode_sfn(const unsigned char *e, char *out)
{
	int i, n = 0;

	for(i = 0; i < 8; i++) {
		if(e[i] == ' ') {
			break;
		}
		if((unsigned char)e[i] < 0x20) {
			continue;
		}
		out[n++] = (char)e[i];
	}
	if(e[8] != ' ') {
		out[n++] = '.';
		for(i = 8; i < 11; i++) {
			if(e[i] == ' ') {
				break;
			}
			out[n++] = (char)e[i];
		}
	}
	out[n] = '\0';
}

static unsigned char lfn_checksum(const unsigned char *e)
{
	unsigned char sum = 0;
	int i;

	for(i = 0; i < 11; i++) {
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + e[i];
	}
	return sum;
}

static void utf16_to_utf8(const unsigned char *u, int units, char *out)
{
	int i, n = 0;
	__u16 uc;

	for(i = 0; i < units; i++) {
		uc = u[i * 2] | (u[i * 2 + 1] << 8);
		if(!uc) {
			break;
		}
		if(uc < 0x80) {
			out[n++] = (char)uc;
		} else if(uc < 0x800) {
			out[n++] = (char)(0xC0 | (uc >> 6));
			out[n++] = (char)(0x80 | (uc & 0x3F));
		} else {
			out[n++] = (char)(0xE0 | (uc >> 12));
			out[n++] = (char)(0x80 | ((uc >> 6) & 0x3F));
			out[n++] = (char)(0x80 | (uc & 0x3F));
		}
	}
	out[n] = '\0';
}

/* ---- logical record scanner ---- */

struct fat_rec {
	char name[NAME_MAX + 1];
	unsigned char u16[NAME_MAX * 2];	/* LFN scratch (UTF-16LE) */
	unsigned char attr;
	__u32 cluster;
	__u32 size;
	unsigned long slot;		/* 0-based slot of the short entry */
	int is_dot;
};

/*
 * Read the next logical record from the iterator. Returns 1 with rec
 * filled, 0 at end of directory. LFN parts arrive reverse-chunked and
 * are placed at (part-1)*13 UTF-16 units.
 */
static int scan_record(struct fat_dir_it *it, struct fat_rec *rec)
{
	int have_lfn = 0, u16_units = 0;
	unsigned char lfn_sum = 0;
	unsigned char *e;

	rec->name[0] = '\0';
	rec->is_dot = 0;
	for(;;) {
		if(!dir_next(it)) {
			return 0;
		}
		e = dir_ent(it);
		if(e[0] == FAT_ENTRY_END) {
			return 0;
		}
		if(e[0] == FAT_ENTRY_DELETED) {
			continue;
		}
		if(e[11] == FAT_ATTR_LFN) {
			int part = e[0] & 0x1F;
			int off = (part - 1) * 13;
			int j;

			if(part < 1 || part > 20) {
				continue;
			}
			if(!have_lfn) {
				have_lfn = 1;
				u16_units = 0;
				lfn_sum = e[13];
			}
			for(j = 0; j < 13; j++) {
				/* the 13 UTF-16 units are not contiguous: chars
				 * 0-4 at bytes 1-10, 5-10 at bytes 14-25 and
				 * 11-12 at bytes 28-31 of the 32-byte slot */
				int bo = (j < 5) ? 1 + j * 2 :
					 (j < 11) ? 14 + (j - 5) * 2 :
						    28 + (j - 11) * 2;
				__u16 uc = e[bo] | (e[bo + 1] << 8);

				/* 0x0000 = end of the name, 0xFFFF = padding:
				 * neither is a name character */
				if(uc == 0x0000 || uc == 0xFFFF) {
					continue;
				}
				if((off + j) < NAME_MAX) {
					int at = (off + j) * 2;

					rec->u16[at] = e[bo];
					rec->u16[at + 1] = e[bo + 1];
					if(off + j + 1 > u16_units) {
						u16_units = off + j + 1;
					}
				}
			}
			continue;
		}
		/* short entry: the record's payload */
		rec->attr = e[11];
		/* fst_clus_hi at bytes 20-21, fst_clus_lo at bytes 26-27 */
		rec->cluster = ((e[20] | (e[21] << 8)) << 16) |
			       (e[26] | (e[27] << 8));
		rec->size = e[28] | (e[29] << 8) | (e[30] << 16) |
			    (e[31] << 24);
		if(rec->attr & FAT_ATTR_DIRECTORY) {
			rec->size = 0;
		}
		rec->slot = it->consumed - 1;
		if(have_lfn && lfn_checksum(e) == lfn_sum && u16_units > 0) {
			utf16_to_utf8(rec->u16, u16_units, rec->name);
		} else {
			decode_sfn(e, rec->name);
		}
		if(rec->name[0] == '.') {
			rec->is_dot = (rec->name[1] == '\0' ||
				       (rec->name[1] == '.' &&
					rec->name[2] == '\0'));
		}
		return 1;
	}
}

/* does the directory contain an entry matching 'name'? (create/rename
 * duplicate checks; the generic scanner matches LFN + 8.3 forms) */
int fat_dir_has_name(struct inode *dir, const char *name)
{
	struct fat_dir_it it;
	struct fat_rec rec;
	int found = 0;

	if(!S_ISDIR(dir->i_mode)) {
		return 1;	/* be safe: not a dir */
	}
	dir_it_open(&it, dir);
	if(!it.ok) {
		return 1;
	}
	while(!found) {
		int r = scan_record(&it, &rec);

		if(r <= 0) {
			break;
		}
		if(!rec.is_dot && name_ieq(rec.name, name)) {
			found = 1;
		}
	}
	dir_it_close(&it);
	return found;
}

/* ---- readdir/readdir64 emitters ---- */

static int fat_readdir_common(struct inode *dir, struct fd *f,
			      void *dirent, __size_t count, int is64)
{
	struct fat_dir_it it;
	struct fat_rec rec;
	int base_len, size = 0, reclen;
	unsigned long skip;

	if(!S_ISDIR(dir->i_mode)) {
		return -ENOTDIR;
	}
	base_len = is64 ?
		(int)((char *)&((struct dirent64 *)0)->d_name - (char *)0) :
		(int)((char *)&((struct dirent *)0)->d_name - (char *)0);
	skip = (unsigned long)f->offset;
	dir_it_open(&it, dir);
	if(!it.ok) {
		return -EIO;
	}
	if(!dir_skip(&it, skip)) {
		dir_it_close(&it);
		return 0;	/* resumed past the end */
	}
	for(;;) {
		if(!scan_record(&it, &rec)) {
			break;
		}
		if(rec.is_dot) {
			continue;	/* don't emit "." / ".." */
		}
		reclen = (base_len + (int)strlen(rec.name) + 1 + 7) & ~7;
		if(size + reclen >= (int)count) {
			break;
		}
		if(is64) {
			struct dirent64 *de =
				(struct dirent64 *)((char *)dirent + size);

			de->d_ino = rec.cluster ? rec.cluster : rec.slot;
			de->d_off = (__loff_t)(rec.slot + 1);
			de->d_reclen = (unsigned short)reclen;
			de->d_type = (rec.attr & FAT_ATTR_DIRECTORY) ?
				     DT_DIR : DT_REG;
			memcpy_b(de->d_name, rec.name, strlen(rec.name) + 1);
		} else {
			struct dirent *de =
				(struct dirent *)((char *)dirent + size);

			de->d_ino = rec.cluster ? rec.cluster : rec.slot;
			de->d_off = (unsigned int)(rec.slot + 1);
			de->d_reclen = (unsigned short)reclen;
			memcpy_b(de->d_name, rec.name, strlen(rec.name) + 1);
		}
		size += reclen;
		f->offset = (__loff_t)(rec.slot + 1);
	}
	dir_it_close(&it);
	return size;
}

int fat_readdir64(struct inode *dir, struct fd *f,
		  struct dirent64 *dirent, __size_t count)
{
	return fat_readdir_common(dir, f, (void *)dirent, count, 1);
}

int fat_readdir(struct inode *dir, struct fd *f,
		       struct dirent *dirent, __size_t count)
{
	return fat_readdir_common(dir, f, (void *)dirent, count, 0);
}

/* ---- lookup ---- */

int fat_lookup(const char *name, struct inode *dir, struct inode **res)
{
	struct fat_dir_it it;
	struct fat_rec rec;
	__ino_t ino;
	int found = 0;

	*res = NULL;
	if(!S_ISDIR(dir->i_mode)) {
		return -ENOTDIR;
	}
	if(dir->inode == FAT_ROOT_INO &&
	   (!strcmp(name, ".") || !strcmp(name, ".."))) {
		if(!(*res = iget(dir->sb, FAT_ROOT_INO))) {
			return -EIO;
		}
		return 0;
	}
	dir_it_open(&it, dir);
	if(!it.ok) {
		return -EIO;
	}
	while(!found) {
		int r = scan_record(&it, &rec);

		if(r <= 0) {
			break;
		}
		if(!rec.is_dot && (name_ieq(rec.name, name) ||
				   !strcmp(rec.name, name))) {
			found = 1;
		}
	}
	dir_it_close(&it);
	if(!found) {
		return -ENOENT;
	}
	/* ".." on a subdir resolves to the parent cluster; map the volume
	 * root back to FAT_ROOT_INO (the root has no ".." on disk) */
	if(rec.is_dot && !strcmp(name, "..") &&
	   rec.cluster == dir->sb->u.fatfs.root_cluster) {
		if(!(*res = iget(dir->sb, FAT_ROOT_INO))) {
			return -EIO;
		}
		return 0;
	}
	ino = rec.cluster ? rec.cluster :
	      (0x80000000u | (unsigned int)rec.slot);	/* empty file */
	if(!ino) {
		ino = 2;
	}
	if(fatfs_ent_add(dir->sb, ino, rec.cluster, rec.size,
			 !!(rec.attr & FAT_ATTR_DIRECTORY),
			 dir->u.fatfs.cluster, rec.slot)) {
		return -ENOMEM;
	}
	if(!(*res = iget(dir->sb, ino))) {
		return -ENOENT;
	}
	return 0;
}
