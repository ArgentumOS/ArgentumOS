/*
 * fnx/fs/bfs/btree.c
 *
 * BFS B+tree access (read-only).
 *
 * The directory B+tree lives in the directory inode's data stream: the
 * btree header (struct bfs_btree_header) is at stream offset 0, and the
 * tree nodes (struct bfs_btree_node, node_size bytes each) are at byte
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
#include <fnx/bfs.h>
#include <fnx/buffer.h>
#include <fnx/string.h>

struct bfs_btree_pair {
	const char *key;
	int keylen;
	__u64 val;
};

static int bfs_btree_descend_t(struct inode *, const struct bfs_btree_header *,
			      const char *, int, __u64 *, int *);
static int bfs_btree_descend(struct inode *, const struct bfs_btree_header *,
			     const char *, __u64 *, int *);
static int bfs_btree_search_node_t(struct bfs_btree_node *, const char *, int,
				   int *, int);
static int bfs_btree_collect_t(struct bfs_btree_node *, struct bfs_btree_pair *,
			      int, int, const char *, int, __u64, int, char *);
static int bfs_btree_serialize(struct bfs_btree_node *, struct bfs_btree_pair *,
			       int, __u64, __u64, __u64);
static int bfs_btree_split(struct inode *, struct bfs_btree_header *,
			   __u64 *, int, struct bfs_btree_pair *, int, int,
			   __u64, int, int);
static int bfs_btree_insert_dup(struct inode *, struct buffer *, int, __u64);
static int bfs_btree_remove_dup(struct inode *, struct buffer *, int, __u64);
static int bfs_dup_link_type(__u64);
static __u64 bfs_dup_link_off(__u64);
static int bfs_dup_link_frag(__u64);
static __u64 *bfs_dup_array(struct buffer *, int);
static __u64 *bfs_dup_node_array(struct bfs_btree_node *);

/* key-length index: cumulative end offsets of each key */
static __u16 *bfs_btree_keylen_index(struct bfs_btree_node *n)
{
	return (__u16 *)((char *)n
			+ ((sizeof(struct bfs_btree_node) + n->all_key_length + 7) & ~7));
}

/* value array: follows the key-length index */
static __u64 *bfs_btree_values(struct bfs_btree_node *n)
{
	return (__u64 *)((char *)bfs_btree_keylen_index(n)
			+ n->all_key_count * sizeof(__u16));
}

/* key i: pointer into the key area + its length */
static char *bfs_btree_key(struct bfs_btree_node *n, int index, int *keylen)
{
	__u16 *kl = bfs_btree_keylen_index(n);
	char *keys = (char *)n + sizeof(struct bfs_btree_node);
	int prev = index ? kl[index - 1] : 0;

	*keylen = kl[index] - prev;
	return keys + prev;
}

/*
 * Read a btree node at stream byte offset 'off'. Returns NULL on error.
 * The stream block mapping goes through the directory inode's bmap.
 */
static struct buffer *bfs_btree_read_node(struct inode *i, __u64 off)
{
	__blk_t block;
	struct buffer *buf;

	if(off & (BFS_BLOCK_SIZE - 1)) {
		return NULL;	/* nodes are block aligned */
	}
		if((block = bfs_bmap(i, (__off_t)off, FOR_READING)) < 0) {
				return NULL;
	}
		if(!block) {
		return NULL;
	}
	if(!(buf = bread(i->dev, block, BFS_BLOCK_SIZE))) {
				return NULL;
	}
		return buf;
}

/*
 * Read the btree header (stream offset 0) and validate it.
 * Returns 0 with *header filled on success.
 */
static int bfs_btree_read_header(struct inode *i, struct bfs_btree_header *header)
{
	struct buffer *buf;
	struct bfs_btree_header *h;

	if(!(buf = bfs_btree_read_node(i, 0))) {
		return -EIO;
	}
	h = (struct bfs_btree_header *)buf->data;
	if(h->magic != BFS_BTREE_MAGIC || h->node_size != BFS_BLOCK_SIZE) {
		brelse(buf);
		return -EINVAL;
	}
	memcpy_b(header, h, sizeof(struct bfs_btree_header));
	brelse(buf);
	return 0;
}

/* update the tree header on disk (max_depth changes on split). The
 * maximum_size field tracks the stream length (Haiku validates links
 * against MaximumSize() - NodeSize()) */
static int bfs_btree_write_header(struct inode *i, struct bfs_btree_header *header)
{
	struct buffer *buf;
	struct bfs_btree_header *h;

	if(!(buf = bfs_btree_read_node(i, 0))) {
		return -EIO;
	}
	h = (struct bfs_btree_header *)buf->data;
	header->max_size = i->i_size;
	memcpy_b(h, header, sizeof(struct bfs_btree_header));
	bfs_log_write_block(i->sb, buf->block, buf);
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
static int bfs_btree_key_cmp(int dtype, const char *k1, int l1,
			     const char *k2, int l2)
{
#define BFS_CMP_INT(_t) do {						\
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
	case BFS_BTREE_INT32_TYPE:
		BFS_CMP_INT(__s32);
	case BFS_BTREE_UINT32_TYPE:
		BFS_CMP_INT(__u32);
	case BFS_BTREE_INT64_TYPE:
		BFS_CMP_INT(__s64);
	case BFS_BTREE_UINT64_TYPE:
		BFS_CMP_INT(__u64);
	case BFS_BTREE_FLOAT_TYPE:
		BFS_CMP_INT(float);
	case BFS_BTREE_DOUBLE_TYPE:
		BFS_CMP_INT(double);
	case BFS_BTREE_INT8_TYPE:
		BFS_CMP_INT(__s8);
	case BFS_BTREE_INT16_TYPE:
		BFS_CMP_INT(__s16);
	default:
		break;
	}
#undef BFS_CMP_INT
	/* STRING and anything else: byte order with the prefix rule */
	{
		int cmp = strncmp(k1, k2, (l1 < l2) ? l1 : l2);
		if(cmp == 0) {
			cmp = l1 - l2;
		}
		return cmp;
	}
}

