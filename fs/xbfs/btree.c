/*
 * fnx/fs/xbfs/btree.c
 *
 * XBFS B+tree access (read-only).
 *
 * The directory B+tree lives in the directory inode's data stream: the
 * btree header (struct xbfs_btree_header) is at stream offset 0, and the
 * tree nodes (struct xbfs_btree_node, node_size bytes each) are at byte
 * offsets root_node_ptr / left / right / overflow within the stream.
 * Node links are stream byte offsets; a leaf has overflow == -1.
 *
 * On-disk layout of a node (all little-endian):
 *   header: left(8) right(8) overflow(8) all_key_count(2) all_key_length(2)
 *   key area: all_key_length bytes of concatenated key strings
 *   padding to 8-byte alignment
 *   key-length index: all_key_count x u16 (cumulative end offsets)
 *   values: leaf: all_key_count x u64 inode block numbers
 *           interior: (all_key_count+1) x u64 child node offsets
 *
 * Cross-checked against Linux fs/befs/btree.c and Haiku BPlusTree.cpp.
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/xbfs.h>
#include <fnx/buffer.h>
#include <fnx/string.h>

struct xbfs_btree_pair {
	const char *key;
	int keylen;
	__u64 val;
};

static int xbfs_btree_descend_t(struct inode *, const struct xbfs_btree_header *,
			      const char *, int, __u64 *, int *);
static int xbfs_btree_descend(struct inode *, const struct xbfs_btree_header *,
			     const char *, __u64 *, int *);
static int xbfs_btree_search_node_t(struct xbfs_btree_node *, const char *, int,
				   int *, int);
static int xbfs_btree_collect_t(struct xbfs_btree_node *, struct xbfs_btree_pair *,
			      int, int, const char *, int, __u64, int, char *);
static int xbfs_btree_serialize(struct xbfs_btree_node *, struct xbfs_btree_pair *,
			       int, __u64, __u64, __u64, __u32);
static int xbfs_btree_split(struct inode *, struct xbfs_btree_header *,
			   __u64 *, int, struct xbfs_btree_pair *, int, int,
			   __u64, int, int);
static int xbfs_btree_insert_dup(struct inode *, __u64, int, __u64);
static int xbfs_btree_remove_dup(struct inode *, __u64, int, __u64);
static int xbfs_dup_link_type(__u64);
static __u64 xbfs_dup_link_off(__u64);
static int xbfs_dup_link_frag(__u64);
static __u64 *xbfs_dup_array(struct buffer *, __u64, int);
static __u64 *xbfs_dup_node_array(struct xbfs_btree_node *);

/* key-length index: cumulative end offsets of each key */
static __u16 *xbfs_btree_keylen_index(struct xbfs_btree_node *n)
{
	return (__u16 *)((char *)n
			+ ((sizeof(struct xbfs_btree_node) + n->all_key_length + 7) & ~7));
}

/* value array: follows the key-length index */
static __u64 *xbfs_btree_values(struct xbfs_btree_node *n)
{
	return (__u64 *)((char *)xbfs_btree_keylen_index(n)
			+ n->all_key_count * sizeof(__u16));
}

/* key i: pointer into the key area + its length */
static char *xbfs_btree_key(struct xbfs_btree_node *n, int index, int *keylen)
{
	__u16 *kl = xbfs_btree_keylen_index(n);
	char *keys = (char *)n + sizeof(struct xbfs_btree_node);
	int prev = index ? kl[index - 1] : 0;

	*keylen = kl[index] - prev;
	return keys + prev;
}

/*
 * Read a btree node at stream byte offset 'off'. Returns NULL on error.
 * Nodes are XBFS_BTREE_NODE_SIZE (1024) bytes, addressed at 1024-byte
 * offsets within the inode's stream; with block sizes above 1024 several
 * nodes share one block, so the caller must use xbfs_btree_node_buf() to
 * find the node inside the returned buffer.
 */
static struct buffer *xbfs_btree_read_node_tagged(struct inode *, __u64,
					      const char *);

static struct buffer *xbfs_btree_read_node(struct inode *i, __u64 off)
{
	return xbfs_btree_read_node_tagged(i, off, "?");
}

static struct buffer *xbfs_btree_read_node_tagged(struct inode *i, __u64 off,
					      const char *tag)
{
	__blk_t block;
	struct buffer *buf;

	if(off % XBFS_BTREE_NODE_SIZE) {
		return NULL;	/* nodes are node-size aligned */
	}
		if((block = xbfs_bmap(i, (__off_t)off, FOR_READING)) < 0) {
				return NULL;
	}
		if(!block) {
		return NULL;
	}
	if(buffer_locked(i->dev, block, i->sb->s_blocksize)) {
		return NULL;
	}
	if(!(buf = bread(i->dev, block, i->sb->s_blocksize))) {
				return NULL;
	}
		return buf;
}

/* the node pointer inside a read/written block buffer */
static struct xbfs_btree_node *xbfs_btree_node_buf(struct buffer *buf, __u64 off)
{
	return (struct xbfs_btree_node *)(buf->data + (off % buf->size));
}

/*
 * Read the btree header (stream offset 0) and validate it.
 * Returns 0 with *header filled on success.
 */
static int xbfs_btree_read_header(struct inode *i, struct xbfs_btree_header *header)
{
	struct buffer *buf;
	struct xbfs_btree_header *h;

	if(!(buf = xbfs_btree_read_node(i, 0))) {
		return -EIO;
	}
	h = (struct xbfs_btree_header *)buf->data;
	if(h->magic != XBFS_BTREE_MAGIC || h->node_size != XBFS_BTREE_NODE_SIZE) {
		brelse(buf);
		return -EINVAL;
	}
	memcpy_b(header, h, sizeof(struct xbfs_btree_header));
	brelse(buf);
	return 0;
}

/* update the tree header on disk (max_depth changes on split). The
 * maximum_size field tracks the stream length (Haiku validates links
 * against MaximumSize() - NodeSize()) */
static int xbfs_btree_write_header(struct inode *i, struct xbfs_btree_header *header)
{
	struct buffer *buf;
	struct xbfs_btree_header *h;

	if(!(buf = xbfs_btree_read_node(i, 0))) {
		return -EIO;
	}
	h = (struct xbfs_btree_header *)buf->data;
	header->max_size = i->i_size;
	memcpy_b(h, header, sizeof(struct xbfs_btree_header));
	xbfs_log_write_block(i->sb, buf->block, buf);
	return 0;
}

/*
 * Binary search for 'name' among the node's keys.
 * Returns 0 if found (with *index = the key index, *res = 0),
 * or -ENOENT with *index = the insertion point (first key > name).
 */
/* compare a search key against a stored key. STRING trees use the
 * prefix-aware byte order; the fixed-size index trees compare the
 * native (little-endian, host-order) values numerically — exactly
 * Haiku's QueryParser::compareKeys (no canonical byte-swap; the tree
 * stores numeric keys in host byte order) */
static int xbfs_btree_key_cmp(int dtype, const char *k1, int l1,
			     const char *k2, int l2)
{
#define XBFS_CMP_INT(_t) do {						\
	_t v1, v2;							\
	if(l1 != (int)sizeof(_t) || l2 != (int)sizeof(_t)) {		\
		return l1 - l2;						\
	}								\
	memcpy_b(&v1, k1, sizeof(_t));					\
	memcpy_b(&v2, k2, sizeof(_t));					\
	if(v1 < v2) {							\
		return -1;						\
	}								\
	if(v1 > v2) {							\
		return 1;						\
	}								\
	return 0;							\
} while(0)

	switch(dtype) {
	case XBFS_BTREE_INT32_TYPE:
		XBFS_CMP_INT(__s32);
	case XBFS_BTREE_UINT32_TYPE:
		XBFS_CMP_INT(__u32);
	case XBFS_BTREE_INT64_TYPE:
		XBFS_CMP_INT(__s64);
	case XBFS_BTREE_UINT64_TYPE:
		XBFS_CMP_INT(__u64);
	case XBFS_BTREE_FLOAT_TYPE:
		XBFS_CMP_INT(float);
	case XBFS_BTREE_DOUBLE_TYPE:
		XBFS_CMP_INT(double);
	case XBFS_BTREE_INT8_TYPE:
		XBFS_CMP_INT(__s8);
	case XBFS_BTREE_INT16_TYPE:
		XBFS_CMP_INT(__s16);
	default:
		break;
	}
#undef XBFS_CMP_INT
	/* STRING and anything else: byte order with the prefix rule */
	{
		int cmp = strncmp(k1, k2, (l1 < l2) ? l1 : l2);
		if(cmp == 0) {
			cmp = l1 - l2;
		}
		return cmp;
	}
}

