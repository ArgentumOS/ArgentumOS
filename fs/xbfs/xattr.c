/*
 * fnx/fs/xbfs/xattr.c
 *
 * XBFS small_data attributes. Attributes are packed into the small_data
 * tail of the inode (raw + sizeof(struct xbfs_inode) .. raw + inode_size)
 * as consecutive records in the HAIKU on-disk layout (xbfs.h):
 *
 *	struct xbfs_small_data {
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
#include <fnx/xbfs.h>
#include <fnx/fcntl.h>
#include <fnx/stat.h>
#include <fnx/string.h>

/* record size excluding the name/data payload (type + 2 x u16) */
#define XBFS_SD_HDR		8
/* Haiku small_data layout: name + strcpy NUL + 2 pad, then the data,
 * then a trailing NUL */
#define XBFS_SD_SIZE(n, d)	(XBFS_SD_HDR + (n) + 3 + (d) + 1)
#define XBFS_SD_DATA(sd)		((sd)->name + (sd)->name_size + 3)

/* the file-name record: one byte 0x13 (Haiku FILE_NAME_NAME) */
#define XBFS_SD_IS_NAME(sd)	((sd)->name_size == 1 && (sd)->name[0] == 0x13)

static char *xbfs_xattr_area(struct inode *i)
{
	return i->u.xbfs.small_data;
}

/*
 * Walk the small_data area of inode i, calling cb() for each record.
 * cb returns 0 to keep going or nonzero to stop (the value is returned
 * to the caller). Returns the cb result, or 0 when the area is
 * exhausted.
 */
typedef int (*xbfs_xattr_cb)(struct xbfs_small_data *, void *);

