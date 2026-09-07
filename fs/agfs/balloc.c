/*
 * fnx/fs/agfs/balloc.c
 *
 * AGFS free-space management.
 *
 * Each allocation group has a bit bitmap stored in the FIRST blocks of
 * the group, right after the superblock (Haiku's disk_super_block
 * convention: the sb's blocks_per_ag field holds the number of bitmap
 * blocks per group). Bit b of group g covers volume block
 * (g << ag_shift) + b. A set bit means the block is used. Block 0 (the
 * boot block + superblock), the bitmap blocks themselves, and the log
 * area are reserved by setting their bits.
 *
 * The whole bitmap is cached in memory (sb->u.agfs.bitmap); changes are
 * flushed by agfs_write_superblock (via sync_superblocks).
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/agfs.h>
#include <fnx/mm.h>
#include <fnx/buffer.h>
#include <fnx/devices.h>
#include <fnx/string.h>

/* block number -> (group, bit-in-group) */
static __u32 agfs_group(struct superblock *sb, __blk_t block)
{
	return block >> sb->u.agfs.ag_shift;
}

static __u32 agfs_group_bit(struct superblock *sb, __blk_t block)
{
	return block & ((1 << sb->u.agfs.ag_shift) - 1);
}

/* the bitmap byte for a block, split into its PAGE_SIZE chunk + offset */
static void agfs_bitmap_byte(struct superblock *sb, __blk_t block,
			    unsigned char **chunk, __u32 *byte)
{
	__u64 off = (__u64)agfs_group(sb, block) * sb->u.agfs.blocks_per_ag
		* sb->u.agfs.block_size
		+ (agfs_group_bit(sb, block) >> 3);

	*chunk = sb->u.agfs.bitmap[off >> 12];
	*byte = off & (PAGE_SIZE - 1);
}

static int agfs_bitmap_test(struct superblock *sb, __blk_t block)
{
	unsigned char *chunk;
	__u32 byte;
	__u32 bit = agfs_group_bit(sb, block);

	agfs_bitmap_byte(sb, block, &chunk, &byte);
	return chunk[byte] & (1 << (bit & 7));
}

static void agfs_bitmap_set(struct superblock *sb, __blk_t block)
{
	unsigned char *chunk;
	__u32 byte;
	__u32 bit = agfs_group_bit(sb, block);

	agfs_bitmap_byte(sb, block, &chunk, &byte);
	chunk[byte] |= (1 << (bit & 7));
}

static void agfs_bitmap_clear(struct superblock *sb, __blk_t block)
{
	unsigned char *chunk;
	__u32 byte;
	__u32 bit = agfs_group_bit(sb, block);

	agfs_bitmap_byte(sb, block, &chunk, &byte);
	chunk[byte] &= ~(1 << (bit & 7));
}

/*
 * Allocate a free block. Returns the block number or a negative errno.
 */
