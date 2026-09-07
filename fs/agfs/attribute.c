/*
 * fnx/fs/agfs/attribute.c
 *
 * Haiku-compatible attribute inodes (the per-file attributes B+tree).
 *
 * When an attribute no longer fits in the inode's small_data tail (see
 * xattr.c), Haiku moves it into a per-file attributes tree
 * (Inode.cpp CreateAttribute / _RemoveAttribute / WriteAttribute):
 *
 *	file->attributes run -> the ATTRIBUTES INODE:
 *		mode   = S_ATTR_DIR | S_IFDIR | S_STR_INDEX | 0666
 *		flags  = INODE_IN_USE | INODE_ATTR_INODE (legacy)
 *		parent = the file's block run
 *		type   = 0
 *		stream = a STRING B+tree: key = attribute name, value = the
 *			attribute file's inode block number
 *		(no "." / ".." entries, no 0x13 name record, no index bits)
 *
 *	each ATTRIBUTE FILE inode:
 *		mode   = S_ATTR | S_IFREG | 0666
 *		flags  = INODE_IN_USE | INODE_ATTR_INODE (legacy)
 *		parent = the attributes inode's block run
 *		type   = the attribute type ('CSTR' = B_STRING_TYPE for us)
 *		stream = the attribute value
 *		(no name record: the name lives in the tree key)
 *
 * The btree engine (btree.c) is reused as-is: the attributes inode looks
 * like a directory to it (STRING keys, values = inode block numbers),
 * except its tree starts empty (no "." / "..").
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/agfs.h>
#include <fnx/buffer.h>
#include <fnx/stat.h>
#include <fnx/string.h>

extern struct fs_operations agfs_fsop;
extern int agfs_btree_insert(struct inode *, const char *, __ino_t);
extern int agfs_btree_delete(struct inode *, const char *);
extern int agfs_btree_find(struct inode *, const char *, __ino_t *);
extern int agfs_btree_iterate(struct inode *,
			     int (*)(const char *, __ino_t, void *), void *);

/* remove one small_data attribute record (xattr.c); used by the tree
 * write path so the attribute leaves the small_data section (Haiku) */
extern int agfs_xattr_remove_sd(struct inode *, const char *);

static int agfs_attr_count_cb(const char *, __ino_t, void *);
static int agfs_attr_free_cb(const char *, __ino_t, void *);

/* block number of a block_run (all groups are ag_shift'd blocks) */
static __blk_t agfs_run_block(struct superblock *sb, struct agfs_block_run *r)
{
	return (r->allocation_group << sb->u.agfs.ag_shift) + r->start;
}

/*
 * Load the attributes inode of 'i' (iget, counted) or NULL when the
 * file has none.
 */
static struct inode *agfs_attr_dir(struct inode *i)
{
	struct agfs_block_run *r = &i->u.agfs.raw.attributes;

	if(!r->len) {
		return NULL;
	}
	return iget(i->sb, agfs_run_block(i->sb, r));
}

/*
 * Create the attributes inode for 'i' (Haiku's CreateAttribute):
 * allocates the inode, gives it its own tree (header + one empty leaf,
 * NO dots), and points the file's attributes run at it.
 */
static struct inode *agfs_attr_dir_create(struct inode *i)
{
	struct inode *ai;
	struct buffer *buf, *buf2;
	struct agfs_btree_header *h;
	struct agfs_btree_node *n;
	__blk_t block, block2;

	if(!(ai = ialloc(i->sb, S_IFDIR | 0666))) {
		return NULL;
	}
	ai->count = 1;
	ai->dev = i->dev;
	ai->fsop = i->fsop;
	ai->i_mode = S_IFDIR | 0666;
	ai->i_uid = current->euid;
	ai->i_gid = current->egid;
	ai->i_nlink = 1;
	ai->i_blocks = 0;

