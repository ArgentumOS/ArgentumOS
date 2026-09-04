/*
 * fs/bfs/journal.c - BFS journal (log): faithful Haiku on-disk
 * log-entry format (run_array index blocks + data blocks) with
 * mount-time replay and metadata transaction logging.
 *
 * On-disk log entry (Haiku fs/bfs/Journal.cpp, run_array):
 *   one run_array block:  { s32 count; s32 max_runs (127);
 *                           block_run runs[127]; }   (1024 bytes)
 *   followed by `count` data blocks (the raw block contents, one per
 *   block run, in run order; every run has len == 1 - Be's replay can
 *   only deal with length-1 runs).
 *
 * The superblock's log_start/log_end are BLOCK offsets into the log
 * extent (Haiku runtime semantics); log_start == log_end means the log
 * is empty (clean). Replay walks log_start..log_end, writing each
 * entry's data blocks back to their real locations, then clears the
 * log.
 *
 * Transaction model (write-ahead, deferred apply):
 *   bfs_log_begin(sb)    - open a transaction (nested begin joins the
 *                          outer transaction)
 *   bfs_log_record(sb, blk, data) - record a modified block's NEW
 *                          content (a copy; the real block stays clean
 *                          in the buffer cache until commit)
 *   bfs_log_commit(sb)   - (1) write [run_array + data blocks] to the
 *                          log and sync,
 *                          (2) write the on-disk superblock with
 *                              log_end advanced + flags DIRTY, sync,
 *                          (3) apply the real blocks and sync,
 *                          (4) advance the in-memory log_end.
 *
 * A crash before (1) loses nothing (the transaction never happened);
 * between (1) and (3) the log covers the transaction and mount-time
 * replay repairs the real blocks; after (3) the transaction is fully
 * on disk.
 *
 * Metadata write sites use bfs_log_write_block(sb, blk, buf): inside a
 * transaction it records the block (deferred); otherwise it writes
 * through directly.
 */

#include <fnx/fs.h>
#include <fnx/buffer.h>
#include <fnx/bfs.h>
#include <fnx/mm.h>
#include <fnx/string.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>

/* absolute disk block of a block run */
__blk_t bfs_log_run_abs(struct superblock *sb,
			struct bfs_block_run *run)
{
	return ((__blk_t)run->allocation_group << sb->u.bfs.ag_shift)
		+ run->start;
}

/* the journal's transaction lock (a struct resource stored raw in the
 * superblock to avoid a header cycle; see bfs.h) */
static struct resource *bfs_log_resource(struct superblock *sb)
{
	return (struct resource *)&sb->u.bfs.journal_locked;
}

/* exported for the umount drain (bfs_write_superblock), which must not
 * race an in-flight commit writing the same superblock block */
void bfs_log_lock(struct superblock *sb)
{
	lock_resource(bfs_log_resource(sb));
}

void bfs_log_unlock(struct superblock *sb)
{
	unlock_resource(bfs_log_resource(sb));
}

/* block offset within the log extent of the run_array block of the
 * entry at log position `pos` (the data blocks follow at pos+1..) */
static __blk_t bfs_log_block(struct superblock *sb, __u64 pos)
{
	return bfs_log_run_abs(sb, &sb->u.bfs.log_blocks) + pos;
}

/* the log extent, in blocks */
static __u64 bfs_log_len(struct superblock *sb)
{
	return sb->u.bfs.log_blocks.len;
}

/*
 * Replay any uncommitted transactions in the log. Called from
 * bfs_read_superblock() right after the superblock + bitmap are
 * loaded, before the fs becomes usable. Returns the number of blocks
 * replayed (0 if the log was empty).
 */
