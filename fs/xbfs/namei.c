/*
 * fnx/fs/xbfs/namei.c
 *
 * XBFS inode operations: create, mkdir, link, unlink, rmdir, rename,
 * symlink. Directory entries live in the B+tree; creating/removing a
 * name = xbfs_btree_insert/xbfs_btree_delete on the parent directory.
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
#include <fnx/stat.h>
#include <fnx/sched.h>
#include <fnx/string.h>

extern int xbfs_btree_insert(struct inode *, const char *, __ino_t);
extern int xbfs_btree_delete(struct inode *, const char *);
extern int xbfs_btree_delete_ino(struct inode *, __ino_t);
extern int xbfs_btree_iterate(struct inode *, int (*)(const char *, __ino_t, void *), void *);
extern int xbfs_btree_find(struct inode *, const char *, __ino_t *);
extern int xbfs_ialloc(struct inode *, int);

struct xbfs_dir_count_arg {
	int count;
};

static int xbfs_count_entry(const char *name, __ino_t ino, void *arg)
{
	((struct xbfs_dir_count_arg *)arg)->count++;
	return 0;
}

/* count the entries in the directory tree */
static int xbfs_dir_count(struct inode *dir)
{
	struct xbfs_dir_count_arg arg;

	arg.count = 0;
	xbfs_btree_iterate(dir, xbfs_count_entry, &arg);
	return arg.count;
}

/* the directory tree is empty when it only holds '.' and '..' */
static int xbfs_dir_empty(struct inode *dir)
{
	return xbfs_dir_count(dir) <= 2;
}

int xbfs_create(struct inode *dir, char *name, int flags, __mode_t mode,
	       struct inode **i_res)
{
	struct inode *i;
	__ino_t ino;
	int errno;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	inode_lock(dir);

	if(flags & O_CREAT) {
		if(!(errno = xbfs_btree_find(dir, name, &ino))) {
			inode_unlock(dir);
			return -EEXIST;
		}
	}

	if(!(i = ialloc(dir->sb, S_IFREG))) {
		inode_unlock(dir);
		return -ENOSPC;
	}
	i->count = 1;
	i->dev = dir->dev;
	i->fsop = dir->fsop;

	__off_t old_dir_size = dir->i_size;
	if((errno = xbfs_btree_insert(dir, name, i->inode))) {
		i->i_nlink = 0;
		iput(i);
		inode_unlock(dir);
		return errno;
	}

	i->i_mode = (mode & ~current->umask) & ~S_IFMT;
	i->i_mode |= S_IFREG;
	i->i_uid = current->euid;
	i->i_gid = current->egid;
	i->i_nlink = 1;
	i->i_blocks = 0;
	i->state |= INODE_DIRTY;

	/* the file-name 0x13 small_data record (Haiku SetName) */
	xbfs_inode_set_name(i, name);
	/* keep the name/size/last_modified indices in sync (Haiku) */
	xbfs_index_add(dir->sb, i, name);

	xbfs_dir_touch(dir, old_dir_size);
	dir->state |= INODE_DIRTY;

	*i_res = i;
	inode_unlock(dir);
	return 0;
}

/* FNX: XBFS had no mknod (do_mknod fell back to -EPERM), which broke
 * every path-based AF_UNIX socket on a XBFS root (X11, the compositor's
 * /tmp/gui.sock, ...). Socket/fifo/regular nodes are stored as ordinary
 * data-less inodes whose i_mode carries the type bits — the same trick
 * Linux ext2 uses. Char/block devices stay unsupported (devfs owns /dev). */