static int xbfs_btree_search_node_t(struct xbfs_btree_node *n, const char *key,
				   int keylen, int *index, int dtype)
{
	int lo, hi, mid, cmp, klen;
	char *k;

	lo = 0;
	hi = n->all_key_count - 1;
	while(lo <= hi) {
		mid = (lo + hi) >> 1;
		k = xbfs_btree_key(n, mid, &klen);
		cmp = xbfs_btree_key_cmp(dtype, key, keylen, k, klen);
		if(cmp == 0) {
			*index = mid;
			return 0;
		}
		if(cmp < 0) {
			hi = mid - 1;
		} else {
			lo = mid + 1;
		}
	}
	*index = lo;	/* insertion point */
	return -ENOENT;
}

static int xbfs_btree_search_node(struct xbfs_btree_node *n, const char *name,
				 int *index)
{
	return xbfs_btree_search_node_t(n, name, strlen(name), index,
				       XBFS_BTREE_STRING_TYPE);
}

/*
 * Look up 'name' in the directory tree.
 * On success returns 0 and sets *ino to the inode block number.
 */
int xbfs_btree_find(struct inode *dir, const char *name, __ino_t *ino)
{
	struct xbfs_btree_header header;
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 path[16];
	int npath, index, res;

	if(!name[0]) {
		return -ENOENT;
	}
	if((res = xbfs_btree_read_header(dir, &header))) {
		return res;
	}
	if((res = xbfs_btree_descend_t(dir, &header, name, strlen(name),
				      path, &npath))) {
		return res;
	}

	if(!(buf = xbfs_btree_read_node(dir, path[npath - 1]))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, path[npath - 1]);
	if(n->all_key_count == 0 || n->all_key_length == 0) {
		brelse(buf);
		return -ENOENT;
	}
	if(xbfs_btree_search_node_t(n, name, strlen(name), &index,
				   header.data_type) == 0) {
		*ino = (__ino_t)xbfs_btree_values(n)[index];
		brelse(buf);
		return 0;
	}
	brelse(buf);
	return -ENOENT;
}

/*
 * Iterate over all directory entries (key, value) in key order.
 */
