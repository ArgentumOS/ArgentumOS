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
			   __u64 *, int, struct bfs_btree_pair *, int, int);

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
	bwrite(buf);
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

static int bfs_btree_write_leaf(struct inode *dir, __u64 off,
				struct bfs_btree_pair *pairs, int count,
				__u64 right)
{
	struct bfs_btree_node *n;
	struct buffer *buf;

	if(!(buf = bfs_btree_read_node(dir, off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	bfs_btree_serialize(n, pairs, count, BFS_BTREE_NULL, right);
	bwrite(buf);
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

static int bfs_btree_split(struct inode *dir, struct bfs_btree_header *header,
			   __u64 *path, int npath, struct bfs_btree_pair *pairs,
			   int count, int split_at)
{
	struct bfs_btree_node *n, *parent;
	struct buffer *buf, *parent_buf;
	__u64 leaf_off = path[npath - 1];
	__u64 left_off, right_off;
	__u64 old_right;
	const char *sep;
	int sep_len;
	int res;

	if(split_at < 1 || split_at >= count) {
		return -EIO;
	}
	sep = pairs[split_at - 1].key;
	sep_len = pairs[split_at - 1].keylen;

	if(!(buf = bfs_btree_read_node(dir, leaf_off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	old_right = n->right;

	if(npath == 1) {
		/* the leaf is the root: the root block becomes an interior
		 * node; BOTH halves move to fresh blocks */
		if((res = bfs_btree_grow(dir, &left_off)) < 0) {
			brelse(buf);
			return res;
		}
		if((res = bfs_btree_grow(dir, &right_off)) < 0) {
			brelse(buf);
			return res;
		}
		if((res = bfs_btree_write_leaf(dir, left_off, pairs, split_at,
					       right_off)) < 0) {
			brelse(buf);
			return res;
		}
		if((res = bfs_btree_write_leaf(dir, right_off, pairs + split_at,
					       count - split_at,
					       old_right)) < 0) {
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
		bwrite(buf);

		if(header->max_depth < 2) {
			header->max_depth = 2;
			bfs_btree_write_header(dir, header);
		}
		return 0;
	}

	/* depth >= 2: the leaf keeps the lower half; the upper half goes to
	 * a new block; insert (sep, right_off) into the parent interior */
	if(!(parent_buf = bfs_btree_read_node(dir, path[npath - 2]))) {
		brelse(buf);
		return -EIO;
	}
	parent = (struct bfs_btree_node *)parent_buf->data;
	if(!bfs_btree_node_room(parent, sep_len)) {
		brelse(parent_buf);
		brelse(buf);
		return -ENOSPC;
	}

	if((res = bfs_btree_grow(dir, &right_off)) < 0) {
		brelse(parent_buf);
		brelse(buf);
		return res;
	}
	if((res = bfs_btree_write_leaf(dir, right_off, pairs + split_at,
				       count - split_at, old_right)) < 0) {
		brelse(parent_buf);
		brelse(buf);
		return res;
	}

	bfs_btree_serialize(n, pairs, split_at, BFS_BTREE_NULL, right_off);
	bwrite(buf);

	/* update the parent interior after the child split */
	{
		struct bfs_btree_pair *pp = (struct bfs_btree_pair *)kmalloc(
			128 * sizeof(struct bfs_btree_pair));
		char *pkb = (char *)kmalloc(BFS_BLOCK_SIZE);
		__u64 old_overflow = parent->overflow;
		__u64 new_overflow = old_overflow;
		int pcount, i, j = -1;

		if(!pp || !pkb) {
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
			if(pp[i].val == path[npath - 1]) {
				j = i;
				break;
			}
		}

		if(j < 0 && old_overflow != path[npath - 1]) {
			kfree((addr_t)pkb);
			kfree((addr_t)pp);
			brelse(parent_buf);
			return -EIO;
		}

		if(j >= 0) {
			int old_keylen = pp[j].keylen;
			int room, k;

			room = sizeof(struct bfs_btree_node) + sep_len
				+ old_keylen;
			for(k = 0; k < pcount; k++) {
				room += pp[k].keylen;
			}
			if(((room + 7) & ~7) + (pcount + 1) * 10
					> BFS_BLOCK_SIZE) {
				kfree((addr_t)pkb);
				kfree((addr_t)pp);
				brelse(parent_buf);
				return -ENOSPC;
			}
			for(k = pcount; k > j + 1; k--) {
				pp[k] = pp[k - 1];
			}
			pp[j + 1].key = pp[j].key;
			pp[j + 1].keylen = old_keylen;
			pp[j + 1].val = right_off;
			pp[j].key = pkb;
			memcpy_b((char *)pkb, sep, sep_len);
			pp[j].keylen = sep_len;
			pcount++;
		} else {
			int room, k, klen_total = 0;

			room = sizeof(struct bfs_btree_node) + sep_len;
			for(k = 0; k < pcount; k++) {
				room += pp[k].keylen;
				klen_total += pp[k].keylen;
			}
			if(((room + 7) & ~7) + (pcount + 1) * 10
					> BFS_BLOCK_SIZE) {
				kfree((addr_t)pkb);
				kfree((addr_t)pp);
				brelse(parent_buf);
				return -ENOSPC;
			}
			pp[pcount].key = pkb + klen_total;
			memcpy_b((char *)pkb + klen_total, sep, sep_len);
			pp[pcount].keylen = sep_len;
			pp[pcount].val = path[npath - 1];
			pcount++;
			new_overflow = right_off;
		}

		bfs_btree_serialize(parent, pp, pcount, new_overflow,
				    BFS_BTREE_NULL);
		kfree((addr_t)pkb);
		kfree((addr_t)pp);
	}
	bwrite(parent_buf);
	return 0;
}


/*
 * Insert a (name, inode) pair into the directory tree. Splits a full
 * leaf; -ENOSPC when the parent interior is full too.
 */
int bfs_btree_insert(struct inode *dir, const char *name, __ino_t ino)
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
		bwrite(buf);
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
				      count / 2);
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
int bfs_btree_delete(struct inode *dir, const char *name)
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
	bwrite(buf);
	return 0;
}

/*
 * Remove the entry whose inode matches 'ino' (used by rmdir, which has no
 * name). Searches every leaf.
 */
int bfs_btree_delete_ino(struct inode *dir, __ino_t ino)
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
				bwrite(buf);
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
