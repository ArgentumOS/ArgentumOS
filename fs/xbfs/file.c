/*
 * fnx/fs/xbfs/file.c
 *
 * XBFS file operations: write (with block allocation via bmap
 * FOR_WRITING) and llseek. Reads use the generic page-cache file_read.
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/xbfs.h>
#include <fnx/buffer.h>
#include <fnx/fcntl.h>
#include <fnx/mm.h>
#include <fnx/stat.h>
#include <fnx/string.h>

extern int file_read(struct inode *, struct fd *, char *, __size_t);

__loff_t xbfs_file_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}

/*
 * X-SSD6: the byte offset of an inline file's content inside the
 * small_data tail. The tail starts with the small_data attribute records
 * every inode carries (at least the file-name 0x13 record, which the
 * checker + rmdir/rename rely on), followed by a zero terminator. The
 * inline content lives after that terminator, so the records stay intact
 * and the attribute walkers stop before the content. XBFS_INLINE_MAX
 * (512) fits for every name length up to the 255-byte limit.
 */
int xbfs_inline_base(struct inode *i)
{
	int left = i->sb->s_blocksize - (int)sizeof(struct xbfs_inode);
	unsigned char *p = i->u.xbfs.small_data;
	int pos = 0;

	while(left >= 8) {
		unsigned int ns, ds;

		if(!p[4] && !p[5] && !p[6] && !p[7]) {
			/* the zero terminator: content starts after it */
			pos += 12;
			return pos <= left ? pos : left;
		}
		ns = p[4] | (p[5] << 8);
		ds = p[6] | (p[7] << 8);
		if(ns == 0) {
			break;
		}
		pos += 12 + ns + ds;
		if(pos > left) {
			break;
		}
		p += 12 + ns + ds;
		left -= 12 + ns + ds;
	}
	return left;	/* no terminator: content at the tail end */
}

/*
 * X-SSD6: convert an inline file to a regular stream file. The content
 * (data.size bytes in small_data) is copied into freshly allocated data
 * blocks, then the INLINE_DATA flag is dropped. Used when a write or an
 * xattr would outgrow the inode tail. The block allocations happen
 * through the same bmap FOR_WRITING path a normal write uses, so the
 * stream/runs + the bitmap stay consistent with a plain file write.
 */
int xbfs_inline_expand(struct inode *i)
{
	__u64 S = i->i_size;
	__blk_t block;
	unsigned int boffset, bytes;
	int blksize = i->sb->s_blocksize;
	unsigned char *data = i->u.xbfs.small_data + xbfs_inline_base(i);
	__u64 done = 0;
	struct buffer *buf;

	while(done < S) {
		boffset = (unsigned int)(done & (blksize - 1));
		if((block = bmap(i, (__off_t)done, FOR_WRITING)) < 0) {
			return (int)block;
		}
		bytes = blksize - boffset;
		bytes = MIN(bytes, (unsigned int)(S - done));
		if(!(buf = bread(i->dev, block, blksize))) {
			return -EIO;
		}
		memcpy_b(buf->data + boffset, data + done, bytes);
		update_page_cache(i, (__off_t)done, (const char *)data + done, bytes);
		bwrite(buf);
		done += bytes;
	}
	i->u.xbfs.raw.flags &= ~XBFS_INODE_INLINE_DATA;
	i->i_blocks = (i->i_size + 511) >> 9;
	i->state |= INODE_DIRTY;
	return 0;
}

/* X-SSD6: an inline file's content lives in the inode's small_data tail
 * and has no data blocks, so the generic block-cache read cannot serve
 * it. Reads copy straight out of the tail; normal files defer to the
 * generic page-cache file_read. */
int xbfs_file_read(struct inode *i, struct fd *f, char *buffer,
		   __size_t count)
{
	__size_t n;

	if(!(i->u.xbfs.raw.flags & XBFS_INODE_INLINE_DATA)) {
		return file_read(i, f, buffer, count);
	}
	if(f->offset >= i->i_size) {
		return 0;
	}
	n = count;
	if((__u64)(f->offset + n) > (__u64)i->i_size) {
		n = (__size_t)(i->i_size - f->offset);
	}
	memcpy_b(buffer, i->u.xbfs.small_data + xbfs_inline_base(i) + f->offset, n);
	f->offset += n;
	return n;
}