int xbfs_mknod(struct inode *dir, char *name, __mode_t mode, __dev_t dev)
{
	struct inode *i;
	__ino_t ino;
	__off_t old_dir_size;
	int errno;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	switch(mode & S_IFMT) {
		case S_IFSOCK:
		case S_IFIFO:
		case S_IFREG:
			break;
		default:
			/* S_IFCHR / S_IFBLK (and garbage): not representable */
			return -EPERM;
	}

	inode_lock(dir);

	if(!(errno = xbfs_btree_find(dir, name, &ino))) {
		inode_unlock(dir);
		return -EEXIST;
	}

	if(!(i = ialloc(dir->sb, S_IFREG))) {
		inode_unlock(dir);
		return -ENOSPC;
	}
	i->count = 1;
	i->dev = dir->dev;
	i->fsop = dir->fsop;

	old_dir_size = dir->i_size;
	if((errno = xbfs_btree_insert(dir, name, i->inode))) {
		i->i_nlink = 0;
		iput(i);
		inode_unlock(dir);
		return errno;
	}

	/* keep the type bits (S_IFSOCK/...) so the inode reads back right */
	i->i_mode = (mode & ~current->umask);
	i->i_uid = current->euid;
	i->i_gid = current->egid;
	i->i_nlink = 1;
	i->i_blocks = 0;
	i->state |= INODE_DIRTY;

	/* the file-name 0x13 small_data record (Haiku SetName) */
	xbfs_inode_set_name(i, name);
	/* keep the name/size/last_modified indices in sync (Haiku) */
	xbfs_index_add(dir->sb, i, name);

	xbfs_dir_touch(dir, old_dir_size);
	dir->state |= INODE_DIRTY;

	iput(i);
	inode_unlock(dir);
	return 0;
}

int xbfs_mkdir(struct inode *dir, char *name, __mode_t mode)
{
	struct inode *i;
	struct buffer *buf;
	struct xbfs_inode *raw;
	__blk_t block, block2;
	int errno;
	__ino_t ino;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	inode_lock(dir);

	if(!(errno = xbfs_btree_find(dir, name, &ino))) {
		inode_unlock(dir);
		return -EEXIST;
	}

	if(!(i = ialloc(dir->sb, S_IFDIR))) {
		inode_unlock(dir);
		return -ENOSPC;
	}
	i->count = 1;
	i->dev = dir->dev;
	i->fsop = dir->fsop;

	i->i_mode = ((mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX)) & ~current->umask);	/* keep the sticky bit (mkdir 01777) */
	i->i_mode |= S_IFDIR;
	i->i_uid = current->euid;
	i->i_gid = current->egid;
	i->i_nlink = 2;

	/* give the new directory its own tree ('.' and '..' + room) */
	if((block = bmap(i, 0, FOR_WRITING)) < 0) {
		iput(i);
		inode_unlock(dir);
		return block;
	}
	if((block2 = bmap(i, XBFS_BTREE_NODE_SIZE, FOR_WRITING)) < 0) {
		iput(i);
		inode_unlock(dir);
		return block2;
	}
	if(!(buf = bread(i->dev, block, i->sb->s_blocksize))) {
		iput(i);
		inode_unlock(dir);
		return -EIO;
	}
	{
		struct xbfs_btree_header *h = (struct xbfs_btree_header *)buf->data;
		h->magic = XBFS_BTREE_MAGIC;
		h->node_size = XBFS_BTREE_NODE_SIZE;
		h->max_depth = 1;
		h->data_type = XBFS_BTREE_STRING_TYPE;
		h->root_node_ptr = XBFS_BTREE_NODE_SIZE;
		h->free_node_ptr = XBFS_BTREE_NULL;
		h->max_size = 1 << 20;
		bwrite(buf);
	}
	/* the leaf: '.' and '..' (a 1024-byte node; with blocks larger
	 * than the node size it shares the header's block) */
	{
		struct buffer *buf2;
		struct xbfs_btree_node *n;
		if(!(buf2 = bread(i->dev, block2, i->sb->s_blocksize))) {
			iput(i);
			inode_unlock(dir);
			return -EIO;
		}
		n = (struct xbfs_btree_node *)((char *)buf2->data
			+ (XBFS_BTREE_NODE_SIZE % i->sb->s_blocksize));
		memset_b(n, 0, XBFS_BTREE_NODE_SIZE);
		n->left = XBFS_BTREE_NULL;
		n->right = XBFS_BTREE_NULL;
		n->overflow = XBFS_BTREE_NULL;
		n->all_key_count = 2;
		n->all_key_length = 3;	/* "." + ".." */
		{
			char *keys = (char *)n + sizeof(struct xbfs_btree_node);
			__u16 *kl = (__u16 *)((char *)n
				+ ((sizeof(struct xbfs_btree_node) + 3 + 7) & ~7));
			__u64 *values = (__u64 *)((char *)kl + 2 * 2);
			keys[0] = '.';
			kl[0] = 1;
			values[0] = i->inode;
			keys[1] = '.';
			keys[2] = '.';
			kl[1] = 3;
			values[1] = dir->inode;
		}
		i->i_size = 2 * XBFS_BTREE_NODE_SIZE;
		/* the two bmap() calls above already built the runs; the
		 * second node is only in a new block when it crosses a block
		 * boundary (at 1024-byte blocks; at larger sizes it shares
		 * the header's block). */
		raw = &i->u.xbfs.raw;
		raw->u.data.size = 2 * XBFS_BTREE_NODE_SIZE;
		i->state |= INODE_DIRTY;
		bwrite(buf2);
	}

	__off_t old_dir_size = dir->i_size;
	if((errno = xbfs_btree_insert(dir, name, i->inode))) {
		iput(i);
		inode_unlock(dir);
		return errno;
	}

	/* the file-name 0x13 small_data record (Haiku SetName) */
	xbfs_inode_set_name(i, name);
	xbfs_index_add(dir->sb, i, name);

	xbfs_dir_touch(dir, old_dir_size);
	/* directory nlink does not count subdirectories (no on-disk link
	 * count field; read_inode reconstructs nlink per-inode), so a
	 * subdir's later removal can never free its parent */
	dir->state |= INODE_DIRTY;

	iput(i);
	inode_unlock(dir);
	return 0;
}

