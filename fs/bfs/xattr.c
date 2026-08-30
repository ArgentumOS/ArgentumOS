/*
 * fnx/fs/bfs/xattr.c
 *
 * BFS small_data attributes (M4d). Attributes are packed into the
 * 58-byte small_data tail of the inode (raw + 198 .. raw + 256) as
 * consecutive records:
 *
 *	struct bfs_small_data {
 *		__u32 type;		attribute type ('CSTR' for xattrs)
 *		__u16 name_size;	INCLUDES the NUL terminator
 *		__u16 data_size;
 *		char name[1];		name_size bytes + data_size bytes
 *	}
 *
 * The area is terminated by a zero name_size (or a record that does not
 * fit). Values that do not fit inline are refused with -ENOSPC: larger
 * attributes would need the inode's attributes run / attribute nodes,
 * which are out of M4d scope. Symlink inodes are refused (-EOPNOTSUPP):
 * their pad[0] symlink length aliases the small_data area.
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
		int need = BFS_SD_HDR + sd->name_size + sd->data_size;

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
	int name_size;		/* strlen + 1 */
	struct bfs_small_data *found;
};

static int bfs_xattr_find_cb(struct bfs_small_data *sd, void *arg)
{
	struct bfs_xattr_find *f = (struct bfs_xattr_find *)arg;

	if(sd->name_size == f->name_size &&
	   !memcmp(sd->name, f->name, f->name_size)) {
		f->found = sd;
		return 1;
	}
	return 0;
}

struct bfs_xattr_remove {
	const char *name;
	int name_size;
};

int bfs_getxattr(struct inode *i, const char *name, char *buffer,
		 __size_t size)
{
	struct bfs_xattr_find f;
	struct bfs_small_data *sd;
	int nlen;

	if(S_ISLNK(i->i_mode)) {
		return -EOPNOTSUPP;
	}

	inode_lock(i);
	nlen = strlen(name) + 1;
	f.name = name;
	f.name_size = nlen;
	f.found = NULL;
	bfs_xattr_walk(i, bfs_xattr_find_cb, &f);
	inode_unlock(i);

	if(!(sd = f.found)) {
		return -ENODATA;
	}
	if(!buffer || size == 0) {
		return sd->data_size;
	}
	if(size < sd->data_size) {
		return -ERANGE;
	}
	memcpy_b(buffer, sd->name + nlen, sd->data_size);
	return sd->data_size;
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

	if(S_ISLNK(i->i_mode)) {
		return -EOPNOTSUPP;
	}
	if(flags & ~(XATTR_CREATE | XATTR_REPLACE)) {
		return -EINVAL;
	}

	inode_lock(i);

	nlen = strlen(name) + 1;
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

	/* copy every record except the one being replaced, then append
	 * the new one */
	while(left >= BFS_SD_HDR) {
		sd = (struct bfs_small_data *)p;
		need = BFS_SD_HDR + sd->name_size + sd->data_size;

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

	need = BFS_SD_HDR + nlen + size;
	total = (q - area) + need;
	if(total > BFS_SMALL_DATA_SIZE) {
		inode_unlock(i);
		return -ENOSPC;
	}

	*(__u32 *)(q + 0) = BFS_FILE_NAME_TYPE;
	*(__u16 *)(q + 4) = nlen;
	*(__u16 *)(q + 6) = size;
	memcpy_b(q + 8, name, nlen);
	if(size) {
		memcpy_b(q + 8 + nlen, value, size);
	}

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

	if(l->size == 0) {
		/* size-0 query: return the needed size, never touch the list */
		l->total += sd->name_size;
		return 0;
	}
	if(l->total + sd->name_size <= l->size) {
		memcpy_b(l->list + l->total, sd->name, sd->name_size);
	} else {
		l->errno = -ERANGE;
		return 1;
	}
	l->total += sd->name_size;
	return 0;
}

int bfs_listxattr(struct inode *i, char *list, __size_t size)
{
	struct bfs_xattr_list l;
	int res;

	if(S_ISLNK(i->i_mode)) {
		return -EOPNOTSUPP;
	}

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
	return l.total;
}

static int bfs_xattr_remove_cb(struct bfs_small_data *sd, void *arg)
{
	struct bfs_xattr_remove *r = (struct bfs_xattr_remove *)arg;

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

	if(S_ISLNK(i->i_mode)) {
		return -EOPNOTSUPP;
	}

	inode_lock(i);

	r.name = name;
	r.name_size = strlen(name) + 1;
	found = bfs_xattr_walk(i, bfs_xattr_remove_cb, &r);
	if(!found) {
		inode_unlock(i);
		return -ENODATA;
	}

	/* compact: shift the records after the removed one down */
	p = bfs_xattr_area(i);
	left = BFS_SMALL_DATA_SIZE;
	while(left >= BFS_SD_HDR) {
		struct bfs_small_data *sd = (struct bfs_small_data *)p;

		need = BFS_SD_HDR + sd->name_size + sd->data_size;
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
