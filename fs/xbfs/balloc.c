/*
 * fnx/fs/xbfs/balloc.c
 *
 * XBFS free-space management.
 *
 * Each allocation group has a bit bitmap stored in the FIRST blocks of
 * the group, right after the superblock (Haiku's disk_super_block
 * convention: the sb's blocks_per_ag field holds the number of bitmap
 * blocks per group). Bit b of group g covers volume block
 * (g << ag_shift) + b. A set bit means the block is used. Block 0 (the
 * boot block + superblock), the bitmap blocks themselves, and the log
 * area are reserved by setting their bits.
 *
 * The whole bitmap is cached in memory (sb->u.xbfs.bitmap); changes are
 * flushed by xbfs_write_superblock (via sync_superblocks).
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/xbfs.h>
#include <fnx/mm.h>
#include <fnx/buffer.h>
#include <fnx/string.h>

/* block number -> (group, bit-in-group) */
static __u32 xbfs_group(struct superblock *sb, __blk_t block)
{
	return block >> sb->u.xbfs.ag_shift;
}

static __u32 xbfs_group_bit(struct superblock *sb, __blk_t block)
{
	return block & ((1 << sb->u.xbfs.ag_shift) - 1);
}

/* the bitmap byte for a block, split into its PAGE_SIZE chunk + offset */
static void xbfs_bitmap_byte(struct superblock *sb, __blk_t block,
			    unsigned char **chunk, __u32 *byte)
{
	__u64 off = (__u64)xbfs_group(sb, block) * sb->u.xbfs.blocks_per_ag
		* sb->u.xbfs.block_size
		+ (xbfs_group_bit(sb, block) >> 3);

	*chunk = sb->u.xbfs.bitmap[off >> 12];
	*byte = off & (PAGE_SIZE - 1);
}

static int xbfs_bitmap_test(struct superblock *sb, __blk_t block)
{
	unsigned char *chunk;
	__u32 byte;
	__u32 bit = xbfs_group_bit(sb, block);

	xbfs_bitmap_byte(sb, block, &chunk, &byte);
	return chunk[byte] & (1 << (bit & 7));
}

static void xbfs_bitmap_set(struct superblock *sb, __blk_t block)
{
	unsigned char *chunk;
	__u32 byte;
	__u32 bit = xbfs_group_bit(sb, block);

	xbfs_bitmap_byte(sb, block, &chunk, &byte);
	chunk[byte] |= (1 << (bit & 7));
}

static void xbfs_bitmap_clear(struct superblock *sb, __blk_t block)
{
	unsigned char *chunk;
	__u32 byte;
	__u32 bit = xbfs_group_bit(sb, block);

	xbfs_bitmap_byte(sb, block, &chunk, &byte);
	chunk[byte] &= ~(1 << (bit & 7));
}

/*
 * Allocate a free block. Returns the block number or a negative errno.
 */
int xbfs_balloc(struct superblock *sb)
{
	__u64 num_blocks = sb->u.xbfs.num_blocks;
	__u32 hint = sb->u.xbfs.next_free;
	__blk_t block;

	superblock_lock(sb);

	if(hint >= num_blocks) {
		hint = 0;
	}
	block = hint;
	do {
		if(!xbfs_bitmap_test(sb, block)) {
			xbfs_bitmap_set(sb, block);
			sb->u.xbfs.used_blocks++;
			sb->u.xbfs.next_free = block + 1;
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
int xbfs_balloc_specific(struct superblock *sb, __blk_t block)
{
	if(block >= sb->u.xbfs.num_blocks) {
		return -EINVAL;
	}

	superblock_lock(sb);
	if(xbfs_bitmap_test(sb, block)) {
		superblock_unlock(sb);
		return -EEXIST;
	}
	xbfs_bitmap_set(sb, block);
	sb->u.xbfs.used_blocks++;
	sb->state |= SUPERBLOCK_DIRTY;
	superblock_unlock(sb);
	return 0;
}

/*
 * Free a block.
 */
void xbfs_bfree(struct superblock *sb, __blk_t block)
{
	if(block >= sb->u.xbfs.num_blocks) {
		return;
	}

	superblock_lock(sb);
	if(xbfs_bitmap_test(sb, block)) {
		xbfs_bitmap_clear(sb, block);
		sb->u.xbfs.used_blocks--;
		sb->state |= SUPERBLOCK_DIRTY;
		if(sb->u.xbfs.next_free > block) {
			sb->u.xbfs.next_free = block;
		}
	}
	superblock_unlock(sb);
}