int xbfs_link(struct inode *i_old, struct inode *dir_new, char *name)
{
	int errno;

	if(IS_RDONLY_FS(dir_new)) {
		return -EROFS;
	}
	if(S_ISDIR(i_old->i_mode)) {
		return -EPERM;
	}

	inode_lock(dir_new);

	if(!(errno = xbfs_btree_find(dir_new, name, (__ino_t *)&errno))) {
		inode_unlock(dir_new);
		return -EEXIST;
	}
	__off_t old_dir_size = dir_new->i_size;
	if((errno = xbfs_btree_insert(dir_new, name, i_old->inode))) {
		inode_unlock(dir_new);
		return errno;
	}

	i_old->i_nlink++;
	xbfs_touch_ctime(i_old);
	i_old->state |= INODE_DIRTY;

	xbfs_dir_touch(dir_new, old_dir_size);
	dir_new->state |= INODE_DIRTY;

	inode_unlock(dir_new);
	return 0;
}

int xbfs_unlink(struct inode *dir, struct inode *i, char *name)
{
	int errno;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	inode_lock(dir);
	inode_lock(i);

	__off_t old_dir_size = dir->i_size;
	if((errno = xbfs_btree_delete(dir, name))) {
		inode_unlock(dir);
		inode_unlock(i);
		return errno;
	}

	xbfs_index_remove(dir->sb, i, name);

	if(!--i->i_nlink) {
		/* freed when iput'd */
	}
	xbfs_touch_ctime(i);
	xbfs_dir_touch(dir, old_dir_size);

	i->state |= INODE_DIRTY;
	dir->state |= INODE_DIRTY;

	inode_unlock(dir);
	inode_unlock(i);
	return 0;
}