int agfs_balloc(struct superblock *sb)
{
	__u64 num_blocks = sb->u.agfs.num_blocks;
	__u32 hint = sb->u.agfs.next_free;
	__blk_t block;

	superblock_lock(sb);

	if(hint >= num_blocks) {
		hint = 0;
	}
	block = hint;
	do {
		if(!agfs_bitmap_test(sb, block)) {
			agfs_bitmap_set(sb, block);
			sb->u.agfs.used_blocks++;
			sb->u.agfs.next_free = block + 1;
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
int agfs_balloc_specific(struct superblock *sb, __blk_t block)
{
	if(block >= sb->u.agfs.num_blocks) {
		return -EINVAL;
	}

	superblock_lock(sb);
	if(agfs_bitmap_test(sb, block)) {
		superblock_unlock(sb);
		return -EEXIST;
	}
	agfs_bitmap_set(sb, block);
	sb->u.agfs.used_blocks++;
	sb->state |= SUPERBLOCK_DIRTY;
	superblock_unlock(sb);
	return 0;
}

/*
 * Allocate a contiguous window of up to 'want' free blocks (X-SSD5(a)
 * allocation windows). Scans forward from the next_free hint for the
 * first free block, then takes every free block after it until 'want'
 * are taken or the volume end is reached. Returns the first block and
 * stores the number actually taken in *len (always >= 1); -ENOSPC when
 * the volume is full.
 */
int agfs_balloc_contig(struct superblock *sb, __u32 want, __u32 *len)
{
	__u64 num_blocks = sb->u.agfs.num_blocks;
	__u32 hint = sb->u.agfs.next_free;
	__blk_t first;
	__u32 got, i;

	superblock_lock(sb);

	if(hint >= num_blocks) {
		hint = 0;
	}
	first = hint;
	while(agfs_bitmap_test(sb, first)) {
		first++;
		if(first >= num_blocks) {
			first = 0;
		}
		if(first == hint) {
			/* wrapped the whole volume: nothing free */
			superblock_unlock(sb);
			return -ENOSPC;
		}
	}
	for(got = 0; got < want; got++) {
		__u64 b = (__u64)first + got;
		__u32 ag_blocks = (__u32)1 << sb->u.agfs.ag_shift;

		if(b >= num_blocks) {
			break;	/* stop at the volume end, no wrap-around */
		}
		/* a run never crosses an AG boundary: stop when the next block
		 * starts a new AG (the first block of a later AG is a real
		 * run start, not a contiguous continuation) */
		if(b > (__u64)first && (b & (ag_blocks - 1)) == 0) {
			break;
		}
		if(agfs_bitmap_test(sb, b)) {
			break;
		}
		agfs_bitmap_set(sb, b);
	}
	sb->u.agfs.used_blocks += got;
	sb->u.agfs.next_free = first + got;
	if(sb->u.agfs.next_free >= num_blocks) {
		sb->u.agfs.next_free = 0;
	}
	sb->state |= SUPERBLOCK_DIRTY;
	superblock_unlock(sb);
	*len = got;
	return (int)first;
}

/*
 * Resync sb->used_blocks from the (authoritative) bitmap. A crash between
 * the flush's bitmap write and its superblock write leaves the counter
 * behind the bitmap; called at every mount so the two never diverge for
 * more than one boot. Prints when a repair was needed.
 */
void agfs_resync_used_blocks(struct superblock *sb)
{
	__u32 count = 0;
	__blk_t block;

	for(block = 0; block < sb->u.agfs.num_blocks; block++) {
		if(agfs_bitmap_test(sb, block)) {
			count++;
		}
	}
	if(count != sb->u.agfs.used_blocks) {
		printk("AGFS: superblock used_blocks %lu resynced to bitmap count %u.\n",
		       (unsigned long)sb->u.agfs.used_blocks, count);
		sb->u.agfs.used_blocks = count;
		sb->state |= SUPERBLOCK_DIRTY;
		sb->u.agfs.flags = AGFS_SUPER_DIRTY;
	}
}

/*
 * Free a block.
 */
static void agfs_discard_add(struct superblock *sb, __blk_t block)
{
	__u32 n = sb->u.agfs.ndiscard;

	/* coalesce with the previous pending extent when adjacent */
	if(n && sb->u.agfs.d_start[n - 1] + sb->u.agfs.d_count[n - 1] == block) {
		sb->u.agfs.d_count[n - 1]++;
		return;
	}
	if(n && sb->u.agfs.d_count[n - 1] == 1) {	}
	if(n >= AGFS_NR_PENDING_DISCARD) {
		/* overflow: drop the extent (loses only the reclaim
		 * opportunity; the bitmap state is authoritative) */
		return;
	}
	sb->u.agfs.d_start[n] = block;
	sb->u.agfs.d_count[n] = 1;
	sb->u.agfs.ndiscard = n + 1;
}

void agfs_bfree(struct superblock *sb, __blk_t block)
{
	if(block >= sb->u.agfs.num_blocks) {
		return;
	}

	superblock_lock(sb);
	if(agfs_bitmap_test(sb, block)) {
		agfs_bitmap_clear(sb, block);
		sb->u.agfs.used_blocks--;
		sb->state |= SUPERBLOCK_DIRTY;
		if(sb->u.agfs.next_free > block) {
			sb->u.agfs.next_free = block;
		}
		agfs_discard_add(sb, block);
	}
	superblock_unlock(sb);
}

/*
 * Flush the pending discard extents to the block device. Only call this
 * AFTER the freed blocks' bitmap state is durable on disk (the journal
 * commit tail): a discard tells the device the blocks are unmapped, so
 * metadata that a crash could roll back must never reference them.
 *
 * Each pending extent is re-checked against the in-memory bitmap at
 * flush time and split into still-free sub-runs (a block freed and then
 * reallocated inside the same transaction window must not be trimmed),
 * then handed to the driver's discard op one extent at a time.
 */
void agfs_flush_discards(struct superblock *sb)
{
	struct device *d;
	__u32 n, i;
	__blk_t b;

	if(!sb->u.agfs.ndiscard) {
		return;
	}
	if(!(d = get_device(BLK_DEV, sb->dev))) {
		return;	/* keep the list; retry on the next commit */
	}
	if(!d->fsop->discard_blocks) {
		sb->u.agfs.ndiscard = 0;
		return;	/* no discard support on this device */
	}	for(n = 0; n < sb->u.agfs.ndiscard; n++) {
		b = sb->u.agfs.d_start[n];		while(b < sb->u.agfs.d_start[n] + sb->u.agfs.d_count[n]) {
			__blk_t run = 0;

			while(b + run < sb->u.agfs.d_start[n] + sb->u.agfs.d_count[n]
			      && !agfs_bitmap_test(sb, b + run)) {
				run++;
			}
			if(run) {				d->fsop->discard_blocks(sb->dev, b, run,
							sb->u.agfs.block_size);
				b += run;
			} else {
				b++;	/* reallocated: skip */
			}
		}
	}
	sb->u.agfs.ndiscard = 0;
	/* device-side discard is fire-and-forget; the caller's commit
	 * path holds the superblock lock */
}
