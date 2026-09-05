/*
 * fs/xbfs/journal.c - XBFS journal (log): faithful Haiku on-disk
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
 *   xbfs_log_begin(sb)    - open a transaction (nested begin joins the
 *                          outer transaction)
 *   xbfs_log_record(sb, blk, data) - record a modified block's NEW
 *                          content (a copy; the real block stays clean
 *                          in the buffer cache until commit)
 *   xbfs_log_commit(sb)   - (1) write [run_array + data blocks] to the
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
 * Metadata write sites use xbfs_log_write_block(sb, blk, buf): inside a
 * transaction it records the block (deferred); otherwise it writes
 * through directly.
 */

#include <fnx/fs.h>
#include <fnx/buffer.h>
#include <fnx/xbfs.h>
#include <fnx/asm.h>
#include <fnx/mm.h>
#include <fnx/string.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>

/* absolute disk block of a block run */
__blk_t xbfs_log_run_abs(struct superblock *sb,
			struct xbfs_block_run *run)
{
	return ((__blk_t)run->allocation_group << sb->u.xbfs.ag_shift)
		+ run->start;
}

/* the journal's transaction lock (a struct resource stored raw in the
 * superblock to avoid a header cycle; see xbfs.h) */
static struct resource *xbfs_log_resource(struct superblock *sb)
{
	return (struct resource *)&sb->u.xbfs.journal_locked;
}

/* exported for the umount drain (xbfs_write_superblock), which must not
 * race an in-flight commit writing the same superblock block */
void xbfs_log_lock(struct superblock *sb)
{
	lock_resource(xbfs_log_resource(sb));
}

void xbfs_log_unlock(struct superblock *sb)
{
	unlock_resource(xbfs_log_resource(sb));
}

/* block offset within the log extent of the run_array block of the
 * entry at log position `pos` (the data blocks follow at pos+1..) */
static __blk_t xbfs_log_block(struct superblock *sb, __u64 pos)
{
	return xbfs_log_run_abs(sb, &sb->u.xbfs.log_blocks) + pos;
}

/* the log extent, in blocks */
static __u64 xbfs_log_len(struct superblock *sb)
{
	return sb->u.xbfs.log_blocks.len;
}

/*
 * Replay any uncommitted transactions in the log. Called from
 * xbfs_read_superblock() right after the superblock + bitmap are
 * loaded, before the fs becomes usable. Returns the number of blocks
 * replayed (0 if the log was empty).
 */