int xbfs_rmdir(struct inode *dir, struct inode *i)
{
	int errno;

	inode_lock(i);

	if(!xbfs_dir_empty(i)) {
		inode_unlock(i);
		return -ENOTEMPTY;
	}

	inode_lock(dir);

	__off_t old_dir_size = dir->i_size;
	if((errno = xbfs_btree_delete_ino(dir, i->inode))) {
		inode_unlock(i);
		inode_unlock(dir);
		return errno;
	}
	{
		char namebuf[256];

		if(xbfs_inode_get_name(i, namebuf, 255) >= 0) {
			xbfs_index_remove(dir->sb, i, namebuf);
		}
	}
	i->i_nlink = 0;
	/* do NOT decrement the parent: a directory read back from disk has
	 * nlink 2 (see xbfs_read_inode) regardless of its subdirectories, so
	 * removing a subdir must not be able to drive the parent to 0 and
	 * free it (that was freeing /tmp whenever a replayed /tmp/.X11-unix
	 * was rmdir'd - the parent's inode block then got recycled for the
	 * next file, corrupting the tree) */
	xbfs_touch_ctime(i);
	xbfs_dir_touch(dir, old_dir_size);

	i->state |= INODE_DIRTY;
	dir->state |= INODE_DIRTY;

	inode_unlock(i);
	inode_unlock(dir);
	return 0;
}

int xbfs_symlink(struct inode *dir, char *name, char *oldname)
{
	struct inode *i;
	__ino_t ino;
	int errno, n;
	__size_t len;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	inode_lock(dir);

	if(!(errno = xbfs_btree_find(dir, name, &ino))) {
		inode_unlock(dir);
		return -EEXIST;
	}

	if(!(i = ialloc(dir->sb, S_IFLNK))) {
		inode_unlock(dir);
		return -ENOSPC;
	}
	i->count = 1;
	i->dev = dir->dev;
	i->fsop = dir->fsop;

	i->i_mode = S_IFLNK | (S_IRWXU | S_IRWXG | S_IRWXO);
	i->i_uid = current->euid;
	i->i_gid = current->egid;
	i->i_nlink = 1;
	i->state |= INODE_DIRTY;

	__off_t old_dir_size = dir->i_size;
	if((errno = xbfs_btree_insert(dir, name, i->inode))) {
		i->i_nlink = 0;
		iput(i);
		inode_unlock(dir);
		return errno;
	}

	len = strlen(oldname);
	if(len <= 143) {
		/* fast symlink: the target lives in the inode's symlink area,
		 * NUL-terminated (Haiku reads it with strlen) */
		for(n = 0; n < len; n++) {
			i->u.xbfs.raw.u.symlink[n] = oldname[n];
		}
		i->u.xbfs.raw.u.symlink[n] = 0;
		i->u.xbfs.raw.pad[0] = len;
		/* the data.size union field carries the logical size so the
		 * on-disk inode, stat and the size index all agree */
		i->u.xbfs.raw.u.data.size = n;
		i->i_size = n;
	} else {
		/* long symlink: the target lives in the data stream; the
		 * INODE_LONG_SYMLINK flag tells Haiku (and us) not to read
		 * the symlink area */
		__off_t offset = 0;

		i->u.xbfs.raw.flags |= XBFS_INODE_LONG_SYMLINK;
		i->i_blocks = (len + 511) >> 9;
		while(offset < len) {
			__blk_t block;
			struct buffer *buf;
			unsigned int boffset, bytes;
			int blksize = i->sb->s_blocksize;

			boffset = offset & (blksize - 1);
			if((block = bmap(i, offset, FOR_WRITING)) < 0) {
				errno = block;
				goto err;
			}
			bytes = blksize - boffset;
			bytes = MIN(bytes, len - offset);
			if(!(buf = bread(i->dev, block, blksize))) {
				errno = -EIO;
				goto err;
			}
			memcpy_b(buf->data + boffset, oldname + offset, bytes);
			bwrite(buf);
			offset += bytes;
		}
		i->i_size = len;
		i->u.xbfs.raw.pad[0] = len;
		i->u.xbfs.raw.u.data.size = len;
	}

	/* the file-name 0x13 small_data record (Haiku SetName) */
	xbfs_inode_set_name(i, name);
	xbfs_index_add(dir->sb, i, name);

	xbfs_dir_touch(dir, old_dir_size);
	dir->state |= INODE_DIRTY;

	iput(i);
	inode_unlock(dir);
	return 0;

err:
	xbfs_btree_delete(dir, name);
	i->i_nlink = 0;
	iput(i);
	inode_unlock(dir);
	return errno;
}

