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
