/*
 * fnx/fs/bfs/namei.c
 *
 * BFS inode operations: create, mkdir, link, unlink, rmdir, rename,
 * symlink. Directory entries live in the B+tree; creating/removing a
 * name = bfs_btree_insert/bfs_btree_delete on the parent directory.
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
#include <fnx/stat.h>
#include <fnx/sched.h>
#include <fnx/string.h>

extern int bfs_btree_insert(struct inode *, const char *, __ino_t);
extern int bfs_btree_delete(struct inode *, const char *);
extern int bfs_btree_delete_ino(struct inode *, __ino_t);
extern int bfs_btree_iterate(struct inode *, int (*)(const char *, __ino_t, void *), void *);
extern int bfs_btree_find(struct inode *, const char *, __ino_t *);
extern int bfs_ialloc(struct inode *, int);

struct bfs_dir_count_arg {
	int count;
};

static int bfs_count_entry(const char *name, __ino_t ino, void *arg)
{
	((struct bfs_dir_count_arg *)arg)->count++;
	return 0;
}

/* count the entries in the directory tree */
static int bfs_dir_count(struct inode *dir)
{
	struct bfs_dir_count_arg arg;

	arg.count = 0;
	bfs_btree_iterate(dir, bfs_count_entry, &arg);
	return arg.count;
}

/* the directory tree is empty when it only holds '.' and '..' */
static int bfs_dir_empty(struct inode *dir)
{
	return bfs_dir_count(dir) <= 2;
}