	/* the tree: a header block and one empty leaf. On any failure the
	 * half-built attrs inode must be freed, not left cached */
	if((block = bmap(ai, 0, FOR_WRITING)) < 0) {
		ai->i_nlink = 0;
		iput(ai);
		return NULL;
	}
	if((block2 = bmap(ai, AGFS_BTREE_NODE_SIZE, FOR_WRITING)) < 0) {
		ai->i_nlink = 0;
		iput(ai);
		return NULL;
	}
	if(!(buf = bread(ai->dev, block, ai->sb->s_blocksize))) {
		ai->i_nlink = 0;
		iput(ai);
		return NULL;
	}
	h = (struct agfs_btree_header *)buf->data;
	h->magic = AGFS_BTREE_MAGIC;
	h->node_size = AGFS_BTREE_NODE_SIZE;
	h->max_depth = 1;
	h->data_type = AGFS_BTREE_STRING_TYPE;
	h->root_node_ptr = AGFS_BTREE_NODE_SIZE;
	h->free_node_ptr = AGFS_BTREE_NULL;
	h->max_size = 2 * AGFS_BTREE_NODE_SIZE;
	bwrite(buf);

	if(!(buf2 = bread(ai->dev, block2, ai->sb->s_blocksize))) {
		ai->i_nlink = 0;
		iput(ai);
		return NULL;
	}
	n = (struct agfs_btree_node *)((char *)buf2->data
		+ (AGFS_BTREE_NODE_SIZE % ai->sb->s_blocksize));
	n->left = AGFS_BTREE_NULL;
	n->right = AGFS_BTREE_NULL;
	n->overflow = AGFS_BTREE_NULL;
	n->all_key_count = 0;
	n->all_key_length = 0;
	bwrite(buf2);

	/* the inode: extended mode bits + legacy attribute flag. NOTE:
	 * the in-memory raw.inode_num is NOT set by ialloc (only the disk
	 * buffer and i->inode are), so runs must be built from i->inode */
	ai->u.agfs.raw.mode = AGFS_S_ATTR_DIR | AGFS_S_STR_INDEX | S_IFDIR | 0666;
	ai->u.agfs.raw.flags = AGFS_INODE_IN_USE | AGFS_INODE_ATTR_INODE;
	ai->u.agfs.raw.type = 0;
	agfs_run_encode(&ai->u.agfs.raw.parent, i->inode, i->sb->u.agfs.ag_shift);
	ai->u.agfs.raw.parent.len = 1;
	ai->i_size = 2 * AGFS_BTREE_NODE_SIZE;
	ai->i_blocks = (ai->i_size + 511) >> 9;
	ai->state |= INODE_DIRTY;

	/* point the file's attributes run at the new inode */
	agfs_run_encode(&i->u.agfs.raw.attributes, ai->inode, i->sb->u.agfs.ag_shift);
	i->u.agfs.raw.attributes.len = 1;
	i->state |= INODE_DIRTY;
	return ai;
}

/*
 * Find the attribute file inode for 'name' in the attributes tree.
 * Returns 0 with '*attr' set (counted) on success, or a negative errno.
 */
int agfs_attr_find(struct inode *i, const char *name,
		   struct inode **attr)
{
	struct inode *ai;
	__ino_t ino;

	if(!(ai = agfs_attr_dir(i))) {
		return -ENODATA;
	}
	if(agfs_btree_find(ai, name, &ino)) {
		iput(ai);
		return -ENODATA;
	}
	iput(ai);
	if(!(*attr = iget(i->sb, ino))) {
		return -EIO;
	}
	return 0;
}

/* write 'size' bytes of 'value' into the data stream of 'attr' */
static int agfs_attr_write_stream(struct inode *attr, const char *value,
				 __size_t size)
{
	__size_t total = 0;
	__off_t offset = 0;

	while(total < size) {
		int boffset = offset & (attr->sb->s_blocksize - 1);
		__blk_t block;
		struct buffer *buf;
		int bytes;

		if((block = bmap(attr, offset, FOR_WRITING)) < 0) {
			return block;
		}
		bytes = attr->sb->s_blocksize - boffset;
		bytes = MIN(bytes, (int)(size - total));
		if(!(buf = bread(attr->dev, block, attr->sb->s_blocksize))) {
			return -EIO;
		}
		memcpy_b(buf->data + boffset, value + total, bytes);
		bwrite(buf);
		total += bytes;
		offset += bytes;
	}
	attr->i_size = size;
	attr->i_blocks = (size + 511) >> 9;
	attr->state |= INODE_DIRTY;
	return 0;
}