static int bfs_btree_search_node_t(struct bfs_btree_node *n, const char *key,
				   int keylen, int *index, int dtype)
{
	int lo, hi, mid, cmp, klen;
	char *k;

	lo = 0;
	hi = n->all_key_count - 1;
	while(lo <= hi) {
		mid = (lo + hi) >> 1;
		k = bfs_btree_key(n, mid, &klen);
		cmp = bfs_btree_key_cmp(dtype, key, keylen, k, klen);
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

static int bfs_btree_search_node(struct bfs_btree_node *n, const char *name,
				 int *index)
{
	return bfs_btree_search_node_t(n, name, strlen(name), index,
				       BFS_BTREE_STRING_TYPE);
}

/*
 * Look up 'name' in the directory tree.
 * On success returns 0 and sets *ino to the inode block number.
 */
int bfs_btree_find(struct inode *dir, const char *name, __ino_t *ino)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 path[16];
	int npath, index, res;

	if(!name[0]) {
		return -ENOENT;
	}
	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}
	if((res = bfs_btree_descend_t(dir, &header, name, strlen(name),
				      path, &npath))) {
		return res;
	}

	if(!(buf = bfs_btree_read_node(dir, path[npath - 1]))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	if(n->all_key_count == 0 || n->all_key_length == 0) {
		brelse(buf);
		return -ENOENT;
	}
	if(bfs_btree_search_node_t(n, name, strlen(name), &index,
				   header.data_type) == 0) {
		*ino = (__ino_t)bfs_btree_values(n)[index];
		brelse(buf);
		return 0;
	}
	brelse(buf);
	return -ENOENT;
}

/*
 * Iterate over all directory entries (key, value) in key order.
 */
int bfs_btree_iterate(struct inode *dir, int (*fn)(const char *, __ino_t, void *),
		      void *arg)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int index, keylen, res;

	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}

	/* descend to the leftmost leaf, then iterate right via the links */
	node_off = header.root_node_ptr;
	for(;;) {
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->overflow != BFS_BTREE_NULL) {
			node_off = n->all_key_count ?
				bfs_btree_values(n)[0] : n->overflow;
			brelse(buf);
			continue;
		}
		brelse(buf);
		break;
	}

	for(;;) {
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->overflow != BFS_BTREE_NULL) {
			brelse(buf);
			return -EIO;
		}
		for(index = 0; index < n->all_key_count; index++) {
			char keybuf[BFS_BTREE_MAX_KEY_LEN];
			keylen = 0;
			{
				char *key = bfs_btree_key(n, index, &keylen);
				if(keylen >= BFS_BTREE_MAX_KEY_LEN) {
					brelse(buf);
					return -EIO;
				}
				memcpy_b(keybuf, key, keylen);
			}
			keybuf[keylen] = 0;
			if(fn(keybuf, (__ino_t)bfs_btree_values(n)[index], arg)) {
				brelse(buf);
				return 0;
			}
		}
		if(n->right == BFS_BTREE_NULL) {
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
 * inode, a fragment link (BFS_BTREE_DUPLICATE_FRAGMENT) or a
 * duplicate-node link (BFS_BTREE_DUPLICATE_NODE, a right-link chain);
 * each expanded value is reported once. This is the query engine's
 * view of an index tree (Haiku's BQuery walks the same structures).
 */
int bfs_btree_iterate_values(struct inode *dir, int dtype,
			     int (*fn)(const char *, int, __ino_t, void *),
			     void *arg)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int index, keylen, res;

	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}
	if(header.data_type != dtype && dtype != BFS_BTREE_STRING_TYPE
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
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->overflow != BFS_BTREE_NULL) {
			node_off = n->all_key_count ?
				bfs_btree_values(n)[0] : n->overflow;
			brelse(buf);
			continue;
		}
		brelse(buf);
		break;
	}

	for(;;) {
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->overflow != BFS_BTREE_NULL) {
			brelse(buf);
			return -EIO;
		}
		for(index = 0; index < n->all_key_count; index++) {
			char keybuf[BFS_BTREE_MAX_KEY_LEN];
			__u64 value;
			keylen = 0;
			{
				char *key = bfs_btree_key(n, index, &keylen);
				if(keylen >= BFS_BTREE_MAX_KEY_LEN) {
					brelse(buf);
					return -EIO;
				}
				memcpy_b(keybuf, key, keylen);
			}
			keybuf[keylen] = 0;
			value = bfs_btree_values(n)[index];

			/* direct value */
			if(bfs_dup_link_type(value) <= 1) {
				if(fn(keybuf, keylen, (__ino_t)value, arg)) {
					brelse(buf);
					return 0;
				}
				continue;
			}
			if(bfs_dup_link_type(value) == BFS_BTREE_DUPLICATE_FRAGMENT) {
				/* a fragment slot: {count, values[7]} */
				struct buffer *fb;
				__u64 *arr;
				int s;

				if(!(fb = bfs_btree_read_node(dir,
						bfs_dup_link_off(value)))) {
					brelse(buf);
					return -EIO;
				}
				arr = bfs_dup_array(fb, bfs_dup_link_frag(value));
				if(arr[0] > 7) {
					/* a fragment slot holds at most 7
					 * values; refuse a corrupt count */
					brelse(fb);
					brelse(buf);
					return -EIO;
				}
				for(s = 0; s < (int)arr[0]; s++) {
					if(fn(keybuf, keylen, (__ino_t)arr[1 + s],
					    arg)) {
						brelse(fb);
						brelse(buf);
						return 0;
					}
				}
				brelse(fb);
				continue;
			}
			if(bfs_dup_link_type(value) == BFS_BTREE_DUPLICATE_NODE) {
				/* a duplicate-node chain: {left, right,
				 * count@16, values[125]@24} */
				__u64 noff = bfs_dup_link_off(value);
				int hops = 0;

				while(noff != (__u64)BFS_BTREE_NULL) {
					if(++hops > 4096) {
						/* a corrupt next link would
						 * walk forever */
						brelse(buf);
						return -EIO;
					}
					struct bfs_btree_node *dn;
					struct buffer *db;
					__u64 *arr;
					int s;

					if(!(db = bfs_btree_read_node(dir,
							noff))) {
						brelse(buf);
						return -EIO;
					}
					dn = (struct bfs_btree_node *)db->data;
					arr = bfs_dup_node_array(dn);
					if(arr[0] > 125) {
						brelse(db);
						brelse(buf);
						return -EIO;
					}
					for(s = 0; s < (int)arr[0]; s++) {
						if(fn(keybuf, keylen,
						    (__ino_t)arr[1 + s], arg)) {
							brelse(db);
							brelse(buf);
							return 0;
						}
					}
					noff = dn->right;
					brelse(db);
				}
				continue;
			}
			/* unknown link type: skip (defensive) */
		}
		if(n->right == BFS_BTREE_NULL) {
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
static int bfs_btree_node_room(struct bfs_btree_node *n, int keylen)
{
	int used = sizeof(struct bfs_btree_node) + n->all_key_length + keylen;
	int cnt = n->all_key_count + 1;

	return ((used + 7) & ~7) + cnt * 2 + cnt * 8 <= BFS_BLOCK_SIZE;
}

/*
 * From an interior node, pick the child offset for 'name'.
 * Comparison is prefix-aware like bfs_btree_search_node(): a name that
 * extends a separator key (e.g. "whoami" vs separator "who") is GREATER
 * than the key, so it descends into the next child instead of matching
 * the current one.
 */
static void bfs_btree_pick_child_t(struct bfs_btree_node *n, const char *key,
				   int keylen, __u64 *child, int dtype)
{
	int i, klen, cmp;
	char *k;
	__u64 *values = bfs_btree_values(n);

	if(n->all_key_count == 0) {
		*child = n->overflow;
		return;
	}

	k = bfs_btree_key(n, n->all_key_count - 1, &klen);
	cmp = bfs_btree_key_cmp(dtype, key, keylen, k, klen);
	if(cmp > 0) {
		*child = (n->overflow != BFS_BTREE_NULL) ?
			n->overflow : values[n->all_key_count - 1];
		return;
	}

	for(i = 0; i < n->all_key_count && i < 128; i++) {
		k = bfs_btree_key(n, i, &klen);
		cmp = bfs_btree_key_cmp(dtype, key, keylen, k, klen);
		if(cmp <= 0) {
			*child = values[i];
			return;
		}
	}
	*child = values[n->all_key_count - 1];
}

static void bfs_btree_pick_child(struct bfs_btree_node *n, const char *name,
				 __u64 *child)
{
	bfs_btree_pick_child_t(n, name, strlen(name), child,
			       BFS_BTREE_STRING_TYPE);
}

static int bfs_btree_descend_t(struct inode *dir,
			       const struct bfs_btree_header *h,
			       const char *key, int keylen, __u64 *path,
			       int *npath)
{
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int depth = 0;

	node_off = h->root_node_ptr;
	for(;;) {
		if(depth >= 16) {
			return -EIO;
		}
		path[depth++] = node_off;
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->all_key_count > 128 || n->all_key_length > 900) {
			brelse(buf);
			return -EIO;
		}
		if(n->overflow == BFS_BTREE_NULL) {
			brelse(buf);
			*npath = depth;
			return 0;
		}
		bfs_btree_pick_child_t(n, key, keylen, &node_off, h->data_type);
		brelse(buf);
	}
}

