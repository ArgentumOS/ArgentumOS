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
#include <fnx/timer.h>
#include <fnx/bfs.h>
#include <fnx/buffer.h>
#include <fnx/fcntl.h>
#include <fnx/mm.h>
#include <fnx/stat.h>
#include <fnx/string.h>

extern struct fs_operations bfs_fsop;
static int bfs_indirect_bmap(struct inode *, __off_t, int);

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

/*
 * Haiku stores times as (seconds << 16) | subsecond (the low 16 bits
 * are a 65536th-of-a-second fraction). The subsecond comes from the
 * tick clock (HZ = 100), the same source gettimeofday uses.
 */
static __u16 bfs_subsecond(void)
{
	return (__u16)(((__u64)(CURRENT_TICKS % HZ) * 65536) / HZ);
}

/*
 * Set the modification / status-change times, storing the full
 * (sec << 16) | subsecond value in the in-memory raw inode (the same
 * value the last_modified index key and bfs_write_inode() use).
 */
void bfs_touch_mtime(struct inode *i)
{
	i->i_mtime = CURRENT_TIME;
	i->u.bfs.raw.last_modified_time =
		((__u64)i->i_mtime << 16) | bfs_subsecond();
}

void bfs_touch_ctime(struct inode *i)
{
	i->i_ctime = CURRENT_TIME;
	i->u.bfs.raw.status_change_time =
		((__u64)i->i_ctime << 16) | bfs_subsecond();
}

/*
 * A directory's content changed (a create/link/unlink/rename inside):
 * its size and last_modified moved, so its index entries (Haiku
 * indexes directories too) must move with them. 'old_size' is the
 * directory's i_size BEFORE the tree mutation (the caller captures it
 * before bfs_btree_insert/delete) — reading i_size here would already
 * reflect the mutation, making the size index resize a no-op.
 */
void bfs_dir_touch(struct inode *dir, __off_t old_size)
{
	__u64 old_mtime = dir->u.bfs.raw.last_modified_time;

	bfs_touch_mtime(dir);
	bfs_touch_ctime(dir);
	bfs_index_resize(dir->sb, dir, old_size, old_mtime);
}