int xbfs_rename(struct inode *i_old, struct inode *dir_old,
	       struct inode *i_new, struct inode *dir_new,
	       char *oldpath, char *newpath)
{
	int errno;

	if(IS_RDONLY_FS(dir_old) || IS_RDONLY_FS(dir_new)) {
		return -EROFS;
	}

	inode_lock(dir_old);
	if(dir_new != dir_old) {
		inode_lock(dir_new);
	}

	__off_t old_dir_old_size = dir_old->i_size;
	__off_t old_dir_new_size = dir_new->i_size;
	/* POSIX rename replaces an existing target: unlink/rmdir it first.
	 * The VFS (sys_rename) already rejected a regular file onto a dir
	 * (EISDIR) and a dir onto a non-dir (ENOTDIR); a directory target
	 * must be empty (rmdir semantics). Without this, rename(2) over an
	 * existing file returned EEXIST (the btree rejects the duplicate),
	 * breaking libconfig's atomic temp+rename writes. */
	if(i_new) {
		if(S_ISDIR(i_new->i_mode)) {
			if(!xbfs_dir_empty(i_new)) {
				errno = -ENOTEMPTY;
				if(dir_new != dir_old) {
					inode_unlock(dir_new);
				}
				inode_unlock(dir_old);
				return errno;
			}
			errno = xbfs_btree_delete_ino(dir_new, i_new->inode);
		} else {
			errno = xbfs_btree_delete(dir_new, newpath);
		}
		if(errno) {
			if(dir_new != dir_old) {
				inode_unlock(dir_new);
			}
			inode_unlock(dir_old);
			return errno;
		}
		{
			char namebuf[256];

			if(xbfs_inode_get_name(i_new, namebuf, 255) >= 0) {
				xbfs_index_remove(dir_new->sb, i_new, namebuf);
			}
		}
		if(S_ISDIR(i_new->i_mode)) {
			i_new->i_nlink = 0;
		} else if(i_new->i_nlink) {
			i_new->i_nlink--;
		}
		xbfs_touch_ctime(i_new);
		i_new->state |= INODE_DIRTY;
	}
	if((errno = xbfs_btree_delete(dir_old, oldpath))) {
		if(dir_new != dir_old) {
			inode_unlock(dir_new);
		}
		inode_unlock(dir_old);
		return errno;
	}
	if((errno = xbfs_btree_insert(dir_new, newpath, i_old->inode))) {
		/* roll back */
		xbfs_btree_insert(dir_old, oldpath, i_old->inode);
		if(dir_new != dir_old) {
			inode_unlock(dir_new);
		}
		inode_unlock(dir_old);
		return errno;
	}

	/* update the file-name 0x13 small_data record (Haiku SetName) */
	xbfs_inode_set_name(i_old, newpath);
	xbfs_index_remove(dir_old->sb, i_old, oldpath);
	xbfs_index_add(dir_old->sb, i_old, newpath);

	xbfs_dir_touch(dir_old, old_dir_old_size);
	dir_old->state |= INODE_DIRTY;
	if(dir_new != dir_old) {
		xbfs_dir_touch(dir_new, old_dir_new_size);
		dir_new->state |= INODE_DIRTY;
	}

	if(dir_new != dir_old) {
		inode_unlock(dir_new);
	}
	inode_unlock(dir_old);
	return 0;
}