int bfs_log_replay(struct superblock *sb)
{
	__u64 log_size = bfs_log_len(sb);
	__u64 pos = sb->u.bfs.log_start;
	struct buffer *buf;
	int replayed = 0;

	if(!log_size || pos >= log_size) {
		return 0;
	}
	if(sb->u.bfs.log_start == sb->u.bfs.log_end) {
		/* empty log: nothing to replay */
		return 0;
	}

	printk("BFS-LOG: replaying transactions (%lu blocks in log)...\n",
	       (unsigned long)(sb->u.bfs.log_end - sb->u.bfs.log_start));

	while(pos < sb->u.bfs.log_end && pos < log_size) {
		struct bfs_run_array *array;
		int i;

		if(!(buf = bread(sb->dev, bfs_log_block(sb, pos), sb->u.bfs.block_size))) {
			printk("WARNING: %s(): I/O error reading log at block %lu.\n",
			       __FUNCTION__, (unsigned long)pos);
			/* do NOT clear the log: the un-replayed tail must
			 * survive for the next mount */
			return -EIO;
		}
		array = (struct bfs_run_array *)buf->data;

		/* a fully-zeroed block (stale log_end pointing past the last
		 * entry, or a crash between the wrap's entry write and its
		 * super write) means the log ends here */
		if(array->count == 0 && array->max_runs == 0) {
			brelse(buf);
			break;
		}

		/* validate the run array (Haiku's _CheckRunArray: count in
		 * 1..max_runs-1 to work around Be's off-by-one) */
		if(array->count < 1 || array->count > BFS_LOG_MAX_RUNS - 1 ||
		   array->max_runs != BFS_LOG_MAX_RUNS ||
		   pos + 1 + array->count > log_size) {
			brelse(buf);
			printk("WARNING: %s(): bad log entry at block %lu (count %d).\n",
			       __FUNCTION__, (unsigned long)pos, array->count);
			return -EIO;
		}

		/* write the data blocks back to their real locations */
		for(i = 0; i < array->count; i++) {
			struct bfs_block_run *run = &array->runs[i];
			struct buffer *rb;
			struct buffer *db;
			__blk_t real = bfs_log_run_abs(sb, run);
			__blk_t dbuf_blk = bfs_log_block(sb, pos + 1 + i);

			if(run->len != 1) {
				printk("WARNING: %s(): run %d has len %d.\n",
				       __FUNCTION__, i, run->len);
				continue;
			}
			if(real >= sb->u.bfs.num_blocks) {
				printk("WARNING: %s(): log run %d targets block %lu out of range.\n",
				       __FUNCTION__, i, (unsigned long)real);
				brelse(buf);
				return -EIO;
			}
			if(!(db = bread(sb->dev, dbuf_blk, sb->u.bfs.block_size))) {
				brelse(buf);
				return -EIO;
			}
			if(!(rb = bread(sb->dev, real, sb->u.bfs.block_size))) {
				brelse(db);
				brelse(buf);
				return -EIO;
			}
			memcpy_b(rb->data, db->data, sb->u.bfs.block_size);
			bwrite(rb);
			brelse(db);
			replayed++;
		}
		brelse(buf);
		pos += 1 + array->count;
	}

	/* the restored blocks must be on disk before the log is cleared:
	 * otherwise a crash right after the drain (which writes log
	 * positions 0) would lose the replayed transaction */
	sync_buffers(sb->dev);

	/* the walk completed: the log is now empty — clear it and mark
	 * the superblock dirty so the replayed blocks + the cleared log
	 * are persisted */
	sb->u.bfs.log_start = 0;
	sb->u.bfs.log_end = 0;
	sb->u.bfs.flags = BFS_SUPER_CLEAN;
	sb->state |= SUPERBLOCK_DIRTY;

	printk("BFS-LOG: replay done, %d block(s) restored.\n", replayed);
	return replayed;
}

/*
 * Open a transaction. Nested begins join the outer transaction; only
 * the outermost bfs_log_commit() writes anything to the log.
 */
int bfs_log_begin(struct superblock *sb)
{
	if(sb->u.bfs.log_flushing) {
		/* the log reset's sync_buffers() is running write-backs:
		 * they must NOT take the journal lock (the committing
		 * outer transaction holds it and is blocked in that very
		 * sync — taking the lock would deadlock), nor record;
		 * everything below writes straight through */
		sb->u.bfs.tx_depth++;
		return 0;
	}
	if(sb->u.bfs.tx_depth == 0) {
		/* outermost transaction: take the journal lock so no other
		 * context can interleave a transaction on this superblock
		 * while we own the tx state (the commit sleeps on I/O) */
		lock_resource(bfs_log_resource(sb));
		sb->u.bfs.tx_nblocks = 0;
	}
	sb->u.bfs.tx_depth++;
	return 0;
}

/*
 * Record a modified block into the current transaction (a copy of its
 * NEW content). The real block is applied only at bfs_log_commit().
 * Returns -ENOSPC if the transaction would not fit in the log.
 */