int bfs_read_inode(struct inode *i)
{
	struct buffer *buf;
	struct bfs_inode *raw;
	__u32 tail;

	if(!(buf = bread(i->dev, i->inode, i->sb->s_blocksize))) {
		return -EIO;
	}
	raw = (struct bfs_inode *)buf->data;
	if(raw->magic1 != BFS_INODE_MAGIC) {
		brelse(buf);
		return -EINVAL;
	}
	memcpy_b(&i->u.bfs.raw, raw, sizeof(struct bfs_inode));
	/* the on-disk small_data tail is block_size - inode; the in-memory
	 * copy is sized for the largest block, so zero the rest */
	tail = i->sb->s_blocksize - sizeof(struct bfs_inode);
	if(!i->u.bfs.small_data) {
		if(!(i->u.bfs.small_data = (unsigned char *)kmalloc(
				BFS_SMALL_DATA_SIZE))) {
			brelse(buf);
			return -ENOMEM;
		}
	}
	i->u.bfs.magic = BFS_II_MAGIC;
	memcpy_b(i->u.bfs.small_data, (char *)raw + sizeof(struct bfs_inode),
		 tail);
	memset_b(i->u.bfs.small_data + tail, 0,
		BFS_SMALL_DATA_SIZE - tail);

	if(S_ISDIR(raw->mode) || S_ISREG(raw->mode) || S_ISLNK(raw->mode)
	   || S_ISSOCK(raw->mode) || S_ISFIFO(raw->mode)) {
		/* socket/fifo nodes are data-less inodes whose type lives in
		 * i_mode (bfs_mknod) — bfs_fsop is right for them since they
		 * are never opened through the filesystem */
		i->fsop = &bfs_fsop;
	} else {
		/* unsupported inode type */
		brelse(buf);
		return -EINVAL;
	}
	i->i_mode = raw->mode;
	i->i_uid = raw->uid;
	i->i_gid = raw->gid;
	if(S_ISLNK(raw->mode)) {
		/* symlink length: pad[0] on our images; HAIKU inline
		 * symlinks store neither pad[0] nor data.size (the target is
		 * NUL-terminated text in the symlink area, data.size == 0),
		 * so scan for the NUL. Stream symlinks carry the
		 * INODE_LONG_SYMLINK flag and their length in data.size. */
		if(raw->flags & BFS_INODE_LONG_SYMLINK) {
			i->i_size = raw->u.data.size;
		} else if(raw->pad[0]) {
			i->i_size = raw->pad[0];
		} else {
			for(i->i_size = 0; i->i_size < 143
				&& raw->u.symlink[i->i_size]; i->i_size++)
				;
		}
	} else {
		i->i_size = raw->u.data.size;
	}
	/* BFS stores times as (seconds << 16); there is no access time */
	i->i_atime = raw->last_modified_time >> 16;
	i->i_ctime = raw->status_change_time >> 16;
	i->i_mtime = raw->last_modified_time >> 16;
	i->i_nlink = 1;
	/* gate for bfs_ifree()'s truncate-on-unlink: a file/dir/stream-
	 * symlink with a nonzero size has allocated stream blocks and must
	 * be truncated when unlinked (i_blocks == 0 would leak them); an
	 * INLINE symlink has no stream, and bfs_truncate() would misread
	 * the target text as run descriptors, so it must stay 0 */
	if(S_ISLNK(i->i_mode)) {
		i->i_blocks = (i->i_size > 143) ? ((i->i_size + 511) >> 9) : 0;
	} else {
		i->i_blocks = (i->i_size + 511) >> 9;
	}
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

	if(!(buf = bread(i->dev, i->inode, i->sb->s_blocksize))) {
		return -EIO;
	}
	raw = (struct bfs_inode *)buf->data;
	ds = &i->u.bfs.raw.u.data;
	if(!i->u.bfs.small_data) {
		/* defensive: a dirty inode must always be writable. Every
		 * creation path (ialloc, read_inode) allocates the tail, but
		 * recycled inode structs can carry a NULL here (get_free_inode
		 * zeroes the union) - allocate rather than memcpy from NULL. */
		printk("BWI-NULLSD: ino %d dirty %x\n", i->inode, i->state);
		if(!(i->u.bfs.small_data = (unsigned char *)kmalloc(
				BFS_SMALL_DATA_SIZE))) {
			brelse(buf);
			return -ENOMEM;
		}
		memset_b(i->u.bfs.small_data, 0, BFS_SMALL_DATA_SIZE);
	}

	/* the VFS owns the low 16 bits (type + permissions); the high 16
	 * bits carry Haiku's extended mode bits (S_ATTR, S_ATTR_DIR,
	 * S_STR_INDEX, ...) which a 16-bit i_mode cannot hold — preserve
	 * them so rewriting a Haiku attribute/index inode keeps them */
	i->u.bfs.raw.mode = (i->u.bfs.raw.mode & 0xFFFF0000u)
		| (i->i_mode & 0xFFFFu);
	i->u.bfs.raw.uid = i->i_uid;
	i->u.bfs.raw.gid = i->i_gid;
	if(S_ISLNK(i->i_mode)) {
		/* the symlink length lives in pad[0]; u.data.size aliases
		 * symlink[136..143], so writing it for an inline symlink
		 * would corrupt the last bytes of targets >= 136 chars */
		i->u.bfs.raw.pad[0] = i->i_size;
		if(i->i_size > 143) {
			i->u.bfs.raw.u.data.size = i->i_size;
		}
	} else {
		i->u.bfs.raw.u.data.size = i->i_size;
	}
	{
		__u64 lm = i->u.bfs.raw.last_modified_time;
		__u64 sc = i->u.bfs.raw.status_change_time;
		/* the raw fields already carry (sec << 16) | subsecond from
		 * the touch; only rewrite when the seconds moved directly
		 * (e.g. utimensat), in which case there is no subsecond */
		i->u.bfs.raw.last_modified_time =
			((lm >> 16) == (__u64)i->i_mtime)
			? ((__u64)i->i_mtime << 16) | (lm & 0xFFFF)
			: ((__u64)i->i_mtime << 16);
		i->u.bfs.raw.status_change_time =
			((sc >> 16) == (__u64)i->i_ctime)
			? ((__u64)i->i_ctime << 16) | (sc & 0xFFFF)
			: ((__u64)i->i_ctime << 16);
	}
	if(i->inode >= 23 && i->inode <= 27) {
			}
	memcpy_b(raw, &i->u.bfs.raw, sizeof(struct bfs_inode));
	memcpy_b((char *)raw + sizeof(struct bfs_inode), i->u.bfs.small_data,
		 i->sb->s_blocksize - sizeof(struct bfs_inode));
	raw->magic1 = BFS_INODE_MAGIC;
	/* preserve the permanent on-disk flags (e.g. INODE_LONG_SYMLINK on
	 * Haiku-created stream symlinks) — only IN_USE is ours to manage */
	raw->flags |= BFS_INODE_IN_USE;
	bfs_run_encode(&raw->inode_num, i->inode, i->sb->u.bfs.ag_shift);
	raw->inode_num.len = 1;
	if(!(S_ISLNK(i->i_mode) && i->i_size <= 143)) {
		raw->u.data.size = i->i_size;
	}
	/* max_direct_range was already copied by the memcpy above; do NOT
	 * reset it to the full direct range here — bmap tracks the real
	 * coverage and bfs_indirect_bmap translates offsets against it */
	(void)ds;

	/* journal the inode write: inside an open transaction it joins it
	 * (e.g. a dir-entry create), otherwise it is its own transaction */
	bfs_log_begin(i->sb);
	bfs_log_write_block(i->sb, buf->block, buf);
	bfs_log_commit(i->sb);
	i->state &= ~INODE_DIRTY;
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
	if(!(buf = bread(i->sb->dev, block, i->sb->s_blocksize))) {
		bfs_bfree(i->sb, block);
		return -EIO;
	}
	memset_b(buf->data, 0, i->sb->s_blocksize);
	raw = (struct bfs_inode *)buf->data;
	raw->magic1 = BFS_INODE_MAGIC;
	bfs_run_encode(&raw->inode_num, block, i->sb->u.bfs.ag_shift);
	raw->inode_num.len = 1;
	raw->mode = mode;
	raw->flags = BFS_INODE_IN_USE;
	/* inode_size == block_size: Haiku's Inode::InitCheck requires
	 * InodeSize() == volume->InodeSize() */
	raw->inode_size = i->sb->s_blocksize;
	raw->u.data.max_direct_range = BFS_NUM_DIRECT_BLOCKS * i->sb->s_blocksize;
	bfs_log_begin(i->sb);
	bfs_log_write_block(i->sb, buf->block, buf);
	bfs_log_commit(i->sb);

	i->inode = block;
	i->i_size = 0;
	i->i_blocks = 0;
	memset_b(&i->u.bfs.raw, 0, sizeof(struct bfs_inode));
	if(!i->u.bfs.small_data) {
		if(!(i->u.bfs.small_data = (unsigned char *)kmalloc(
				BFS_SMALL_DATA_SIZE))) {
			bfs_bfree(i->sb, block);
			return -ENOMEM;
		}
	}
	i->u.bfs.magic = BFS_II_MAGIC;
	memset_b(i->u.bfs.small_data, 0, BFS_SMALL_DATA_SIZE);
	i->u.bfs.raw.mode = mode;
	/* keep the in-memory copy in sync: bfs_write_inode() rewrites the
	 * buffer from i->u.bfs.raw, which ialloc leaves zeroed */
	i->u.bfs.raw.inode_size = i->sb->s_blocksize;
	i->u.bfs.raw.u.data.max_direct_range = BFS_NUM_DIRECT_BLOCKS * i->sb->s_blocksize;
	/* creation time: Haiku encodes (seconds << 16) | subsecond */
	i->u.bfs.raw.create_time =
		((__u64)CURRENT_TIME << 16) | bfs_subsecond();
	i->i_atime = CURRENT_TIME;
	bfs_touch_mtime(i);
	bfs_touch_ctime(i);
	return 0;
}

