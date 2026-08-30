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
	__u64 node_off;
	int index, keylen, res;
	char *key;

	if(!name[0]) {
		return -ENOENT;
	}
	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}

	node_off = header.root_node_ptr;
	for(;;) {
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->all_key_count == 0 || n->all_key_length == 0) {
			brelse(buf);
			return -ENOENT;
		}

		if(n->overflow == BFS_BTREE_NULL) {
			/* leaf node */
			if(bfs_btree_search_node(n, name, &index) == 0) {
				*ino = (__ino_t)bfs_btree_values(n)[index];
				brelse(buf);
				return 0;
			}
			brelse(buf);
			return -ENOENT;
		}

		/* interior node: descend into the child at the insertion point */
		key = bfs_btree_key(n, 0, &keylen);
		if(strncmp(name, key, keylen) < 0) {
			node_off = bfs_btree_values(n)[0];
		} else {
			if(bfs_btree_search_node(n, name, &index) == 0) {
				node_off = bfs_btree_values(n)[index + 1];
			} else {
				node_off = bfs_btree_values(n)[index];
			}
		}
		brelse(buf);
	}
}

/*
 * Iterate over all directory entries (key, value) in key order.
 * 'fn' is called for each entry; if it returns non-zero the iteration
 * stops. Returns 0 on success, -EIO on error.
 */
