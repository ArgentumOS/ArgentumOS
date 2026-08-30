/*
 * fnx/fs/bfs/inode.c
 *
 * BFS inode + block mapping.
 *
 * Each inode occupies a full 1024-byte block; the vfs inode number is
 * the absolute disk block number of the inode:
 *
 *	block = (allocation_group << ag_shift) + start
 *
 * The raw on-disk inode (struct bfs_inode, 256 bytes at the start of
 * the block) is cached in i->u.bfs.raw for bmap and readdir. Writes:
 * bmap(FOR_WRITING) allocates blocks (extending the last direct run or
 * appending a new one) into the cached raw inode; bfs_write_inode
 * flushes the raw inode + vfs fields to disk.
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
#include <fnx/fcntl.h>
#include <fnx/mm.h>
#include <fnx/stat.h>
#include <fnx/string.h>

extern struct fs_operations bfs_fsop;

/* single fsop for files and dirs: dispatch on the inode type */
int bfs_open(struct inode *i, struct fd *f)
{
	f->offset = 0;
	if(S_ISREG(i->i_mode) && (f->flags & O_TRUNC)) {
		bfs_truncate(i, 0);
	}
	return 0;
}

int bfs_close(struct inode *i, struct fd *f)
{
	return 0;
}

int bfs_read_inode(struct inode *i)
{
	struct buffer *buf;
	struct bfs_inode *raw;

	if(!(buf = bread(i->dev, i->inode, BFS_BLOCK_SIZE))) {
		return -EIO;
	}
	raw = (struct bfs_inode *)buf->data;
	if(raw->magic1 != BFS_INODE_MAGIC) {
		brelse(buf);
		return -EINVAL;
	}
	memcpy_b(&i->u.bfs.raw, raw, sizeof(struct bfs_inode));

	if(S_ISDIR(raw->mode) || S_ISREG(raw->mode) || S_ISLNK(raw->mode)) {
		i->fsop = &bfs_fsop;
	} else {
		/* unsupported inode type */
		brelse(buf);
		return -EINVAL;
	}
	i->i_mode = raw->mode;
	i->i_uid = raw->uid;
	i->i_gid = raw->gid;
	i->i_size = raw->u.data.size;
	/* BFS stores times as (seconds << 16); there is no access time */
	i->i_atime = raw->last_modified_time >> 16;
	i->i_ctime = raw->status_change_time >> 16;
	i->i_mtime = raw->last_modified_time >> 16;
	i->i_nlink = 1;
	i->i_blocks = 0;
	i->i_flags = 0;
	brelse(buf);
	return 0;
}

/*
 * Flush the inode to disk. The runs live in i->u.bfs.raw (updated by
 * bmap FOR_WRITING); the vfs fields (mode, uid/gid, size, times) are
 * refreshed into the raw copy before writing.
 */
int bfs_write_inode(struct inode *i)
{
	struct buffer *buf;
	struct bfs_inode *raw;
	struct bfs_data_stream *ds;

	if(!(buf = bread(i->dev, i->inode, BFS_BLOCK_SIZE))) {
		return -EIO;
	}
	raw = (struct bfs_inode *)buf->data;
	ds = &i->u.bfs.raw.u.data;

	i->u.bfs.raw.mode = i->i_mode;
	i->u.bfs.raw.uid = i->i_uid;
	i->u.bfs.raw.gid = i->i_gid;
	i->u.bfs.raw.u.data.size = i->i_size;
	i->u.bfs.raw.last_modified_time = (__u64)i->i_mtime << 16;
	i->u.bfs.raw.status_change_time = (__u64)i->i_ctime << 16;

	memcpy_b(raw, &i->u.bfs.raw, sizeof(struct bfs_inode));
	raw->magic1 = BFS_INODE_MAGIC;
	raw->flags = BFS_INODE_IN_USE;
	raw->inode_num.allocation_group = 0;
	raw->inode_num.start = i->inode;
	raw->inode_num.len = 1;
	raw->u.data.size = i->i_size;
	/* keep the runs consistent with the size */
	raw->u.data.max_direct_range = BFS_NUM_DIRECT_BLOCKS * BFS_BLOCK_SIZE;
	(void)ds;

	bwrite(buf);
	return 0;
}

/*
 * Allocate a new inode (inodes are normal blocks in BFS). Initializes
 * the inode block and sets i->inode to the new block number.
 */
int bfs_ialloc(struct inode *i, int mode)
{
	__blk_t block;
	struct buffer *buf;
	struct bfs_inode *raw;

	if((block = bfs_balloc(i->sb)) < 0) {
		return block;
	}
	if(!(buf = bread(i->sb->dev, block, BFS_BLOCK_SIZE))) {
		bfs_bfree(i->sb, block);
		return -EIO;
	}
	memset_b(buf->data, 0, BFS_BLOCK_SIZE);
	raw = (struct bfs_inode *)buf->data;
	raw->magic1 = BFS_INODE_MAGIC;
	raw->inode_num.allocation_group = 0;
	raw->inode_num.start = block;
	raw->inode_num.len = 1;
	raw->mode = mode;
	raw->flags = BFS_INODE_IN_USE;
	raw->inode_size = BFS_INODE_SIZE;
	raw->u.data.max_direct_range = BFS_NUM_DIRECT_BLOCKS * BFS_BLOCK_SIZE;
	bwrite(buf);

	i->inode = block;
	i->i_size = 0;
	i->i_blocks = 0;
	memset_b(&i->u.bfs.raw, 0, sizeof(struct bfs_inode));
	i->u.bfs.raw.mode = mode;
	i->u.bfs.raw.u.data.max_direct_range = BFS_NUM_DIRECT_BLOCKS * BFS_BLOCK_SIZE;
	i->i_atime = CURRENT_TIME;
	i->i_mtime = CURRENT_TIME;
	i->i_ctime = CURRENT_TIME;
	return 0;
}

