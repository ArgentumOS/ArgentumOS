/*
 * fnx/include/fnx/xbfs.h
 *
 * XBFS ("the ex-Be filesystem"): forked from the Be File System. The
 * on-disk structures follow the BeOS/Haiku BFS layout (block_run
 * allocation, B+tree directories, attribute indexes), but the
 * superblock magic is our own 'XBFS' — XBFS volumes no longer present
 * as BFS, and BFS readers reject them.
 *
 * Cross-checked against Linux fs/befs (befs_fs_types.h, btree.c) and
 * Haiku src/add-ons/kernel/file_systems/bfs (bfs.h, BPlusTree.h).
 * All fields are little-endian (fs_byte_order == 'BIGE').
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_XBFS_H
#define _FNX_XBFS_H

#include <fnx/types.h>

#define XBFS_BLOCK_SIZE		1024	/* the reference/default block size */
#define XBFS_BLOCK_SHIFT		10
/* a volume's block size (== inode size, Haiku requires them equal) may
 * be 1024 or 2048 — the buffer cache caps bread() at PAGE_SIZE and the
 * per-inode small_data tail must fit inside a single kmalloc'd
 * struct inode (kmalloc caps at PAGE_SIZE), so a 4096-byte block
 * would need a VFS destroy-inode hook for a separately allocated
 * tail (documented as remaining). The on-disk size is read from the
 * superblock at mount; every driver structure that spans a block
 * (btree node, small_data tail, indirect table slot count) uses the
 * volume's size at runtime. */
#define XBFS_MIN_BLOCK_SIZE	1024
#define XBFS_MAX_BLOCK_SIZE	4096
#define XBFS_MIN_BLOCK_SHIFT	10
#define XBFS_MAX_BLOCK_SHIFT	12
#define XBFS_INODE_SIZE		256
#define XBFS_INODES_PER_BLOCK	(XBFS_BLOCK_SIZE / XBFS_INODE_SIZE)
#define XBFS_BLOCKS_PER_AG	8192
#define XBFS_AG_SHIFT		13
#define XBFS_NUM_DIRECT_BLOCKS	12
#define XBFS_NAME_LEN		255

/* superblock magic values (magic1 is our own 'XBFS'; magic2/magic3
 * stay BFS's fixed validation constants) */
#define XBFS_SUPER_MAGIC1	0x58424653	/* 'XBFS' */
#define XBFS_SUPER_MAGIC2	0xdd121031
#define XBFS_SUPER_MAGIC3	0x15b6830e
#define XBFS_SUPER_BYTEORDER	0x42494745	/* 'BIGE' (LE disk) */
#define XBFS_SUPER_CLEAN		0x434c454e	/* 'CLEN' */
#define XBFS_SUPER_DIRTY		0x44495254	/* 'DIRT' */

/* inode magic + flags */
#define XBFS_INODE_MAGIC		0x3bbe0ad9
#define XBFS_INODE_IN_USE	0x00000001
/* Haiku inode_flags (permanent bits; the low 16 bits are on-disk) */
#define XBFS_INODE_LONG_SYMLINK	0x00000040	/* target in the data stream */
#define XBFS_INODE_ATTR_INODE	0x00000004	/* legacy BeOS: attribute node */

/* Haiku stat.h extended mode bits (stored in the HIGH 16 bits of the
 * on-disk inode mode; invisible to 16-bit i_mode). Values are the
 * octal constants from Haiku's <sys/stat.h> truncated to 32 bits:
 * 01000000000 -> 0x08000000, etc. */
#define XBFS_S_DOUBLE_INDEX	0x00040000	/* double index */
#define XBFS_S_ALLOW_DUPS	0x00080000	/* allow duplicates (unused) */
#define XBFS_S_LONG_LONG_INDEX	0x00200000	/* int64 index */
#define XBFS_S_ULONG_LONG_INDEX	0x00400000	/* uint64 index */
#define XBFS_S_FLOAT_INDEX	0x00800000	/* float index */
#define XBFS_S_STR_INDEX		0x01000000	/* string index */
#define XBFS_S_INT_INDEX		0x02000000	/* int32 index */
#define XBFS_S_UINT_INDEX	0x04000000	/* uint32 index */
#define XBFS_S_ATTR_DIR		0x08000000	/* attribute directory */
#define XBFS_S_ATTR		0x10000000	/* attribute */
#define XBFS_S_INDEX_DIR		0x20000000	/* index (or index directory) */

/* duplicate-key value links (the top two bits of a leaf value; the low
 * 10 bits carry the fragment index) — Haiku's BPlusTree encoding */
