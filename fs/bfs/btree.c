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

static int bfs_btree_descend(struct inode *, const struct bfs_btree_header *,
			     const char *, __u64 *, int *);
static int bfs_btree_collect(struct bfs_btree_node *, struct bfs_btree_pair *,
			     int, int, const char *, int, __u64, char *);
static int bfs_btree_serialize(struct bfs_btree_node *, struct bfs_btree_pair *,
			       int, __u64, __u64);
static int bfs_btree_split(struct inode *, struct bfs_btree_header *,
			   __u64 *, int, struct bfs_btree_pair *, int, int,
			   __u64, int);

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

/* update the tree header on disk (max_depth changes on split) */
static int bfs_btree_write_header(struct inode *i, struct bfs_btree_header *header)
{
	struct buffer *buf;
	struct bfs_btree_header *h;

	if(!(buf = bfs_btree_read_node(i, 0))) {
		return -EIO;
	}
	h = (struct bfs_btree_header *)buf->data;
	memcpy_b(h, header, sizeof(struct bfs_btree_header));
	bfs_log_write_block(i->sb, buf->block, buf);
	return 0;
}

/*
 * Binary search for 'name' among the node's keys.
 * Returns 0 if found (with *index = the key index, *res = 0),
 * or -ENOENT with *index = the insertion point (first key > name).
 */