void bfs_ifree(struct inode *i)
{
	if(!i->inode || i->inode >= i->sb->u.bfs.num_blocks) {
		return;
	}
	if(i->i_blocks) {
		invalidate_inode_pages(i);
		bfs_truncate(i, 0);
	}
	bfs_bfree(i->sb, i->inode);
}

/*
 * Map a byte offset in the data stream to a disk block.
 * FOR_READING: return the mapped block or 0 (unmapped).
 * FOR_WRITING: allocate the block - either by extending the last
 * direct run (when the next block is contiguous and free) or by
 * appending a new run. Only the 12 direct runs are supported; indirect
 * streams return -EIO.
 */
int bfs_bmap(struct inode *i, __off_t offset, int mode)
{
	struct bfs_inode *raw = &i->u.bfs.raw;
	struct bfs_data_stream *ds = &raw->u.data;
	__u32 block = (__u32)(offset >> BFS_BLOCK_SHIFT);
	__u32 ag_shift = i->sb->u.bfs.ag_shift;
	__u64 covered = 0;
	__u32 last_ag = 0, last_start = 0, last_len = 0;
	int run, nrun = -1;

	if(block >= BFS_NUM_DIRECT_BLOCKS) {
		return -EIO;	/* indirect stream not supported */
	}

	for(run = 0; run < BFS_NUM_DIRECT_BLOCKS; run++) {
		__u32 len = ds->direct[run].len;
		if(!len) {
			nrun = run;
			break;
		}
		if(block < covered + len) {
			return (int)((ds->direct[run].allocation_group << ag_shift)
					+ ds->direct[run].start + (block - covered));
		}
		covered += len;
		last_ag = ds->direct[run].allocation_group;
		last_start = ds->direct[run].start;
		last_len = len;
	}

	if(mode != FOR_WRITING) {
		return 0;	/* unmapped */
	}

	if(block != covered) {
		/* allocation must be sequential (write path allocates in
		 * order); a gap means an indirect stream would be needed */
		return -EIO;
	}

	/* try to extend the last run with the contiguous next block */
	if(nrun > 0 && last_len) {
		__blk_t next = (last_ag << ag_shift) + last_start + last_len;
		if(bfs_balloc_specific(i->sb, next) == 0) {
			ds->direct[nrun - 1].len++;
			return next;
		}
	}

	/* append a new run */
	if(nrun < 0) {
		return -ENOSPC;
	}
	{
		__blk_t nb = bfs_balloc(i->sb);
		if(nb < 0) {
			return nb;
		}
		ds->direct[nrun].allocation_group = (__u32)(nb >> ag_shift);
		ds->direct[nrun].start = nb & ((1 << ag_shift) - 1);
		ds->direct[nrun].len = 1;
		return nb;
	}
}

/*
 * Truncate the data stream to 'length'. Frees every block beyond the
 * new size and resets the direct runs.
 */
int bfs_truncate(struct inode *i, __off_t length)
{
	struct bfs_data_stream *ds = &i->u.bfs.raw.u.data;
	__u32 ag_shift = i->sb->u.bfs.ag_shift;
	__u64 covered = 0;
	int run;

	for(run = 0; run < BFS_NUM_DIRECT_BLOCKS; run++) {
		__u32 len = ds->direct[run].len;
		__blk_t base;

		if(!len) {
			break;
		}
		base = (ds->direct[run].allocation_group << ag_shift)
			+ ds->direct[run].start;
		if(((__u64)base << BFS_BLOCK_SHIFT) >= (__u64)length) {
			/* the whole run is beyond the new size: free it */
			__u32 n;
			for(n = 0; n < len; n++) {
				bfs_bfree(i->sb, base + n);
			}
			ds->direct[run].allocation_group = 0;
			ds->direct[run].start = 0;
			ds->direct[run].len = 0;
		} else {
			__u64 run_bytes = (__u64)len << BFS_BLOCK_SHIFT;
			__u64 run_end = covered + run_bytes;
			if(run_end > (__u64)length) {
				/* partial run: free the tail blocks */
				__u32 keep = ((__u32)length - (__u32)covered
						+ BFS_BLOCK_SIZE - 1) >> BFS_BLOCK_SHIFT;
				__u32 n;
				for(n = keep; n < len; n++) {
					bfs_bfree(i->sb, base + n);
				}
				ds->direct[run].len = keep;
			}
		}
		covered += ds->direct[run].len;
	}

	i->i_size = length;
	i->u.bfs.raw.u.data.size = length;
	i->state |= INODE_DIRTY;
	return 0;
}