int bfs_log_record(struct superblock *sb, __blk_t blk, unsigned char *data)
{
	int i;

	if(sb->u.bfs.tx_depth == 0) {
		/* not in a transaction: nothing to record */
		return 0;
	}
	/* dedupe: a block modified twice records its final content once */
	for(i = 0; i < sb->u.bfs.tx_nblocks; i++) {
		if(sb->u.bfs.tx_blocks[i] == blk) {
			memcpy_b(sb->u.bfs.tx_data[i], data, sb->u.bfs.block_size);
			return 0;
		}
	}
	if(sb->u.bfs.tx_nblocks >= BFS_LOG_MAX_BLOCKS) {
		return -ENOSPC;
	}
	/* kmalloc a scratch copy (the caller's buffer may be reused) */
	if(!(sb->u.bfs.tx_data[sb->u.bfs.tx_nblocks] =
			(unsigned char *)kmalloc(sb->u.bfs.block_size))) {
		return -ENOMEM;
	}
	memcpy_b(sb->u.bfs.tx_data[sb->u.bfs.tx_nblocks], data, sb->u.bfs.block_size);
	sb->u.bfs.tx_blocks[sb->u.bfs.tx_nblocks] = blk;
	sb->u.bfs.tx_nblocks++;
	return 0;
}

/* free the transaction's recorded block copies */
static void bfs_log_free_tx(struct superblock *sb)
{
	while(sb->u.bfs.tx_nblocks > 0) {
		sb->u.bfs.tx_nblocks--;
		kfree((addr_t)sb->u.bfs.tx_data[sb->u.bfs.tx_nblocks]);
		sb->u.bfs.tx_data[sb->u.bfs.tx_nblocks] = NULL;
	}
}

/*
 * Write the on-disk superblock area (block 0, offset 512) with the
 * current journal positions and flags. Lighter than bfs_write_superblock
 * (no bitmap flush); used by the commit path to persist log_end before
 * the real blocks are written.
 */
/*
 * Persist the free-space bitmap (sb->u.bfs.bitmap, bitmap_blocks blocks)
 * to disk. The journal only covers the tree/inode blocks it records, so
 * the bitmap — which every transaction mutates via bfs_balloc/bfree —
 * must hit disk together with the on-disk superblock BEFORE the real
 * blocks of a transaction: after a crash + replay the on-disk bitmap is
 * then at least as new as the replayed metadata (otherwise the next
 * allocation could reuse a block the replayed metadata references).
 */
static int bfs_log_write_bitmap(struct superblock *sb)
{
	struct buffer *bb;
	__u32 i;

	for(i = 0; i < sb->u.bfs.bitmap_blocks; i++) {
		__u32 chunk = (i * sb->u.bfs.block_size) >> 12;
		__u32 coff = (i * sb->u.bfs.block_size) & (PAGE_SIZE - 1);
		if(!(bb = bread(sb->dev, 1 + i, sb->u.bfs.block_size))) {
			return -EIO;
		}
		memcpy_b(bb->data, sb->u.bfs.bitmap[chunk] + coff,
			 sb->u.bfs.block_size);
		bwrite(bb);
	}
	return 0;
}

static int bfs_log_write_super(struct superblock *sb)
{
	struct buffer *buf;
	struct bfs_superblock *bsb;

	if(!(buf = bread(sb->dev, 0, sb->u.bfs.block_size))) {
		return -EIO;
	}
	bsb = (struct bfs_superblock *)(buf->data + 512);
	bsb->log_blocks = sb->u.bfs.log_blocks;
	bsb->log_start = sb->u.bfs.log_start;
	bsb->log_end = sb->u.bfs.log_end;
	bsb->flags = sb->u.bfs.flags;
	bwrite(buf);
	sync_buffers(sb->dev);
	/* the on-disk superblock now holds new log positions (a pending
	 * transaction): mark the in-memory sb dirty so the next
	 * sync_superblocks() drains the log (power-off / sync). Without
	 * this the power-off drain runs BEFORE the last inode flushes,
	 * which re-populate the log, and the final sync skips it. */
	sb->state |= SUPERBLOCK_DIRTY;
	return 0;
}

/*
 * Commit the open transaction. See the file header for the write-ahead
 * ordering (log entry, then on-disk superblock with log_end advanced,
 * then the real blocks).
 */