int bfs_create(struct inode *dir, char *name, int flags, __mode_t mode,
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
		if(!(errno = bfs_btree_find(dir, name, &ino))) {
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

	if((errno = bfs_btree_insert(dir, name, i->inode))) {
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
	bfs_inode_set_name(i, name);
	/* keep the name/size/last_modified indices in sync (Haiku) */
	bfs_index_add(dir->sb, i, name);

	bfs_dir_touch(dir);
	dir->state |= INODE_DIRTY;

	*i_res = i;
	inode_unlock(dir);
	return 0;
}

int bfs_mkdir(struct inode *dir, char *name, __mode_t mode)
{
	struct inode *i;
	struct buffer *buf;
	struct bfs_inode *raw;
	__blk_t block, block2;
	int errno;
	__ino_t ino;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	inode_lock(dir);

	if(!(errno = bfs_btree_find(dir, name, &ino))) {
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

	i->i_mode = ((mode & (S_IRWXU | S_IRWXG | S_IRWXO)) & ~current->umask);
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
	if((block2 = bmap(i, BFS_BLOCK_SIZE, FOR_WRITING)) < 0) {
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
		struct bfs_btree_header *h = (struct bfs_btree_header *)buf->data;
		h->magic = BFS_BTREE_MAGIC;
		h->node_size = BFS_BLOCK_SIZE;
		h->max_depth = 1;
		h->data_type = BFS_BTREE_STRING_TYPE;
		h->root_node_ptr = BFS_BLOCK_SIZE;
		h->free_node_ptr = BFS_BTREE_NULL;
		h->max_size = 1 << 20;
		bwrite(buf);
	}
	/* the leaf: '.' and '..' */
	{
		struct buffer *buf2;
		struct bfs_btree_node *n;
		if(!(buf2 = bread(i->dev, block2, i->sb->s_blocksize))) {
			iput(i);
			inode_unlock(dir);
			return -EIO;
		}
		n = (struct bfs_btree_node *)buf2->data;
		memset_b(n, 0, BFS_BLOCK_SIZE);
		n->left = BFS_BTREE_NULL;
		n->right = BFS_BTREE_NULL;
		n->overflow = BFS_BTREE_NULL;
		n->all_key_count = 2;
		n->all_key_length = 3;	/* "." + ".." */
		{
			char *keys = (char *)n + sizeof(struct bfs_btree_node);
			__u16 *kl = (__u16 *)((char *)n
				+ ((sizeof(struct bfs_btree_node) + 3 + 7) & ~7));
			__u64 *values = (__u64 *)((char *)kl + 2 * 2);
			keys[0] = '.';
			kl[0] = 1;
			values[0] = i->inode;
			keys[1] = '.';
			keys[2] = '.';
			kl[1] = 3;
			values[1] = dir->inode;
		}
		i->i_size = 2 * BFS_BLOCK_SIZE;
		/* the two bmap() calls above already built the runs; the
		 * second block is only contiguous when the extension of the
		 * first run succeeds (block2 == block + 1). Hardcoding
		 * len = 2 here would claim a block the allocator may have
		 * given to something else (the hello.txt at block 32 in the
		 * mkbfs layout), corrupting the new directory's tree. */
		raw = &i->u.bfs.raw;
		raw->u.data.size = 2 * BFS_BLOCK_SIZE;
		i->state |= INODE_DIRTY;
		bwrite(buf2);
	}

	if((errno = bfs_btree_insert(dir, name, i->inode))) {
		iput(i);
		inode_unlock(dir);
		return errno;
	}

	/* the file-name 0x13 small_data record (Haiku SetName) */
	bfs_inode_set_name(i, name);
	bfs_index_add(dir->sb, i, name);

	bfs_dir_touch(dir);
	dir->i_nlink++;
	dir->state |= INODE_DIRTY;

	iput(i);
	inode_unlock(dir);
	return 0;
}

int bfs_link(struct inode *i_old, struct inode *dir_new, char *name)
{
	int errno;

	if(IS_RDONLY_FS(dir_new)) {
		return -EROFS;
	}
	if(S_ISDIR(i_old->i_mode)) {
		return -EPERM;
	}

	inode_lock(dir_new);

	if(!(errno = bfs_btree_find(dir_new, name, (__ino_t *)&errno))) {
		inode_unlock(dir_new);
		return -EEXIST;
	}
	if((errno = bfs_btree_insert(dir_new, name, i_old->inode))) {
		inode_unlock(dir_new);
		return errno;
	}

	i_old->i_nlink++;
	bfs_touch_ctime(i_old);
	i_old->state |= INODE_DIRTY;

	bfs_dir_touch(dir_new);
	dir_new->state |= INODE_DIRTY;

	inode_unlock(dir_new);
	return 0;
}

int bfs_unlink(struct inode *dir, struct inode *i, char *name)
{
	int errno;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	inode_lock(dir);
	inode_lock(i);

	if((errno = bfs_btree_delete(dir, name))) {
		inode_unlock(dir);
		inode_unlock(i);
		return errno;
	}

	bfs_index_remove(dir->sb, i, name);

	if(!--i->i_nlink) {
		/* freed when iput'd */
	}
	bfs_touch_ctime(i);
	bfs_dir_touch(dir);

	i->state |= INODE_DIRTY;
	dir->state |= INODE_DIRTY;

	inode_unlock(dir);
	inode_unlock(i);
	return 0;
}

int bfs_rmdir(struct inode *dir, struct inode *i)
{
	int errno;

	inode_lock(i);

	if(!bfs_dir_empty(i)) {
		inode_unlock(i);
		return -ENOTEMPTY;
	}

	inode_lock(dir);

	if((errno = bfs_btree_delete_ino(dir, i->inode))) {
		inode_unlock(i);
		inode_unlock(dir);
		return errno;
	}
	{
		char namebuf[256];

		if(bfs_inode_get_name(i, namebuf, 255) >= 0) {
			bfs_index_remove(dir->sb, i, namebuf);
		}
	}
	i->i_nlink = 0;
	dir->i_nlink--;

	bfs_touch_ctime(i);
	bfs_dir_touch(dir);

	i->state |= INODE_DIRTY;
	dir->state |= INODE_DIRTY;

	inode_unlock(i);
	inode_unlock(dir);
	return 0;
}

int bfs_symlink(struct inode *dir, char *name, char *oldname)
{
	struct inode *i;
	__ino_t ino;
	int errno, n;
	__size_t len;

	if(IS_RDONLY_FS(dir)) {
		return -EROFS;
	}

	inode_lock(dir);

	if(!(errno = bfs_btree_find(dir, name, &ino))) {
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

	if((errno = bfs_btree_insert(dir, name, i->inode))) {
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
			i->u.bfs.raw.u.symlink[n] = oldname[n];
		}
		i->u.bfs.raw.u.symlink[n] = 0;
		i->u.bfs.raw.pad[0] = len;
		/* the data.size union field carries the logical size so the
		 * on-disk inode, stat and the size index all agree */
		i->u.bfs.raw.u.data.size = n;
		i->i_size = n;
	} else {
		/* long symlink: the target lives in the data stream; the
		 * INODE_LONG_SYMLINK flag tells Haiku (and us) not to read
		 * the symlink area */
		__off_t offset = 0;

		i->u.bfs.raw.flags |= BFS_INODE_LONG_SYMLINK;
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
		i->u.bfs.raw.pad[0] = len;
		i->u.bfs.raw.u.data.size = len;
	}

	/* the file-name 0x13 small_data record (Haiku SetName) */
	bfs_inode_set_name(i, name);
	bfs_index_add(dir->sb, i, name);

	bfs_dir_touch(dir);
	dir->state |= INODE_DIRTY;

	iput(i);
	inode_unlock(dir);
	return 0;

err:
	bfs_btree_delete(dir, name);
	i->i_nlink = 0;
	iput(i);
	inode_unlock(dir);
	return errno;
}

int bfs_rename(struct inode *i_old, struct inode *dir_old,
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

	if((errno = bfs_btree_delete(dir_old, oldpath))) {
		if(dir_new != dir_old) {
			inode_unlock(dir_new);
		}
		inode_unlock(dir_old);
		return errno;
	}
	if((errno = bfs_btree_insert(dir_new, newpath, i_old->inode))) {
		/* roll back */
		bfs_btree_insert(dir_old, oldpath, i_old->inode);
		if(dir_new != dir_old) {
			inode_unlock(dir_new);
		}
		inode_unlock(dir_old);
		return errno;
	}

	/* update the file-name 0x13 small_data record (Haiku SetName) */
	bfs_inode_set_name(i_old, newpath);
	bfs_index_remove(dir_old->sb, i_old, oldpath);
	bfs_index_add(dir_old->sb, i_old, newpath);

	bfs_dir_touch(dir_old);
	dir_old->state |= INODE_DIRTY;
	if(dir_new != dir_old) {
		bfs_dir_touch(dir_new);
		dir_new->state |= INODE_DIRTY;
	}

	if(dir_new != dir_old) {
		inode_unlock(dir_new);
	}
	inode_unlock(dir_old);
	return 0;
}