#define XBFS_BTREE_DUPLICATE_NODE	2
#define XBFS_BTREE_DUPLICATE_FRAGMENT	3
#define XBFS_BTREE_NUM_FRAGMENT_VALUES	7	/* values per fragment */
#define XBFS_BTREE_NUM_DUPLICATE_VALUES	125	/* values per duplicate node */
#define XBFS_BTREE_MAX_FRAGMENTS		16	/* 1024 / ((7+1)*8) */

/* inode 'type' values (attribute type of the main data stream) */
#define XBFS_FILE_TYPE_DIR	0x00000000
#define XBFS_FILE_TYPE_REG	0x00000001
#define XBFS_FILE_TYPE_LNK	0x00000002

/* B+tree */
#define XBFS_BTREE_MAGIC		0x69f6c2e8
#define XBFS_BTREE_NULL		(-1LL)
#define XBFS_BTREE_FREE		(-2LL)
#define XBFS_BTREE_STRING_TYPE	0
#define XBFS_BTREE_INT8_TYPE	1
#define XBFS_BTREE_INT16_TYPE	2
#define XBFS_BTREE_INT32_TYPE	3
#define XBFS_BTREE_UINT32_TYPE	4
#define XBFS_BTREE_INT64_TYPE	5
#define XBFS_BTREE_UINT64_TYPE	6
#define XBFS_BTREE_FLOAT_TYPE	7
#define XBFS_BTREE_DOUBLE_TYPE	8
#define XBFS_BTREE_MAX_KEY_LEN	256
/* Haiku's BPlusTree node size is a hard-coded 1024 bytes REGARDLESS of
 * the volume block size (BPlusTree.cpp: "the node size is hard-coded to
 * 1024 bytes"; the duplicate-fragment handling is likewise). The tree
 * nodes are addressed at 1024-byte offsets within the inode's stream —
 * with 2048/4096-byte blocks several nodes pack into one block. */
#define XBFS_BTREE_NODE_SIZE	1024
/* structural cap on pairs per node (a 4096-byte leaf could hold ~370
 * short-key pairs, but the pair-collect arrays are sized to 128; the
 * room check splits a node when it would exceed this, so every array
 * stays bounded) */
#define XBFS_BTREE_MAX_PAIRS	128

/* small_data attribute types */
#define XBFS_FILE_NAME_TYPE	0x43535452	/* 'CSTR' */

/* XBFS attribute-type ioctls (FNX extension: the Linux xattr ABI has no
 * type field, but Haiku's fs_stat_attr / BNode::WriteAttr carry one, so
 * a volume can move between FNX and Haiku without losing types). */
#define XBFS_ATTR_NAME_MAX	255

struct xbfs_attr_info {
	char name[XBFS_ATTR_NAME_MAX + 1];
	__u32 type;			/* on GET: the record's type */
	__u64 size;			/* on GET: the value's size */
};

#define XBFS_IOC_GET_ATTR_INFO	0x42530001	/* 'BS' + 1 */
#define XBFS_IOC_SET_ATTR_TYPE	0x42530002
#define XBFS_IOC_QUERY		0x42530003	/* 'BS' + 3 */

/* volume query (Haiku's BQuery): evaluate a query expression against
 * the volume's indices and return the matching inode numbers.
 * count in = capacity of inodes[] (0 = probe, just get the count);
 * count out = the total number of matches. The kernel never copies
 * more than min(matches, capacity) results. */
#define XBFS_QUERY_MAX_LEN	512
#define XBFS_QUERY_MAX_RESULTS	65536
struct xbfs_query {
	char query[XBFS_QUERY_MAX_LEN];
	__u32 count;
	__u32 inodes[XBFS_QUERY_MAX_RESULTS];
};
#define XBFS_QUERY_INODES_OFF	((unsigned long)&((struct xbfs_query *)0)->inodes)

/* the query expression grammar (Haiku's QueryParser):
 *   expr   := orexpr
 *   orexpr := andexpr | orexpr '||' andexpr
 *   andexpr:= term | andexpr '&&' term
 *   term   := '(' expr ')' | '!' term | equation
 *   equation := attr op value
 *   op     := '=' | '!=' | '>' | '>=' | '<' | '<='
 * '!' only negates a parenthesized term (DeMorgan); values are quoted
 * with ' or " or run to the next operator/')'; '*' '?' '[' are
 * wildcards for '=' / '!=' on STRING indices only. */

/* block run: allocation_group << ag_shift + start = absolute block */
struct xbfs_block_run {
	__u32 allocation_group;
	__u16 start;
	__u16 len;
} __attribute__((packed));

