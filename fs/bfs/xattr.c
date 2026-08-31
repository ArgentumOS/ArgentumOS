/*
 * fnx/fs/bfs/xattr.c
 *
 * BFS small_data attributes. Attributes are packed into the small_data
 * tail of the inode (raw + sizeof(struct bfs_inode) .. raw + inode_size)
 * as consecutive records in the HAIKU on-disk layout (bfs.h):
 *
 *	struct bfs_small_data {
 *		__u32 type;		attribute type ('CSTR' for xattrs)
 *		__u16 name_size;	strlen(name), WITHOUT the NUL
 *		__u16 data_size;
 *		char name[1];		name (name_size bytes) + 3 pad +
 *					data_size bytes + 1 NUL
 *	}
 *
 * On disk a record is: type(4) name_size(2) data_size(2) name(N)
 * [strcpy NUL] [2 zero pad] data(D) [NUL], i.e. 8 + N + 3 + D + 1
 * bytes, and the data starts at name + N + 3 (Haiku small_data::Data()).
 * The area is terminated by a zero name_size (or a record that does not
 * fit). Values that do not fit inline are refused with -ENOSPC: larger
 * attributes would need the inode's attributes run / attribute nodes,
 * which are out of scope. The file-name record (name_size == 1, name ==
 * 0x13, data = the file name) is written at create/rename like Haiku's
 * Inode::SetName() and is hidden from the xattr API (Haiku's
 * AttributeIterator skips it and CheckAccess refuses it).
 *
 * Symlink inodes are refused for xattr set/remove (-EOPNOTSUPP): the
 * symlink target occupies the data union, and keeping the tail untouched
 * there is simplest. The 0x13 name record is still written for symlinks.
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/bfs.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/string.h>

/* record size excluding the name/data payload (type + 2 x u16) */
#define BFS_SD_HDR		8
/* Haiku small_data layout: name + strcpy NUL + 2 pad, then the data,
 * then a trailing NUL */
#define BFS_SD_SIZE(n, d)	(BFS_SD_HDR + (n) + 3 + (d) + 1)
#define BFS_SD_DATA(sd)		((sd)->name + (sd)->name_size + 3)

/* the file-name record: one byte 0x13 (Haiku FILE_NAME_NAME) */
#define BFS_SD_IS_NAME(sd)	((sd)->name_size == 1 && (sd)->name[0] == 0x13)

static char *bfs_xattr_area(struct inode *i)
{
	return i->u.bfs.small_data;
}

/*
 * Walk the small_data area of inode i, calling cb() for each record.
 * cb returns 0 to keep going or nonzero to stop (the value is returned
 * to the caller). Returns the cb result, or 0 when the area is
 * exhausted.
 */
typedef int (*bfs_xattr_cb)(struct bfs_small_data *, void *);

static int bfs_xattr_walk(struct inode *i, bfs_xattr_cb cb, void *arg)
{
	char *p = bfs_xattr_area(i);
	int left = BFS_SMALL_DATA_SIZE;
	int res;

	while(left >= BFS_SD_HDR) {
		struct bfs_small_data *sd = (struct bfs_small_data *)p;
		int need = BFS_SD_SIZE(sd->name_size, sd->data_size);

		if(!sd->name_size || need > left) {
			break;
		}
		if((res = cb(sd, arg))) {
			return res;
		}
		p += need;
		left -= need;
	}
	return 0;
}

struct bfs_xattr_find {
	const char *name;
	int name_size;		/* strlen (no NUL) */
	struct bfs_small_data *found;
};

struct bfs_xattr_remove {
	const char *name;
	int name_size;		/* strlen (no NUL) */
};

static int bfs_xattr_find_cb(struct bfs_small_data *sd, void *arg)
{
	struct bfs_xattr_find *f = (struct bfs_xattr_find *)arg;

	if(BFS_SD_IS_NAME(sd)) {
		return 0;
	}
	if(sd->name_size == f->name_size &&
	   !memcmp(sd->name, f->name, f->name_size)) {
		f->found = sd;
		return 1;
	}
	return 0;
}

