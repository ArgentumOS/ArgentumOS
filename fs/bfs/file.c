/*
 * fnx/fs/bfs/file.c
 *
 * BFS file operations: write (with block allocation via bmap
 * FOR_WRITING) and llseek. Reads use the generic page-cache file_read.
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

extern int file_read(struct inode *, struct fd *, char *, __size_t);

__loff_t bfs_file_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}

int bfs_file_write(struct inode *i, struct fd *f, const char *buffer,
		   __size_t count)
{
	__blk_t block;
	__size_t total_written;
	unsigned int boffset, bytes;
	int blksize, retval;
	struct buffer *buf;
	__loff_t offset;

	inode_lock(i);

	blksize = i->sb->s_blocksize;
	retval = total_written = 0;

	if(f->flags & O_APPEND) {
		f->offset = i->i_size;
	}
	offset = f->offset;

	while(total_written < count) {
		boffset = offset & (blksize - 1);	/* mod blksize */
		if((block = bmap(i, offset, FOR_WRITING)) < 0) {
			retval = block;
			break;
		}
		bytes = blksize - boffset;
		bytes = MIN(bytes, (count - total_written));
		if(!(buf = bread(i->dev, block, blksize))) {
			retval = -EIO;
			break;
		}
		memcpy_b(buf->data + boffset, buffer + total_written, bytes);
		update_page_cache(i, offset, buffer + total_written, bytes);
		bwrite(buf);
		total_written += bytes;
		offset += bytes;
	}

	if(!retval) {
		f->offset = offset;
		if(f->offset > i->i_size) {
			i->i_size = f->offset;
		}
		/* track the stream size so bfs_ifree() truncates (frees the
		 * data blocks) when the file is unlinked */
		i->i_blocks = i->i_size >> 9;
		i->i_ctime = CURRENT_TIME;
		i->i_mtime = CURRENT_TIME;
		i->state |= INODE_DIRTY;
	}

	inode_unlock(i);

	if(retval) {
		return retval;
	}
	return total_written;
}
