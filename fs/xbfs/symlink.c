/*
 * fnx/fs/xbfs/symlink.c
 *
 * XBFS symlink operations. Targets that fit in the inode's 144-byte
 * symlink area are stored inline (fast symlink); longer targets are
 * stored in the inode's data stream (the union member used for regular
 * files), discriminated by i_size > 143.
 *
 * Copyright 2024, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/xbfs.h>
#include <fnx/sched.h>
#include <fnx/stat.h>
#include <fnx/string.h>
#include <fnx/buffer.h>
#include <fnx/stdio.h>
#include <fnx/limits.h>

static int xbfs_read_symlink_stream(struct inode *i, char *buffer,
				   __size_t count)
{
	__blk_t block;
	__size_t total = 0;
	unsigned int boffset, bytes;
	int blksize = i->sb->s_blocksize;
	__off_t offset = 0;
	struct buffer *buf;

	count = MIN(count, (__size_t)i->i_size);
	while(total < count) {
		boffset = offset & (blksize - 1);
		if((block = bmap(i, offset, FOR_READING)) < 0) {
			printk("XBFS-SYMSTREAM bmap fail %d\n", block);
			return block;
		}
		bytes = blksize - boffset;
		bytes = MIN(bytes, count - total);
		if(!(buf = bread(i->dev, block, blksize))) {
			printk("XBFS-SYMSTREAM bread fail blk %d\n", block);
			return -EIO;
		}
		memcpy_b(buffer + total, buf->data + boffset, bytes);
		brelse(buf);
		total += bytes;
		offset += bytes;
	}
	return total;
}

int xbfs_readlink(struct inode *i, char *buffer, __size_t count)
{
	int n;
	__size_t bufsize = count;

	if(!S_ISLNK(i->i_mode)) {
		return 0;
	}

	inode_lock(i);
	if(i->i_size > 143) {
		/* long symlink: the target lives in the data stream */
		n = xbfs_read_symlink_stream(i, buffer, count);
		if(n >= 0 && n < count) {
			buffer[n] = 0;
		}
	} else {
		count = MIN(count, i->i_size);
		count = MIN(count, 143);
		for(n = 0; n < count; n++) {
			buffer[n] = i->u.xbfs.raw.u.symlink[n];
		}
		/* only NUL-terminate when there is room: writing buffer[count]
		 * when count == bufsize would go one byte past the verified
		 * user area */
		if(count < bufsize) {
			buffer[count] = 0;
		}
	}
	inode_unlock(i);
	return n;
}

int xbfs_followlink(struct inode *dir, struct inode *i, struct inode **i_res)
{
	char name[PATH_MAX + 1];
	int n, errno;

	if(!i) {
		return -ENOENT;
	}
	if(!S_ISLNK(i->i_mode)) {
		return 0;
	}
	if(current->loopcnt > MAX_SYMLINKS) {
		iput(i);
		return -ELOOP;
	}

	if(i->i_size > 143) {
		/* long symlink: the target lives in the data stream */
		n = xbfs_read_symlink_stream(i, name, sizeof(name) - 1);
		if(n < 0) {
			iput(i);
			return n;
		}
		name[n] = 0;
	} else {
		for(n = 0; n < 143; n++) {
			if((name[n] = i->u.xbfs.raw.u.symlink[n])) {
				continue;
			}
			break;
		}
		name[n] = 0;
	}

	current->loopcnt++;
	iput(i);
	errno = parse_namei(name, dir, i_res, NULL, FOLLOW_LINKS);
	current->loopcnt--;
	return errno;
}
