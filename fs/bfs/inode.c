/*
 * fnx/fs/bfs/inode.c
 *
 * BFS inode + block mapping (read-only).
 *
 * Each inode occupies a full 1024-byte block; the vfs inode number is
 * the absolute disk block number of the inode:
 *
 *	block = (allocation_group << ag_shift) + start
 *
 * The raw on-disk inode (struct bfs_inode, 256 bytes at the start of
 * the block) is cached in i->u.bfs.raw for bmap and readdir.
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/stat.h>
#include <fnx/bfs.h>
#include <fnx/buffer.h>
#include <fnx/fcntl.h>
#include <fnx/string.h>

extern int bfs_bmap(struct inode *, __off_t, int);
extern struct fs_operations bfs_fsop;

/* single fsop for files and dirs: dispatch on the inode type */
int bfs_open(struct inode *i, struct fd *f)
{
	f->offset = 0;
	if(S_ISREG(i->i_mode) && (f->flags & O_TRUNC)) {
		return -EROFS;	/* read-only */
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

	if(S_ISDIR(raw->mode) || S_ISREG(raw->mode)) {
		i->fsop = &bfs_fsop;
	} else {
		/* M1 is read-only with file/dir support only */
		brelse(buf);
		return -EINVAL;
	}
	i->i_mode = raw->mode;
	i->i_uid = raw->uid;
	i->i_gid = raw->gid;
	i->i_size = raw->u.data.size;
	/* BFS stores times as (seconds << 16); there is no access time */
	i->i_atime = raw->last_modified_time >> 16;
	i->i_ctime = raw->last_modified_time >> 16;
	i->i_mtime = raw->last_modified_time >> 16;
	i->i_nlink = 1;
	i->i_blocks = 0;
	i->i_flags = 0;
	brelse(buf);
	return 0;
}

/*
 * Map a byte offset in the data stream to a disk block.
 * M1 (read-only): only the 12 direct block runs are mapped; indirect
 * and double-indirect streams return -EIO.
 */
int bfs_bmap(struct inode *i, __off_t offset, int mode)
{
	struct bfs_inode *raw = &i->u.bfs.raw;
	struct bfs_data_stream *ds = &raw->u.data;
	__u32 block = (__u32)(offset >> BFS_BLOCK_SHIFT);
	__u32 ag_shift = i->sb->u.bfs.ag_shift;
	__u64 covered = 0;
	int run;

	if(block >= BFS_NUM_DIRECT_BLOCKS) {
		return -EIO;	/* indirect stream not supported (read-only) */
	}

	for(run = 0; run < BFS_NUM_DIRECT_BLOCKS; run++) {
		__u32 len = ds->direct[run].len;
		if(!len) {
			break;
		}
		if(block < covered + len) {
			__u32 ag = ds->direct[run].allocation_group;
			__u32 start = ds->direct[run].start;
			return (int)((ag << ag_shift) + start + (block - covered));
		}
		covered += len;
	}
	return 0;	/* unmapped */
}