/* the file-name record is not accessible through the xattr API (it is
 * written at create/rename via bfs_inode_set_name()) */
static int bfs_xattr_refuse_name(const char *name)
{
	return name[0] == 0x13 && name[1] == 0;
}

int bfs_getxattr(struct inode *i, const char *name, char *buffer,
		 __size_t size)
{
	struct bfs_xattr_find f;
	struct bfs_small_data *sd;
	int nlen, dsize;

	if(bfs_xattr_refuse_name(name)) {
		return -EACCES;
	}

	inode_lock(i);
	nlen = strlen(name);
	f.name = name;
	f.name_size = nlen;
	f.found = NULL;
	bfs_xattr_walk(i, bfs_xattr_find_cb, &f);
	sd = f.found;
	if(sd && (!buffer || size == 0)) {
		/* size query: report without copying */
		dsize = sd->data_size;
		inode_unlock(i);
		return dsize;
	}
	if(sd && size < sd->data_size) {
		dsize = -ERANGE;
		inode_unlock(i);
		return dsize;
	}
	if(sd) {
		memcpy_b(buffer, BFS_SD_DATA(sd), sd->data_size);
		dsize = sd->data_size;
		inode_unlock(i);
		return dsize;
	}
	inode_unlock(i);

	/* not inline: the attribute may live in the attributes tree */
	return bfs_attr_get(i, name, buffer, size);
}

int bfs_setxattr(struct inode *i, const char *name, const char *value,
		 __size_t size, int flags)
{
	struct bfs_xattr_find f;
	char area[BFS_SMALL_DATA_SIZE];
	char *q = area;
	char *p = bfs_xattr_area(i);
	int left = BFS_SMALL_DATA_SIZE;
	int nlen, need, total, found;
	struct bfs_small_data *sd;

	if(bfs_xattr_refuse_name(name)) {
		return -EACCES;
	}
	if(flags & ~(XATTR_CREATE | XATTR_REPLACE)) {
		return -EINVAL;
	}
	/* Values bigger than the small_data section fall through to the
	 * attributes tree (bfs_attr_set) below; the fit test below uses
	 * u64 arithmetic so a huge size can never wrap into the 792-byte
	 * stack area. */
	if(size > 0x7FFFFFFF) {
		return -ENOSPC;
	}

	inode_lock(i);

	nlen = strlen(name);
	f.name = name;
	f.name_size = nlen;
	f.found = NULL;
	bfs_xattr_walk(i, bfs_xattr_find_cb, &f);
	found = (f.found != NULL);

	if(found && (flags & XATTR_CREATE)) {
		inode_unlock(i);
		return -EEXIST;
	}
	if(!found && (flags & XATTR_REPLACE)) {
		inode_unlock(i);
		return -ENODATA;
	}
	if(!found && (flags & (XATTR_CREATE | XATTR_REPLACE))) {
		/* the attribute may live in the attributes tree; the flags
		 * must see it there too (Haiku checks both layers) */
		struct inode *attr;
		int tres = bfs_attr_find(i, name, &attr);
		if(tres == 0) {
			iput(attr);
			if(flags & XATTR_CREATE) {
				inode_unlock(i);
				return -EEXIST;
			}
			found = 1;
		} else if(flags & XATTR_REPLACE) {
			inode_unlock(i);
			return -ENODATA;
		}
	}

	/* copy every record except the one being replaced, then append
	 * the new one */
	while(left >= BFS_SD_HDR) {
		sd = (struct bfs_small_data *)p;
		need = BFS_SD_SIZE(sd->name_size, sd->data_size);

		if(!sd->name_size || need > left) {
			break;
		}
		if(!(found && sd->name_size == nlen &&
		     !memcmp(sd->name, name, nlen))) {
			memcpy_b(q, p, need);
			q += need;
		}
		p += need;
		left -= need;
	}

	need = BFS_SD_SIZE(nlen, size);
	total = (q - area) + need;
	if((__u64)total > BFS_SMALL_DATA_SIZE) {
		/* no room inline: Haiku moves the attribute into the
		 * per-file attributes tree */
		inode_unlock(i);
		return bfs_attr_set(i, name, value, size);
	}

	*(__u32 *)(q + 0) = BFS_FILE_NAME_TYPE;
	*(__u16 *)(q + 4) = nlen;
	*(__u16 *)(q + 6) = size;
	memcpy_b(q + 8, name, nlen);
	q[8 + nlen] = 0;			/* strcpy NUL */
	memset_b(q + 9 + nlen, 0, 2);		/* pad before the data */
	if(size) {
		memcpy_b(q + 8 + nlen + 3, value, size);
	}
	q[8 + nlen + 3 + size] = 0;		/* trailing NUL */

	memset_b(bfs_xattr_area(i), 0, BFS_SMALL_DATA_SIZE);
	memcpy_b(bfs_xattr_area(i), area, total);
	i->state |= INODE_DIRTY;

	inode_unlock(i);

	/* the value now fits inline: if the attribute previously lived in
	 * the attributes tree (a bigger value), remove the stale tree
	 * entry so the two copies do not diverge (Haiku's WriteAttribute
	 * migrates back to the small_data section the same way) */
	if(!found) {
		bfs_attr_remove(i, name);
	}
	return 0;
}