/* X-SSD6: write into an inline file's tail (offset+count already known
 * to fit XBFS_INLINE_MAX). Updates the same bookkeeping as the stream
 * write path so the index + times stay consistent. Returns count. */
static __size_t xbfs_inline_write(struct inode *i, struct fd *f,
				  const char *buffer, __size_t count,
				  __off_t offset, __off_t old_size,
				  __u64 old_mtime)
{
	__u64 end = (__u64)(offset + count);
	__u64 newsize = end > (__u64)i->i_size ? end : (__u64)i->i_size;
	int base = xbfs_inline_base(i);

	if((__u64)offset > (__u64)i->i_size) {
		/* sparse write into an inline file: zero the gap */
		memset_b(i->u.xbfs.small_data + base + i->i_size, 0,
			offset - i->i_size);
	}
	memcpy_b(i->u.xbfs.small_data + base + offset, buffer, count);
	/* zero the tail beyond the new size (a shrinking overwrite can
	 * leave stale bytes past the end) */
	if((__u64)base + newsize < XBFS_INLINE_MAX + 0) {
		int tail = i->sb->s_blocksize - (int)sizeof(struct xbfs_inode);
		memset_b(i->u.xbfs.small_data + base + newsize, 0,
			tail - base - (int)newsize);
	}
	f->offset = (__loff_t)(offset + count);
	i->i_size = (__off_t)newsize;
	i->u.xbfs.raw.flags |= XBFS_INODE_INLINE_DATA;
	i->i_blocks = 0;
	xbfs_touch_mtime(i);
	xbfs_touch_ctime(i);
	i->state |= INODE_DIRTY;
	xbfs_index_resize(i->sb, i, old_size, old_mtime);
	return count;
}

int xbfs_file_write(struct inode *i, struct fd *f, const char *buffer,
		   __size_t count)
{
	__blk_t block;
	__size_t total_written;
	unsigned int boffset, bytes;
	int blksize, retval;
	struct buffer *buf;
	__loff_t offset;

	__off_t old_size;
	__u64 old_mtime;

	inode_lock(i);

	blksize = i->sb->s_blocksize;
	retval = total_written = 0;
	old_size = i->i_size;
	old_mtime = i->u.xbfs.raw.last_modified_time;

	if(f->flags & O_APPEND) {
		f->offset = i->i_size;
	}
	offset = f->offset;

	/* X-SSD6: inline files stay in the inode tail while they fit; a
	 * stream file that is still empty (freshly created or truncated
	 * to 0) becomes inline for its first small write */
	if(S_ISREG(i->i_mode) && count) {
		if(i->u.xbfs.raw.flags & XBFS_INODE_INLINE_DATA) {
			if((__u64)(offset + count) <= XBFS_INLINE_MAX) {
				retval = xbfs_inline_write(i, f, buffer, count,
							   offset, old_size,
							   old_mtime);
				inode_unlock(i);
				return retval;
			}
			/* the write would outgrow the tail: become a stream */
			if((retval = xbfs_inline_expand(i)) < 0) {
				inode_unlock(i);
				return retval;
			}
		} else if(i->i_size == 0 && offset == 0
			  && count <= XBFS_INLINE_MAX) {
			/* an empty file's first small write: fold inline */
			retval = xbfs_inline_write(i, f, buffer, count,
						   0, 0, old_mtime);
			inode_unlock(i);
			return retval;
		}
	}

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
		/* track the stream size so xbfs_ifree() truncates (frees the
		 * data blocks) when the file is unlinked; 512-byte units with
		 * rounding so any allocated block counts (i_blocks == 0 would
		 * skip the truncate and leak the block) */
		i->i_blocks = (i->i_size + 511) >> 9;
		xbfs_touch_mtime(i);
		xbfs_touch_ctime(i);
		i->state |= INODE_DIRTY;
		xbfs_index_resize(i->sb, i, old_size, old_mtime);
	}

	inode_unlock(i);

	if(retval) {
		return retval;
	}
	return total_written;
}