int bfs_log_commit(struct superblock *sb)
{
	struct buffer *buf;
	struct bfs_run_array *array;
	__u64 log_size, entry_pos;
	int n, i;

	if(sb->u.bfs.tx_depth <= 0) {
		return 0;
	}
	if(--sb->u.bfs.tx_depth > 0) {
		/* nested: the outermost commit does the work */
		return 0;
	}
	n = sb->u.bfs.tx_nblocks;
	if(sb->u.bfs.log_flushing) {
		/* a write-back tx that ran during the reset: nothing was
		 * recorded (all writes went through); do not touch the
		 * journal lock — the reset's outer tx owns it */
		bfs_log_free_tx(sb);
		return 0;
	}
	if(n == 0) {
		bfs_log_free_tx(sb);
		unlock_resource(bfs_log_resource(sb));
		return 0;
	}

	log_size = bfs_log_len(sb);
	if((__u64)(n + 1) > log_size) {
		/* the transaction cannot fit in the log at all: apply the
		 * recorded blocks directly (write-through) instead of
		 * dropping them, so the deferred writes are not lost */
		__blk_t *bp = sb->u.bfs.tx_blocks;
		unsigned char **dp = sb->u.bfs.tx_data;
		printk("WARNING: %s(): transaction (%d blocks) larger than the log (%lu blocks); writing through without journaling.\n",
		       __FUNCTION__, n, (unsigned long)log_size);
		sb->u.bfs.flags = BFS_SUPER_DIRTY;
		bfs_log_write_bitmap(sb);
		bfs_log_write_super(sb);
		for(i = 0; i < n; i++, bp++, dp++) {
			if(!(buf = bread(sb->dev, *bp, sb->u.bfs.block_size))) {
				continue;
			}
			memcpy_b(buf->data, *dp, sb->u.bfs.block_size);
			bwrite(buf);
		}
		sync_buffers(sb->dev);
		bfs_log_free_tx(sb);
		unlock_resource(bfs_log_resource(sb));
		return 0;
	}

	if((__u64)(sb->u.bfs.log_end + n + 1) > log_size) {
		/* log full: everything before is already applied and
		 * synced, so flush it all, zero the log and restart it at
		 * 0. The on-disk superblock MUST be written with the new
		 * positions (0) and synced BEFORE the new entry lands over
		 * the old blocks: otherwise a crash between the entry write
		 * and commit step (2) would make replay walk the stale
		 * on-disk log_end into the new entry's data blocks. */
		printk("BFS-LOG: log full, resetting.\n");
		sb->u.bfs.log_flushing = 1;
		sync_buffers(sb->dev);
		sb->u.bfs.log_flushing = 0;
		/* CRASH-ATOMICITY: publish the empty log positions (0,0) on
		 * disk BEFORE zeroing the extent. If the guest is killed
		 * between the two, the stale on-disk log_end would make the
		 * next mount's replay walk the just-zeroed blocks as
		 * run_arrays and restore garbage over real blocks (observed
		 * as intermittent on-disk corruption, e.g. /tmp's inode
		 * clobbered by another file's inode). With the superblock
		 * written first, a kill anywhere after this point leaves an
		 * empty log on disk (no replay), and a kill before it
		 * leaves the untouched old entries, which re-replay
		 * idempotently (the sync above already applied them). */
		sb->u.bfs.log_start = 0;
		sb->u.bfs.log_end = 0;
		bfs_log_write_super(sb);
			/* positions 0 are on disk before the log is cleared */
		for(i = 0; i < sb->u.bfs.log_blocks.len; i++) {
			if((buf = bread(sb->dev,
					bfs_log_run_abs(sb, &sb->u.bfs.log_blocks) + i,
					sb->u.bfs.block_size))) {
				memset_b(buf->data, 0, sb->u.bfs.block_size);
				bwrite(buf);
			}
		}
	}

	entry_pos = sb->u.bfs.log_end;

	/* (1) write the run_array block + the data blocks to the log.
	 * NOTE: the loops below deliberately walk POINTERS (bp/dp) rather
	 * than indexing tx_blocks[i]/tx_data[i] — gcc -O2 miscompiles the
	 * indexed forms here (a recurring FNX -O2 bounds miscompile),
	 * shifting the induction variable to 1..n and reading/writing
	 * one past the arrays. */
	if(!(buf = bread(sb->dev, bfs_log_block(sb, entry_pos), sb->u.bfs.block_size))) {
		bfs_log_free_tx(sb);
		sb->u.bfs.tx_nblocks = 0;
		unlock_resource(bfs_log_resource(sb));
		return -EIO;
	}
	memset_b(buf->data, 0, sb->u.bfs.block_size);
	array = (struct bfs_run_array *)buf->data;
	array->count = n;
	array->max_runs = BFS_LOG_MAX_RUNS;
	{
		__blk_t *bp = sb->u.bfs.tx_blocks;
		struct bfs_block_run *rp = array->runs;
		for(i = 0; i < n; i++, bp++, rp++) {
			rp->allocation_group = (__u32)(*bp >> sb->u.bfs.ag_shift);
			rp->start = *bp & ((1 << sb->u.bfs.ag_shift) - 1);
			rp->len = 1;
		}
	}
	bwrite(buf);

	{
		unsigned char **dp = sb->u.bfs.tx_data;
		for(i = 0; i < n; i++, dp++) {
			if(!(buf = bread(sb->dev, bfs_log_block(sb, entry_pos + 1 + i),
					 sb->u.bfs.block_size))) {
				bfs_log_free_tx(sb);
				sb->u.bfs.tx_nblocks = 0;
				unlock_resource(bfs_log_resource(sb));
				return -EIO;
			}
			memcpy_b(buf->data, *dp, sb->u.bfs.block_size);
			bwrite(buf);
		}
	}
	sync_buffers(sb->dev);
		/* the log entry is on disk now */

	/* (2) advance log_end + write the on-disk superblock and the
	 * bitmap BEFORE the real blocks flush: a crash between (1) and
	 * (3) leaves the log covering the transaction, so replay can
	 * repair the blocks; the bitmap must be on disk too or the next
	 * allocation could reuse a replayed block */
	sb->u.bfs.log_end = entry_pos + n + 1;
	sb->u.bfs.flags = BFS_SUPER_DIRTY;
	bfs_log_write_bitmap(sb);
	bfs_log_write_super(sb);

	/* (3) apply the real blocks */
	{
		__blk_t *bp = sb->u.bfs.tx_blocks;
		unsigned char **dp = sb->u.bfs.tx_data;
		for(i = 0; i < n; i++, bp++, dp++) {
			if(!(buf = bread(sb->dev, *bp, sb->u.bfs.block_size))) {
				continue;
			}
			memcpy_b(buf->data, *dp, sb->u.bfs.block_size);
			bwrite(buf);
		}
	}
	sync_buffers(sb->dev);
		/* the transaction is complete on disk */

	bfs_log_free_tx(sb);
	unlock_resource(bfs_log_resource(sb));
	return 0;
}