/*
 * Write (or replace) the file-name 0x13 small_data record, like Haiku's
 * Inode::SetName(). Called at create/rename so Haiku sees the names it
 * expects in the inode. Works for symlinks too (the name record lives in
 * the small_data tail, not the data union).
 */
int bfs_inode_set_name(struct inode *i, const char *name)
{
	char area[BFS_SMALL_DATA_SIZE];
	char *q = area;
	char *p = bfs_xattr_area(i);
	int left = BFS_SMALL_DATA_SIZE;
	int nlen, need, total;
	struct bfs_small_data *sd;

	inode_lock(i);

	nlen = strlen(name);

	/* copy every record except an existing file-name record */
	while(left >= BFS_SD_HDR) {
		sd = (struct bfs_small_data *)p;
		need = BFS_SD_SIZE(sd->name_size, sd->data_size);

		if(!sd->name_size || need > left) {
			break;
		}
		if(!BFS_SD_IS_NAME(sd)) {
			memcpy_b(q, p, need);
			q += need;
		}
		p += need;
		left -= need;
	}

	need = BFS_SD_SIZE(1, nlen);
	total = (q - area) + need;
	if(total > BFS_SMALL_DATA_SIZE) {
		inode_unlock(i);
		return -ENOSPC;
	}

	*(__u32 *)(q + 0) = BFS_FILE_NAME_TYPE;
	*(__u16 *)(q + 4) = 1;
	*(__u16 *)(q + 6) = nlen;
	q[8] = 0x13;				/* FILE_NAME_NAME */
	q[9] = 0;				/* strcpy NUL */
	memset_b(q + 10, 0, 2);			/* pad before the data */
	memcpy_b(q + 12, name, nlen);
	q[12 + nlen] = 0;			/* trailing NUL */

	memset_b(bfs_xattr_area(i), 0, BFS_SMALL_DATA_SIZE);
	memcpy_b(bfs_xattr_area(i), area, total);
	i->state |= INODE_DIRTY;

	inode_unlock(i);
	return 0;
}

struct bfs_xattr_list {
	char *list;
	__size_t size;
	int total;
	int errno;
};

static int bfs_xattr_list_cb(struct bfs_small_data *sd, void *arg)
{
	struct bfs_xattr_list *l = (struct bfs_xattr_list *)arg;
	int nlen = sd->name_size;

	if(BFS_SD_IS_NAME(sd)) {
		/* the file-name record is not exposed */
		return 0;
	}

	if(l->size == 0) {
		/* size-0 query: return the needed size, never touch the list */
		l->total += nlen + 1;
		return 0;
	}
	if(l->total + nlen + 1 <= l->size) {
		memcpy_b(l->list + l->total, sd->name, nlen);
		l->list[l->total + nlen] = 0;	/* NUL-separated names */
	} else {
		l->errno = -ERANGE;
		return 1;
	}
	l->total += nlen + 1;
	return 0;
}