typedef struct xbfs_block_run xbfs_inode_addr;

/*
 * Encode an ABSOLUTE block number as an on-disk (allocation_group,
 * start) block_run. The start field is u16: storing the absolute block
 * there truncates beyond block 65535 (Haiku's root sits at block
 * 131072), so every run must be split as (block >> ag_shift, block &
 * mask). The volume's ag_shift is 13..15 (mkxbfs 1024-byte volumes keep
 * 13; Haiku picks 13..15 by size).
 */
static __inline__ void xbfs_run_encode(struct xbfs_block_run *run, __blk_t block,
				      __u32 ag_shift)
{
	run->allocation_group = block >> ag_shift;
	run->start = block & ((1u << ag_shift) - 1);
}

/*
 * Superblock: 512 bytes at offset 512 of block 0 (the first 512 bytes
 * of block 0 are the boot block). PACKED size = 126 bytes; the rest of
 * the 512-byte area is unused/reserved.
 */
struct xbfs_superblock {
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
	struct xbfs_block_run log_blocks;
	__u64 log_start;
	__u64 log_end;
	__u32 magic3;
	xbfs_inode_addr root_dir;
	xbfs_inode_addr indices;
} __attribute__((packed));

/* Dual-copy superblock (a torn write of the single copy left the volume
 * unmountable; each 512-byte copy sits in its own sector so a kill
 * between the two sector writes can tear at most one). Copy A lives at
 * offset 512 of block 0 (the on-disk struct, as always); copy B at
 * offset 0 of block 0 (the boot-block sector, unused on XBFS volumes —
 * FNX boots from the ESP, and mkxbfs writes no boot code). The struct
 * (0x84 bytes) is followed by a u64 sequence and a u32 checksum of
 * [0, 0x8C); a copy is valid when its magics match and, for new-format
 * copies (seq != 0), the checksum does too. Mounts take the valid copy
 * with the highest sequence. */
#define XBFS_SB_A_OFF		512
#define XBFS_SB_B_OFF		0
#define XBFS_SB_SEQ_OFF		0x84
#define XBFS_SB_CKSUM_OFF	0x8C
#define XBFS_SB_COPY_LEN	0x90

/* data stream: direct runs + indirect + double indirect + size */
struct xbfs_data_stream {
	struct xbfs_block_run direct[XBFS_NUM_DIRECT_BLOCKS];
	__u64 max_direct_range;
	struct xbfs_block_run indirect;
	__u64 max_indirect_range;
	struct xbfs_block_run double_indirect;
	__u64 max_double_indirect_range;
	__u64 size;
} __attribute__((packed));

/*
 * Inode: 256 bytes, one inode per block (the vfs inode number is the
 * absolute block number of the inode). PACKED size (without small_data)
 * = 232 bytes; the 24-byte small_data attribute tail follows.
 */
struct xbfs_inode {
	__u32 magic1;
	xbfs_inode_addr inode_num;
	__u32 uid;
	__u32 gid;
	__u32 mode;
	__u32 flags;
	__u64 create_time;
	__u64 last_modified_time;
	xbfs_inode_addr parent;
	xbfs_inode_addr attributes;
	__u32 type;
	__u32 inode_size;
	__u32 etc;
	union {
		struct xbfs_data_stream data;
		char symlink[144];
	} u;
	__u64 status_change_time;
	__u32 pad[2];
	/* small_data attributes follow (to the end of the block; the
	 * superblock's inode_size == block_size, so the tail is
	 * XBFS_BLOCK_SIZE - sizeof(struct xbfs_inode) bytes) */
} __attribute__((packed));

/* small_data attribute header (packed inline in the inode tail) */
struct xbfs_small_data {
	__u32 type;
	__u16 name_size;
	__u16 data_size;
	char name[1];		/* name_size bytes + data */
} __attribute__((packed));

/* B+tree header: lives at offset 0 of the directory's data stream */
struct xbfs_btree_header {
	__u32 magic;
	__u32 node_size;
	__u32 max_depth;
	__u32 data_type;
	__u64 root_node_ptr;	/* byte offset of the root node in the stream */
	__u64 free_node_ptr;
	__u64 max_size;
} __attribute__((packed));

/* B+tree node header (node_size = 1024). Links are byte offsets in the
 * stream; a leaf has overflow == XBFS_BTREE_NULL. After the header: the
 * key area (all_key_length bytes of concatenated key strings), aligned
 * to 8, then all_key_count u16 key-length indexes (cumulative end
 * offsets), then (leaf) all_key_count u64 values = inode block numbers. */
