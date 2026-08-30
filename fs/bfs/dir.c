/*
 * fnx/fs/bfs/dir.c
 *
 * BFS directory operations (read-only): readdir, lookup.
 *
 * Directory entries are stored in the B+tree; each entry is a
 * (name, inode-block-number) pair. The readdir implementation walks
 * the tree in key order and presents the entries as a synthetic dirent
 * byte stream so the fd->offset accounting matches the other drivers.
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
#include <fnx/fd.h>
#include <fnx/dirent.h>
#include <fnx/string.h>

extern int bfs_bmap(struct inode *, __off_t, int);
extern int bfs_btree_find(struct inode *, const char *, __ino_t *);
extern int bfs_btree_iterate(struct inode *, int (*)(const char *, __ino_t, void *), void *);

int bfs_dir_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	return -EISDIR;
}

struct bfs_readdir_arg {
	struct dirent *dirent;		/* 32-bit packing target */
	struct dirent64 *dirent64;	/* 64-bit packing target */
	unsigned int size;		/* bytes packed so far */
	unsigned int skip;		/* bytes to skip from the stream start */
	unsigned int count;		/* user buffer size */
	struct fd *f;
	int is64;
};

static int bfs_readdir_entry(const char *name, __ino_t ino, void *arg)
{
	struct bfs_readdir_arg *a = (struct bfs_readdir_arg *)arg;
	int name_len = 0;
	int dirent_len, base_dirent_len;

	while(name[name_len]) {
		name_len++;
	}
	if(a->is64) {
		base_dirent_len = sizeof(__ino64_t) + sizeof(__loff_t)
			+ sizeof(unsigned short) + sizeof(unsigned char);
	} else {
		base_dirent_len = sizeof(a->dirent->d_ino)
			+ sizeof(a->dirent->d_off)
			+ sizeof(a->dirent->d_reclen);
	}
	dirent_len = (base_dirent_len + name_len + 1) + 3;
	dirent_len &= ~3;	/* round up */

	if(a->skip) {
		if(a->skip >= (unsigned int)dirent_len) {
			a->skip -= dirent_len;
			return 0;
		}
		a->skip = 0;
	}

	if((a->size + dirent_len) >= a->count) {
		return 1;	/* stop: user buffer full */
	}

	if(a->is64) {
		struct dirent64 *d = a->dirent64;
		d->d_ino = ino;
		d->d_off = a->f->offset + a->size;
		d->d_reclen = dirent_len;
		d->d_type = DT_UNKNOWN;
		memcpy_b(d->d_name, name, name_len);
		d->d_name[name_len] = 0;
		a->dirent64 = (struct dirent64 *)((char *)d + dirent_len);
	} else {
		struct dirent *d = a->dirent;
		d->d_ino = ino;
		d->d_off = a->f->offset + a->size;
		d->d_reclen = dirent_len;
		memcpy_b(d->d_name, name, name_len);
		d->d_name[name_len] = 0;
		a->dirent = (struct dirent *)((char *)d + dirent_len);
	}
	a->size += dirent_len;
	return 0;
}

int bfs_readdir(struct inode *i, struct fd *f, struct dirent *dirent, __size_t count)
{
	struct bfs_readdir_arg arg;

	if(!(S_ISDIR(i->i_mode))) {
		return -EBADF;
	}

	if(f->offset > i->i_size) {
		f->offset = i->i_size;
	}

	arg.dirent = dirent;
	arg.dirent64 = NULL;
	arg.size = 0;
	arg.skip = f->offset;
	arg.count = count;
	arg.f = f;
	arg.is64 = 0;

	if(bfs_btree_iterate(i, bfs_readdir_entry, &arg)) {
		return -EIO;
	}

	f->offset += arg.size;
	return arg.size;
}

int bfs_readdir64(struct inode *i, struct fd *f, struct dirent64 *dirent, __size_t count)
{
	struct bfs_readdir_arg arg;

	if(!(S_ISDIR(i->i_mode))) {
		return -EBADF;
	}

	if(f->offset > i->i_size) {
		f->offset = i->i_size;
	}

	arg.dirent = NULL;
	arg.dirent64 = dirent;
	arg.size = 0;
	arg.skip = f->offset;
	arg.count = count;
	arg.f = f;
	arg.is64 = 1;

	if(bfs_btree_iterate(i, bfs_readdir_entry, &arg)) {
		return -EIO;
	}

	f->offset += arg.size;
	return arg.size;
}

int bfs_lookup(const char *name, struct inode *dir, struct inode **i_res)
{
	__ino_t ino;

	if(!(S_ISDIR(dir->i_mode))) {
		return -ENOTDIR;
	}
	if(bfs_btree_find(dir, name, &ino)) {
		iput(dir);
		return -ENOENT;
	}
	if(!(*i_res = iget(dir->sb, ino))) {
		iput(dir);
		return -EACCES;
	}
	iput(dir);
	return 0;
}