/*
 * Journal-aware metadata write. Inside a transaction the block is
 * recorded (the real write happens at commit); otherwise it is written
 * through immediately. The caller passes the buffer it modified and
 * must NOT bwrite() it afterwards: both paths release it exactly once.
 */
void bfs_log_write_block(struct superblock *sb, __blk_t blk,
			 struct buffer *buf)
{
	int i;

	if(sb->u.bfs.log_draining || sb->u.bfs.log_flushing) {
		/* unmount / log reset: stop journaling, write through
		 * directly (during a reset's sync_buffers() the write-backs
		 * must not re-enter the journal — the log is mid-reset and
		 * a nested commit would recurse into another reset) */
		bwrite(buf);
		return;
	}
	if(sb->u.bfs.tx_depth > 0) {
		if(bfs_log_record(sb, blk, buf->data)) {
			/* the tx no longer fits (or kmalloc failed): abort
			 * it consistently by writing through EVERY recorded
			 * block + this one, then emptying the tx, so the
			 * metadata does not end up half-deferred */
			struct buffer *wb;
			printk("WARNING: %s(): journal tx overflow, aborting (write-through).\n",
			       __FUNCTION__);
			for(i = 0; i < sb->u.bfs.tx_nblocks; i++) {
				if(sb->u.bfs.tx_blocks[i] == blk) {
					/* this block is the buffer we are
					 * already holding — breading it
					 * would sleep on our own lock; it is
					 * written via 'buf' below */
					continue;
				}
				if(buffer_locked(sb->dev, sb->u.bfs.tx_blocks[i],
						 sb->u.bfs.block_size)) {
					/* the caller holds another buffer for
					 * this block too (e.g. a tree leaf
					 * sharing a block with the header):
					 * breading it would deadlock. The
					 * caller's own write records/flushes
					 * it after the abort. */
					continue;
				}
				if((wb = bread(sb->dev, sb->u.bfs.tx_blocks[i],
					       sb->u.bfs.block_size))) {
					memcpy_b(wb->data,
						 sb->u.bfs.tx_data[i],
						 sb->u.bfs.block_size);
					bwrite(wb);
				}
			}
			bfs_log_free_tx(sb);
			bwrite(buf);
			return;
		}
		brelse(buf);
	} else {
		bwrite(buf);
	}
}