struct xbfs_btree_node {
	__u64 left;
	__u64 right;
	__u64 overflow;
	__u16 all_key_count;
	__u16 all_key_length;
} __attribute__((packed));

/* fs-private superblock info (per mounted XBFS volume) */
struct xbfs_sb_info {
	__u32 block_size;
	__u32 blocks_per_ag;	/* bitmap blocks per allocation group */
	__u32 ag_shift;
	__u32 num_ags;
	__u64 num_blocks;
	__u32 flags;
	__u32 root_inode;	/* block number of the root directory inode */
	__u32 indices_inode;	/* block number of the indices dir inode (0 = none) */
	__u64 used_blocks;
	/* free-space bitmap (all groups concatenated), cached in memory as
	 * PAGE_SIZE chunks (a volume's bitmap can exceed the kmalloc cap) */
	unsigned char **bitmap;	/* chunk pointers */
	__u32 bitmap_chunks;	/* number of PAGE_SIZE chunks */
	__u32 bitmap_blocks;	/* total bitmap blocks on the volume */
	__u32 next_free;	/* allocation hint (volume block number) */
	/* journal (log) state */
	struct xbfs_block_run log_blocks;	/* the log extent */
	__u64 log_start;			/* BLOCK offset of the first entry */
	__u64 log_end;				/* BLOCK offset past the last entry */
	__u64 log_since_reset;		/* blocks journaled since the last wrap */
	__u64 log_peak;				/* high-water of log_since_reset */
	/* in-memory journal transaction state */
#define XBFS_LOG_MAX_BLOCKS	15	/* count <= log size - run_array block */
	__blk_t tx_blocks[XBFS_LOG_MAX_BLOCKS];	/* blocks modified in this tx */
	unsigned char *tx_data[XBFS_LOG_MAX_BLOCKS];	/* their new content */
	int tx_nblocks;				/* entries used in tx_blocks[] */
	int tx_depth;				/* nesting depth (0 = no tx) */
	int log_draining;			/* umount: write through, no journal */
	/* journal transaction lock (serializes tx ownership): the tx
	 * state is per-superblock but the commit sleeps on I/O, so a
	 * concurrent tx on the same sb would clobber it. Stored as a
	 * struct resource (fnx/sleep.h) without the include to avoid a
	 * header cycle. */
	char journal_locked;
	char journal_wanted;
	__u64 sb_seq;			/* superblock copy sequence (dual-copy) */
};

/* the packed inode struct is 232 bytes; the small_data attribute tail
 * is the remaining 24 bytes of the 256-byte on-disk inode (the header
 * comment saying "198" was wrong) */
/* Haiku's inode_size field is the BLOCK size (Volume.cpp Initialize:
 * "block_size = inode_size = blockSize"), so the small_data tail spans
 * from the end of the struct to the end of the block. 24 bytes (the old
 * 256 - 232) was too small for Haiku-compatible file-name records
 * (8 + 1 + 3 + NAME + 1, up to 268 bytes for a 255-char name). The
 * in-memory copy is sized for the LARGEST possible tail; the on-disk
 * tail for a given volume is block_size - sizeof(struct xbfs_inode). */
#define XBFS_SMALL_DATA_SIZE	(XBFS_MAX_BLOCK_SIZE - sizeof(struct xbfs_inode))

/* fs-private inode info (the raw on-disk inode, for bmap/readdir) */
#define XBFS_II_MAGIC	0x4b425349	/* 'KBSI' — marks a union holding a XBFS inode */

struct xbfs_i_info {
	struct xbfs_inode raw;	/* raw on-disk inode (232 bytes) */
	unsigned char *small_data;	/* kmalloc'd inline-attribute tail,
				 * sized to the volume's block size - inode;
				 * freed via fsop->destroy_inode (the tail
				 * can be 3864 bytes at 4096-byte blocks, too
				 * big to embed in the kmalloc'd struct inode) */
	__u32 magic;		/* XBFS_II_MAGIC while the union is ours */
};

/* block allocation (balloc.c) */
int xbfs_balloc(struct superblock *);
void xbfs_bfree(struct superblock *, __blk_t);
int xbfs_balloc_specific(struct superblock *, __blk_t);

/* inode.c */
int xbfs_write_inode(struct inode *);
int xbfs_ialloc(struct inode *, int);
void xbfs_ifree(struct inode *);
void xbfs_destroy_inode(struct inode *);
int xbfs_truncate(struct inode *, __off_t);

