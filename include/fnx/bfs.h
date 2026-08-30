/*
 * fnx/include/fnx/bfs.h
 *
 * BeOS BFS (Be File System) on-disk structures.
 *
 * Cross-checked against Linux fs/befs (befs_fs_types.h, btree.c) and
 * Haiku src/add-ons/kernel/file_systems/bfs (bfs.h, BPlusTree.h).
 * All fields are little-endian (fs_byte_order == 'BIGE').
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_BFS_H
#define _FNX_BFS_H

#include <fnx/types.h>

#define BFS_BLOCK_SIZE		1024
#define BFS_BLOCK_SHIFT		10
#define BFS_INODE_SIZE		256
#define BFS_INODES_PER_BLOCK	(BFS_BLOCK_SIZE / BFS_INODE_SIZE)
#define BFS_BLOCKS_PER_AG	8192
#define BFS_AG_SHIFT		13
#define BFS_NUM_DIRECT_BLOCKS	12
#define BFS_NAME_LEN		255

/* superblock magic values */
#define BFS_SUPER_MAGIC1	0x42465331	/* 'BFS1' */
#define BFS_SUPER_MAGIC2	0xdd121031
#define BFS_SUPER_MAGIC3	0x15b6830e
#define BFS_SUPER_BYTEORDER	0x42494745	/* 'BIGE' (LE disk) */
#define BFS_SUPER_CLEAN		0x434c454e	/* 'CLEN' */
#define BFS_SUPER_DIRTY		0x44495254	/* 'DIRT' */

/* inode magic + flags */
#define BFS_INODE_MAGIC		0x3bbe0ad9
#define BFS_INODE_IN_USE	0x00000001

/* inode 'type' values (attribute type of the main data stream) */
#define BFS_FILE_TYPE_DIR	0x00000000
#define BFS_FILE_TYPE_REG	0x00000001
#define BFS_FILE_TYPE_LNK	0x00000002

/* B+tree */
#define BFS_BTREE_MAGIC		0x69f6c2e8
#define BFS_BTREE_NULL		(-1LL)
#define BFS_BTREE_FREE		(-2LL)
#define BFS_BTREE_STRING_TYPE	0
#define BFS_BTREE_MAX_KEY_LEN	256

/* small_data attribute types */
#define BFS_FILE_NAME_TYPE	0x43535452	/* 'CSTR' */

/* block run: allocation_group << ag_shift + start = absolute block */
struct bfs_block_run {
	__u32 allocation_group;
	__u16 start;
	__u16 len;
} __attribute__((packed));

typedef struct bfs_block_run bfs_inode_addr;

/*
 * Superblock: 512 bytes at offset 512 of block 0 (the first 512 bytes
 * of block 0 are the boot block). PACKED size = 126 bytes; the rest of
 * the 512-byte area is unused/reserved.
 */
struct bfs_superblock {
	char name[32];
	__u32 magic1;
	__u32 fs_byte_order;
	__u32 block_size;
	__u32 block_shift;
	__u64 num_blocks;
	__u64 used_blocks;
	__u32 inode_size;
	__u32 magic2;
	__u32 blocks_per_ag;
	__u32 ag_shift;
	__u32 num_ags;
	__u32 flags;
	struct bfs_block_run log_blocks;
	__u64 log_start;
	__u64 log_end;
	__u32 magic3;
	bfs_inode_addr root_dir;
	bfs_inode_addr indices;
} __attribute__((packed));

/* data stream: direct runs + indirect + double indirect + size */
struct bfs_data_stream {
	struct bfs_block_run direct[BFS_NUM_DIRECT_BLOCKS];
	__u64 max_direct_range;
	struct bfs_block_run indirect;
	__u64 max_indirect_range;
	struct bfs_block_run double_indirect;
	__u64 max_double_indirect_range;
	__u64 size;
} __attribute__((packed));