static int bfs_btree_search_node(struct bfs_btree_node *n, const char *name,
				 int *index)
{
	int lo, hi, mid, cmp, keylen;
	char *key;

	lo = 0;
	hi = n->all_key_count - 1;
	while(lo <= hi) {
		mid = (lo + hi) >> 1;
		key = bfs_btree_key(n, mid, &keylen);
		cmp = strncmp(name, key, keylen);
		if(cmp == 0) {
			if(name[keylen]) {
				cmp = 1;	/* name longer than key */
			} else {
				*index = mid;
				return 0;
			}
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
	if((res = bfs_btree_descend(dir, &header, name, path, &npath))) {
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
	if(bfs_btree_search_node(n, name, &index) == 0) {
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
 */
static void bfs_btree_pick_child(struct bfs_btree_node *n, const char *name,
				 __u64 *child)
{
	int i, keylen;
	char *key;
	__u64 *values = bfs_btree_values(n);

	if(n->all_key_count == 0) {
		*child = n->overflow;
		return;
	}

	key = bfs_btree_key(n, n->all_key_count - 1, &keylen);
	if(strncmp(name, key, keylen) > 0) {
		*child = (n->overflow != BFS_BTREE_NULL) ?
			n->overflow : values[n->all_key_count - 1];
		return;
	}

	for(i = 0; i < n->all_key_count && i < 128; i++) {
		key = bfs_btree_key(n, i, &keylen);
		if(strncmp(name, key, keylen) <= 0) {
			*child = values[i];
			return;
		}
	}
	*child = values[n->all_key_count - 1];
}

static int bfs_btree_descend(struct inode *dir, const struct bfs_btree_header *h,
			     const char *name, __u64 *path, int *npath)
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
		bfs_btree_pick_child(n, name, &node_off);
		brelse(buf);
	}
}

/* collect a node's pairs, copying the keys into 'keybuf' (the node buffer
 * may be released before the pairs are used); optionally insert one pair */
static int bfs_btree_collect(struct bfs_btree_node *n, struct bfs_btree_pair *pairs,
			     int max, int do_insert, const char *iname,
			     int iname_len, __u64 ino, char *keybuf)
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
		int cmp = strncmp(iname, pairs[i].key, pairs[i].keylen);
		if(cmp == 0 && !iname[pairs[i].keylen]) {
			/* exact duplicate: the name is already in the tree */
			return -1;
		}
		if(cmp < 0 || (cmp == 0 && iname[pairs[i].keylen])) {
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
			       __u64 interior_overflow, __u64 right)
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
	n->left = BFS_BTREE_NULL;
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
				__u64 overflow, __u64 right)
{
	struct bfs_btree_node *n;
	struct buffer *buf;

	if(!(buf = bfs_btree_read_node(dir, off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	bfs_btree_serialize(n, pairs, count, overflow, right);
	bfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
}

static int bfs_btree_grow(struct inode *dir, __u64 *off)
{
	__blk_t block;

	if((block = bmap(dir, dir->i_size, FOR_WRITING)) < 0) {
		return block;
	}
	*off = dir->i_size;
	dir->i_size += BFS_BLOCK_SIZE;
	dir->u.bfs.raw.u.data.size = dir->i_size;
	dir->state |= INODE_DIRTY;
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
			   int depth)
{
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off = path[npath - 1];
	__u64 old_right, left_overflow, right_overflow;
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

	if((res = bfs_btree_grow(dir, &right_off)) < 0) {
		brelse(buf);
		return res;
	}

	if(npath == 1) {
		/* the split node is the root: both halves move to fresh
		 * blocks and the root block becomes a new interior node
		 * with one separator and two children */
		if((res = bfs_btree_grow(dir, &left_off)) < 0) {
			brelse(buf);
			return res;
		}
		if((res = bfs_btree_write_node(dir, left_off, pairs, split_at,
				left_overflow, right_off)) < 0) {
			brelse(buf);
			return res;
		}
		if((res = bfs_btree_write_node(dir, right_off, right_pairs,
				right_count, right_overflow,
				is_leaf ? old_right : BFS_BTREE_NULL)) < 0) {
			brelse(buf);
			return res;
		}
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
			values[0] = left_off;
		}
		bfs_log_write_block(dir->sb, buf->block, buf);

		if(header->max_depth < depth + 1) {
			header->max_depth = depth + 1;
			bfs_btree_write_header(dir, header);
		}
		return 0;
	}

	/* depth >= 2: the node keeps the lower half in place; the upper
	 * half goes to a new block; insert (sep, right_off) into the
	 * parent interior */
	if((res = bfs_btree_write_node(dir, right_off, right_pairs,
			right_count, right_overflow,
			is_leaf ? old_right : BFS_BTREE_NULL)) < 0) {
		brelse(buf);
		return res;
	}
	bfs_btree_serialize(n, pairs, split_at, left_overflow,
			    is_leaf ? right_off : BFS_BTREE_NULL);
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

		pcount = bfs_btree_collect(parent, pp, 128, 0, NULL, 0, 0, pkb);
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
				      pcount / 2, parent_overflow, depth);
		kfree((addr_t)pkb);
		kfree((addr_t)pp);
		return res;
	}
}


/*
 * Insert a (name, inode) pair into the directory tree. Splits a full
 * leaf, and splits the interior parents recursively when they fill.
 */
static int bfs_btree_insert_impl(struct inode *dir, const char *name,
				 __ino_t ino)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	struct bfs_btree_pair *pairs;
	__u64 path[16];
	int npath, count, res, name_len;

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
	n = (struct bfs_btree_node *)buf->data;
	name_len = strlen(name);

	if(bfs_btree_node_room(n, name_len)) {
		/* the leaf has room: rebuild it in place */
		static struct bfs_btree_pair spairs[128];
		static char skb[BFS_BLOCK_SIZE];
		pairs = spairs;
		count = bfs_btree_collect(n, pairs, 128, 1, name, name_len, ino, skb);
		if(count < 0) {
			brelse(buf);
			return -EEXIST;
		}
		bfs_btree_serialize(n, pairs, count, BFS_BTREE_NULL, n->right);
		bfs_log_write_block(dir->sb, buf->block, buf);
		return 0;
	}

	/* the leaf is full: collect everything + the new pair, then split */
	{
		static struct bfs_btree_pair spairs[128];
		static char skb[BFS_BLOCK_SIZE];
		pairs = spairs;
		count = bfs_btree_collect(n, pairs, 128, 1, name, name_len, ino, skb);
		if(count < 0) {
			brelse(buf);
			return -EEXIST;
		}
		/* release the leaf buffer BEFORE the split (the split re-reads
		 * the same node; holding it would deadlock the buffer cache) */
		brelse(buf);
		buf = NULL;
		res = bfs_btree_split(dir, &header, path, npath, pairs, count,
				      count / 2, BFS_BTREE_NULL, npath);
		return res;
	}
}

/* remove the entry at index 'i' from the leaf in 'buf' */
static void bfs_btree_remove_at(struct bfs_btree_node *n, int i)
{
	__u16 *kl = bfs_btree_keylen_index(n);
	__u64 *values = bfs_btree_values(n);
	char *keys = (char *)n + sizeof(struct bfs_btree_node);
	int start = i ? kl[i - 1] : 0;
	int key_end = kl[i];
	int klen = key_end - start;
	int tail = n->all_key_length - key_end;
	int j;

	memmove(keys + start, keys + key_end, tail);
	for(j = i; j < n->all_key_count - 1; j++) {
		kl[j] = kl[j + 1] - klen;
	}
	memmove(values + i, values + i + 1,
		(n->all_key_count - i - 1) * sizeof(__u64));
	n->all_key_count--;
	n->all_key_length -= klen;
}

/*
 * Remove a (name, inode) pair from the directory tree.
 */
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
	n = (struct bfs_btree_node *)buf->data;
	if(bfs_btree_search_node(n, name, &index)) {
		brelse(buf);
		return -ENOENT;
	}
	bfs_btree_remove_at(n, index);
	dir->state |= INODE_DIRTY;
	bfs_log_write_block(dir->sb, buf->block, buf);
	return 0;
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