int bfs_btree_iterate(struct inode *dir, int (*fn)(const char *, __ino_t, void *),
		      void *arg)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int index, keylen, res;
	char *key;

	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}

	/* walk 'left' links to the leftmost node, then iterate right */
	node_off = header.root_node_ptr;
	for(;;) {
		if(!(buf = bfs_btree_read_node(dir, node_off))) {
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf->data;
		if(n->left != BFS_BTREE_NULL) {
			node_off = n->left;
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
			/* interior node: skip (M1 images are depth-1) */
			brelse(buf);
			return -EIO;
		}
		for(index = 0; index < n->all_key_count; index++) {
			char keybuf[BFS_BTREE_MAX_KEY_LEN];
			key = bfs_btree_key(n, index, &keylen);
			if(keylen >= BFS_BTREE_MAX_KEY_LEN) {
				brelse(buf);
				return -EIO;
			}
			/* tree keys are not NUL-terminated: copy + terminate */
			memcpy_b(keybuf, key, keylen);
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
static int bfs_btree_rebuild(struct bfs_btree_node *n, int do_insert,
			     const char *iname, __ino_t ino,
			     const char *dname)
{
	struct pair {
		const char *key;
		int keylen;
		__u64 val;
	} *pairs;
	struct bfs_btree_node *copy;
	int count = 0, i, index, total = 0;

	if(!(copy = (struct bfs_btree_node *)kmalloc(BFS_BLOCK_SIZE)))
		return -ENOMEM;
	if(!(pairs = (struct pair *)kmalloc(64 * sizeof(struct pair)))) {
		kfree((addr_t)copy);
		return -ENOMEM;
	}
	memcpy_b(copy, n, BFS_BLOCK_SIZE);
	if(copy->all_key_count > 64) {
		kfree((addr_t)copy);
		kfree((addr_t)pairs);
		return -EIO;
	}

	/* collect existing pairs (keys point into the copy) */
	for(i = 0; i < copy->all_key_count; i++) {
		pairs[count].key = bfs_btree_key(copy, i, &pairs[count].keylen);
		pairs[count].val = bfs_btree_values(copy)[i];
		count++;
	}
	kfree((addr_t)copy);

	/* apply the change */
	index = count;
	if(do_insert) {
		for(i = 0; i < count; i++) {
			int cmp = strncmp(iname, pairs[i].key, pairs[i].keylen);
			if(cmp == 0 && !iname[pairs[i].keylen])
				cmp = 0;
			if(cmp <= 0) {
				index = i;
				break;
			}
		}
		if(index < count) {
			int same = (strncmp(iname, pairs[index].key,
					pairs[index].keylen) == 0)
				&& !iname[pairs[index].keylen];
			if(same) {
				kfree((addr_t)pairs);
				return -EEXIST;
			}
		}
		for(i = count; i > index; i--) {
			pairs[i] = pairs[i - 1];
		}
		pairs[index].key = iname;
		pairs[index].keylen = strlen(iname);
		pairs[index].val = ino;
		count++;
	} else {
		for(i = 0; i < count; i++) {
			int cmp = strncmp(dname, pairs[i].key, pairs[i].keylen);
			if(cmp == 0 && !dname[pairs[i].keylen]) {
				index = i;
				break;
			}
		}
		if(i == count) {
			kfree((addr_t)pairs);
			return -ENOENT;
		}
		for(i = index; i < count - 1; i++) {
			pairs[i] = pairs[i + 1];
		}
		count--;
	}

	/* re-serialize */
	memset_b(n, 0, BFS_BLOCK_SIZE);
	n->left = BFS_BTREE_NULL;
	n->right = BFS_BTREE_NULL;
	n->overflow = BFS_BTREE_NULL;	/* leaf */
	n->all_key_count = count;
	for(i = 0; i < count; i++) {
		total += pairs[i].keylen;
	}
	n->all_key_length = total;
	{
		char *keys = (char *)n + sizeof(struct bfs_btree_node);
		__u16 *kl = bfs_btree_keylen_index(n);
		__u64 *values = bfs_btree_values(n);
		int off = 0;
		for(i = 0; i < count; i++) {
			memcpy_b(keys + off, pairs[i].key, pairs[i].keylen);
			off += pairs[i].keylen;
			kl[i] = off;
			values[i] = pairs[i].val;
		}
	}
	kfree((addr_t)pairs);
	return 0;
}

/*
 * Insert a (name, inode) pair into the directory tree.
 *
 * M2 supports depth-1 trees (the root node is a leaf); node splitting
 * for full leaves is future work (-ENOSPC).
 */
int bfs_btree_insert(struct inode *dir, const char *name, __ino_t ino)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int res;

	if(!name[0]) {
		return -EINVAL;
	}
	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}

	node_off = header.root_node_ptr;
	if(!(buf = bfs_btree_read_node(dir, node_off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	if(n->overflow != BFS_BTREE_NULL) {
		brelse(buf);
		return -EIO;	/* interior root: not supported yet */
	}

	res = bfs_btree_rebuild(n, 1, name, ino, NULL);
	if(res) {
		brelse(buf);
		return res;
	}

	dir->state |= INODE_DIRTY;
	bwrite(buf);
	return 0;
}

/*
 * Remove the entry whose inode matches 'ino' (used by rmdir, which has
 * no name). Returns -ENOENT if not found.
 */
int bfs_btree_delete_ino(struct inode *dir, __ino_t ino)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	__u16 *kl;
	__u64 *values;
	char *keys;
	int i, res, keylen;

	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}
	node_off = header.root_node_ptr;
	if(!(buf = bfs_btree_read_node(dir, node_off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	if(n->overflow != BFS_BTREE_NULL) {
		brelse(buf);
		return -EIO;
	}
	values = bfs_btree_values(n);
	for(i = 0; i < n->all_key_count; i++) {
		if(values[i] == ino) {
			break;
		}
	}
	if(i == n->all_key_count) {
		brelse(buf);
		return -ENOENT;
	}

	keys = (char *)n + sizeof(struct bfs_btree_node);
	kl = bfs_btree_keylen_index(n);
	{
		int start = i ? kl[i - 1] : 0;
		int key_end = kl[i];
		int klen = key_end - start;
		int tail = n->all_key_length - key_end;

		memmove(keys + start, keys + key_end, tail);
		{
			int j;
			for(j = i; j < n->all_key_count - 1; j++) {
				kl[j] = kl[j + 1] - klen;
			}
		}
		memmove(values + i, values + i + 1,
			(n->all_key_count - i - 1) * sizeof(__u64));
		n->all_key_count--;
		n->all_key_length -= klen;
		(void)keylen;
	}

	dir->state |= INODE_DIRTY;
	bwrite(buf);
	return 0;
}

/*
 * Remove a (name, inode) pair from the directory tree.
 */
int bfs_btree_delete(struct inode *dir, const char *name)
{
	struct bfs_btree_header header;
	struct bfs_btree_node *n;
	struct buffer *buf;
	__u64 node_off;
	int res;

	if(!name[0]) {
		return -EINVAL;
	}
	if((res = bfs_btree_read_header(dir, &header))) {
		return res;
	}

	node_off = header.root_node_ptr;
	if(!(buf = bfs_btree_read_node(dir, node_off))) {
		return -EIO;
	}
	n = (struct bfs_btree_node *)buf->data;
	if(n->overflow != BFS_BTREE_NULL) {
		brelse(buf);
		return -EIO;
	}

	res = bfs_btree_rebuild(n, 0, NULL, 0, name);
	if(res) {
		brelse(buf);
		return res;
	}

	dir->state |= INODE_DIRTY;
	bwrite(buf);
	return 0;
}