/* journal.c */
struct buffer;	/* forward decl (fnx/buffer.h) */
__blk_t xbfs_log_run_abs(struct superblock *, struct xbfs_block_run *);
int xbfs_log_replay(struct superblock *);
int xbfs_log_begin(struct superblock *);
int xbfs_log_record(struct superblock *, __blk_t, unsigned char *);
int xbfs_log_commit(struct superblock *);
void xbfs_log_write_block(struct superblock *, __blk_t, struct buffer *);
void xbfs_log_lock(struct superblock *);
void xbfs_log_unlock(struct superblock *);
/* R-M2 crash injection: arm a deliberate halt at journal commit state
 * 1..7 on the COUNT-th hit (docs/bfs-journal-reclaim.md) */
void xbfs_crash_set(int state, int count);
/* dual-copy superblock flush: the caller built the copy-A struct at
 * XBFS_SB_A_OFF in buf->data; stamp the sequence + checksum on both
 * copies, write the block and sync (fs/xbfs/super.c) */
int xbfs_sb_dual_write(struct superblock *sb, struct buffer *buf);

/* btree.c */
int xbfs_btree_insert(struct inode *, const char *, __ino_t);
int xbfs_btree_delete(struct inode *, const char *);
int xbfs_btree_delete_ino(struct inode *, __ino_t);

/* ------------------------------------------------------------------ */
/* Journal (log) — faithful Haiku XBFS on-disk log-entry format (Haiku
 * fs/xbfs/Journal.cpp, run_array). The log is a sequence of entries in
 * the extent described by the superblock's log_blocks run; the
 * superblock's log_start/log_end are BLOCK offsets into that extent
 * (Haiku runtime semantics); log_start == log_end means the log is
 * empty (clean).
 *
 * A transaction entry is:
 *   one run_array block:  { s32 count; s32 max_runs (127);
 *                           block_run runs[127]; }   (1024 bytes)
 *   followed by `count` data blocks (the raw block contents, one per
 *   block run, in run order; every run has len == 1 — Be's replay can
 *   only deal with length-1 runs).
 *
 * Replay walks log_start..log_end, writing each entry's data blocks
 * back to their real locations, then clears the log (log_start =
 * log_end = 0). There is no transaction id on disk: log_start/log_end
 * ARE the commit markers (log_end is advanced in the on-disk
 * superblock before the real blocks are written, so a crash mid-flush
 * leaves the log covering the transaction).
 */
struct xbfs_run_array {
	__s32 count;		/* number of block runs in this entry */
	__s32 max_runs;		/* 127 (max run_array capacity) */
	struct xbfs_block_run runs[127];	/* the modified blocks */
} __attribute__((packed));	/* 8 + 127*8 = 1024 = one block */
#define XBFS_LOG_MAX_RUNS	127

/* write (or replace) the file-name 0x13 small_data record (Haiku
 * Inode::SetName()); called at create/rename */
int xbfs_inode_set_name(struct inode *, const char *);
int xbfs_inode_get_name(struct inode *, char *, int);

/* the indices tree (Haiku's directory of index B+trees) - fs/bfs/indices.c */
void xbfs_index_add(struct superblock *, struct inode *, const char *);
void xbfs_index_remove(struct superblock *, struct inode *, const char *);
void xbfs_index_resize(struct superblock *, struct inode *, __off_t, __u64);
void xbfs_touch_mtime(struct inode *);
void xbfs_touch_ctime(struct inode *);
void xbfs_dir_touch(struct inode *, __off_t);

/* the typed index-tree API (duplicate-key aware); 'dtype' is one of
 * the XBFS_BTREE_*_TYPE constants from the tree header */
int xbfs_btree_insert_value(struct inode *, const char *, int, int, __u64);
int xbfs_btree_find_value(struct inode *, const char *, int, int, __u64 *);
int xbfs_btree_delete_value(struct inode *, const char *, int, int, __u64);
int xbfs_btree_iterate_values(struct inode *, int,
	int (*)(const char *, int, __ino_t, void *), void *);

/* attribute inodes (Haiku's per-file attributes B+tree) - see
 * fs/xbfs/attribute.c */
int xbfs_attr_find(struct inode *, const char *, struct inode **);
int xbfs_attr_get(struct inode *, const char *, char *, __size_t);
int xbfs_attr_set(struct inode *, const char *, const char *, __size_t, __u32);
int xbfs_attr_info(struct inode *, struct xbfs_attr_info *);
int xbfs_attr_set_type(struct inode *, const char *, __u32);
int xbfs_query(struct superblock *, const char *, __u32 *, __u32);
int xbfs_attr_list(struct inode *, char *, __size_t, int);
int xbfs_attr_remove(struct inode *, const char *);
void xbfs_attr_free_all(struct inode *);
#endif /* _FNX_BFS_H */