static int bfs_btree_descend(struct inode *dir, const struct bfs_btree_header *h,
			     const char *name, __u64 *path, int *npath)
{
	return bfs_btree_descend_t(dir, h, name, strlen(name), path, npath);
}

/* collect a node's pairs, copying the keys into 'keybuf' (the node buffer
 * may be released before the pairs are used); optionally insert one pair */
static int bfs_btree_collect_t(struct bfs_btree_node *n,
			      struct bfs_btree_pair *pairs, int max,
			      int do_insert, const char *iname, int iname_len,
			      __u64 ino, int dtype, char *keybuf)
{
	int count = 0, i, index, off = 0;
	__u64 *values = bfs_btree_values(n);

	if(n->all_key_count > max) {
		return -1;
	}
	for(i = 0; i < n->all_key_count; i++) {
		pairs[count].keylen = 0;
		{
			char *key = bfs_btree_key(n, i, &pairs[count].keylen);
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
		int cmp = bfs_btree_key_cmp(dtype, iname, iname_len,
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

static int bfs_btree_serialize(struct bfs_btree_node *n,
			       struct bfs_btree_pair *pairs, int count,
			       __u64 interior_overflow, __u64 right,
			       __u64 left)
{
	char *keydata;
	int total = 0, i, off = 0;

	if(!(keydata = (char *)kmalloc(BFS_BLOCK_SIZE))) {
		return -ENOMEM;
	}
	for(i = 0; i < count; i++) {
		memcpy_b(keydata + off, pairs[i].key, pairs[i].keylen);
		off += pairs[i].keylen;
		total += pairs[i].keylen;
	}

	memset_b(n, 0, BFS_BLOCK_SIZE);
	n->left = left;
	n->right = right;
	n->overflow = interior_overflow;	/* -1 => leaf */
	n->all_key_count = count;
	n->all_key_length = total;
	{
		char *keys = (char *)n + sizeof(struct bfs_btree_node);
		__u16 *kl = bfs_btree_keylen_index(n);
		__u64 *values = bfs_btree_values(n);
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

static int bfs_btree_write_node(struct inode *dir, __u64 off,
				struct bfs_btree_pair *pairs, int count,
				__u64 overflow, __u64 right, __u64 left)
{
	struct bfs_btree_node *n;
	struct buffer *buf;

	if(!(buf = bfs_btree_read_node(dir, off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	bfs_btree_serialize(n, pairs, count, overflow, right, left);
	bfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
}


/*
 * Point a leaf's left link at 'left' (the previous leaf in the stream).
 * Called after a leaf split: the leaf that used to follow the split node
 * now follows the new right half. Haiku keeps these links consistent,
 * and checkfs validates them.
 */
static int bfs_btree_relink_left(struct inode *dir, __u64 off, __u64 left)
{
	struct bfs_btree_node *n;
	struct buffer *buf;

	if(off == BFS_BTREE_NULL) {
		return 0;
	}
	if(!(buf = bfs_btree_read_node(dir, off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	n->left = left;
	bfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
}

static int bfs_btree_grow(struct inode *dir, __u64 *off)
{
	struct bfs_btree_header *h;
	struct buffer *hbuf;
	__blk_t block;

	if((block = bmap(dir, dir->i_size, FOR_WRITING)) < 0) {
		return block;
	}
		*off = dir->i_size;
	dir->i_size += BFS_BLOCK_SIZE;
	dir->u.bfs.raw.u.data.size = dir->i_size;
	dir->state |= INODE_DIRTY;

	/* Haiku validates node links against MaximumSize() - NodeSize(),
	 * so the header's maximum_size must track the stream length on
	 * EVERY grow, not just root splits */
	if((hbuf = bfs_btree_read_node(dir, 0))) {
		h = (struct bfs_btree_header *)hbuf->data;
		h->max_size = dir->i_size;
		bfs_log_write_block(dir->sb, hbuf->block, hbuf);
	}
	return 0;
}

/*
 * Split the node at path[npath-1] into two halves. 'pairs' holds every
 * pair of the node plus the one being inserted (leaves: the new directory
 * entry; interiors: the new separator + right child), 'count' = pairs
 * count, 'split_at' = pairs kept in the left half, 'node_overflow' = the
 * node's rightmost child (BFS_BTREE_NULL for leaves), 'depth' = the
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
static int bfs_btree_split(struct inode *dir, struct bfs_btree_header *header,
			   __u64 *path, int npath, struct bfs_btree_pair *pairs,
			   int count, int split_at, __u64 node_overflow,
			   int depth, int dtype)
{
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off = path[npath - 1];
	__u64 old_right, old_left, left_overflow, right_overflow;
	__u64 left_off, right_off;
	const char *sep;
	struct bfs_btree_pair *right_pairs;
	int sep_len, is_leaf, right_count;
	int res;

	if(split_at < 1 || split_at >= count) {
		return -EIO;
	}
	is_leaf = (node_overflow == BFS_BTREE_NULL);
	if(is_leaf) {
		/* leaf: the separator is the last key of the left half */
		sep = pairs[split_at - 1].key;
		sep_len = pairs[split_at - 1].keylen;
		left_overflow = BFS_BTREE_NULL;
		right_overflow = BFS_BTREE_NULL;
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

	if(!(buf = bfs_btree_read_node(dir, node_off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	old_right = n->right;
	old_left = n->left;

	if((res = bfs_btree_grow(dir, &right_off)) < 0) {
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

		if((res = bfs_btree_write_node(dir, right_off, right_pairs,
				right_count, right_overflow,
				is_leaf ? old_right : BFS_BTREE_NULL,
				is_leaf ? node_off : BFS_BTREE_NULL)) < 0) {
			brelse(buf);
			return res;
		}
		if(is_leaf && (res = bfs_btree_relink_left(dir, old_right,
				right_off)) < 0) {
			brelse(buf);
			return res;
		}
		/* the left half stays in the old root block */
		bfs_btree_serialize(n, pairs, split_at, left_overflow,
				    is_leaf ? right_off : BFS_BTREE_NULL,
				    is_leaf ? old_left : BFS_BTREE_NULL);
		bfs_log_write_block(dir->sb, buf->block, buf);

		/* a new root block: one separator, two children */
		brelse(buf);
		if((res = bfs_btree_grow(dir, &root_off)) < 0) {
			return res;
		}
		if(!(buf = bfs_btree_read_node(dir, root_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		memset_b(n, 0, BFS_BLOCK_SIZE);
		n->left = BFS_BTREE_NULL;
		n->right = BFS_BTREE_NULL;
		n->overflow = right_off;
		n->all_key_count = 1;
		n->all_key_length = sep_len;
		{
			char *keys = (char *)n + sizeof(struct bfs_btree_node);
			__u16 *kl = bfs_btree_keylen_index(n);
			__u64 *values = bfs_btree_values(n);
			memcpy_b(keys, sep, sep_len);
			kl[0] = sep_len;
			values[0] = node_off;
		}
		bfs_log_write_block(dir->sb, buf->block, buf);

		header->root_node_ptr = root_off;
		if(header->max_depth < depth + 1) {
			header->max_depth = depth + 1;
		}
		bfs_btree_write_header(dir, header);
		return 0;
	}

	/* depth >= 2: the node keeps the lower half in place; the upper
	 * half goes to a new block; insert (sep, right_off) into the
	 * parent interior */
	if((res = bfs_btree_write_node(dir, right_off, right_pairs,
			right_count, right_overflow,
			is_leaf ? old_right : BFS_BTREE_NULL,
			is_leaf ? node_off : BFS_BTREE_NULL)) < 0) {
		brelse(buf);
		return res;
	}
	if(is_leaf && (res = bfs_btree_relink_left(dir, old_right,
			right_off)) < 0) {
		brelse(buf);
		return res;
	}
	bfs_btree_serialize(n, pairs, split_at, left_overflow,
			    is_leaf ? right_off : BFS_BTREE_NULL,
			    is_leaf ? old_left : BFS_BTREE_NULL);
	bfs_log_write_block(dir->sb, buf->block, buf);

	/* insert the separator into the parent, splitting the parent
	 * (recursively) if the rebuilt parent would not fit */
	{
		struct bfs_btree_node *parent;
		struct buffer *parent_buf;
		struct bfs_btree_pair *pp;
		char *pkb;
		__u64 parent_overflow;
		int pcount, i, j = -1;

		if(!(parent_buf = bfs_btree_read_node(dir, path[npath - 2]))) {
			return -EIO;
		}
		parent = (struct bfs_btree_node *)parent_buf->data;
		parent_overflow = parent->overflow;

		if(!(pp = (struct bfs_btree_pair *)kmalloc(
				128 * sizeof(struct bfs_btree_pair)))
				|| !(pkb = (char *)kmalloc(2 * BFS_BLOCK_SIZE))) {
			if(pp) {
				kfree((addr_t)pp);
			}
			if(pkb) {
				kfree((addr_t)pkb);
			}
			brelse(parent_buf);
			return -ENOMEM;
		}

		pcount = bfs_btree_collect_t(parent, pp, 128, 0, NULL, 0, 0,
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
			if(((sizeof(struct bfs_btree_node) + klen_total + 7) & ~7)
					+ pcount * 2 + (pcount + 1) * 8
					<= BFS_BLOCK_SIZE) {
				/* the parent has room: rebuild it in place */
				bfs_btree_serialize(parent, pp, pcount,
						    parent_overflow,
						    BFS_BTREE_NULL,
						    BFS_BTREE_NULL);
				bfs_log_write_block(dir->sb, parent_buf->block,
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
		res = bfs_btree_split(dir, header, path, npath - 1, pp, pcount,
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
static int bfs_btree_insert_impl_t(struct inode *dir, const char *key,
				   int keylen, __u64 value, int dtype,
				   int allow_dups)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	struct bfs_btree_pair *pairs;
	__u64 path[16];
	int npath, count, res, index;

	if(!keylen) {
		return -EINVAL;
	}
	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}
	if((res = bfs_btree_descend_t(dir, &header, key, keylen,
				      path, &npath))) {
		return res;
	}

	if(!(buf = bfs_btree_read_node(dir, path[npath - 1]))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;

	if(allow_dups &&
	   bfs_btree_search_node_t(n, key, keylen, &index, dtype) == 0) {
		/* the key already exists: the new value joins the
		 * duplicate chain (Haiku's _InsertDuplicate) */
		res = bfs_btree_insert_dup(dir, buf, index, value);
		if(res > 0) {
			/* the leaf's value link changed: write it */
			bfs_log_write_block(dir->sb, buf->block, buf);
			return 0;
		}
		/* the leaf is untouched: release it */
		brelse(buf);
		return (res < 0) ? res : 0;
	}

	if(bfs_btree_node_room(n, keylen)) {
		/* the leaf has room: rebuild it in place */
		static struct bfs_btree_pair spairs[128];
		static char skb[BFS_BLOCK_SIZE];
		pairs = spairs;
		count = bfs_btree_collect_t(n, pairs, 128, 1, key, keylen,
					    value, dtype, skb);
		if(count < 0) {
			brelse(buf);
			return -EEXIST;
		}
		bfs_btree_serialize(n, pairs, count, BFS_BTREE_NULL, n->right,
				    n->left);
		bfs_log_write_block(dir->sb, buf->block, buf);
		return 0;
	}

	/* the leaf is full: collect everything + the new pair, then split */
	{
		static struct bfs_btree_pair spairs[128];
		static char skb[BFS_BLOCK_SIZE];
		pairs = spairs;
		count = bfs_btree_collect_t(n, pairs, 128, 1, key, keylen,
					    value, dtype, skb);
		if(count < 0) {
			brelse(buf);
			return -EEXIST;
		}
		/* release the leaf buffer BEFORE the split (the split re-reads
		 * the same node; holding it would deadlock the buffer cache) */
		brelse(buf);
		buf = NULL;
		res = bfs_btree_split(dir, &header, path, npath, pairs, count,
				      count / 2, BFS_BTREE_NULL, npath,
				      header.data_type);
		return res;
	}
}

static int bfs_btree_insert_impl(struct inode *dir, const char *name,
				 __ino_t ino)
{
	return bfs_btree_insert_impl_t(dir, name, strlen(name), ino,
				       BFS_BTREE_STRING_TYPE, 0);
}

/* remove the entry at index 'i' from the leaf in 'buf' */
static void bfs_btree_remove_at(struct bfs_btree_node *n, int i)
{
	__u16 *kl = bfs_btree_keylen_index(n);
	__u64 *values = bfs_btree_values(n);
	char *keys = (char *)n + sizeof(struct bfs_btree_node);
	int old_count = n->all_key_count;
	int start = i ? kl[i - 1] : 0;
	int key_end = kl[i];
	int klen = key_end - start;
	int tail = n->all_key_length - key_end;
	int old_align = (sizeof(struct bfs_btree_node)
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
	new_align = (sizeof(struct bfs_btree_node)
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

static __u64 bfs_dup_make_link(int type, __u64 off, int frag)
{
	return ((__u64)type << 62) | (off & 0x3ffffffffffffc00ULL)
		| (frag & 0x3ff);
}

static int bfs_dup_link_type(__u64 v)
{
	return (int)(v >> 62);
}

static __u64 bfs_dup_link_off(__u64 v)
{
	return v & 0x3ffffffffffffc00ULL;
}

static int bfs_dup_link_frag(__u64 v)
{
	return (int)(v & 0x3ff);
}

/* the duplicate_array at slot 's' of the fragment/duplicate node 'nb' */
static __u64 *bfs_dup_array(struct buffer *nb, int slot)
{
	return (__u64 *)(nb->data
		+ slot * 8 * (BFS_BTREE_NUM_FRAGMENT_VALUES + 1));
}

/* the {count, values[125]} array of a duplicate node (offset 16, the
 * overflow field) — byte cast to dodge the packed-member warning */
static __u64 *bfs_dup_node_array(struct bfs_btree_node *dn)
{
	return (__u64 *)((char *)dn + 16);
}

/* number of non-empty fragment slots in a fragment node */
static int bfs_dup_fragments_used(struct buffer *nb)
{
	int s, used = 0;

	for(s = 0; s < BFS_BTREE_MAX_FRAGMENTS; s++) {
		if(bfs_dup_array(nb, s)[0] != 0) {
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
static int bfs_dup_find_fragment(struct inode *dir, struct bfs_btree_node *n,
				 struct buffer **nb, int *slot, __u64 *off)
{
	__u64 *values = bfs_btree_values(n);
	int i, s, res;

	for(i = 0; i < n->all_key_count; i++) {
		if(bfs_dup_link_type(values[i]) != BFS_BTREE_DUPLICATE_FRAGMENT) {
			continue;
		}
		if(!(*nb = bfs_btree_read_node(dir, bfs_dup_link_off(values[i])))) {
			continue;
		}
		*off = bfs_dup_link_off(values[i]);
		for(s = 0; s < BFS_BTREE_MAX_FRAGMENTS; s++) {
			if(bfs_dup_array(*nb, s)[0] == 0) {
				*slot = s;
				return 0;
			}
		}
		brelse(*nb);
		*nb = NULL;
	}

	/* no free slot anywhere: allocate a fresh fragment node */
	if((res = bfs_btree_grow(dir, off)) < 0) {
		return res;
	}
	if(!(*nb = bfs_btree_read_node(dir, *off))) {
		return -EIO;
	}
	memset_b((*nb)->data, 0, BFS_BLOCK_SIZE);
	*slot = 0;
	return 0;
}

/* the first duplicate: promote the direct value into a fragment [old, v] */
static int bfs_btree_dup_promote(struct inode *dir, struct buffer *buf,
				 int index, __u64 old, __u64 value)
{
	struct bfs_btree_node *n = (struct bfs_btree_node *)buf->data;
	__u64 *values = bfs_btree_values(n);
	struct buffer *nb = NULL;
	__u64 noff;
	__u64 *arr;
	int slot, res;

	if((res = bfs_dup_find_fragment(dir, n, &nb, &slot, &noff)) < 0) {
		return res;
	}
	arr = bfs_dup_array(nb, slot);
	arr[0] = 2;
	arr[1] = old;
	arr[2] = value;
	bfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */

	values[index] = bfs_dup_make_link(BFS_BTREE_DUPLICATE_FRAGMENT,
					  noff, slot);
	return 1;	/* the leaf's value link changed */
}

/* append to a fragment; promote to a duplicate node when the fragment fills */
static int bfs_btree_dup_fragment_add(struct inode *dir, struct buffer *buf,
				      int index, __u64 value)
{
	struct bfs_btree_node *n = (struct bfs_btree_node *)buf->data;
	__u64 *values = bfs_btree_values(n);
	struct bfs_btree_node *dn;
	struct buffer *nb, *ndb;
	__u64 noff, nd;
	__u64 *arr;
	int frag, res;

	noff = bfs_dup_link_off(values[index]);
	frag = bfs_dup_link_frag(values[index]);
	if(!(nb = bfs_btree_read_node(dir, noff))) {
		return -EIO;
	}
	arr = bfs_dup_array(nb, frag);
	if(arr[0] > BFS_BTREE_NUM_FRAGMENT_VALUES) {
		/* a garbage count (an unread/corrupt buffer): the slot
		 * cannot hold that many values — rebuild it from empty */
		arr[0] = 0;
	}
	if(arr[0] < BFS_BTREE_NUM_FRAGMENT_VALUES) {
		arr[arr[0] + 1] = value;
		arr[0]++;
		bfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
		return 0;	/* the leaf's link is unchanged */
	}

	if(bfs_dup_fragments_used(nb) < 2) {
		/* only this array: convert the node into a duplicate
		 * node in place (array at &overflow_link, left/right
		 * links cleared) */
		dn = (struct bfs_btree_node *)nb->data;
		/* the fragment slot (offset 0) and the dup-node array (offset
		 * 16) OVERLAP in the same block: memmove, not memcpy, or the
		 * copy clobbers its own source */
		memmove(bfs_dup_node_array(dn), arr, 64);
		dn->left = BFS_BTREE_NULL;
		dn->right = BFS_BTREE_NULL;
		arr = bfs_dup_node_array(dn);
		arr[arr[0] + 1] = value;
		arr[0]++;
		bfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
		values[index] = bfs_dup_make_link(BFS_BTREE_DUPLICATE_NODE,
						  noff, 0);
		return 1;	/* the leaf's value link changed */
	}

	/* allocate a new duplicate node, copy the array + the value */
		if((res = bfs_btree_grow(dir, &nd)) < 0) {
		brelse(nb);
		return res;
	}
		if(!(ndb = bfs_btree_read_node(dir, nd))) {
		brelse(nb);
		return -EIO;
	}
	memset_b(ndb->data, 0, BFS_BLOCK_SIZE);
	dn = (struct bfs_btree_node *)ndb->data;
	dn->left = BFS_BTREE_NULL;
	dn->right = BFS_BTREE_NULL;
	memcpy_b(bfs_dup_node_array(dn), arr, 64);
	arr = bfs_dup_node_array(dn);
	arr[arr[0] + 1] = value;
	arr[0]++;
	bfs_log_write_block(dir->sb, ndb->block, ndb);	/* consumes ndb */

	/* free the fragment slot */
	arr = bfs_dup_array(nb, frag);
	memset_b(arr, 0, 64);
	bfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */

	values[index] = bfs_dup_make_link(BFS_BTREE_DUPLICATE_NODE, nd, 0);
	return 1;	/* the leaf's value link changed */
}

/* append to a duplicate-node chain, allocating a new node when full */
static int bfs_btree_dup_node_add(struct inode *dir, struct buffer *buf,
				  int index, __u64 value)
{
	struct bfs_btree_node *n = (struct bfs_btree_node *)buf->data;
	__u64 *values = bfs_btree_values(n);
	struct bfs_btree_node *dn;
	struct buffer *nb, *ndb;
	__u64 noff, nd;
	__u64 *arr;
	int res;

	noff = bfs_dup_link_off(values[index]);
	if(!(nb = bfs_btree_read_node(dir, noff))) {
		return -EIO;
	}
	for(;;) {
		dn = (struct bfs_btree_node *)nb->data;
		arr = bfs_dup_node_array(dn);
		if(arr[0] < BFS_BTREE_NUM_DUPLICATE_VALUES) {
			arr[arr[0] + 1] = value;
			arr[0]++;
			bfs_log_write_block(dir->sb, nb->block, nb);
			return 0;	/* log_write consumed nb */
		}
		if(dn->right != BFS_BTREE_NULL) {
			noff = dn->right;
			brelse(nb);
			if(!(nb = bfs_btree_read_node(dir, noff))) {
				return -EIO;
			}
			continue;
		}
		/* full: allocate a new node and chain it */
		if((res = bfs_btree_grow(dir, &nd)) < 0) {
			brelse(nb);
			return res;
		}
		if(!(ndb = bfs_btree_read_node(dir, nd))) {
			brelse(nb);
			return -EIO;
		}
		memset_b(ndb->data, 0, BFS_BLOCK_SIZE);
		dn = (struct bfs_btree_node *)ndb->data;
		dn->left = noff;
		dn->right = BFS_BTREE_NULL;
		arr = bfs_dup_node_array(dn);
		arr[0] = 1;
		arr[1] = value;
		bfs_log_write_block(dir->sb, ndb->block, ndb);	/* consumes ndb */
		dn = (struct bfs_btree_node *)nb->data;
		dn->right = nd;
		bfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
		return 0;	/* the leaf's value link is unchanged */
	}
}

/* insert 'value' for the existing key at values[index] */
/* returns > 0 when the leaf's value link changed (the caller must write
 * the leaf), 0 when it did not (the caller must brelse the leaf) */
static int bfs_btree_insert_dup(struct inode *dir, struct buffer *buf,
				int index, __u64 value)
{
	struct bfs_btree_node *n = (struct bfs_btree_node *)buf->data;
	__u64 *values = bfs_btree_values(n);
	__u64 old = values[index];

	if(bfs_dup_link_type(old) == 0) {
		return bfs_btree_dup_promote(dir, buf, index, old, value);
	}
	if(bfs_dup_link_type(old) == BFS_BTREE_DUPLICATE_FRAGMENT) {
		return bfs_btree_dup_fragment_add(dir, buf, index, value);
	}
	return bfs_btree_dup_node_add(dir, buf, index, value);
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
static void bfs_dup_free_node(struct inode *dir, __u64 off)
{
	(void)dir;
	(void)off;
}

/*
 * Remove 'value' from the duplicate chain of the key at values[index].
 * Demotes the value slot back to a direct value when one remains, and
 * frees empty fragment/duplicate nodes (Haiku's _RemoveDuplicate).
 */
static int bfs_btree_remove_dup(struct inode *dir, struct buffer *buf,
				int index, __u64 value)
{
	struct bfs_btree_node *n = (struct bfs_btree_node *)buf->data;
	__u64 *values = bfs_btree_values(n);
	__u64 old = values[index];
	struct bfs_btree_node *dn;
	struct buffer *nb;
	__u64 noff;
	__u64 *arr;
	int frag, i, cnt, res = -ENOENT;

	if(bfs_dup_link_type(old) == BFS_BTREE_DUPLICATE_FRAGMENT) {
		noff = bfs_dup_link_off(old);
		frag = bfs_dup_link_frag(old);
				if(!(nb = bfs_btree_read_node(dir, noff))) {
			return -EIO;
		}
		arr = bfs_dup_array(nb, frag);
		cnt = arr[0];
				for(i = 1; i <= cnt; i++) {
			if(arr[i] == value) {
				res = 0;
				break;
			}
		}
		if(res) {
			brelse(nb);
			return -ENOENT;
		}
		for(; i < cnt; i++) {
			arr[i] = arr[i + 1];
		}
		cnt--;
		arr[0] = cnt;	/* the slot's values (0 = empty) */
		/* the node stays allocated (see bfs_dup_free_node) */
		bfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */
		if(cnt == 1) {
			/* demote: the remaining value becomes direct */
			values[index] = arr[1];
			return 1;	/* the leaf's value link changed */
		}
		if(cnt == 0) {
			/* the key's last value is gone: remove the key too */
			bfs_btree_remove_at(n, index);
			return 1;	/* the leaf's value link changed */
		}
		return 0;
	}

	/* a duplicate-node chain: walk it, remove, then clean up */
	if(bfs_dup_link_type(old) != BFS_BTREE_DUPLICATE_NODE) {
		return -ENOENT;
	}
	noff = bfs_dup_link_off(old);
	if(!(nb = bfs_btree_read_node(dir, noff))) {
		return -EIO;
	}
	for(;;) {
		dn = (struct bfs_btree_node *)nb->data;
		arr = bfs_dup_node_array(dn);
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
		if(dn->right == BFS_BTREE_NULL) {
			brelse(nb);
			return -ENOENT;
		}
		noff = dn->right;
		brelse(nb);
		if(!(nb = bfs_btree_read_node(dir, noff))) {
			return -EIO;
		}
	}
	/* remove the value from this node */
	for(; i < cnt; i++) {
		arr[i] = arr[i + 1];
	}
	cnt--;
	arr[0] = cnt;
	bfs_log_write_block(dir->sb, nb->block, nb);	/* consumes nb */

	/* clean up empty nodes and demote a lone remaining value. The
	 * chain head stays at values[index] unless the whole chain drops
	 * to one value, in which case it demotes to a direct value and
	 * every chain node is freed. */
	{
		__u64 chain = bfs_dup_link_off(old);
		__u64 total = 0;
		__u64 last = 0;

		for(noff = chain; noff != BFS_BTREE_NULL;) {
			struct buffer *next;
			__u64 nnext;

			if(!(nb = bfs_btree_read_node(dir, noff))) {
				return -EIO;
			}
			dn = (struct bfs_btree_node *)nb->data;
			arr = bfs_dup_node_array(dn);
			total += arr[0];
			if(arr[0] == 1) {
				last = arr[1];
			}
			nnext = dn->right;
			brelse(nb);
			noff = nnext;
		}
		if(total == 1) {
			values[index] = last;
			res = 1;	/* the leaf's value link changed */
		} else if(total == 0) {
			bfs_btree_remove_at(n, index);
			res = 1;
		}
		if(total <= 1) {
			for(noff = chain; noff != BFS_BTREE_NULL;) {
				__u64 nnext;

				if(!(nb = bfs_btree_read_node(dir, noff))) {
					break;
				}
				nnext = ((struct bfs_btree_node *)nb->data)
					->right;
				brelse(nb);
				bfs_dup_free_node(dir, noff);
				noff = nnext;
			}
		}
	}
	return res;
}

static int bfs_btree_delete_impl(struct inode *dir, const char *name)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 path[16];
	int npath, index, res;

	if(!name[0]) {
		return -EINVAL;
	}
	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}
	if((res = bfs_btree_descend(dir, &header, name, path, &npath))) {
		return res;
	}

	if(!(buf = bfs_btree_read_node(dir, path[npath - 1]))) {
		return -EIO;
	}
	{
		int dtype = header.data_type;

	n = (struct bfs_btree_node *)buf->data;
	if(bfs_btree_search_node_t(n, name, strlen(name), &index, dtype)) {
		brelse(buf);
		return -ENOENT;
	}
	bfs_btree_remove_at(n, index);
	dir->state |= INODE_DIRTY;
	bfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
	}
}

/*
 * Remove the entry whose inode matches 'ino' (used by rmdir, which has no
 * name). Searches every leaf.
 */
static int bfs_btree_delete_ino_impl(struct inode *dir, __ino_t ino)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int i, res;

	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}

	node_off = header.root_node_ptr;
	for(;;) {
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->overflow != BFS_BTREE_NULL) {
			node_off = n->all_key_count ?
				bfs_btree_values(n)[0] : n->overflow;
			brelse(buf);
			continue;
		}
		brelse(buf);
		break;
	}

	for(;;) {
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->overflow != BFS_BTREE_NULL) {
			brelse(buf);
			return -EIO;
		}
		for(i = 0; i < n->all_key_count; i++) {
			if(bfs_btree_values(n)[i] == ino) {
				bfs_btree_remove_at(n, i);
				dir->state |= INODE_DIRTY;
				bfs_log_write_block(dir->sb, buf->block, buf);
				return 0;
			}
		}
		if(n->right == BFS_BTREE_NULL) {
			brelse(buf);
			return -ENOENT;
		}
		node_off = n->right;
		brelse(buf);
	}
}

/*
 * Transaction wrappers: every directory-tree mutation (insert/delete)
 * runs in a single journal transaction so a crash can never leave a
 * partially-applied tree change.
 */
/*
 * The typed index-tree API (values may repeat; the duplicate machinery
 * stores extra values in fragments / duplicate nodes).
 */

int bfs_btree_insert_value(struct inode *dir, const char *key, int keylen,
			   int dtype, __u64 value)
{
	int res;

	bfs_log_begin(dir->sb);
	res = bfs_btree_insert_impl_t(dir, key, keylen, value, dtype, 1);
	bfs_log_commit(dir->sb);
	return res;
}

int bfs_btree_find_value(struct inode *dir, const char *key, int keylen,
			 int dtype, __u64 *value)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 path[16];
	int npath, index, res;

	if(!keylen) {
		return -ENOENT;
	}
	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}
	if((res = bfs_btree_descend_t(dir, &header, key, keylen,
				      path, &npath))) {
		return res;
	}
	if(!(buf = bfs_btree_read_node(dir, path[npath - 1]))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	if(n->all_key_count == 0 || n->all_key_length == 0) {
		brelse(buf);
		return -ENOENT;
	}
	if(bfs_btree_search_node_t(n, key, keylen, &index,
				   header.data_type) == 0) {
		*value = bfs_btree_values(n)[index];
		brelse(buf);
		return 0;
	}
	brelse(buf);
	return -ENOENT;
}

int bfs_btree_delete_value(struct inode *dir, const char *key, int keylen,
			   int dtype, __u64 value)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 path[16];
	int npath, index, res;
	int begun = 0;

	if(!keylen) {
		return -ENOENT;
	}
	bfs_log_begin(dir->sb);
	begun = 1;
	if((res = bfs_btree_read_header(dir, &header))) {
		goto out;
	}
	if((res = bfs_btree_descend_t(dir, &header, key, keylen,
				      path, &npath))) {
		goto out;
	}
	if(!(buf = bfs_btree_read_node(dir, path[npath - 1]))) {
		res = -EIO;
		goto out;
	}
	n = (struct bfs_btree_node *)buf->data;
	if(bfs_btree_search_node_t(n, key, keylen, &index,
				   header.data_type)) {
		brelse(buf);
		res = -ENOENT;
		goto out;
	}
	if(bfs_dup_link_type(bfs_btree_values(n)[index]) == 0) {
		/* a direct value: remove the whole key */
		if(bfs_btree_values(n)[index] != value) {
			brelse(buf);
			res = -ENOENT;
			goto out;
		}
		bfs_btree_remove_at(n, index);
		dir->state |= INODE_DIRTY;
		bfs_log_write_block(dir->sb, buf->block, buf);	/* consumes buf */
		res = 0;
		goto out;
	}
	/* a duplicate chain: remove just this value */
	res = bfs_btree_remove_dup(dir, buf, index, value);
	if(res > 0) {
		/* the leaf's value link changed: write it */
		bfs_log_write_block(dir->sb, buf->block, buf);
		res = 0;
	} else if(res == 0) {
		brelse(buf);
	} else {
		brelse(buf);
	}
out:
	if(begun) {
		bfs_log_commit(dir->sb);
	}
	return res;
}

int bfs_btree_insert(struct inode *dir, const char *name, __ino_t ino)
{
	int res;

	bfs_log_begin(dir->sb);
	res = bfs_btree_insert_impl(dir, name, ino);
	bfs_log_commit(dir->sb);
	return res;
}

int bfs_btree_delete(struct inode *dir, const char *name)
{
	int res;

	bfs_log_begin(dir->sb);
	res = bfs_btree_delete_impl(dir, name);
	bfs_log_commit(dir->sb);
	return res;
}

int bfs_btree_delete_ino(struct inode *dir, __ino_t ino)
{
	int res;

	bfs_log_begin(dir->sb);
	res = bfs_btree_delete_ino_impl(dir, ino);
	bfs_log_commit(dir->sb);
	return res;
}