void bfs_ifree(struct inode *i){
	if(!i->inode || i->inode >= i->sb->u.bfs.num_blocks) {
		return;
	}
	/* free any attribute inodes (tree + attribute files) first, so
	 * their blocks are not leaked when a Haiku file with attributes
	 * is unlinked */
	bfs_attr_free_all(i);
	if(i->i_blocks) {
		invalidate_inode_pages(i);
		bfs_truncate(i, 0);
	}
	bfs_bfree(i->sb, i->inode);
}

/*
 * The VFS calls this via fsop->destroy_inode when the inode leaves the
 * cache (remove_from_hash): release the kmalloc'd small_data tail. The
 * struct itself is recycled by get_free_inode(), so read_inode/ialloc
 * re-allocate on the next use.
 */
void bfs_destroy_inode(struct inode *i)
{
	/* the union may have been repurposed by another filesystem (e.g.
	 * the mount_root probe reuses one superblock struct, so a minix
	 * inode can carry i->sb->fsop == bfs_fsop); only act while the
	 * union actually holds a BFS inode */
	if(i->u.bfs.magic != BFS_II_MAGIC) {
		return;
	}
	if(i->u.bfs.small_data) {
		kfree((addr_t)i->u.bfs.small_data);
		i->u.bfs.small_data = NULL;
	}
	i->u.bfs.magic = 0;
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
	__u32 block = (__u32)(offset >> i->sb->s_blocksize_bits);
	__u32 ag_shift = i->sb->u.bfs.ag_shift;
	__u64 covered = 0;
	__u32 last_ag = 0, last_start = 0, last_len = 0;
	int run, nrun = -1;

	/* direct runs (up to BFS_NUM_DIRECT_BLOCKS runs; the runs' total
	 * coverage is tracked in max_direct_range). A run pointing at
	 * block 0 in AG 0 is a phantom (the superblock owns block 0) and
	 * is treated as empty; a run starting at block 0 of a LATER AG
	 * is a real run (the first block of that group). */
	for(run = 0; run < BFS_NUM_DIRECT_BLOCKS; run++) {
		__u32 len = ds->direct[run].len;
		if(!len || (ds->direct[run].allocation_group == 0
				&& ds->direct[run].start == 0)) {
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

	if(block < covered) {
		/* inside the direct range but not in any run: a hole. Reads
		 * see it as unmapped (zeros); a write would have no run slot
		 * to represent it (returning 0 would make the caller write
		 * into block 0) */
		if(mode == FOR_WRITING) {
			return -EIO;
		}
		return 0;
	}

	if(nrun < 0) {
		/* all 12 direct runs are used: the indirect stream covers the
		 * file beyond them (this is where reads of the indirect region
		 * must go too — returning 0 here would turn the tail of a
		 * fragmented file into zeros) */
		return bfs_indirect_bmap(i, offset, mode);
	}

	if(mode != FOR_WRITING) {
		return 0;	/* unmapped: the direct runs don't reach here */
	}

	/* Allocate file blocks [covered, block]. BFS runs are positional
	 * (run i covers the file range after runs 0..i-1), so a sparse
	 * write past a hole must materialize the hole: every intermediate
	 * block gets allocated and the last run is extended, or a new run
	 * is appended, block by block. Returns the disk block for the
	 * requested file block ('block').
	 */
	while(block >= covered) {
		__blk_t nb;

		/* try to extend the last direct run with the contiguous next block */
		if(nrun > 0 && last_len) {
			nb = (last_ag << ag_shift) + last_start + last_len;
			if(bfs_balloc_specific(i->sb, nb) == 0) {
				ds->direct[nrun - 1].len++;
				ds->max_direct_range = (covered + 1) << i->sb->s_blocksize_bits;
				last_len++;
				covered++;
				if(block < covered) {
					return nb;
				}
				continue;
			}
		}
		if(nrun >= BFS_NUM_DIRECT_BLOCKS) {
			/* all 12 direct runs are used: go indirect */
			return bfs_indirect_bmap(i, offset, mode);
		}
		/* append a new direct run (or start the first one) */
		nb = bfs_balloc(i->sb);
		if(nb < 0) {
			return nb;
		}
		ds->direct[nrun].allocation_group = (__u32)(nb >> ag_shift);
		ds->direct[nrun].start = nb & ((1 << ag_shift) - 1);
		ds->direct[nrun].len = 1;
		ds->max_direct_range = (covered + 1) << i->sb->s_blocksize_bits;
		last_ag = ds->direct[nrun].allocation_group;
		last_start = ds->direct[nrun].start;
		last_len = 1;
		nrun++;
		covered++;
		if(block < covered) {
			return nb;
		}
	}
	return 0;	/* unreachable */
}

/*
 * Indirect streams: the data_stream.indirect run points at a table of
 * block runs (each table block holds 128 runs at 1KB blocks); the table
 * runs address the file data beyond the direct runs. When the first table
 * run cannot be extended contiguously (the stream is fragmented), further
 * table blocks are recorded in the double_indirect run: each
 * double_indirect block holds 128 __blk_t disk-block addresses of table
 * blocks.
 */
static __blk_t bfs_indirect_table_block(struct inode *i, __u32 t)
{
	struct bfs_data_stream *ds = &i->u.bfs.raw.u.data;
	__u32 ag_shift = i->sb->u.bfs.ag_shift;
	__u32 darray = i->sb->s_blocksize / sizeof(__blk_t);
	struct buffer *dbuf;

	if(t < ds->indirect.len) {
		return (ds->indirect.allocation_group << ag_shift)
			+ ds->indirect.start + t;
	}
	/* beyond the first table run: through the double-indirect table
	 * (each double block holds s_blocksize/sizeof(__blk_t) addresses) */
	t -= ds->indirect.len;
	if(!(dbuf = bread(i->dev, (ds->double_indirect.allocation_group
			<< ag_shift) + ds->double_indirect.start
			+ (t / darray), i->sb->s_blocksize))) {
		return 0;
	}
	{
		__blk_t blk = ((__blk_t *)dbuf->data)[t % darray];
		brelse(dbuf);
		return blk;
	}
}

static int bfs_indirect_bmap(struct inode *i, __off_t offset, int mode)
{
	struct bfs_inode *raw = &i->u.bfs.raw;
	struct bfs_data_stream *ds = &raw->u.data;
	__u32 block = (__u32)(offset >> i->sb->s_blocksize_bits);
	__u32 ag_shift = i->sb->u.bfs.ag_shift;
	__u32 arraylen = i->sb->s_blocksize / sizeof(struct bfs_block_run);
	__u64 covered = 0;
	__u32 table_len = !ds->indirect.len ? 0
		: ds->indirect.len + ds->double_indirect.len
			* (i->sb->s_blocksize / sizeof(__blk_t));
	struct buffer *buf = NULL;
	int t, j;

	block -= (__u32)(ds->max_direct_range >> i->sb->s_blocksize_bits);

	/* walk the indirect table looking for the run covering 'block' */
	for(t = 0; t < table_len; t++) {
		struct bfs_block_run *runs;
		__blk_t tbl = bfs_indirect_table_block(i, t);

		if(!tbl) {
			/* the table ends here: a double-indirect entry that
			 * was never set. Reads see the stream as unmapped;
			 * writes grow the table at this slot (the phantom
			 * table_len counts dind.len*256 slots, most of which
			 * are empty — the walk must not treat them as
			 * errors) */
			if(mode != FOR_WRITING) {
				return 0;
			}
			break;
		}
		if(!(buf = bread(i->dev, tbl, i->sb->s_blocksize))) {
			return -EIO;
		}
		runs = (struct bfs_block_run *)buf->data;
		for(j = 0; j < arraylen; j++) {
			__u32 len = runs[j].len;
			if(!len || (runs[j].allocation_group == 0
					&& runs[j].start == 0)) {
				/* free slot: only meaningful for writes */
				if(mode == FOR_WRITING) {
					__blk_t nb;

					if(block > covered) {
						/* sparse write into the indirect region: the
						 * table walk can't materialize the hole across
						 * table-block boundaries; reject loudly rather
						 * than map the wrong file block (the direct
						 * path handles sparse writes up to 12 runs) */
						brelse(buf);
						return -EIO;
					}
					/* try to extend the previous run */
					if(j > 0) {
						struct bfs_block_run *pr = &runs[j - 1];
						__blk_t next = (pr->allocation_group << ag_shift)
							+ pr->start + pr->len;
						if(bfs_balloc_specific(i->sb, next) == 0) {
							pr->len++;
							bwrite(buf);	/* persist the table block */
							return next;
						}
					}
					if((nb = bfs_balloc(i->sb)) < 0) {
						brelse(buf);
						return nb;
					}
					runs[j].allocation_group = (__u32)(nb >> ag_shift);
					runs[j].start = nb & ((1 << ag_shift) - 1);
					runs[j].len = 1;
					bwrite(buf);	/* persist the table block */
					return nb;
				}
				brelse(buf);
				return 0;
			}
			if(block < covered + len) {
				__blk_t nb = (runs[j].allocation_group << ag_shift)
					+ runs[j].start + (block - covered);
				brelse(buf);
				return nb;
			}
			covered += len;
		}
		brelse(buf);
	}

	if(mode != FOR_WRITING) {
		return 0;	/* unmapped */
	}

	/* the table is full (or does not exist yet): grow it by one block */
	{
		__blk_t next;
		struct buffer *zbuf;
		__u32 new_slot;	/* the table slot that receives the new block */

		if(table_len == 0) {
			/* first indirect block: allocate a fresh block for the
			 * table (there is no existing run to extend) */
			if((next = bfs_balloc(i->sb)) < 0) {
				return next;
			}
			ds->indirect.allocation_group = (__u32)(next >> ag_shift);
			ds->indirect.start = next & ((1 << ag_shift) - 1);
			ds->indirect.len = 1;
			new_slot = 0;
		} else if(ds->double_indirect.len == 0
				&& bfs_balloc_specific(i->sb,
					(ds->indirect.allocation_group
						<< ag_shift)
					+ ds->indirect.start
					+ ds->indirect.len) == 0) {
			/* extend the indirect run with the contiguous next block.
			 * Only valid while the double table is empty: extending
			 * past existing double entries would renumber them
			 * (slot indirect.len already maps through the double
			 * table) and orphan them */
			ds->indirect.len++;
			new_slot = ds->indirect.len - 1;
		} else {
			/* the first table run is fragmented: record further table
			 * blocks in the double-indirect table. The new table block
			 * goes at slot 't' (the first unset double entry — the
			 * walk above broke there; when every table_len slot is
			 * set, t == table_len and the double table itself must
			 * grow) */
			__u32 darray = i->sb->s_blocksize / sizeof(__blk_t);
			__u32 dslot = t - ds->indirect.len;
			struct buffer *dbuf;

			new_slot = t;
			if(dslot >= ds->double_indirect.len * darray) {
				/* the double table is full: grow it (contiguous
				 * only — a fragmented double table is not worth
				 * a third level) */
				if(ds->double_indirect.len == 0) {
					if((next = bfs_balloc(i->sb)) < 0) {
						return next;
					}
					ds->double_indirect.allocation_group =
						(__u32)(next >> ag_shift);
					ds->double_indirect.start =
						next & ((1 << ag_shift) - 1);
					ds->double_indirect.len = 1;
				} else if(bfs_balloc_specific(i->sb,
						(ds->double_indirect.allocation_group
							<< ag_shift)
						+ ds->double_indirect.start
						+ ds->double_indirect.len) == 0) {
					ds->double_indirect.len++;
				} else {
					return -ENOSPC;
				}
				if(!(zbuf = bread(i->dev,
						(ds->double_indirect.allocation_group
							<< ag_shift)
						+ ds->double_indirect.start
						+ ds->double_indirect.len - 1,
						i->sb->s_blocksize))) {
					return -EIO;
				}
				memset_b(zbuf->data, 0, i->sb->s_blocksize);
				bwrite(zbuf);
			}
			/* allocate a fresh table block and record its address in
			 * the double table */
			if((next = bfs_balloc(i->sb)) < 0) {
				return next;
			}
			if(!(dbuf = bread(i->dev,
					(ds->double_indirect.allocation_group
						<< ag_shift)
					+ ds->double_indirect.start
					+ (dslot / darray),
					i->sb->s_blocksize))) {
				return -EIO;
			}
			((__blk_t *)dbuf->data)[dslot % darray] = next;
			bwrite(dbuf);
		}
		/* zero the new table block so free slots read len == 0 (the
		 * block may have been reused and hold stale run data). The
		 * slot depends on the branch above: 0 for the first table
		 * block, indirect.len-1 for a contiguous extension, 't' for
		 * a double-table block — never the phantom table_len */
		if(!(zbuf = bread(i->dev, bfs_indirect_table_block(i, new_slot),
				i->sb->s_blocksize))) {
			return -EIO;
		}
		memset_b(zbuf->data, 0, i->sb->s_blocksize);
		bwrite(zbuf);
		ds->max_indirect_range = (ds->indirect.len
			+ ds->double_indirect.len
				* (i->sb->s_blocksize / sizeof(__blk_t)))
			* arraylen << i->sb->s_blocksize_bits;
		ds->max_double_indirect_range = ds->max_indirect_range;
		/* recurse: the new table block is empty, the write
		 * path above fills it */
		return bfs_indirect_bmap(i, offset, mode);
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
	__off_t old_size = i->i_size;
	__u64 old_mtime = i->u.bfs.raw.last_modified_time;

	__u64 covered = 0;
	int run;

	for(run = 0; run < BFS_NUM_DIRECT_BLOCKS; run++) {
		__u32 len = ds->direct[run].len;
		__blk_t base;
		__u64 run_end;

		if(!len || (ds->direct[run].allocation_group == 0
				&& ds->direct[run].start == 0)) {
			break;
		}
		base = (ds->direct[run].allocation_group << ag_shift)
			+ ds->direct[run].start;
		run_end = covered + ((__u64)len << i->sb->s_blocksize_bits);
		if(covered >= (__u64)length) {
			/* the whole run is beyond the new size: free it */
			__u32 n;
			for(n = 0; n < len; n++) {
				bfs_bfree(i->sb, base + n);
			}
			ds->direct[run].allocation_group = 0;
			ds->direct[run].start = 0;
			ds->direct[run].len = 0;
		} else if(run_end > (__u64)length) {
			/* partial run: free the tail blocks */
			__u64 keep = ((__u64)length - covered
					+ i->sb->s_blocksize - 1) >> i->sb->s_blocksize_bits;
			__u32 n;
			for(n = (__u32)keep; n < len; n++) {
				bfs_bfree(i->sb, base + n);
			}
			ds->direct[run].len = (__u32)keep;
		}
		covered = run_end;
	}

	/* free the indirect table if the new size is within the direct
	 * range; otherwise free the indirect runs beyond the new size */
	{
		__u32 arraylen = i->sb->s_blocksize / sizeof(struct bfs_block_run);
		__u32 table_len = !ds->indirect.len ? 0
			: ds->indirect.len + ds->double_indirect.len
				* (i->sb->s_blocksize / sizeof(__blk_t));
		int t;

		/* file position of each table run = max_direct_range + covered */
		{
		__u64 covered = ds->max_direct_range;
		for(t = 0; t < table_len; t++) {
			struct bfs_block_run *runs;
			struct buffer *ibuf;
			__blk_t tbl = bfs_indirect_table_block(i, t);
			int j;

			if(!tbl || !(ibuf = bread(i->dev, tbl, i->sb->s_blocksize))) {
				break;
			}
			runs = (struct bfs_block_run *)ibuf->data;
			for(j = 0; j < arraylen; j++) {
				__u32 len = runs[j].len;
				__u64 base, run_end;

				if(!len || (runs[j].allocation_group == 0
						&& runs[j].start == 0)) {
					break;
				}
				base = (__u64)(runs[j].allocation_group << ag_shift)
					+ runs[j].start;
				run_end = covered + ((__u64)len << i->sb->s_blocksize_bits);
				if(covered >= (__u64)length) {
					/* the whole run is beyond the new size */
					__u32 n;
					for(n = 0; n < len; n++) {
						bfs_bfree(i->sb, (__blk_t)base + n);
					}
					runs[j].allocation_group = 0;
					runs[j].start = 0;
					runs[j].len = 0;
				} else if(run_end > (__u64)length) {
					/* partial run: free the tail blocks */
					__u64 keep = ((__u64)length - covered
							+ i->sb->s_blocksize - 1) >> i->sb->s_blocksize_bits;
					__u32 n;
					for(n = (__u32)keep; n < len; n++) {
						bfs_bfree(i->sb, (__blk_t)base + n);
					}
					runs[j].len = (__u32)keep;
				}
				covered = run_end;
			}
			bwrite(ibuf);
		}
		}
	if((__u64)length <= ds->max_direct_range) {
			/* the whole indirect table is beyond the new size */
			__u32 n;
			__blk_t base = (ds->indirect.allocation_group << ag_shift)
				+ ds->indirect.start;
			for(n = 0; n < ds->indirect.len; n++) {
				bfs_bfree(i->sb, base + n);
			}
			base = (ds->double_indirect.allocation_group << ag_shift)
				+ ds->double_indirect.start;
			for(n = 0; n < ds->double_indirect.len; n++) {
				struct buffer *dbuf;
				__blk_t *addrs;
				__u32 s, darray = i->sb->s_blocksize / sizeof(__blk_t);

				/* free the table blocks addressed by this double block */
				if((dbuf = bread(i->dev, base + n, i->sb->s_blocksize))) {
					addrs = (__blk_t *)dbuf->data;
					for(s = 0; s < darray; s++) {
						if(addrs[s]
							&& addrs[s] < i->sb->u.bfs.num_blocks) {
							bfs_bfree(i->sb, addrs[s]);
						}
					}
					brelse(dbuf);
				}
				bfs_bfree(i->sb, base + n);
			}
			ds->indirect.allocation_group = 0;
			ds->indirect.start = 0;
			ds->indirect.len = 0;
			ds->double_indirect.allocation_group = 0;
			ds->double_indirect.start = 0;
			ds->double_indirect.len = 0;
			ds->max_indirect_range = 0;
			ds->max_double_indirect_range = 0;
		}
	}

	i->i_size = length;
	i->u.bfs.raw.u.data.size = length;
	bfs_touch_mtime(i);
	i->state |= INODE_DIRTY;
	bfs_index_resize(i->sb, i, old_size, old_mtime);
	return 0;
}