/*
 * Create the attribute file inode for 'name' (if missing) and write
 * 'value'. Called by agfs_attr_set when the attribute does not fit the
 * small_data section (Haiku's CreateAttribute path: remove the inline
 * record first, then create the file, then write its stream).
 */
int agfs_attr_set(struct inode *i, const char *name, const char *value,
		 __size_t size, __u32 type)
{
	struct inode *ai, *attr;
	int res;

	/* Haiku: the attribute leaves the small_data section */
	agfs_xattr_remove_sd(i, name);

	if(!(ai = agfs_attr_dir(i))) {
		if(!(ai = agfs_attr_dir_create(i))) {
			return -ENOSPC;
		}
	}

	res = agfs_attr_find(i, name, &attr);
	if(res == -ENODATA) {
		/* create the attribute file inode + the tree entry */
		if(!(attr = ialloc(i->sb, S_IFREG | 0666))) {
				iput(ai);
			return -ENOSPC;
		}
		attr->count = 1;
		attr->dev = i->dev;
		attr->fsop = i->fsop;
		attr->i_mode = S_IFREG | 0666;
		attr->i_uid = current->euid;
		attr->i_gid = current->egid;
		attr->i_nlink = 1;
		attr->i_blocks = 0;
		attr->u.agfs.raw.mode = AGFS_S_ATTR | S_IFREG | 0666;
		attr->u.agfs.raw.flags = AGFS_INODE_IN_USE | AGFS_INODE_ATTR_INODE;
		/* 'CSTR' unless the caller migrated a typed inline record
		 * into the tree (Haiku's CreateAttribute carries the type) */
		attr->u.agfs.raw.type = type ? type : AGFS_FILE_NAME_TYPE;
		agfs_run_encode(&attr->u.agfs.raw.parent, ai->inode, i->sb->u.agfs.ag_shift);
		attr->u.agfs.raw.parent.len = 1;
		attr->state |= INODE_DIRTY;

		if((res = agfs_btree_insert(ai, name, attr->inode)) < 0) {
			attr->i_nlink = 0;
			iput(attr);
			iput(ai);
			return res;
		}
	} else if(res < 0) {
		iput(ai);
		return res;
	}

	if((res = agfs_attr_write_stream(attr, value, size)) < 0) {
		iput(attr);
		iput(ai);
		return res;
	}

	iput(attr);
	iput(ai);
	return 0;
}

/*
 * Read an attribute that lives in the attributes tree (the small_data
 * path in xattr.c already tried). Returns the size, or copies up to
 * 'size' bytes like getxattr.
 */
int agfs_attr_get(struct inode *i, const char *name, char *buffer,
		 __size_t size)
{
	struct inode *attr;
	__size_t total = 0;
	int res;

	if((res = agfs_attr_find(i, name, &attr)) < 0) {
		return res;
	}
	if(!buffer || size == 0) {
		res = attr->i_size;
		iput(attr);
		return res;
	}
	if(size < attr->i_size) {
		iput(attr);
		return -ERANGE;
	}
	while(total < attr->i_size) {
		int boffset = total & (attr->sb->s_blocksize - 1);
		__blk_t block;
		struct buffer *buf;
		int bytes;

		if((block = bmap(attr, total, FOR_READING)) < 0) {
			iput(attr);
			return block;
		}
		if(!block) {
			/* sparse: zeros */
			bytes = attr->sb->s_blocksize - boffset;
			bytes = MIN(bytes, (int)(attr->i_size - total));
			memset_b(buffer + total, 0, bytes);
			total += bytes;
			continue;
		}
		if(!(buf = bread(attr->dev, block, attr->sb->s_blocksize))) {
			iput(attr);
			return -EIO;
		}
		bytes = attr->sb->s_blocksize - boffset;
		bytes = MIN(bytes, (int)(attr->i_size - total));
		memcpy_b(buffer + total, buf->data + boffset, bytes);
		brelse(buf);
		total += bytes;
	}
	iput(attr);
	return total;
}

