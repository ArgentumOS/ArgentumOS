/*
 * fnx/fs/bfs/balloc.c
 *
 * BFS free-space management.
 *
 * Each allocation group has a bit bitmap stored in the FIRST blocks of
 * the group, right after the superblock (Haiku's disk_super_block
 * convention: the sb's blocks_per_ag field holds the number of bitmap
 * blocks per group). Bit b of group g covers volume block
 * (g << ag_shift) + b. A set bit means the block is used. Block 0 (the
 * boot block + superblock), the bitmap blocks themselves, and the log
 * area are reserved by setting their bits.
 *
 * The whole bitmap is cached in memory (sb->u.bfs.bitmap); changes are
 * flushed by bfs_write_superblock (via sync_superblocks).
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

/* block number -> (group, bit-in-group) */
static __u32 bfs_group(struct superblock *sb, __blk_t block)
{
	return block >> sb->u.bfs.ag_shift;
}

static __u32 bfs_group_bit(struct superblock *sb, __blk_t block)
{
	return block & ((1 << sb->u.bfs.ag_shift) - 1);
}

static int bfs_bitmap_test(struct superblock *sb, __blk_t block)
{
	__u32 bit = bfs_group_bit(sb, block);
	unsigned char *bm = sb->u.bfs.bitmap
		+ ((__u64)bfs_group(sb, block) * sb->u.bfs.blocks_per_ag
			* sb->u.bfs.block_size);

	return bm[bit >> 3] & (1 << (bit & 7));
}

static void bfs_bitmap_set(struct superblock *sb, __blk_t block)
{
	__u32 bit = bfs_group_bit(sb, block);
	unsigned char *bm = sb->u.bfs.bitmap
		+ ((__u64)bfs_group(sb, block) * sb->u.bfs.blocks_per_ag
			* sb->u.bfs.block_size);

	bm[bit >> 3] |= (1 << (bit & 7));
}

static void bfs_bitmap_clear(struct superblock *sb, __blk_t block)
{
	__u32 bit = bfs_group_bit(sb, block);
	unsigned char *bm = sb->u.bfs.bitmap
		+ ((__u64)bfs_group(sb, block) * sb->u.bfs.blocks_per_ag
			* sb->u.bfs.block_size);

	bm[bit >> 3] &= ~(1 << (bit & 7));
}

/*
 * Allocate a free block. Returns the block number or a negative errno.
 */
int bfs_balloc(struct superblock *sb)
{
	__u64 num_blocks = sb->u.bfs.num_blocks;
	__u32 hint = sb->u.bfs.next_free;
	__blk_t block;

	superblock_lock(sb);

	if(hint >= num_blocks) {
		hint = 0;
	}
	block = hint;
	do {
		if(!bfs_bitmap_test(sb, block)) {
			bfs_bitmap_set(sb, block);
			sb->u.bfs.used_blocks++;
			sb->u.bfs.next_free = block + 1;
			sb->state |= SUPERBLOCK_DIRTY;
			superblock_unlock(sb);
			return block;
		}
		block++;
		if(block >= num_blocks) {
			block = 0;
		}
	} while(block != hint);

	superblock_unlock(sb);
	return -ENOSPC;
}

/*
 * Allocate a SPECIFIC block (used to extend an existing block run).
 * Returns 0 if the block was free (now marked used), -EEXIST if it was
 * already in use.
 */
int bfs_balloc_specific(struct superblock *sb, __blk_t block)
{
	if(block >= sb->u.bfs.num_blocks) {
		return -EINVAL;
	}

	superblock_lock(sb);
	if(bfs_bitmap_test(sb, block)) {
		superblock_unlock(sb);
		return -EEXIST;
	}
	bfs_bitmap_set(sb, block);
	sb->u.bfs.used_blocks++;
	sb->state |= SUPERBLOCK_DIRTY;
	superblock_unlock(sb);
	return 0;
}

/*
 * Free a block.
 */
void bfs_bfree(struct superblock *sb, __blk_t block)
{
	if(block >= sb->u.bfs.num_blocks) {
		return;
	}

	superblock_lock(sb);
	if(bfs_bitmap_test(sb, block)) {
		bfs_bitmap_clear(sb, block);
		sb->u.bfs.used_blocks--;
		sb->state |= SUPERBLOCK_DIRTY;
		if(sb->u.bfs.next_free > block) {
			sb->u.bfs.next_free = block;
		}
	}
	superblock_unlock(sb);
}