/*
 * Inode: 256 bytes, one inode per block (the vfs inode number is the
 * absolute block number of the inode). PACKED size (without small_data)
 * = 232 bytes; the 24-byte small_data attribute tail follows.
 */
struct bfs_inode {
	__u32 magic1;
	bfs_inode_addr inode_num;
	__u32 uid;
	__u32 gid;
	__u32 mode;
	__u32 flags;
	__u64 create_time;
	__u64 last_modified_time;
	bfs_inode_addr parent;
	bfs_inode_addr attributes;
	__u32 type;
	__u32 inode_size;
	__u32 etc;
	union {
		struct bfs_data_stream data;
		char symlink[144];
	} u;
	__u64 status_change_time;
	__u32 pad[2];
	/* small_data attributes follow (24 bytes to the 256-byte inode) */
} __attribute__((packed));

/* small_data attribute header (packed inline in the inode tail) */
struct bfs_small_data {
	__u32 type;
	__u16 name_size;
	__u16 data_size;
	char name[1];		/* name_size bytes + data */
} __attribute__((packed));

/* B+tree header: lives at offset 0 of the directory's data stream */
struct bfs_btree_header {
	__u32 magic;
	__u32 node_size;
	__u32 max_depth;
	__u32 data_type;
	__u64 root_node_ptr;	/* byte offset of the root node in the stream */
	__u64 free_node_ptr;
	__u64 max_size;
} __attribute__((packed));

/* B+tree node header (node_size = 1024). Links are byte offsets in the
 * stream; a leaf has overflow == BFS_BTREE_NULL. After the header: the
 * key area (all_key_length bytes of concatenated key strings), aligned
 * to 8, then all_key_count u16 key-length indexes (cumulative end
 * offsets), then (leaf) all_key_count u64 values = inode block numbers. */
struct bfs_btree_node {
	__u64 left;
	__u64 right;
	__u64 overflow;
	__u16 all_key_count;
	__u16 all_key_length;
} __attribute__((packed));

/* fs-private superblock info (per mounted BFS volume) */
struct bfs_sb_info {
	__u32 block_size;
	__u32 blocks_per_ag;	/* bitmap blocks per allocation group */
	__u32 ag_shift;
	__u32 num_ags;
	__u64 num_blocks;
	__u32 flags;
	__u32 root_inode;	/* block number of the root directory inode */
	__u64 used_blocks;
	/* free-space bitmap (all groups concatenated), cached in memory */
	unsigned char *bitmap;	/* num_bitmap_blocks * block_size bytes */
	__u32 bitmap_blocks;	/* total bitmap blocks on the volume */
	__u32 next_free;	/* allocation hint (volume block number) */
};

/* the packed inode struct is 232 bytes; the small_data attribute tail
 * is the remaining 24 bytes of the 256-byte on-disk inode (the header
 * comment saying "198" was wrong) */
#define BFS_SMALL_DATA_SIZE	(256 - sizeof(struct bfs_inode))

/* fs-private inode info (the raw on-disk inode, for bmap/readdir) */
struct bfs_i_info {
	struct bfs_inode raw;	/* raw on-disk inode (232 bytes) */
	char small_data[BFS_SMALL_DATA_SIZE];	/* inline attributes */
};

/* block allocation (balloc.c) */
int bfs_balloc(struct superblock *);
void bfs_bfree(struct superblock *, __blk_t);
int bfs_balloc_specific(struct superblock *, __blk_t);

/* inode.c */
int bfs_write_inode(struct inode *);
int bfs_ialloc(struct inode *, int);
void bfs_ifree(struct inode *);
int bfs_truncate(struct inode *, __off_t);

/* btree.c */
int bfs_btree_insert(struct inode *, const char *, __ino_t);
int bfs_btree_delete(struct inode *, const char *);
int bfs_btree_delete_ino(struct inode *, __ino_t);

#endif /* _FNX_BFS_H */