int bfs_listxattr(struct inode *i, char *list, __size_t size)
{
	struct bfs_xattr_list l;
	int res;

	l.list = list;
	l.size = size;
	l.total = 0;
	l.errno = 0;
	inode_lock(i);
	res = bfs_xattr_walk(i, bfs_xattr_list_cb, &l);
	inode_unlock(i);
	if(res && l.errno) {
		return l.errno;
	}
	if(l.size && l.errno) {
		return l.errno;
	}
	/* the attributes tree names follow the small_data ones */
	res = bfs_attr_list(i, list, size, l.total);
	if(res < 0) {
		return res;
	}
	return l.total + res;
}

static int bfs_xattr_remove_cb(struct bfs_small_data *sd, void *arg)
{
	struct bfs_xattr_remove *r = (struct bfs_xattr_remove *)arg;

	if(BFS_SD_IS_NAME(sd)) {
		return 0;
	}
	if(sd->name_size == r->name_size &&
	   !memcmp(sd->name, r->name, r->name_size)) {
		return 1;
	}
	return 0;
}

int bfs_removexattr(struct inode *i, const char *name)
{
	struct bfs_xattr_remove r;
	char *p;
	int left, need, found, tail;

	if(bfs_xattr_refuse_name(name)) {
		return -EACCES;
	}

	inode_lock(i);

	r.name = name;
	r.name_size = strlen(name);
	found = bfs_xattr_walk(i, bfs_xattr_remove_cb, &r);
	if(!found) {
		inode_unlock(i);
		/* not inline: remove from the attributes tree */
		return bfs_attr_remove(i, name);
	}

	/* compact: shift the records after the removed one down */
	p = bfs_xattr_area(i);
	left = BFS_SMALL_DATA_SIZE;
	while(left >= BFS_SD_HDR) {
		struct bfs_small_data *sd = (struct bfs_small_data *)p;

		need = BFS_SD_SIZE(sd->name_size, sd->data_size);
		if(!sd->name_size || need > left) {
			break;
		}
		if(sd->name_size == r.name_size &&
		   !memcmp(sd->name, r.name, r.name_size)) {
			/* remove: shift the tail down by 'need'; 'left'
			 * still includes this record, so the bytes after
			 * it are (left - need) */
			tail = left - need;
			if(tail > 0) {
				memmove(p, p + need, tail);
			}
			memset_b(p + tail, 0, need);
			break;
		}
		p += need;
		left -= need;
	}

	i->state |= INODE_DIRTY;
	inode_unlock(i);
	return 0;
}

/*
 * Remove one small_data record by name (no unlock, no tree fallback).
 * Used by bfs_attr_set() so an attribute that moved into the attributes
 * tree leaves the small_data section (Haiku's _RemoveSmallData).
 */
int bfs_xattr_remove_sd(struct inode *i, const char *name)
{
	struct bfs_xattr_remove r;
	char *p;
	int left, need, found, tail;

	if(bfs_xattr_refuse_name(name)) {
		return 0;
	}

	inode_lock(i);
	r.name = name;
	r.name_size = strlen(name);
	found = bfs_xattr_walk(i, bfs_xattr_remove_cb, &r);
	if(!found) {
		inode_unlock(i);
		return -ENODATA;
	}

	p = bfs_xattr_area(i);
	left = BFS_SMALL_DATA_SIZE;
	while(left >= BFS_SD_HDR) {
		struct bfs_small_data *sd = (struct bfs_small_data *)p;

		need = BFS_SD_SIZE(sd->name_size, sd->data_size);
		if(!sd->name_size || need > left) {
			break;
		}
		if(sd->name_size == r.name_size &&
		   !memcmp(sd->name, r.name, r.name_size)) {
			tail = left - need;
			if(tail > 0) {
				memmove(p, p + need, tail);
			}
			memset_b(p + tail, 0, need);
			break;
		}
		p += need;
		left -= need;
	}

	i->state |= INODE_DIRTY;
	inode_unlock(i);
	return 0;
}