static int xbfs_xattr_walk(struct inode *i, xbfs_xattr_cb cb, void *arg)
{
	char *p = xbfs_xattr_area(i);
	/* the meaningful tail is block_size - inode; the in-memory copy
	 * is sized for the largest block and zeroed past the tail, so
	 * the walk would stop at the first zero record either way */
	int left = i->sb->s_blocksize - sizeof(struct xbfs_inode);
	int res;

	while(left >= XBFS_SD_HDR) {
		struct xbfs_small_data *sd = (struct xbfs_small_data *)p;
		int need = XBFS_SD_SIZE(sd->name_size, sd->data_size);

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

struct xbfs_xattr_find {
	const char *name;
	int name_size;		/* strlen (no NUL) */
	struct xbfs_small_data *found;
};

struct xbfs_xattr_remove {
	const char *name;
	int name_size;		/* strlen (no NUL) */
};

static int xbfs_xattr_find_cb(struct xbfs_small_data *sd, void *arg)
{
	struct xbfs_xattr_find *f = (struct xbfs_xattr_find *)arg;

	if(XBFS_SD_IS_NAME(sd)) {
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
 * written at create/rename via xbfs_inode_set_name()) */
static int xbfs_xattr_refuse_name(const char *name)
{
	return name[0] == 0x13 && name[1] == 0;
}

int xbfs_getxattr(struct inode *i, const char *name, char *buffer,
		 __size_t size)
{
	struct xbfs_xattr_find f;
	struct xbfs_small_data *sd;
	int nlen, dsize;

	if(xbfs_xattr_refuse_name(name)) {
		return -EACCES;
	}
	if(i->u.xbfs.raw.flags & XBFS_INODE_INLINE_DATA) {
		/* X-SSD6: an inline file's tail is pure content - no xattrs
		 * can exist while inline (setxattr converts it first) */
		return -ENODATA;
	}

	inode_lock(i);
	nlen = strlen(name);
	f.name = name;
	f.name_size = nlen;
	f.found = NULL;
	xbfs_xattr_walk(i, xbfs_xattr_find_cb, &f);
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
		memcpy_b(buffer, XBFS_SD_DATA(sd), sd->data_size);
		dsize = sd->data_size;
		inode_unlock(i);
		return dsize;
	}
	inode_unlock(i);

	/* not inline: the attribute may live in the attributes tree */
	return xbfs_attr_get(i, name, buffer, size);
}

int xbfs_setxattr(struct inode *i, const char *name, const char *value,
		 __size_t size, int flags)
{
	struct xbfs_xattr_find f;

	/* X-SSD6: an inline file's tail is pure content - converting it
	 * to a stream frees the tail for the attribute records */
	if(i->u.xbfs.raw.flags & XBFS_INODE_INLINE_DATA) {
		if(xbfs_inline_expand(i) < 0) {
			return -ENOSPC;
		}
	}

	/* Haiku's B_ATTR_NAME_LENGTH (255); refusing keeps volumes we
	 * create writable by Haiku */
	if(strlen(name) > XBFS_ATTR_NAME_MAX) {
		return -ENAMETOOLONG;
	}
	char *area;
	char *q;
	char *p;
	int left;
	int nlen, need, total, found, tree_found = 0;
	__u32 attr_type = XBFS_FILE_NAME_TYPE;
	struct xbfs_small_data *sd;

	if(xbfs_xattr_refuse_name(name)) {
		return -EACCES;
	}
	if(flags & ~(XATTR_CREATE | XATTR_REPLACE)) {
		return -EINVAL;
	}
	/* Values bigger than the small_data section fall through to the
	 * attributes tree (xbfs_attr_set) below; the fit test below uses
	 * u64 arithmetic so a huge size can never wrap into the 792-byte
	 * stack area. */
	if(size > 0x7FFFFFFF) {
		return -ENOSPC;
	}
	if(!(area = (char *)kmalloc(i->sb->s_blocksize
				- sizeof(struct xbfs_inode)))) {
		return -ENOMEM;
	}
	q = area;
	p = xbfs_xattr_area(i);
	left = i->sb->s_blocksize - sizeof(struct xbfs_inode);

	inode_lock(i);

	nlen = strlen(name);
	f.name = name;
	f.name_size = nlen;
	f.found = NULL;
	xbfs_xattr_walk(i, xbfs_xattr_find_cb, &f);
	found = (f.found != NULL);

	if(found && (flags & XATTR_CREATE)) {
		inode_unlock(i);
		kfree((addr_t)area);
		return -EEXIST;
	}
	if(found) {
		/* preserve the type of an attribute being replaced (Haiku
		 * keeps the caller's type: 'MIME' for BEOS:TYPE, int32, ...
		 * — the Linux xattr ABI has no type field, so overwriting
		 * must not clobber a type a Haiku-written record carries) */
		attr_type = f.found->type;
	} else {
		/* not inline: the attribute may live in the attributes tree
		 * (a bigger value migrated it there). A plain set must see
		 * it too: carry its type across the migration back into the
		 * small_data section and drop the stale tree entry below
		 * (Haiku checks both layers for the CREATE/REPLACE flags) */
		struct inode *attr;
		int tres = xbfs_attr_find(i, name, &attr);
		if(tres == 0) {
			if(flags & XATTR_CREATE) {
				iput(attr);
				inode_unlock(i);
				kfree((addr_t)area);
				return -EEXIST;
			}
			attr_type = attr->u.xbfs.raw.type;
			iput(attr);
			tree_found = 1;
		} else if(flags & XATTR_REPLACE) {
			inode_unlock(i);
			kfree((addr_t)area);
			return -ENODATA;
		}
	}

	/* copy every record except the one being replaced, then append
	 * the new one */
	while(left >= XBFS_SD_HDR) {
		sd = (struct xbfs_small_data *)p;
		need = XBFS_SD_SIZE(sd->name_size, sd->data_size);

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

	need = XBFS_SD_SIZE(nlen, size);
	total = (q - area) + need;
	if((__u64)total > i->sb->s_blocksize - sizeof(struct xbfs_inode)) {
		/* no room inline: Haiku moves the attribute into the
		 * per-file attributes tree, carrying the record's type */
		inode_unlock(i);
		kfree((addr_t)area);
		return xbfs_attr_set(i, name, value, size, attr_type);
	}

	*(__u32 *)(q + 0) = attr_type;
	*(__u16 *)(q + 4) = nlen;
	*(__u16 *)(q + 6) = size;
	memcpy_b(q + 8, name, nlen);
	q[8 + nlen] = 0;			/* strcpy NUL */
	memset_b(q + 9 + nlen, 0, 2);		/* pad before the data */
	if(size) {
		memcpy_b(q + 8 + nlen + 3, value, size);
	}
	q[8 + nlen + 3 + size] = 0;		/* trailing NUL */

	memset_b(xbfs_xattr_area(i), 0, XBFS_SMALL_DATA_SIZE);
	memcpy_b(xbfs_xattr_area(i), area, total);
	i->state |= INODE_DIRTY;

	inode_unlock(i);
	kfree((addr_t)area);

	/* the value now fits inline: if the attribute previously lived in
	 * the attributes tree (a bigger value), remove the stale tree
	 * entry so the two copies do not diverge (Haiku's WriteAttribute
	 * migrates back to the small_data section the same way) */
	if(tree_found) {
		xbfs_attr_remove(i, name);
	}
	return 0;
}

/*
 * Write (or replace) the file-name 0x13 small_data record, like Haiku's
 * Inode::SetName(). Called at create/rename so Haiku sees the names it
 * expects in the inode. Works for symlinks too (the name record lives in
 * the small_data tail, not the data union).
 */
int xbfs_inode_set_name(struct inode *i, const char *name)
{
	char *area;
	char *q;
	char *p;
	int left;
	int nlen, need, total;
	struct xbfs_small_data *sd;

	if(!(area = (char *)kmalloc(i->sb->s_blocksize
				- sizeof(struct xbfs_inode)))) {
		return -ENOMEM;
	}
	q = area;
	p = xbfs_xattr_area(i);
	left = i->sb->s_blocksize - sizeof(struct xbfs_inode);

	inode_lock(i);

	nlen = strlen(name);

	/* copy every record except an existing file-name record */
	while(left >= XBFS_SD_HDR) {
		sd = (struct xbfs_small_data *)p;
		need = XBFS_SD_SIZE(sd->name_size, sd->data_size);

		if(!sd->name_size || need > left) {
			break;
		}
		if(!XBFS_SD_IS_NAME(sd)) {
			memcpy_b(q, p, need);
			q += need;
		}
		p += need;
		left -= need;
	}

	need = XBFS_SD_SIZE(1, nlen);
	total = (q - area) + need;
	if(total > i->sb->s_blocksize - sizeof(struct xbfs_inode)) {
		inode_unlock(i);
		kfree((addr_t)area);
		return -ENOSPC;
	}

	*(__u32 *)(q + 0) = XBFS_FILE_NAME_TYPE;
	*(__u16 *)(q + 4) = 1;
	*(__u16 *)(q + 6) = nlen;
	q[8] = 0x13;				/* FILE_NAME_NAME */
	q[9] = 0;				/* strcpy NUL */
	memset_b(q + 10, 0, 2);			/* pad before the data */
	memcpy_b(q + 12, name, nlen);
	q[12 + nlen] = 0;			/* trailing NUL */

	memset_b(xbfs_xattr_area(i), 0, XBFS_SMALL_DATA_SIZE);
	memcpy_b(xbfs_xattr_area(i), area, total);
	i->state |= INODE_DIRTY;

	/* the in-memory small_data has changed: re-record the inode block in
	 * the current journal transaction. The block was last recorded at
	 * ialloc (before this name record existed), and a create that hands
	 * the inode to an fd (no iput at the end) would otherwise flush the
	 * pre-name content — a crash between the flush and the fd's close
	 * replays an indexed-but-nameless IN_USE inode ("missing 0x13"). */
	xbfs_write_inode(i);

	inode_unlock(i);
	kfree((addr_t)area);
	return 0;
}

/*
 * Read the file-name 0x13 small_data record (the name set at create/
 * rename). Returns the length, or -ENOENT. Used by rmdir (which has no
 * name argument) to remove the name-index entry.
 */
int xbfs_inode_get_name(struct inode *i, char *buf, int size)
{
	char *p = xbfs_xattr_area(i);
	int left = i->sb->s_blocksize - sizeof(struct xbfs_inode);
	int nlen;

	while(left >= XBFS_SD_HDR) {
		struct xbfs_small_data *sd = (struct xbfs_small_data *)p;
		int need = XBFS_SD_SIZE(sd->name_size, sd->data_size);

		if(sd->name_size == 1 && sd->data_size
				&& (sd->type == XBFS_FILE_NAME_TYPE)) {
			char *n = p + XBFS_SD_HDR + 4;	/* after 0x13/NUL/pad */
			nlen = sd->data_size;
			if(nlen > size) {
				nlen = size;
			}
			memcpy_b(buf, n, nlen);
			buf[nlen] = 0;
			return nlen;
		}
		if(need <= 0) {
			break;
		}
		p += need;
		left -= need;
	}
	return -ENOENT;
}

struct xbfs_xattr_list {
	char *list;
	__size_t size;
	int total;
	int errno;
};

static int xbfs_xattr_list_cb(struct xbfs_small_data *sd, void *arg)
{
	struct xbfs_xattr_list *l = (struct xbfs_xattr_list *)arg;
	int nlen = sd->name_size;

	if(XBFS_SD_IS_NAME(sd)) {
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

int xbfs_listxattr(struct inode *i, char *list, __size_t size)
{
	struct xbfs_xattr_list l;
	int res;

	if(i->u.xbfs.raw.flags & XBFS_INODE_INLINE_DATA) {
		/* X-SSD6: no xattrs can exist while inline */
		return 0;
	}
	l.list = list;
	l.size = size;
	l.total = 0;
	l.errno = 0;
	inode_lock(i);
	res = xbfs_xattr_walk(i, xbfs_xattr_list_cb, &l);
	inode_unlock(i);
	if(res && l.errno) {
		return l.errno;
	}
	if(l.size && l.errno) {
		return l.errno;
	}
	/* the attributes tree names follow the small_data ones */
	res = xbfs_attr_list(i, list, size, l.total);
	if(res < 0) {
		return res;
	}
	return l.total + res;
}

static int xbfs_xattr_remove_cb(struct xbfs_small_data *sd, void *arg)
{
	struct xbfs_xattr_remove *r = (struct xbfs_xattr_remove *)arg;

	if(XBFS_SD_IS_NAME(sd)) {
		return 0;
	}
	if(sd->name_size == r->name_size &&
	   !memcmp(sd->name, r->name, r->name_size)) {
		return 1;
	}
	return 0;
}

int xbfs_removexattr(struct inode *i, const char *name)
{
	struct xbfs_xattr_remove r;
	char *p;
	int left, need, found, tail;

	if(xbfs_xattr_refuse_name(name)) {
		return -EACCES;
	}
	if(i->u.xbfs.raw.flags & XBFS_INODE_INLINE_DATA) {
		/* X-SSD6: no xattrs can exist while inline */
		return -ENODATA;
	}

	inode_lock(i);

	r.name = name;
	r.name_size = strlen(name);
	found = xbfs_xattr_walk(i, xbfs_xattr_remove_cb, &r);
	if(!found) {
		inode_unlock(i);
		/* not inline: remove from the attributes tree */
		return xbfs_attr_remove(i, name);
	}

	/* compact: shift the records after the removed one down */
	p = xbfs_xattr_area(i);
	left = i->sb->s_blocksize - sizeof(struct xbfs_inode);
	while(left >= XBFS_SD_HDR) {
		struct xbfs_small_data *sd = (struct xbfs_small_data *)p;

		need = XBFS_SD_SIZE(sd->name_size, sd->data_size);
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
 * Used by xbfs_attr_set() so an attribute that moved into the attributes
 * tree leaves the small_data section (Haiku's _RemoveSmallData).
 */
int xbfs_xattr_remove_sd(struct inode *i, const char *name)
{
	struct xbfs_xattr_remove r;
	char *p;
	int left, need, found, tail;

	if(xbfs_xattr_refuse_name(name)) {
		return 0;
	}

	inode_lock(i);
	r.name = name;
	r.name_size = strlen(name);
	found = xbfs_xattr_walk(i, xbfs_xattr_remove_cb, &r);
	if(!found) {
		inode_unlock(i);
		return -ENODATA;
	}

	p = xbfs_xattr_area(i);
	left = XBFS_SMALL_DATA_SIZE;
	while(left >= XBFS_SD_HDR) {
		struct xbfs_small_data *sd = (struct xbfs_small_data *)p;

		need = XBFS_SD_SIZE(sd->name_size, sd->data_size);
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

/*
 * XBFS attribute-type ioctl (FNX extension). The Linux xattr ABI has no
 * type field, but the on-disk record and Haiku's fs_stat_attr /
 * BNode::WriteAttr carry one, so a volume moving between FNX and Haiku
 * must be able to set and query types. The name is the xattr name as
 * passed to setxattr/getxattr.
 */
int xbfs_attr_info(struct inode *i, struct xbfs_attr_info *info)
{
	struct xbfs_xattr_find f;
	struct inode *attr;
	int res;

	inode_lock(i);

	f.name = info->name;
	f.name_size = strlen(info->name);
	f.found = NULL;
	xbfs_xattr_walk(i, xbfs_xattr_find_cb, &f);
	if(f.found) {
		info->type = f.found->type;
		info->size = f.found->data_size;
		inode_unlock(i);
		return 0;
	}
	inode_unlock(i);

	if((res = xbfs_attr_find(i, info->name, &attr)) < 0) {
		return res;
	}
	info->type = attr->u.xbfs.raw.type;
	info->size = attr->i_size;
	iput(attr);
	return 0;
}

int xbfs_attr_set_type(struct inode *i, const char *name, __u32 type)
{
	struct xbfs_xattr_find f;
	struct inode *attr;
	int res;

	inode_lock(i);

	f.name = name;
	f.name_size = strlen(name);
	f.found = NULL;
	xbfs_xattr_walk(i, xbfs_xattr_find_cb, &f);
	if(f.found) {
		f.found->type = type;
		i->state |= INODE_DIRTY;
		inode_unlock(i);
		return 0;
	}
	inode_unlock(i);

	if((res = xbfs_attr_find(i, name, &attr)) < 0) {
		return res;
	}
	attr->u.xbfs.raw.type = type;
	attr->state |= INODE_DIRTY;
	iput(attr);
	return 0;
}

int xbfs_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	struct xbfs_attr_info info;
	int errno;

	switch(cmd) {
	case XBFS_IOC_GET_ATTR_INFO:
		if(copy_from_user(&info, (void *)arg, sizeof(info))) {
			return -EFAULT;
		}
		info.name[XBFS_ATTR_NAME_MAX] = 0;
		if((errno = xbfs_attr_info(i, &info)) < 0) {
			return errno;
		}
		if(copy_to_user((void *)arg, &info, sizeof(info))) {
			return -EFAULT;
		}
		return 0;

	case XBFS_IOC_SET_ATTR_TYPE:
		if(copy_from_user(&info, (void *)arg, sizeof(info))) {
			return -EFAULT;
		}
		info.name[XBFS_ATTR_NAME_MAX] = 0;
		errno = xbfs_attr_set_type(i, info.name, info.type);
		if(errno < 0) {
			return errno;
		}
		if(copy_to_user((void *)arg, &info, sizeof(info))) {
			return -EFAULT;
		}
		return 0;

	case XBFS_IOC_QUERY: {
		/* volume query: evaluate the expression against the
		 * indices and return the matching inode numbers (a probe
		 * with count == 0 only fetches the total). The user
		 * struct is { query[512]; count; inodes[65536] }; the
		 * kernel only ever touches the 516-byte prefix + the
		 * inodes at the fixed offset, so use a compact prefix
		 * struct (a full copy would blow the 4KB kernel stack). */
		struct {
			char query[XBFS_QUERY_MAX_LEN];
			__u32 count;
		} hdr;
		__u32 *inos = NULL;
		int total, n;

		if(copy_from_user(&hdr, (void *)arg,
				  XBFS_QUERY_INODES_OFF)) {
			return -EFAULT;
		}
		hdr.query[XBFS_QUERY_MAX_LEN - 1] = 0;
		if(hdr.count > XBFS_QUERY_MAX_RESULTS) {
			hdr.count = XBFS_QUERY_MAX_RESULTS;
		}
		/* kmalloc caps at PAGE_SIZE (4096): at most 1024 inodes fit in
		 * one allocation, so clamp the buffer (the total is still
		 * returned in hdr.count; a probe with count == 0 skips the
		 * allocation entirely) */
		if(hdr.count > 4096 / sizeof(__u32)) {
			hdr.count = 4096 / sizeof(__u32);
		}
		if(hdr.count) {
			if(!(inos = (__u32 *)kmalloc(hdr.count * sizeof(__u32)))) {
				return -ENOMEM;
			}
		}
		total = xbfs_query(i->sb, hdr.query, inos, hdr.count);
		if(total < 0) {
			if(inos) {
				kfree((addr_t)inos);
			}
			return total;
		}
		n = (hdr.count < (__u32)total) ? hdr.count : total;
		hdr.count = (__u32)total;
		if(n) {
			if(copy_to_user((void *)arg
					+ XBFS_QUERY_INODES_OFF,
					inos, n * sizeof(__u32))) {
				kfree((addr_t)inos);
				return -EFAULT;
			}
		}
		if(copy_to_user((void *)arg, &hdr,
				  XBFS_QUERY_INODES_OFF)) {
			kfree((addr_t)inos);
			return -EFAULT;
		}
		if(inos) {
			kfree((addr_t)inos);
		}
		return 0;
	}
	}
	return -ENOTTY;
}