int xbfs_btree_iterate(struct inode *dir, int (*fn)(const char *, __ino_t, void *),
		      void *arg)
{
	struct xbfs_btree_header header;
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int index, keylen, res;

	if((res = xbfs_btree_read_header(dir, &header))) {
		return res;
	}

	/* descend to the leftmost leaf, then iterate right via the links */
	node_off = header.root_node_ptr;
	for(;;) {
		if(!(buf = xbfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, node_off);
		if(n->overflow != XBFS_BTREE_NULL) {
			node_off = n->all_key_count ?
				xbfs_btree_values(n)[0] : n->overflow;
			brelse(buf);
			continue;
		}
		brelse(buf);
		break;
	}

	for(;;) {
		if(!(buf = xbfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, node_off);
		if(n->overflow != XBFS_BTREE_NULL) {
			brelse(buf);
			return -EIO;
		}
		for(index = 0; index < n->all_key_count; index++) {
			char keybuf[XBFS_BTREE_MAX_KEY_LEN];
			keylen = 0;
			{
				char *key = xbfs_btree_key(n, index, &keylen);
				if(keylen >= XBFS_BTREE_MAX_KEY_LEN) {
					brelse(buf);
					return -EIO;
				}
				memcpy_b(keybuf, key, keylen);
			}
			keybuf[keylen] = 0;
			if(fn(keybuf, (__ino_t)xbfs_btree_values(n)[index], arg)) {
				brelse(buf);
				return 0;
			}
		}
		if(n->right == XBFS_BTREE_NULL) {
			brelse(buf);
			return 0;
		}
		node_off = n->right;
		brelse(buf);
	}
}

/*
 * Iterate every (key, keylen, inode) pair of a tree in key order,
 * EXPANDING the duplicate-key machinery: a leaf value is a direct
 * inode, a fragment link (XBFS_BTREE_DUPLICATE_FRAGMENT) or a
 * duplicate-node link (XBFS_BTREE_DUPLICATE_NODE, a right-link chain);
 * each expanded value is reported once. This is the query engine's
 * view of an index tree (Haiku's BQuery walks the same structures).
 */
int xbfs_btree_iterate_values(struct inode *dir, int dtype,
			     int (*fn)(const char *, int, __ino_t, void *),
			     void *arg)
{
	struct xbfs_btree_header header;
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int index, keylen, res;

	if((res = xbfs_btree_read_header(dir, &header))) {
		return res;
	}
	if(header.data_type != dtype && dtype != XBFS_BTREE_STRING_TYPE
			&& dtype != -1) {
		/* the caller asked for one type but the tree is another
		 * (e.g. STRING name index vs an INT64 index); refuse to
		 * mis-decode the keys. -1 matches any type */
		return -EINVAL;
	}
	(void)dtype;

	/* descend to the leftmost leaf, then iterate right via the links */
	node_off = header.root_node_ptr;
	for(;;) {
		if(!(buf = xbfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, node_off);
		if(n->overflow != XBFS_BTREE_NULL) {
			node_off = n->all_key_count ?
				xbfs_btree_values(n)[0] : n->overflow;
			brelse(buf);
			continue;
		}
		brelse(buf);
		break;
	}

	for(;;) {
		if(!(buf = xbfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, node_off);
		if(n->overflow != XBFS_BTREE_NULL) {
			brelse(buf);
			return -EIO;
		}
		for(index = 0; index < n->all_key_count; index++) {
			char keybuf[XBFS_BTREE_MAX_KEY_LEN];
			__u64 value;
			keylen = 0;
			{
				char *key = xbfs_btree_key(n, index, &keylen);
				if(keylen >= XBFS_BTREE_MAX_KEY_LEN) {
					brelse(buf);
					return -EIO;
				}
				memcpy_b(keybuf, key, keylen);
			}
			keybuf[keylen] = 0;
			value = xbfs_btree_values(n)[index];

			/* direct value */
			if(xbfs_dup_link_type(value) <= 1) {
				if(fn(keybuf, keylen, (__ino_t)value, arg)) {
					brelse(buf);
					return 0;
				}
				continue;
			}
			if(xbfs_dup_link_type(value) == XBFS_BTREE_DUPLICATE_FRAGMENT) {
				/* a fragment slot: {count, values[7]} */
				struct buffer *fb;
				__u64 *arr;
				int s, shared = 0;

				if(xbfs_dup_link_off(value) / dir->sb->s_blocksize
				   == node_off / dir->sb->s_blocksize) {
					/* the fragment shares the leaf's
					 * block (nodes pack at 1024-byte
					 * offsets): borrow the leaf buffer */
					fb = buf;
					shared = 1;
				} else {
					if(!(fb = xbfs_btree_read_node(dir,
							xbfs_dup_link_off(value)))) {
						brelse(buf);
						return -EIO;
					}
				}
				arr = xbfs_dup_array(fb, xbfs_dup_link_off(value),
						   xbfs_dup_link_frag(value));
				if(arr[0] > 7) {
					/* a fragment slot holds at most 7
					 * values; refuse a corrupt count */
					if(!shared) {
						brelse(fb);
					}
					brelse(buf);
					return -EIO;
				}
				for(s = 0; s < (int)arr[0]; s++) {
					if(fn(keybuf, keylen, (__ino_t)arr[1 + s],
					    arg)) {
						if(!shared) {
							brelse(fb);
						}
						brelse(buf);
						return 0;
					}
				}
				if(!shared) {
					brelse(fb);
				}
				continue;
			}
			if(xbfs_dup_link_type(value) == XBFS_BTREE_DUPLICATE_NODE) {
				/* a duplicate-node chain: {left, right,
				 * count@16, values[125]@24} */
				__u64 noff = xbfs_dup_link_off(value);
				int hops = 0;

				while(noff != (__u64)XBFS_BTREE_NULL) {
					if(++hops > 4096) {
						/* a corrupt next link would
						 * walk forever */
						brelse(buf);
						return -EIO;
					}
					struct xbfs_btree_node *dn;
					struct buffer *db;
					__u64 *arr;
					int s, shared = 0;

					if(noff / dir->sb->s_blocksize
					   == node_off / dir->sb->s_blocksize) {
						/* the chain node shares the
						 * leaf's block: borrow it */
						db = buf;
						shared = 1;
					} else {
						if(!(db = xbfs_btree_read_node(dir,
								noff))) {
							brelse(buf);
							return -EIO;
						}
					}
					dn = xbfs_btree_node_buf(db, noff);
					arr = xbfs_dup_node_array(dn);
					if(arr[0] > 125) {
						if(!shared) {
							brelse(db);
						}
						brelse(buf);
						return -EIO;
					}
					for(s = 0; s < (int)arr[0]; s++) {
						if(fn(keybuf, keylen,
						    (__ino_t)arr[1 + s], arg)) {
							if(!shared) {
								brelse(db);
							}
							brelse(buf);
							return 0;
						}
					}
					noff = dn->right;
					if(!shared) {
						brelse(db);
					}
				}
				continue;
			}
			/* unknown link type: skip (defensive) */
		}
		if(n->right == XBFS_BTREE_NULL) {
			brelse(buf);
			return 0;
		}
		node_off = n->right;
		brelse(buf);
	}
}

/*
 * Rebuild a leaf node with the given (insert or delete) change.
 * The node is small (<= 1KB) and the pairs are re-serialized from a
 * scratch copy, which is much less error-prone than in-place shifting.
 * Returns 0 on success, -ENOSPC if the rebuilt node would overflow.
 */
static int xbfs_btree_node_room(struct xbfs_btree_node *n, int keylen,
			      __u32 bsize)
{
	int used = sizeof(struct xbfs_btree_node) + n->all_key_length + keylen;
	int cnt = n->all_key_count + 1;

	/* the pair-count cap matters at 4096-byte blocks (a leaf could
	 * otherwise exceed the 128+1 slot collect arrays) */
	if(cnt > XBFS_BTREE_MAX_PAIRS + 1) {
		return 0;
	}

	return ((used + 7) & ~7) + cnt * 2 + cnt * 8 <= bsize;
}

/*
 * From an interior node, pick the child offset for 'name'.
 * Comparison is prefix-aware like xbfs_btree_search_node(): a name that
 * extends a separator key (e.g. "whoami" vs separator "who") is GREATER
 * than the key, so it descends into the next child instead of matching
 * the current one.
 */
static void xbfs_btree_pick_child_t(struct xbfs_btree_node *n, const char *key,
				   int keylen, __u64 *child, int dtype)
{
	int i, klen, cmp;
	char *k;
	__u64 *values = xbfs_btree_values(n);

	if(n->all_key_count == 0) {
		*child = n->overflow;
		return;
	}

	k = xbfs_btree_key(n, n->all_key_count - 1, &klen);
	cmp = xbfs_btree_key_cmp(dtype, key, keylen, k, klen);
	if(cmp > 0) {
		*child = (n->overflow != XBFS_BTREE_NULL) ?
			n->overflow : values[n->all_key_count - 1];
		return;
	}

	for(i = 0; i < n->all_key_count && i <= XBFS_BTREE_MAX_PAIRS + 1; i++) {
		k = xbfs_btree_key(n, i, &klen);
		cmp = xbfs_btree_key_cmp(dtype, key, keylen, k, klen);
		if(cmp <= 0) {
			*child = values[i];
			return;
		}
	}
	*child = values[n->all_key_count - 1];
}

static void xbfs_btree_pick_child(struct xbfs_btree_node *n, const char *name,
				 __u64 *child)
{
	xbfs_btree_pick_child_t(n, name, strlen(name), child,
			       XBFS_BTREE_STRING_TYPE);
}

static int xbfs_btree_descend_t(struct inode *dir,
			       const struct xbfs_btree_header *h,
			       const char *key, int keylen, __u64 *path,
			       int *npath)
{
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int depth = 0;

	node_off = h->root_node_ptr;
	for(;;) {
		if(depth >= 16) {
			return -EIO;
		}
		path[depth++] = node_off;
		if(!(buf = xbfs_btree_read_node_tagged(dir, node_off, "xbfs_btree_descend_t"))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, node_off);
		if(n->all_key_count > XBFS_BTREE_MAX_PAIRS + 2
		   || n->all_key_length > XBFS_BTREE_NODE_SIZE - sizeof(struct xbfs_btree_node)) {
			brelse(buf);
			return -EIO;
		}
		if(n->overflow == XBFS_BTREE_NULL) {
			brelse(buf);
			*npath = depth;
			return 0;
		}
		xbfs_btree_pick_child_t(n, key, keylen, &node_off, h->data_type);
		brelse(buf);
	}
}

static int xbfs_btree_descend(struct inode *dir, const struct xbfs_btree_header *h,
			     const char *name, __u64 *path, int *npath)
{
	return xbfs_btree_descend_t(dir, h, name, strlen(name), path, npath);
}

/* collect a node's pairs, copying the keys into 'keybuf' (the node buffer
 * may be released before the pairs are used); optionally insert one pair */
static int xbfs_btree_collect_t(struct xbfs_btree_node *n,
			      struct xbfs_btree_pair *pairs, int max,
			      int do_insert, const char *iname, int iname_len,
			      __u64 ino, int dtype, char *keybuf)
{
	int count = 0, i, index, off = 0;
	__u64 *values = xbfs_btree_values(n);

	if(n->all_key_count > max) {
		return -1;
	}
	for(i = 0; i < n->all_key_count; i++) {
		pairs[count].keylen = 0;
		{
			char *key = xbfs_btree_key(n, i, &pairs[count].keylen);
			memcpy_b(keybuf + off, key, pairs[count].keylen);
		}
		pairs[count].key = keybuf + off;
		pairs[count].val = values[i];
		off += pairs[count].keylen;
		count++;
	}

	if(!do_insert) {
		return count;
	}

	index = count;
	for(i = 0; i < count; i++) {
		int cmp = xbfs_btree_key_cmp(dtype, iname, iname_len,
					    pairs[i].key, pairs[i].keylen);
		if(cmp == 0) {
			/* exact duplicate: the key is already in the tree
			 * (the duplicate machinery handles extra values) */
			return -1;
		}
		if(cmp < 0) {
			index = i;
			break;
		}
	}
	if(index >= max) {
		return -1;
	}
	for(i = count; i > index; i--) {
		pairs[i] = pairs[i - 1];
	}
	pairs[index].key = iname;
	pairs[index].keylen = iname_len;
	pairs[index].val = ino;
	return count + 1;
}

static int xbfs_btree_serialize(struct xbfs_btree_node *n,
			       struct xbfs_btree_pair *pairs, int count,
			       __u64 interior_overflow, __u64 right,
			       __u64 left, __u32 bsize)
{
	char *keydata;
	int total = 0, i, off = 0;

	if(!(keydata = (char *)kmalloc(bsize))) {
		return -ENOMEM;
	}
	for(i = 0; i < count; i++) {
		memcpy_b(keydata + off, pairs[i].key, pairs[i].keylen);
		off += pairs[i].keylen;
		total += pairs[i].keylen;
	}

	memset_b(n, 0, bsize);
	n->left = left;
	n->right = right;
	n->overflow = interior_overflow;	/* -1 => leaf */
	n->all_key_count = count;
	n->all_key_length = total;
	{
		char *keys = (char *)n + sizeof(struct xbfs_btree_node);
		__u16 *kl = xbfs_btree_keylen_index(n);
		__u64 *values = xbfs_btree_values(n);
		off = 0;
		for(i = 0; i < count; i++) {
			memcpy_b(keys + off, keydata + off, pairs[i].keylen);
			off += pairs[i].keylen;
			kl[i] = off;
			values[i] = pairs[i].val;
		}
	}
	kfree((addr_t)keydata);
	return 0;
}

static int xbfs_btree_write_node(struct inode *dir, __u64 off,
				struct xbfs_btree_pair *pairs, int count,
				__u64 overflow, __u64 right, __u64 left,
				struct buffer *held, __u64 held_off)
{
	struct xbfs_btree_node *n;
	struct buffer *buf;

	if(held && off / dir->sb->s_blocksize
			== held_off / dir->sb->s_blocksize) {
		/* the target shares the caller's held block (nodes pack
		 * at 1024-byte offsets): serialize into the held buffer
		 * and let the caller's own write flush the whole block
		 * (breading it here would sleep on the caller's lock) */
		n = xbfs_btree_node_buf(held, off);
		xbfs_btree_serialize(n, pairs, count, overflow, right, left,
				       XBFS_BTREE_NODE_SIZE);
		return 0;
	}
	if(!(buf = xbfs_btree_read_node(dir, off))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, off);
	xbfs_btree_serialize(n, pairs, count, overflow, right, left,
			       XBFS_BTREE_NODE_SIZE);
	xbfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
}


/*
 * Point a leaf's left link at 'left' (the previous leaf in the stream).
 * Called after a leaf split: the leaf that used to follow the split node
 * now follows the new right half. Haiku keeps these links consistent,
 * and checkfs validates them.
 */
static int xbfs_btree_relink_left(struct inode *dir, __u64 off, __u64 left,
				 struct buffer *held, __u64 held_off)
{
	struct xbfs_btree_node *n;
	struct buffer *buf;

	if(off == XBFS_BTREE_NULL) {
		return 0;
	}
	if(held && off / dir->sb->s_blocksize
			== held_off / dir->sb->s_blocksize) {
		/* same-block: update the held buffer, defer the write */
		n = xbfs_btree_node_buf(held, off);
		n->left = left;
		return 0;
	}
	if(!(buf = xbfs_btree_read_node_tagged(dir, off, "xbfs_btree_relink_left"))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, off);
	n->left = left;
	xbfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
}

static int xbfs_btree_grow(struct inode *dir, __u64 *off, struct buffer *held)
{
	struct xbfs_btree_header *h;
	struct buffer *hbuf;
	__blk_t block, hdr_block;

	if((block = bmap(dir, dir->i_size, FOR_WRITING)) < 0) {
		return block;
	}
		*off = dir->i_size;
	dir->i_size += XBFS_BTREE_NODE_SIZE;
	dir->u.xbfs.raw.u.data.size = dir->i_size;
	dir->state |= INODE_DIRTY;

	/* Haiku validates node links against MaximumSize() - NodeSize(),
	 * so the header's maximum_size must track the stream length on
	 * EVERY grow, not just root splits. The header shares its block
	 * with the first node(s) at block sizes above the node size, and
	 * the caller usually holds that block — breading it again would
	 * sleep on the caller's own buffer (deadlock). Update the header
	 * through the held buffer when it is the header's block. */
	hdr_block = bmap(dir, 0, FOR_READING);
	if(hdr_block < 0) {
		return hdr_block;
	}
	if(held && held->block == hdr_block) {
		((struct xbfs_btree_header *)held->data)->max_size = dir->i_size;
		return 0;
	}
	if((hbuf = xbfs_btree_read_node(dir, 0))) {
		h = (struct xbfs_btree_header *)hbuf->data;
		h->max_size = dir->i_size;
		xbfs_log_write_block(dir->sb, hbuf->block, hbuf);
	}
	return 0;
}

/*
 * Split the node at path[npath-1] into two halves. 'pairs' holds every
 * pair of the node plus the one being inserted (leaves: the new directory
 * entry; interiors: the new separator + right child), 'count' = pairs
 * count, 'split_at' = pairs kept in the left half, 'node_overflow' = the
 * node's rightmost child (XBFS_BTREE_NULL for leaves), 'depth' = the
 * original descent depth (for the max_depth bookkeeping when the root
 * splits).
 *
 * The left half stays at path[npath-1] (or moves to a fresh block when the
 * split node is the root), the right half gets a fresh block, and the
 * separator plus the right half's offset are inserted into the parent
 * interior — recursively splitting the parent when it is full.
 *
 * For an interior node with children c0..cn (pairs[].val + overflow),
 * pairs[i] = (key[i], c[i]) and key[i] is the separator between c[i] and
 * c[i+1]. Splitting at 'split_at' leaves the left half with keys
 * pairs[0..split_at) and overflow child pairs[split_at].val; the right
 * half starts at pairs[split_at+1] (its first child is
 * pairs[split_at+1].val, after the left's overflow) and keeps node_overflow
 * as its own overflow; the up separator is pairs[split_at].key — the
 * boundary between the left's overflow child and the right's first child.
 * A leaf has no overflow: left = pairs[0..split_at), right =
 * pairs[split_at..count), separator = pairs[split_at-1].key.
 */
static int xbfs_btree_split(struct inode *dir, struct xbfs_btree_header *header,
			   __u64 *path, int npath, struct xbfs_btree_pair *pairs,
			   int count, int split_at, __u64 node_overflow,
			   int depth, int dtype)
{
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off = path[npath - 1];
	__u64 old_right, old_left, left_overflow, right_overflow;
	__u64 left_off, right_off;
	const char *sep;
	struct xbfs_btree_pair *right_pairs;
	int sep_len, is_leaf, right_count;
	int res;

	if(split_at < 1 || split_at >= count) {
		return -EIO;
	}
	is_leaf = (node_overflow == XBFS_BTREE_NULL);
	if(is_leaf) {
		/* leaf: the separator is the last key of the left half */
		sep = pairs[split_at - 1].key;
		sep_len = pairs[split_at - 1].keylen;
		left_overflow = XBFS_BTREE_NULL;
		right_overflow = XBFS_BTREE_NULL;
		right_pairs = pairs + split_at;
		right_count = count - split_at;
	} else {
		/* interior: children are pairs[].val plus node_overflow; the
		 * separator is the key between the left node's overflow child
		 * (pairs[split_at].val) and the right node's first child
		 * (pairs[split_at + 1].val) — the right half therefore starts
		 * at split_at + 1 and pairs[split_at] is not copied */
		sep = pairs[split_at].key;
		sep_len = pairs[split_at].keylen;
		left_overflow = pairs[split_at].val;
		right_overflow = node_overflow;
		right_pairs = pairs + split_at + 1;
		right_count = count - split_at - 1;
	}

	if(!(buf = xbfs_btree_read_node_tagged(dir, node_off, "xbfs_btree_split"))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, node_off);
	old_right = n->right;
	old_left = n->left;

	if((res = xbfs_btree_grow(dir, &right_off, buf)) < 0) {
		brelse(buf);
		return res;
	}

	if(npath == 1) {
		/* the split node is the root. Haiku keeps the LEFT half in
		 * the old root block (so an interior root split orphans
		 * nothing — the old children stay in place), allocates only
		 * the right half, and moves the root to a fresh block:
		 * [sep -> old root offset, overflow -> right half]. */
		__u64 root_off;

		if((res = xbfs_btree_write_node(dir, right_off, right_pairs,
				right_count, right_overflow,
				is_leaf ? old_right : XBFS_BTREE_NULL,
				is_leaf ? node_off : XBFS_BTREE_NULL,
				buf, node_off)) < 0) {
			brelse(buf);
			return res;
		}
		if(is_leaf && (res = xbfs_btree_relink_left(dir, old_right,
				right_off, buf, node_off)) < 0) {
			brelse(buf);
			return res;
		}
		/* the left half stays in the old root block */
		xbfs_btree_serialize(n, pairs, split_at, left_overflow,
				    is_leaf ? right_off : XBFS_BTREE_NULL,
				    is_leaf ? old_left : XBFS_BTREE_NULL,
				    XBFS_BTREE_NODE_SIZE);
		xbfs_log_write_block(dir->sb, buf->block, buf);	/* consumes buf */

		/* a new root block: one separator, two children */
		if((res = xbfs_btree_grow(dir, &root_off, NULL)) < 0) {
			return res;
		}
		if(!(buf = xbfs_btree_read_node_tagged(dir, root_off, "xbfs_btree_split"))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, root_off);
		memset_b(n, 0, XBFS_BTREE_NODE_SIZE);
		n->left = XBFS_BTREE_NULL;
		n->right = XBFS_BTREE_NULL;
		n->overflow = right_off;
		n->all_key_count = 1;
		n->all_key_length = sep_len;
		{
			char *keys = (char *)n + sizeof(struct xbfs_btree_node);
			__u16 *kl = xbfs_btree_keylen_index(n);
			__u64 *values = xbfs_btree_values(n);
			memcpy_b(keys, sep, sep_len);
			kl[0] = sep_len;
			values[0] = node_off;
		}
		xbfs_log_write_block(dir->sb, buf->block, buf);

		header->root_node_ptr = root_off;
		if(header->max_depth < depth + 1) {
			header->max_depth = depth + 1;
		}
		xbfs_btree_write_header(dir, header);
		return 0;
	}

	/* depth >= 2: the node keeps the lower half in place; the upper
	 * half goes to a new block; insert (sep, right_off) into the
	 * parent interior */
	if((res = xbfs_btree_write_node(dir, right_off, right_pairs,
			right_count, right_overflow,
			is_leaf ? old_right : XBFS_BTREE_NULL,
			is_leaf ? node_off : XBFS_BTREE_NULL,
			buf, node_off)) < 0) {
		brelse(buf);
		return res;
	}
	if(is_leaf && (res = xbfs_btree_relink_left(dir, old_right,
			right_off, buf, node_off)) < 0) {
		brelse(buf);
		return res;
	}
	xbfs_btree_serialize(n, pairs, split_at, left_overflow,
			    is_leaf ? right_off : XBFS_BTREE_NULL,
			    is_leaf ? old_left : XBFS_BTREE_NULL,
			    XBFS_BTREE_NODE_SIZE);
	xbfs_log_write_block(dir->sb, buf->block, buf);

	/* insert the separator into the parent, splitting the parent
	 * (recursively) if the rebuilt parent would not fit */
	{
		struct xbfs_btree_node *parent;
		struct buffer *parent_buf;
		struct xbfs_btree_pair *pp;
		char *pkb;
		__u64 parent_overflow;
		int pcount, i, j = -1;

		if(!(parent_buf = xbfs_btree_read_node_tagged(dir, path[npath - 2], "xbfs_btree_split"))) {
			return -EIO;
		}
		parent = xbfs_btree_node_buf(parent_buf, path[npath - 2]);
		parent_overflow = parent->overflow;

		if(!(pp = (struct xbfs_btree_pair *)kmalloc(
				(XBFS_BTREE_MAX_PAIRS + 2) * sizeof(struct xbfs_btree_pair)))
				|| !(pkb = (char *)kmalloc(
					XBFS_BTREE_NODE_SIZE))) {
			if(pp) {
				kfree((addr_t)pp);
			}
			if(pkb) {
				kfree((addr_t)pkb);
			}
			brelse(parent_buf);
			return -ENOMEM;
		}

		pcount = xbfs_btree_collect_t(parent, pp, XBFS_BTREE_MAX_PAIRS + 2,
					0, NULL, 0, 0,
					     dtype, pkb);
		if(pcount < 0) {
			kfree((addr_t)pkb);
			kfree((addr_t)pp);
			brelse(parent_buf);
			return -ENOSPC;
		}

		for(i = 0; i < pcount; i++) {
			if(pp[i].val == node_off) {
				j = i;
				break;
			}
		}
		if(j < 0 && parent_overflow != node_off) {
			kfree((addr_t)pkb);
			kfree((addr_t)pp);
			brelse(parent_buf);
			return -EIO;
		}

		{
			int klen_total = 0;

			for(i = 0; i < pcount; i++) {
				klen_total += pp[i].keylen;
			}
			/* copy the separator to the END of the key area:
			 * pp[j].key must not alias the existing keys (the old
			 * code overwrote pkb[0..sep_len), corrupting the
			 * first keys of the parent) */
			if(j >= 0) {
				int old_keylen = pp[j].keylen;
				for(i = pcount; i > j + 1; i--) {
					pp[i] = pp[i - 1];
				}
				pp[j + 1].key = pp[j].key;
				pp[j + 1].keylen = old_keylen;
				pp[j + 1].val = right_off;
				pp[j].key = pkb + klen_total;
				memcpy_b(pkb + klen_total, sep, sep_len);
				pp[j].keylen = sep_len;
				pcount++;
			} else {
				/* the split child is the parent's overflow */
				pp[pcount].key = pkb + klen_total;
				memcpy_b(pkb + klen_total, sep, sep_len);
				pp[pcount].keylen = sep_len;
				pp[pcount].val = node_off;
				pcount++;
				parent_overflow = right_off;
			}
		}

		{
			int klen_total = 0;

			for(i = 0; i < pcount; i++) {
				klen_total += pp[i].keylen;
			}
			if(((sizeof(struct xbfs_btree_node) + klen_total + 7) & ~7)
					+ pcount * 2 + (pcount + 1) * 8
					<= XBFS_BTREE_NODE_SIZE) {
				/* the parent has room: rebuild it in place */
				xbfs_btree_serialize(parent, pp, pcount,
						    parent_overflow,
						    XBFS_BTREE_NULL,
						    XBFS_BTREE_NULL,
						    XBFS_BTREE_NODE_SIZE);
				xbfs_log_write_block(dir->sb, parent_buf->block,
						    parent_buf);
				kfree((addr_t)pkb);
				kfree((addr_t)pp);
				return 0;
			}
		}

		/* the parent is full: release it and split it recursively
		 * (the recursion re-reads path[npath-2]; the pp/pkb buffers
		 * stay alive until the recursion has serialized them) */
		brelse(parent_buf);
		res = xbfs_btree_split(dir, header, path, npath - 1, pp, pcount,
				      pcount / 2, parent_overflow, depth,
				      header->data_type);
		kfree((addr_t)pkb);
		kfree((addr_t)pp);
		return res;
	}
}


/*
 * Insert a (key, value) pair into a B+tree. 'allow_dups' enables the
 * duplicate-key machinery (used by the index trees); directory trees
 * reject an existing key with -EEXIST. Splits a full leaf, and splits
 * the interior parents recursively when they fill.
 */
static int xbfs_btree_insert_impl_t(struct inode *dir, const char *key,
				   int keylen, __u64 value, int dtype,
				   int allow_dups)
{
	struct xbfs_btree_header header;
	struct xbfs_btree_node *n;
	struct buffer *buf;
	struct xbfs_btree_pair *pairs;
	__u64 path[16];
	int npath, count, res, index;

	if(!keylen) {
		return -EINVAL;
	}
	if((res = xbfs_btree_read_header(dir, &header))) {
		return res;
	}
	if((res = xbfs_btree_descend_t(dir, &header, key, keylen,
				      path, &npath))) {
		return res;
	}
	if(!(buf = xbfs_btree_read_node_tagged(dir, path[npath - 1], "xbfs_btree_insert_impl_t"))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, path[npath - 1]);

	if(allow_dups &&
	   xbfs_btree_search_node_t(n, key, keylen, &index, dtype) == 0) {
		/* the key already exists: the new value joins the
		 * duplicate chain (Haiku's _InsertDuplicate) */
		/* release the leaf before the dup machinery: at block sizes
		 * above the node size a leaf shares its block with the
		 * header (or another node), and the dup path's grow() re-
		 * breads the header block — a held buffer would deadlock.
		 * The dup re-breads the leaf itself and owns its write. */
		brelse(buf);
		res = xbfs_btree_insert_dup(dir, path[npath - 1], index, value);
		return (res < 0) ? res : 0;
	}

	if(xbfs_btree_node_room(n, keylen, XBFS_BTREE_NODE_SIZE)) {
		/* the leaf has room: rebuild it in place */
		static struct xbfs_btree_pair spairs[XBFS_BTREE_MAX_PAIRS + 2];
		static char skb[XBFS_MAX_BLOCK_SIZE];
		pairs = spairs;
		count = xbfs_btree_collect_t(n, pairs, XBFS_BTREE_MAX_PAIRS + 2,
					1, key, keylen,
					value, dtype, skb);
		if(count < 0) {
			brelse(buf);
			return -EEXIST;
		}
		xbfs_btree_serialize(n, pairs, count, XBFS_BTREE_NULL, n->right,
				    n->left, XBFS_BTREE_NODE_SIZE);
		xbfs_log_write_block(dir->sb, buf->block, buf);
		return 0;
	}

	/* the leaf is full: collect everything + the new pair, then split */
	{
		static struct xbfs_btree_pair spairs[XBFS_BTREE_MAX_PAIRS + 2];
		static char skb[XBFS_MAX_BLOCK_SIZE];
		pairs = spairs;
		count = xbfs_btree_collect_t(n, pairs, XBFS_BTREE_MAX_PAIRS + 2,
					1, key, keylen,
					value, dtype, skb);
		if(count < 0) {
			brelse(buf);
			return -EEXIST;
		}
		/* release the leaf buffer BEFORE the split (the split re-reads
		 * the same node; holding it would deadlock the buffer cache) */
		brelse(buf);
		buf = NULL;
		res = xbfs_btree_split(dir, &header, path, npath, pairs, count,
				      count / 2, XBFS_BTREE_NULL, npath,
				      header.data_type);
		return res;
	}
}

static int xbfs_btree_insert_impl(struct inode *dir, const char *name,
				 __ino_t ino)
{
	return xbfs_btree_insert_impl_t(dir, name, strlen(name), ino,
				       XBFS_BTREE_STRING_TYPE, 0);
}

/* remove the entry at index 'i' from the leaf in 'buf' */
static void xbfs_btree_remove_at(struct xbfs_btree_node *n, int i)
{
	__u16 *kl = xbfs_btree_keylen_index(n);
	__u64 *values = xbfs_btree_values(n);
	char *keys = (char *)n + sizeof(struct xbfs_btree_node);
	int old_count = n->all_key_count;
	int start = i ? kl[i - 1] : 0;
	int key_end = kl[i];
	int klen = key_end - start;
	int tail = n->all_key_length - key_end;
	int old_align = (sizeof(struct xbfs_btree_node)
		+ n->all_key_length + 7) & ~7;
	int new_align;
	int j;

	memmove(keys + start, keys + key_end, tail);
	for(j = i; j < old_count - 1; j++) {
		kl[j] = kl[j + 1] - klen;
	}
	memmove(values + i, values + i + 1,
		(old_count - i - 1) * sizeof(__u64));
	n->all_key_count--;
	n->all_key_length -= klen;

	/* relocate the index and values to the layout implied by the NEW
	 * all_key_length: the index directly after the key area (aligned),
	 * the values directly after the index. In-place deletion leaves
	 * the removed key's stale index entry between them, and a klen
	 * shrink across an 8-byte boundary moves the whole array (the
	 * insert path rebuilds nodes with serialize(); delete patches in
	 * place, so the relocation lives here) */
	new_align = (sizeof(struct xbfs_btree_node)
		+ n->all_key_length + 7) & ~7;
	if(new_align != old_align) {
		memmove((char *)n + new_align, (char *)n + old_align,
			old_count * sizeof(__u16));
	}
	memmove((char *)n + new_align + n->all_key_count * sizeof(__u16),
		(char *)n + old_align + old_count * sizeof(__u16),
		n->all_key_count * sizeof(__u64));
}

/*
 * Remove a (name, inode) pair from the directory tree.
 */
/* ------------------------------------------------------------------ */
/* duplicate-key support (Haiku's fragment / duplicate-node encoding). */
/*                                                                     */
/* A leaf value is either a direct value (link type 0/1), a link to a  */
/* FRAGMENT slot (type 3: (off & ~0x3ff) = the fragment node's stream   */
/* offset, low 10 bits = the slot index; each slot is a 64-byte         */
/* duplicate_array {count, values[7]}), or a link to a DUPLICATE NODE   */
/* (type 2: {left_link, right_link, count @16, values[125] @24}; right  */
/* links chain to more nodes when a key has > 125 values).              */
/* ------------------------------------------------------------------ */

static __u64 xbfs_dup_make_link(int type, __u64 off, int frag)
{
	return ((__u64)type << 62) | (off & 0x3ffffffffffffc00ULL)
		| (frag & 0x3ff);
}

static int xbfs_dup_link_type(__u64 v)
{
	return (int)(v >> 62);
}

static __u64 xbfs_dup_link_off(__u64 v)
{
	return v & 0x3ffffffffffffc00ULL;
}

static int xbfs_dup_link_frag(__u64 v)
{
	return (int)(v & 0x3ff);
}

/* the duplicate_array at slot 's' of the fragment/duplicate node 'nb'
 * (the node lives at stream offset 'noff'; with blocks larger than the
 * node size it is not at the start of the block buffer) */
static __u64 *xbfs_dup_array(struct buffer *nb, __u64 noff, int slot)
{
	return (__u64 *)(xbfs_btree_node_buf(nb, noff)
		+ slot * 8 * (XBFS_BTREE_NUM_FRAGMENT_VALUES + 1));
}

/* the {count, values[125]} array of a duplicate node (offset 16, the
 * overflow field) — byte cast to dodge the packed-member warning */
static __u64 *xbfs_dup_node_array(struct xbfs_btree_node *dn)
{
	return (__u64 *)((char *)dn + 16);
}

/* number of non-empty fragment slots in a fragment node */
static int xbfs_dup_fragments_used(struct buffer *nb, __u64 noff)
{
	int s, used = 0;

	for(s = 0; s < XBFS_BTREE_MAX_FRAGMENTS; s++) {
		if(xbfs_dup_array(nb, noff, s)[0] != 0) {
			used++;
		}
	}
	return used;
}

/*
 * Find a free fragment slot: reuse an empty slot in a fragment node
 * already linked from this leaf; otherwise allocate a fresh zeroed
 * block. Returns 0 with '*nb' (held) + '*slot' + '*off'.
 */
static int xbfs_dup_find_fragment(struct inode *dir, struct xbfs_btree_node *n,
				 struct buffer **nb, int *slot, __u64 *off,
				 struct buffer *held, __u64 leaf_off)
{
	__u64 *values = xbfs_btree_values(n);
	int i, s, res;

	for(i = 0; i < n->all_key_count; i++) {
		if(xbfs_dup_link_type(values[i]) != XBFS_BTREE_DUPLICATE_FRAGMENT) {
			continue;
		}
		/* at block sizes above the node size the tree's nodes pack
		 * into shared blocks: a fragment node can share the LEAF's
		 * block. breading it while the leaf is held would sleep on
		 * our own lock — borrow the leaf buffer instead. */
		if(xbfs_dup_link_off(values[i]) / dir->sb->s_blocksize
		   == leaf_off / dir->sb->s_blocksize) {
			*nb = held;
		} else {
			if(!(*nb = xbfs_btree_read_node_tagged(dir,
					xbfs_dup_link_off(values[i]),
					"xbfs_dup_find_fragment"))) {
				continue;
			}
		}
		*off = xbfs_dup_link_off(values[i]);
		for(s = 0; s < XBFS_BTREE_MAX_FRAGMENTS; s++) {
			if(xbfs_dup_array(*nb, *off, s)[0] == 0) {
				*slot = s;
				return 0;
			}
		}
		if(*nb != held) {
			brelse(*nb);
		}
		*nb = NULL;
	}

	/* no free slot anywhere: allocate a fresh fragment node ('held'
	 * is the caller's leaf buffer — it may be the header's block) */
	if((res = xbfs_btree_grow(dir, off, held)) < 0) {
		return res;
	}
	if(*off / dir->sb->s_blocksize == leaf_off / dir->sb->s_blocksize) {
		/* the fresh node lands in the leaf's block: borrow it */
		*nb = held;
	} else {
		if(!(*nb = xbfs_btree_read_node_tagged(dir, *off, "xbfs_dup_find_fragment"))) {
			return -EIO;
		}
	}
	memset_b(xbfs_btree_node_buf(*nb, *off), 0, XBFS_BTREE_NODE_SIZE);
	*slot = 0;
	return 0;
}

/* the first duplicate: promote the direct value into a fragment [old, v] */
static int xbfs_btree_dup_promote(struct inode *dir, struct buffer *buf,
				 __u64 noff, int index, __u64 old, __u64 value)
{
	struct xbfs_btree_node *n = xbfs_btree_node_buf(buf, noff);
	__u64 *values = xbfs_btree_values(n);
	struct buffer *nb = NULL;
	__u64 *arr;
	int slot, res;

	if((res = xbfs_dup_find_fragment(dir, n, &nb, &slot, &noff, buf, noff)) < 0) {
		return res;
	}
	arr = xbfs_dup_array(nb, noff, slot);
	arr[0] = 2;
	arr[1] = old;
	arr[2] = value;
	if(nb != buf) {
		xbfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
	}
	/* when nb == buf the fragment lives in the leaf's own block: the
	 * changes are written by the caller's final leaf write */

	values[index] = xbfs_dup_make_link(XBFS_BTREE_DUPLICATE_FRAGMENT,
					  noff, slot);
	return 1;	/* the leaf's value link changed */
}

/* append to a fragment; promote to a duplicate node when the fragment fills */
static int xbfs_btree_dup_fragment_add(struct inode *dir, struct buffer *buf,
				      __u64 leaf_off, int index, __u64 value)
{
	struct xbfs_btree_node *n = xbfs_btree_node_buf(buf, leaf_off);
	__u64 *values = xbfs_btree_values(n);
	struct xbfs_btree_node *dn;
	struct buffer *nb = NULL, *ndb;
	__u64 noff, nd;
	__u64 *arr;
	int frag, res, shared = 0;

	noff = xbfs_dup_link_off(values[index]);
	frag = xbfs_dup_link_frag(values[index]);
	if(noff / dir->sb->s_blocksize == leaf_off / dir->sb->s_blocksize) {
		/* the fragment shares the leaf's block: borrow it */
		nb = buf;
		shared = 1;
	} else {
		if(!(nb = xbfs_btree_read_node_tagged(dir, noff, "xbfs_btree_dup_fragment_add"))) {
			return -EIO;
		}
	}
	arr = xbfs_dup_array(nb, noff, frag);
	if(arr[0] > XBFS_BTREE_NUM_FRAGMENT_VALUES) {
		/* a garbage count (an unread/corrupt buffer): the slot
		 * cannot hold that many values — rebuild it from empty */
		arr[0] = 0;
	}
	if(arr[0] < XBFS_BTREE_NUM_FRAGMENT_VALUES) {
		arr[arr[0] + 1] = value;
		arr[0]++;
		if(!shared) {
			xbfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
		}
		return 0;	/* the leaf's link is unchanged */
	}

	if(xbfs_dup_fragments_used(nb, noff) < 2) {
		/* only this array: convert the node into a duplicate
		 * node in place (array at &overflow_link, left/right
		 * links cleared) */
		dn = xbfs_btree_node_buf(nb, noff);
		/* the fragment slot (offset 0) and the dup-node array (offset
		 * 16) OVERLAP in the same node: memmove, not memcpy, or the
		 * copy clobbers its own source */
		memmove(xbfs_dup_node_array(dn), arr, 64);
		dn->left = XBFS_BTREE_NULL;
		dn->right = XBFS_BTREE_NULL;
		arr = xbfs_dup_node_array(dn);
		arr[arr[0] + 1] = value;
		arr[0]++;
		if(!shared) {
			xbfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
		}
		values[index] = xbfs_dup_make_link(XBFS_BTREE_DUPLICATE_NODE,
						  noff, 0);
		return 1;	/* the leaf's value link changed */
	}

	/* allocate a new duplicate node, copy the array + the value.
	 * The grow's held buffer must be the one that shares the header
	 * block (the LEAF, not the fragment node). The new node can land
	 * in the fragment's block OR the leaf's block (nodes pack at
	 * 1024-byte offsets): reuse the held buffers in those cases. */
		if((res = xbfs_btree_grow(dir, &nd, buf)) < 0) {
		if(!shared) {
			brelse(nb);
		}
		return res;
	}
	if(nd / dir->sb->s_blocksize == leaf_off / dir->sb->s_blocksize) {
		ndb = buf;	/* shares the leaf's block */
	} else if(nd / dir->sb->s_blocksize == noff / dir->sb->s_blocksize) {
		ndb = nb;	/* shares the fragment's block */
	} else {
		if(!(ndb = xbfs_btree_read_node_tagged(dir, nd, "xbfs_btree_dup_fragment_add"))) {
			if(!shared) {
				brelse(nb);
			}
			return -EIO;
		}
	}
	memset_b(xbfs_btree_node_buf(ndb, nd), 0, XBFS_BTREE_NODE_SIZE);
	dn = xbfs_btree_node_buf(ndb, nd);
	dn->left = XBFS_BTREE_NULL;
	dn->right = XBFS_BTREE_NULL;
	memcpy_b(xbfs_dup_node_array(dn), arr, 64);
	arr = xbfs_dup_node_array(dn);
	arr[arr[0] + 1] = value;
	arr[0]++;
	/* free the fragment slot (same buffer when the node is in it) */
	arr = xbfs_dup_array(nb, noff, frag);
	memset_b(arr, 0, 64);
	if(!shared) {
		xbfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb (+ndb) */
	}
	if(ndb != nb && ndb != buf) {
		xbfs_log_write_block(dir->sb, ndb->block, ndb);	/* consumes ndb */
	}
	/* when the fragment or the new node share the leaf's block, all
	 * the changes live in the leaf buffer and are written by the
	 * caller's final leaf write */

	values[index] = xbfs_dup_make_link(XBFS_BTREE_DUPLICATE_NODE, nd, 0);
	return 1;	/* the leaf's value link changed */
}

static int xbfs_btree_dup_node_add(struct inode *dir, struct buffer *buf,
				  __u64 leaf_off, int index, __u64 value)
{
	struct xbfs_btree_node *n = xbfs_btree_node_buf(buf, leaf_off);
	__u64 *values = xbfs_btree_values(n);
	struct xbfs_btree_node *dn;
	struct buffer *nb, *ndb;
	__u64 noff, nd;
	__u64 *arr;
	int res, shared = 0;

	noff = xbfs_dup_link_off(values[index]);
	if(noff / dir->sb->s_blocksize == leaf_off / dir->sb->s_blocksize) {
		nb = buf;	/* the chain head shares the leaf's block */
		shared = 1;
	} else {
		if(!(nb = xbfs_btree_read_node_tagged(dir, noff, "xbfs_btree_dup_node_add"))) {
			return -EIO;
		}
	}
	for(;;) {
		dn = xbfs_btree_node_buf(nb, noff);
		arr = xbfs_dup_node_array(dn);
		if(arr[0] < XBFS_BTREE_NUM_DUPLICATE_VALUES) {
			arr[arr[0] + 1] = value;
			arr[0]++;
			if(!shared) {
				xbfs_log_write_block(dir->sb, nb->block, nb);
			}
			return 0;	/* the leaf's link is unchanged */
		}
		if(dn->right != XBFS_BTREE_NULL) {
			noff = dn->right;
			if(nb != buf) {
				brelse(nb);
			}
			if(noff / dir->sb->s_blocksize
			   == leaf_off / dir->sb->s_blocksize) {
				nb = buf;
				shared = 1;
			} else {
				if(!(nb = xbfs_btree_read_node_tagged(dir, noff, "xbfs_btree_dup_node_add"))) {
					return -EIO;
				}
				shared = 0;
			}
			continue;
		}
		/* full: allocate a new node and chain it. The new node can
		 * share the current dup node's block OR the leaf's block
		 * (nodes pack at 1024-byte offsets) — reuse the held
		 * buffers in those cases. */
		if((res = xbfs_btree_grow(dir, &nd, buf)) < 0) {
			if(nb != buf) {
				brelse(nb);
			}
			return res;
		}
		if(nd / dir->sb->s_blocksize == leaf_off / dir->sb->s_blocksize) {
			ndb = buf;	/* shares the leaf's block */
		} else if(nd / dir->sb->s_blocksize == noff / dir->sb->s_blocksize) {
			ndb = nb;	/* shares the current node's block */
		} else {
			if(!(ndb = xbfs_btree_read_node_tagged(dir, nd, "xbfs_btree_dup_node_add"))) {
				if(nb != buf) {
					brelse(nb);
				}
				return -EIO;
			}
		}
		memset_b(xbfs_btree_node_buf(ndb, nd), 0, XBFS_BTREE_NODE_SIZE);
		dn = xbfs_btree_node_buf(ndb, nd);
		dn->left = noff;
		dn->right = XBFS_BTREE_NULL;
		arr = xbfs_dup_node_array(dn);
		arr[0] = 1;
		arr[1] = value;
		dn = xbfs_btree_node_buf(nb, noff);
		dn->right = nd;
		if(nb != buf) {
			xbfs_log_write_block(dir->sb, nb->block, nb);
		}
		if(ndb != nb && ndb != buf) {
			xbfs_log_write_block(dir->sb, ndb->block, ndb);
		}
		/* when nb/ndb are the leaf buffer, its final write by the
		 * caller covers the chain + new-node changes */
		return 0;	/* the leaf's link is unchanged */
	}
}

static int xbfs_btree_insert_dup(struct inode *dir, __u64 noff, int index,
			       __u64 value)
{
	struct buffer *buf;
	struct xbfs_btree_node *n;
	__u64 *values;
	__u64 old;
	int res;

	if(!(buf = xbfs_btree_read_node_tagged(dir, noff, "xbfs_btree_insert_dup"))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, noff);
	values = xbfs_btree_values(n);
	old = values[index];
	if(xbfs_dup_link_type(old) == 0) {
		res = xbfs_btree_dup_promote(dir, buf, noff, index, old, value);
	} else if(xbfs_dup_link_type(old) == XBFS_BTREE_DUPLICATE_FRAGMENT) {
		res = xbfs_btree_dup_fragment_add(dir, buf, noff, index, value);
	} else {
		res = xbfs_btree_dup_node_add(dir, buf, noff, index, value);
	}
	if(res >= 0) {
		/* the leaf's value link changed, or a dup node sharing
		 * the leaf's block was modified: write it (the caller no
		 * longer re-breads — the leaf may share its block with
		 * the header, and re-breading while this buffer is held
		 * would sleep on our own lock). The tx dedupes, so an
		 * unchanged leaf costs one extra record at most. */
		xbfs_log_write_block(dir->sb, buf->block, buf);
		return (res > 0) ? 1 : 0;
	}
	brelse(buf);
	return res;
}

/*
 * A fragment/duplicate node block is never returned to the allocator:
 * it stays addressed by the tree stream's runs, so reusing it would
 * reference the block twice (the stream runs do not shrink). Haiku can
 * free them because they sit at the stream tail (DataStream::FreeBlock
 * truncates); our stream only grows, so the block stays allocated and
 * becomes an unreferenced stream block — format-legal, and the bitmap
 * accounting stays exact (the stream counts it).
 */
static void xbfs_dup_free_node(struct inode *dir, __u64 off)
{
	(void)dir;
	(void)off;
}

static int xbfs_btree_remove_dup(struct inode *dir, __u64 noff, int index,
			       __u64 value)
{
	struct buffer *buf;
	struct xbfs_btree_node *n;
	__u64 *values;
	__u64 old;
	struct xbfs_btree_node *dn;
	struct buffer *nb;
	__u64 *arr;
	__u64 leaf_off = noff;
	int frag, i, cnt, res = -ENOENT;
	int shared;

	if(!(buf = xbfs_btree_read_node_tagged(dir, noff, "xbfs_btree_remove_dup"))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, noff);
	values = xbfs_btree_values(n);
	old = values[index];

	if(xbfs_dup_link_type(old) == XBFS_BTREE_DUPLICATE_FRAGMENT) {
		noff = xbfs_dup_link_off(old);
		frag = xbfs_dup_link_frag(old);
		if(noff / dir->sb->s_blocksize
		   == leaf_off / dir->sb->s_blocksize) {
			nb = buf;	/* shares the leaf's block */
			shared = 1;
		} else {
			if(!(nb = xbfs_btree_read_node_tagged(dir, noff,
					"xbfs_btree_remove_dup"))) {
				return -EIO;
			}
			shared = 0;
		}
		arr = xbfs_dup_array(nb, noff, frag);
		cnt = arr[0];
				for(i = 1; i <= cnt; i++) {
			if(arr[i] == value) {
				res = 0;
				break;
			}
		}
		if(res) {
			if(!shared) {
				brelse(nb);
			}
			brelse(buf);
			return -ENOENT;
		}
		for(; i < cnt; i++) {
			arr[i] = arr[i + 1];
		}
		cnt--;
		arr[0] = cnt;	/* the slot's values (0 = empty) */
		/* the node stays allocated (see xbfs_dup_free_node) */
		if(!shared) {
			xbfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
		}
		if(cnt == 1) {
			/* demote: the remaining value becomes direct */
			values[index] = arr[1];
			res = 1;	/* the leaf's value link changed */
			goto leaf_done;
		}
		if(cnt == 0) {
			/* the key's last value is gone: remove the key too */
			xbfs_btree_remove_at(n, index);
			res = 1;	/* the leaf's value link changed */
			goto leaf_done;
		}
		res = 0;
		goto leaf_done;
	}

	/* a duplicate-node chain: walk it, remove, then clean up */
	if(xbfs_dup_link_type(old) != XBFS_BTREE_DUPLICATE_NODE) {
		return -ENOENT;
	}
	noff = xbfs_dup_link_off(old);
	if(noff / dir->sb->s_blocksize
	   == leaf_off / dir->sb->s_blocksize) {
		nb = buf;
		shared = 1;
	} else {
		if(!(nb = xbfs_btree_read_node_tagged(dir, noff,
				"xbfs_btree_remove_dup"))) {
			return -EIO;
		}
		shared = 0;
	}
	for(;;) {
		dn = xbfs_btree_node_buf(nb, noff);
		arr = xbfs_dup_node_array(dn);
		cnt = arr[0];
		for(i = 1; i <= cnt; i++) {
			if(arr[i] == value) {
				res = 0;
				break;
			}
		}
		if(res == 0) {
			break;
		}
		if(dn->right == XBFS_BTREE_NULL) {
			if(nb != buf) {
				brelse(nb);
			}
			brelse(buf);
			return -ENOENT;
		}
		noff = dn->right;
		if(nb != buf) {
			brelse(nb);
		}
		if(noff / dir->sb->s_blocksize
		   == leaf_off / dir->sb->s_blocksize) {
			nb = buf;
			shared = 1;
		} else {
			if(!(nb = xbfs_btree_read_node_tagged(dir, noff,
					"xbfs_btree_remove_dup"))) {
				brelse(buf);
				return -EIO;
			}
			shared = 0;
		}
	}
	/* remove the value from this node */
	for(; i < cnt; i++) {
		arr[i] = arr[i + 1];
	}
	cnt--;
	arr[0] = cnt;
	if(!shared) {
		xbfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
	}

	/* clean up empty nodes and demote a lone remaining value. The
	 * chain head stays at values[index] unless the whole chain drops
	 * to one value, in which case it demotes to a direct value and
	 * every chain node is freed. */
	{
		__u64 chain = xbfs_dup_link_off(old);
		__u64 total = 0;
		__u64 last = 0;

		for(noff = chain; noff != XBFS_BTREE_NULL;) {
			struct buffer *nb2;
			__u64 nnext;
			int sh2;

			if(noff / dir->sb->s_blocksize
			   == leaf_off / dir->sb->s_blocksize) {
				nb2 = buf;
				sh2 = 1;
			} else {
				if(!(nb2 = xbfs_btree_read_node_tagged(dir,
						noff,
						"xbfs_btree_remove_dup"))) {
					brelse(buf);
					return -EIO;
				}
				sh2 = 0;
			}
			dn = xbfs_btree_node_buf(nb2, noff);
			arr = xbfs_dup_node_array(dn);
			total += arr[0];
			if(arr[0] == 1) {
				last = arr[1];
			}
			nnext = dn->right;
			if(!sh2) {
				brelse(nb2);
			}
			noff = nnext;
		}
		if(total == 1) {
			values[index] = last;
			res = 1;	/* the leaf's value link changed */
		} else if(total == 0) {
			xbfs_btree_remove_at(n, index);
			res = 1;
		}
		if(total <= 1) {
			for(noff = chain; noff != XBFS_BTREE_NULL;) {
				struct buffer *nb3;
				__u64 nnext;
				int sh3;

				if(noff / dir->sb->s_blocksize
				   == leaf_off / dir->sb->s_blocksize) {
					nb3 = buf;
					sh3 = 1;
				} else {
					if(!(nb3 = xbfs_btree_read_node_tagged(
							dir, noff,
							"xbfs_btree_remove_dup"))) {
						break;
					}
					sh3 = 0;
				}
				nnext = xbfs_btree_node_buf(nb3, noff)->right;
				if(!sh3) {
					brelse(nb3);
				}
				xbfs_dup_free_node(dir, noff);
				noff = nnext;
			}
		}
	}
leaf_done:
	if(res > 0) {
		/* the leaf's value link changed: write it (the caller no
		 * longer re-breads — see insert_dup) */
		xbfs_log_write_block(dir->sb, buf->block, buf);
	} else {
		brelse(buf);
	}
	return res;
}

static int xbfs_btree_delete_impl(struct inode *dir, const char *name)
{
	struct xbfs_btree_header header;
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 path[16];
	int npath, index, res;

	if(!name[0]) {
		return -EINVAL;
	}
	if((res = xbfs_btree_read_header(dir, &header))) {
		return res;
	}
	if((res = xbfs_btree_descend_t(dir, &header, name, strlen(name),
				      path, &npath))) {
		return res;
	}

	if(!(buf = xbfs_btree_read_node(dir, path[npath - 1]))) {
		return -EIO;
	}
	n = xbfs_btree_node_buf(buf, path[npath - 1]);
	if(xbfs_btree_search_node_t(n, name, strlen(name), &index,
				   header.data_type)) {
		brelse(buf);
		return -ENOENT;
	}
	xbfs_btree_remove_at(n, index);
	dir->state |= INODE_DIRTY;
	xbfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
}

/*
 * Remove the entry whose inode matches 'ino' (used by rmdir, which has no
 * name). Searches every leaf.
 */
static int xbfs_btree_delete_ino_impl(struct inode *dir, __ino_t ino)
{
	struct xbfs_btree_header header;
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int i, res;

	if((res = xbfs_btree_read_header(dir, &header))) {
		return res;
	}

	node_off = header.root_node_ptr;
	for(;;) {
		if(!(buf = xbfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, node_off);
		if(n->overflow != XBFS_BTREE_NULL) {
			node_off = n->all_key_count ?
				xbfs_btree_values(n)[0] : n->overflow;
			brelse(buf);
			continue;
		}
		brelse(buf);
		break;
	}

	for(;;) {
		if(!(buf = xbfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = xbfs_btree_node_buf(buf, node_off);
		if(n->overflow != XBFS_BTREE_NULL) {
			brelse(buf);
			return -EIO;
		}
		for(i = 0; i < n->all_key_count; i++) {
			if(xbfs_btree_values(n)[i] == ino) {
				xbfs_btree_remove_at(n, i);
				dir->state |= INODE_DIRTY;
				xbfs_log_write_block(dir->sb, buf->block, buf);
				return 0;
			}
		}
		if(n->right == XBFS_BTREE_NULL) {
			brelse(buf);
			break;
		}
		node_off = n->right;
		brelse(buf);
	}
	return -ENOENT;
}

int xbfs_btree_insert_value(struct inode *dir, const char *key, int keylen,
			   int dtype, __u64 value)
{
	int res;

	xbfs_log_begin(dir->sb);
	res = xbfs_btree_insert_impl_t(dir, key, keylen, value, dtype, 1);
	xbfs_log_commit(dir->sb);
	return res;
}

int xbfs_btree_delete_value(struct inode *dir, const char *key, int keylen,
			   int dtype, __u64 value)
{
	struct xbfs_btree_header header;
	struct xbfs_btree_node *n;
	struct buffer *buf;
	__u64 path[16];
	int npath, index, res;
	int begun = 0;

	if(!keylen) {
		return -ENOENT;
	}
	xbfs_log_begin(dir->sb);
	begun = 1;
	if((res = xbfs_btree_read_header(dir, &header))) {
		goto out;
	}
	if((res = xbfs_btree_descend_t(dir, &header, key, keylen,
				      path, &npath))) {
		goto out;
	}
	if(!(buf = xbfs_btree_read_node(dir, path[npath - 1]))) {
		res = -EIO;
		goto out;
	}
	n = xbfs_btree_node_buf(buf, path[npath - 1]);
	if(xbfs_btree_search_node_t(n, key, keylen, &index,
				   header.data_type)) {
		brelse(buf);
		res = -ENOENT;
		goto out;
	}
	if(xbfs_dup_link_type(xbfs_btree_values(n)[index]) == 0) {
		/* a direct value: remove the whole key */
		if(xbfs_btree_values(n)[index] != value) {
			brelse(buf);
			res = -ENOENT;
			goto out;
		}
		xbfs_btree_remove_at(n, index);
		dir->state |= INODE_DIRTY;
		xbfs_log_write_block(dir->sb, buf->block, buf);	/* consumes buf */
		res = 0;
		goto out;
	}
	/* a duplicate chain: remove just this value (release the leaf
	 * first — the dup path re-breads nodes that may share the leaf's
	 * block at block sizes above the node size; the dup owns the
	 * leaf's write) */
	brelse(buf);
	res = xbfs_btree_remove_dup(dir, path[npath - 1], index, value);
	if(res > 0) {
		res = 0;
	}
out:
	if(begun) {
		xbfs_log_commit(dir->sb);
	}
	return res;
}

int xbfs_btree_insert(struct inode *dir, const char *name, __ino_t ino)
{
	int res;

	xbfs_log_begin(dir->sb);
	res = xbfs_btree_insert_impl(dir, name, ino);
	xbfs_log_commit(dir->sb);
	return res;
}

int xbfs_btree_delete(struct inode *dir, const char *name)
{
	int res;

	xbfs_log_begin(dir->sb);
	res = xbfs_btree_delete_impl(dir, name);
	xbfs_log_commit(dir->sb);
	return res;
}

int xbfs_btree_delete_ino(struct inode *dir, __ino_t ino)
{
	int res;

	xbfs_log_begin(dir->sb);
	res = xbfs_btree_delete_ino_impl(dir, ino);
	xbfs_log_commit(dir->sb);
	return res;
}