struct agfs_attr_list_ctx {
	char *list;
	__size_t size;
	int total;
	int errno;
};

static int agfs_attr_list_cb(const char *name, __ino_t ino, void *arg)
{
	struct agfs_attr_list_ctx *l = (struct agfs_attr_list_ctx *)arg;
	int nlen = strlen(name);

	if(l->size == 0) {
		l->total += nlen + 1;
		return 0;
	}
	if(l->total + nlen + 1 <= l->size) {
		memcpy_b(l->list + l->total, name, nlen);
		l->list[l->total + nlen] = 0;	/* NUL-separated names */
	} else {
		l->errno = -ERANGE;
		return 1;
	}
	l->total += nlen + 1;
	return 0;
}

/* append the tree attribute names after the small_data ones ('start'
 * is the byte offset already written by the small_data walk) */
int agfs_attr_list(struct inode *i, char *list, __size_t size, int start)
{
	struct inode *ai;
	struct agfs_attr_list_ctx l;
	int res;

	if(!(ai = agfs_attr_dir(i))) {
		return 0;
	}
	l.list = list;
	l.size = size;
	l.total = start;
	l.errno = 0;
	res = agfs_btree_iterate(ai, agfs_attr_list_cb, &l);
	iput(ai);
	if(res && l.errno) {
		return l.errno;
	}
	return l.total - start;
}

/*
 * Remove the attribute file for 'name' from the tree and free it
 * (Haiku's _RemoveAttribute). When the tree becomes empty the
 * attributes inode itself is freed and the file's attributes run is
 * cleared.
 */
int agfs_attr_remove(struct inode *i, const char *name)
{
	struct inode *ai, *attr;
	int count = 0;
	int res;

	if(!(ai = agfs_attr_dir(i))) {
		return -ENODATA;
	}
	if((res = agfs_attr_find(i, name, &attr)) < 0) {
		iput(ai);
		return res;
	}

	if((res = agfs_btree_delete(ai, name)) < 0) {
		iput(attr);
		iput(ai);
		return res;
	}

	/* free the attribute file inode (truncate stream + free block) */
	attr->i_nlink = 0;
	iput(attr);

	/* drop the attributes inode when the tree is empty */
	agfs_btree_iterate(ai, agfs_attr_count_cb, &count);
	if(count == 0) {
		ai->i_nlink = 0;
		iput(ai);
		i->u.agfs.raw.attributes.allocation_group = 0;
		i->u.agfs.raw.attributes.start = 0;
		i->u.agfs.raw.attributes.len = 0;
		i->state |= INODE_DIRTY;
	} else {
		iput(ai);
	}
	return 0;
}

/*
 * Free every attribute of 'i': walk the attributes tree, free each
 * attribute file inode (stream + block), free the tree stream and the
 * attributes inode block, and clear the file's attributes run. Called
 * from agfs_ifree() on unlink — without this, a Haiku file with
 * attribute inodes would leak its tree and attribute blocks.
 */
void agfs_attr_free_all(struct inode *i)
{
	struct inode *ai;

	if(!i->u.agfs.raw.attributes.len) {
		return;
	}
	if(!(ai = agfs_attr_dir(i))) {
		return;
	}

	agfs_btree_iterate(ai, agfs_attr_free_cb, i->sb);

	ai->i_nlink = 0;
	iput(ai);

	i->u.agfs.raw.attributes.allocation_group = 0;
	i->u.agfs.raw.attributes.start = 0;
	i->u.agfs.raw.attributes.len = 0;
	i->state |= INODE_DIRTY;
}

/* callback: count the tree entries */
static int agfs_attr_count_cb(const char *name, __ino_t ino, void *arg)
{
	(*(int *)arg)++;
	return 0;
}

/* callback: free one attribute file (truncate stream + free the block).
 * The superblock rides in 'arg'. */
static int agfs_attr_free_cb(const char *name, __ino_t ino, void *arg)
{
	struct superblock *sb = (struct superblock *)arg;
	struct inode *attr;

	if(!(attr = iget(sb, ino))) {
		return 0;
	}
	attr->i_nlink = 0;
	iput(attr);
	return 0;
}