int xbfs_log_replay(struct superblock *sb)
{
	__u64 log_size = xbfs_log_len(sb);
	__u64 pos = sb->u.xbfs.log_start;
	struct buffer *buf;
	int replayed = 0;

	if(!log_size || pos >= log_size) {
		return 0;
	}
	if(sb->u.xbfs.log_start == sb->u.xbfs.log_end) {
		/* empty log: nothing to replay */
		return 0;
	}

	printk("XBFS-LOG: replaying transactions (%lu blocks in log)...\n",
	       (unsigned long)(sb->u.xbfs.log_end - sb->u.xbfs.log_start));

	while(pos < sb->u.xbfs.log_end && pos < log_size) {
		struct xbfs_run_array *array;
		int i;

		if(!(buf = bread(sb->dev, xbfs_log_block(sb, pos), sb->u.xbfs.block_size))) {
			printk("WARNING: %s(): I/O error reading log at block %lu.\n",
			       __FUNCTION__, (unsigned long)pos);
			/* do NOT clear the log: the un-replayed tail must
			 * survive for the next mount */
			return -EIO;
		}
		array = (struct xbfs_run_array *)buf->data;

		/* a fully-zeroed block (stale log_end pointing past the last
		 * entry, or a crash between the wrap's entry write and its
		 * super write) means the log ends here */
		if(array->count == 0 && array->max_runs == 0) {
			brelse(buf);
			break;
		}

		/* validate the run array (Haiku's _CheckRunArray: count in
		 * 1..max_runs-1 to work around Be's off-by-one) */
		if(array->count < 1 || array->count > XBFS_LOG_MAX_RUNS - 1 ||
		   array->max_runs != XBFS_LOG_MAX_RUNS ||
		   pos + 1 + array->count > log_size) {
			brelse(buf);
			printk("WARNING: %s(): bad log entry at block %lu (count %d).\n",
			       __FUNCTION__, (unsigned long)pos, array->count);
			return -EIO;
		}

		/* write the data blocks back to their real locations */
		for(i = 0; i < array->count; i++) {
			struct xbfs_block_run *run = &array->runs[i];
			struct buffer *rb;
			struct buffer *db;
			__blk_t real = xbfs_log_run_abs(sb, run);
			__blk_t dbuf_blk = xbfs_log_block(sb, pos + 1 + i);

			if(run->len != 1) {
				printk("WARNING: %s(): run %d has len %d.\n",
				       __FUNCTION__, i, run->len);
				continue;
			}
			if(real >= sb->u.xbfs.num_blocks) {
				printk("WARNING: %s(): log run %d targets block %lu out of range.\n",
				       __FUNCTION__, i, (unsigned long)real);
				brelse(buf);
				return -EIO;
			}
			if(!(db = bread(sb->dev, dbuf_blk, sb->u.xbfs.block_size))) {
				brelse(buf);
				return -EIO;
			}
			if(!(rb = bread(sb->dev, real, sb->u.xbfs.block_size))) {
				brelse(db);
				brelse(buf);
				return -EIO;
			}
			memcpy_b(rb->data, db->data, sb->u.xbfs.block_size);
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
	sb->u.xbfs.log_start = 0;
	sb->u.xbfs.log_end = 0;
	sb->u.xbfs.flags = XBFS_SUPER_CLEAN;
	sb->state |= SUPERBLOCK_DIRTY;

	printk("XBFS-LOG: replay done, %d block(s) restored.\n", replayed);
	return replayed;
}

/*
 * Open a transaction. Nested begins join the outer transaction; only
 * the outermost xbfs_log_commit() writes anything to the log.
 */
int xbfs_log_begin(struct superblock *sb)
{
	if(sb->u.xbfs.tx_depth == 0) {
		/* outermost transaction: take the journal lock so no other
		 * context can interleave a transaction on this superblock
		 * while we own the tx state (the commit sleeps on I/O) */
		lock_resource(xbfs_log_resource(sb));
		sb->u.xbfs.tx_nblocks = 0;
	}
	sb->u.xbfs.tx_depth++;
	return 0;
}

/*
 * Record a modified block into the current transaction (a copy of its
 * NEW content). The real block is applied only at xbfs_log_commit().
 * Returns -ENOSPC if the transaction would not fit in the log.
 */
int xbfs_log_record(struct superblock *sb, __blk_t blk, unsigned char *data)
{
	int i;

	if(sb->u.xbfs.tx_depth == 0) {
		/* not in a transaction: nothing to record */
		return 0;
	}
	/* dedupe: a block modified twice records its final content once */
	for(i = 0; i < sb->u.xbfs.tx_nblocks; i++) {
		if(sb->u.xbfs.tx_blocks[i] == blk) {
			memcpy_b(sb->u.xbfs.tx_data[i], data, sb->u.xbfs.block_size);
			return 0;
		}
	}
	if(sb->u.xbfs.tx_nblocks >= XBFS_LOG_MAX_BLOCKS) {
		return -ENOSPC;
	}
	/* kmalloc a scratch copy (the caller's buffer may be reused) */
	if(!(sb->u.xbfs.tx_data[sb->u.xbfs.tx_nblocks] =
			(unsigned char *)kmalloc(sb->u.xbfs.block_size))) {
		return -ENOMEM;
	}
	memcpy_b(sb->u.xbfs.tx_data[sb->u.xbfs.tx_nblocks], data, sb->u.xbfs.block_size);
	sb->u.xbfs.tx_blocks[sb->u.xbfs.tx_nblocks] = blk;
	sb->u.xbfs.tx_nblocks++;
	return 0;
}

/* free the transaction's recorded block copies */
static void xbfs_log_free_tx(struct superblock *sb)
{
	while(sb->u.xbfs.tx_nblocks > 0) {
		sb->u.xbfs.tx_nblocks--;
		kfree((addr_t)sb->u.xbfs.tx_data[sb->u.xbfs.tx_nblocks]);
		sb->u.xbfs.tx_data[sb->u.xbfs.tx_nblocks] = NULL;
	}
}

/*
 * Write the on-disk superblock area (block 0, offset 512) with the
 * current journal positions and flags. Lighter than xbfs_write_superblock
 * (no bitmap flush); used by the commit path to persist log_end before
 * the real blocks are written.
 */
/*
 * Persist the free-space bitmap (sb->u.xbfs.bitmap, bitmap_blocks blocks)
 * to disk. The journal only covers the tree/inode blocks it records, so
 * the bitmap — which every transaction mutates via xbfs_balloc/bfree —
 * must hit disk together with the on-disk superblock BEFORE the real
 * blocks of a transaction: after a crash + replay the on-disk bitmap is
 * then at least as new as the replayed metadata (otherwise the next
 * allocation could reuse a block the replayed metadata references).
 */
static int xbfs_log_write_bitmap(struct superblock *sb)
{
	struct buffer *bb;
	__u32 i;

	for(i = 0; i < sb->u.xbfs.bitmap_blocks; i++) {
		__u32 chunk = (i * sb->u.xbfs.block_size) >> 12;
		__u32 coff = (i * sb->u.xbfs.block_size) & (PAGE_SIZE - 1);
		if(!(bb = bread(sb->dev, 1 + i, sb->u.xbfs.block_size))) {
			return -EIO;
		}
		memcpy_b(bb->data, sb->u.xbfs.bitmap[chunk] + coff,
			 sb->u.xbfs.block_size);
		bwrite(bb);
	}
	return 0;
}

static int xbfs_log_write_super(struct superblock *sb)
{
	struct buffer *buf;
	struct xbfs_superblock *bsb;

	if(!(buf = bread(sb->dev, 0, sb->u.xbfs.block_size))) {
		return -EIO;
	}
	bsb = (struct xbfs_superblock *)(buf->data + XBFS_SB_A_OFF);
	bsb->log_blocks = sb->u.xbfs.log_blocks;
	bsb->log_start = sb->u.xbfs.log_start;
	bsb->log_end = sb->u.xbfs.log_end;
	bsb->flags = sb->u.xbfs.flags;
	xbfs_sb_dual_write(sb, buf);
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
/* R-M2 crash injection (docs/bfs-journal-reclaim.md 5/8). xbfscrash=
 * STATE[,COUNT] on the boot cmdline arms a deliberate halt when the
 * journal reaches STATE for the COUNT-th time (default 1), so a harness
 * can crash the machine at a precise commit-state boundary and verify
 * the next mount's replay. States: 1 commit start; 2/5 after the log
 * entry write, contiguous/wrap; 3/6 after the range publish,
 * contiguous/wrap; 4 after the wrap orphan-publish; 7 after the commit
 * fully syncs (clean crash, nothing to replay). */
static int xbfs_crash_state;
static int xbfs_crash_count = 1;
static int xbfs_crash_seen;

void xbfs_crash_set(int state, int count)
{
	xbfs_crash_state = state;
	xbfs_crash_count = count > 0 ? count : 1;
	xbfs_crash_seen = 0;
	printk("CRASH-INJ: XBFS journal injection armed at state %d (hit %d).\n",
	       state, count);
}

static void xbfs_crash_inject(int state)
{
	if(!xbfs_crash_state || xbfs_crash_state != state) {
		return;
	}
	if(++xbfs_crash_seen < xbfs_crash_count) {
		return;
	}
	printk("CRASH-INJ: XBFS journal state %d reached (hit %d) - halting.\n",
	       state, xbfs_crash_seen);
	CLI();
	for(;;) {
		HLT();
	}
}

int xbfs_log_commit(struct superblock *sb)
{
	struct buffer *buf;
	struct xbfs_run_array *array;
	__u64 log_size, entry_pos;
	int n, i, wrapped = 0;

	if(sb->u.xbfs.tx_depth <= 0) {
		return 0;
	}
	if(--sb->u.xbfs.tx_depth > 0) {
		/* nested: the outermost commit does the work */
		return 0;
	}
	n = sb->u.xbfs.tx_nblocks;
	if(n == 0) {
		xbfs_log_free_tx(sb);
		unlock_resource(xbfs_log_resource(sb));
		return 0;
	}

	log_size = xbfs_log_len(sb);
	if((__u64)(n + 1) > log_size) {
		/* the transaction cannot fit in the log at all: apply the
		 * recorded blocks directly (write-through) instead of
		 * dropping them, so the deferred writes are not lost */
		__blk_t *bp = sb->u.xbfs.tx_blocks;
		unsigned char **dp = sb->u.xbfs.tx_data;
		printk("WARNING: %s(): transaction (%d blocks) larger than the log (%lu blocks); writing through without journaling.\n",
		       __FUNCTION__, n, (unsigned long)log_size);
		sb->u.xbfs.flags = XBFS_SUPER_DIRTY;
		xbfs_log_write_bitmap(sb);
		xbfs_log_write_super(sb);
		for(i = 0; i < n; i++, bp++, dp++) {
			if(!(buf = bread(sb->dev, *bp, sb->u.xbfs.block_size))) {
				continue;
			}
			memcpy_b(buf->data, *dp, sb->u.xbfs.block_size);
			bwrite(buf);
		}
		sync_buffers(sb->dev);
		/* write-through: the frees were applied directly + synced */
		xbfs_flush_discards(sb);
		xbfs_log_free_tx(sb);
		unlock_resource(xbfs_log_resource(sb));
		return 0;
	}

	xbfs_crash_inject(1);
	if((__u64)(sb->u.xbfs.log_end + n + 1) > log_size) {
		/* wrap: the log holds only the previous transaction's entry
		 * (commit publishes log_start = entry_pos, D2), and that
		 * entry was applied and synced by its own commit's step (3)
		 * — so the whole log is dead and the new entry can go at
		 * block 0. CRASH-ATOMICITY: publish the empty log positions
		 * (0,0) on disk BEFORE the new entry lands over the old
		 * blocks. A kill after this superblock write leaves an
		 * empty log (no replay); a kill before it leaves the old
		 * entry, which re-replays idempotently (already applied and
		 * synced). No extent zeroing or full-device sync is needed:
		 * stale entries outside the published range are never
		 * walked by replay. */
		sb->u.xbfs.log_since_reset += (n + 1);
		if(sb->u.xbfs.log_since_reset > sb->u.xbfs.log_peak) {
			sb->u.xbfs.log_peak = sb->u.xbfs.log_since_reset;
		}
		printk("XBFS-LOG: journal wrapped (entry at block 0; %lu journaled block(s) since the last wrap).\n",
		       (unsigned long)sb->u.xbfs.log_since_reset);
		sb->u.xbfs.log_since_reset = 0;
		sb->u.xbfs.log_start = 0;
		sb->u.xbfs.log_end = 0;
		xbfs_log_write_super(sb);
		xbfs_crash_inject(4);
		wrapped = 1;
	}

	entry_pos = sb->u.xbfs.log_end;

	/* (1) write the run_array block + the data blocks to the log.
	 * NOTE: the loops below deliberately walk POINTERS (bp/dp) rather
	 * than indexing tx_blocks[i]/tx_data[i] — gcc -O2 miscompiles the
	 * indexed forms here (a recurring FNX -O2 bounds miscompile),
	 * shifting the induction variable to 1..n and reading/writing
	 * one past the arrays. */
	if(!(buf = bread(sb->dev, xbfs_log_block(sb, entry_pos), sb->u.xbfs.block_size))) {
		xbfs_log_free_tx(sb);
		sb->u.xbfs.tx_nblocks = 0;
		unlock_resource(xbfs_log_resource(sb));
		return -EIO;
	}
	memset_b(buf->data, 0, sb->u.xbfs.block_size);
	array = (struct xbfs_run_array *)buf->data;
	array->count = n;
	array->max_runs = XBFS_LOG_MAX_RUNS;
	{
		__blk_t *bp = sb->u.xbfs.tx_blocks;
		struct xbfs_block_run *rp = array->runs;
		for(i = 0; i < n; i++, bp++, rp++) {
			rp->allocation_group = (__u32)(*bp >> sb->u.xbfs.ag_shift);
			rp->start = *bp & ((1 << sb->u.xbfs.ag_shift) - 1);
			rp->len = 1;
		}
	}
	bwrite(buf);

	{
		unsigned char **dp = sb->u.xbfs.tx_data;
		for(i = 0; i < n; i++, dp++) {
			if(!(buf = bread(sb->dev, xbfs_log_block(sb, entry_pos + 1 + i),
					 sb->u.xbfs.block_size))) {
				xbfs_log_free_tx(sb);
				sb->u.xbfs.tx_nblocks = 0;
				unlock_resource(xbfs_log_resource(sb));
				return -EIO;
			}
			memcpy_b(buf->data, *dp, sb->u.xbfs.block_size);
			bwrite(buf);
		}
	}
	sync_buffers(sb->dev);
		/* the log entry is on disk now */
	xbfs_crash_inject(wrapped ? 5 : 2);

	/* (2) publish the tight single-entry range [entry_pos, log_end)
	 * and write the on-disk superblock and the bitmap BEFORE the real
	 * blocks flush: a crash between (1) and (3) leaves the log
	 * covering the transaction, so replay can repair the blocks; the
	 * bitmap must be on disk too or the next allocation could reuse a
	 * replayed block. Publishing log_start here drops the previous
	 * entry (already applied + synced by its own commit) so the log
	 * never accumulates dead history — the wrap above is then the
	 * only reclaim event. */
	sb->u.xbfs.log_start = entry_pos;
	sb->u.xbfs.log_end = entry_pos + n + 1;
	sb->u.xbfs.log_since_reset += (n + 1);
	if(sb->u.xbfs.log_since_reset > sb->u.xbfs.log_peak) {
		sb->u.xbfs.log_peak = sb->u.xbfs.log_since_reset;
	}
	sb->u.xbfs.flags = XBFS_SUPER_DIRTY;
	xbfs_log_write_bitmap(sb);
	xbfs_log_write_super(sb);
	xbfs_crash_inject(wrapped ? 6 : 3);

	/* (3) apply the real blocks */
	{
		__blk_t *bp = sb->u.xbfs.tx_blocks;
		unsigned char **dp = sb->u.xbfs.tx_data;
		for(i = 0; i < n; i++, bp++, dp++) {
			if(!(buf = bread(sb->dev, *bp, sb->u.xbfs.block_size))) {
				continue;
			}
			memcpy_b(buf->data, *dp, sb->u.xbfs.block_size);
			bwrite(buf);
		}
	}
	sync_buffers(sb->dev);
		/* the transaction is complete on disk */
	xbfs_crash_inject(7);

	/* the freed blocks' bitmap was written + synced above, so the
	 * pending TRIM extents can go to the device now (X-SSD1) */
	xbfs_flush_discards(sb);

	xbfs_log_free_tx(sb);
	unlock_resource(xbfs_log_resource(sb));
	return 0;
}

/*
 * Journal-aware metadata write. Inside a transaction the block is
 * recorded (the real write happens at commit); otherwise it is written
 * through immediately. The caller passes the buffer it modified and
 * must NOT bwrite() it afterwards: both paths release it exactly once.
 */
void xbfs_log_write_block(struct superblock *sb, __blk_t blk,
			 struct buffer *buf)
{
	int i;

	if(sb->u.xbfs.log_draining) {
		/* unmount: stop journaling, write through directly */
		bwrite(buf);
		return;
	}
	if(sb->u.xbfs.tx_depth > 0) {
		if(xbfs_log_record(sb, blk, buf->data)) {
			/* the tx no longer fits (or kmalloc failed): abort
			 * it consistently by writing through EVERY recorded
			 * block + this one, then emptying the tx, so the
			 * metadata does not end up half-deferred */
			struct buffer *wb;
			printk("WARNING: %s(): journal tx overflow, aborting (write-through).\n",
			       __FUNCTION__);
			for(i = 0; i < sb->u.xbfs.tx_nblocks; i++) {
				if(sb->u.xbfs.tx_blocks[i] == blk) {
					/* this block is the buffer we are
					 * already holding — breading it
					 * would sleep on our own lock; it is
					 * written via 'buf' below */
					continue;
				}
				if(buffer_locked(sb->dev, sb->u.xbfs.tx_blocks[i],
						 sb->u.xbfs.block_size)) {
					/* the caller holds another buffer for
					 * this block too (e.g. a tree leaf
					 * sharing a block with the header):
					 * breading it would deadlock. The
					 * caller's own write records/flushes
					 * it after the abort. */
					continue;
				}
				if((wb = bread(sb->dev, sb->u.xbfs.tx_blocks[i],
					       sb->u.xbfs.block_size))) {
					memcpy_b(wb->data,
						 sb->u.xbfs.tx_data[i],
						 sb->u.xbfs.block_size);
					bwrite(wb);
				}
			}
			xbfs_log_free_tx(sb);
			bwrite(buf);
			return;
		}
		brelse(buf);
	} else {
		bwrite(buf);
	}
}
