/*
 * fnx/kernel/syscalls/xattr.c
 *
 * xattr syscalls (setxattr/getxattr/listxattr/removexattr + the l* and
 * f* variants). The filesystem hooks live in fsop->{get,set,list,
 * remove}xattr; BFS implements them with inline small_data attributes
 * (fs/bfs/xattr.c). Musl's <sys/xattr.h> provides the libc wrappers.
 *
 * ABI notes (x86-64): setxattr takes 5 args (path, name, value, size,
 * flags) -> rdi/rsi/rdx/r10/r8; getxattr/listxattr take 4.
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/fcntl.h>
#include <fnx/fd.h>
#include <fnx/stat.h>
#include <fnx/string.h>
#include <fnx/process.h>
#include <fnx/kernel.h>
#include <fnx/mm.h>

#define XATTR_NAME_MAX	255

/* resolve a path to an inode (following symlinks per 'follow'); the
 * caller owns the returned reference and must iput() it */
static int xattr_path(const char *path, int follow, struct inode **i_res)
{
	struct inode *i;
	char *tmp_path;
	int errno;

	if((errno = malloc_name(path, &tmp_path)) < 0) {
		return errno;
	}
	errno = namei(tmp_path, &i, NULL, follow ? FOLLOW_LINKS : !FOLLOW_LINKS);
	free_name(tmp_path);
	if(errno) {
		return errno;
	}
	*i_res = i;
	return 0;
}

static int xattr_fd(int ufd, struct inode **i_res)
{
	CHECK_UFD(ufd);
	*i_res = fd_table[current->fd[ufd]].inode;
	return 0;
}

/* validate + copy an attribute name into the kernel buffer */
static int xattr_name(const char *uname, char *name)
{
	long len;

	if(!uname) {
		return -EFAULT;
	}
	if((len = strnlen_user(uname, XATTR_NAME_MAX + 1)) < 0) {
		return -EFAULT;
	}
	if(len == 0) {
		return -EINVAL;		/* empty name */
	}
	if(len > XATTR_NAME_MAX) {
		return -ERANGE;		/* name too long */
	}
	if(copy_from_user(name, uname, len + 1)) {
		return -EFAULT;
	}
	if(strchr(name, '/')) {
		return -EACCES;		/* '/' is not allowed in attribute names */
	}
	return 0;
}

/* ---- setxattr ------------------------------------------------------ */

static int do_setxattr(struct inode *i, const char *name, const void *value,
		       __size_t size, int flags)
{
	char *kbuf;
	int errno;

	if(!i->fsop || !i->fsop->setxattr) {
		return -EOPNOTSUPP;
	}
	if(IS_RDONLY_FS(i)) {
		return -EROFS;
	}
	if((errno = check_permission(TO_WRITE, i))) {
		return errno;
	}
	if(size) {
		if(!value) {
			return -EFAULT;
		}
		if(!(kbuf = (char *)kmalloc(size))) {
			return -ENOMEM;
		}
		if(copy_from_user(kbuf, value, size)) {
			kfree((addr_t)kbuf);
			return -EFAULT;
		}
		errno = i->fsop->setxattr(i, name, kbuf, size, flags);
		kfree((addr_t)kbuf);
	} else {
		errno = i->fsop->setxattr(i, name, NULL, 0, flags);
	}
	return errno;
}

int sys_setxattr(const char *path, const char *name, const void *value,
		 __size_t size, int flags)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_path(path, 1, &i))) {
		return errno;
	}
	errno = do_setxattr(i, kname, value, size, flags);
	iput(i);
	return errno;
}

int sys_lsetxattr(const char *path, const char *name, const void *value,
		  __size_t size, int flags)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_path(path, 0, &i))) {
		return errno;
	}
	errno = do_setxattr(i, kname, value, size, flags);
	iput(i);
	return errno;
}

int sys_fsetxattr(int ufd, const char *name, const void *value,
		  __size_t size, int flags)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_fd(ufd, &i))) {
		return errno;
	}
	return do_setxattr(i, kname, value, size, flags);
}

/* ---- getxattr ------------------------------------------------------ */

static int do_getxattr(struct inode *i, const char *name, void *value,
		       __size_t size)
{
	int errno;

	if(!i->fsop || !i->fsop->getxattr) {
		return -EOPNOTSUPP;
	}
	if((errno = check_permission(TO_READ, i))) {
		return errno;
	}
	if(value && size) {
		if(check_user_area(VERIFY_WRITE, value, size)) {
			return -EFAULT;
		}
	}
	return i->fsop->getxattr(i, name, (char *)value, size);
}

int sys_getxattr(const char *path, const char *name, void *value, __size_t size)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_path(path, 1, &i))) {
		return errno;
	}
	errno = do_getxattr(i, kname, value, size);
	iput(i);
	return errno;
}

int sys_lgetxattr(const char *path, const char *name, void *value, __size_t size)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_path(path, 0, &i))) {
		return errno;
	}
	errno = do_getxattr(i, kname, value, size);
	iput(i);
	return errno;
}

int sys_fgetxattr(int ufd, const char *name, void *value, __size_t size)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_fd(ufd, &i))) {
		return errno;
	}
	return do_getxattr(i, kname, value, size);
}

/* ---- listxattr ----------------------------------------------------- */

static int do_listxattr(struct inode *i, char *list, __size_t size)
{
	if(!i->fsop || !i->fsop->listxattr) {
		return -EOPNOTSUPP;
	}
	if(list && size) {
		if(check_user_area(VERIFY_WRITE, list, size)) {
			return -EFAULT;
		}
	}
	return i->fsop->listxattr(i, list, size);
}

int sys_listxattr(const char *path, char *list, __size_t size)
{
	struct inode *i;
	char *tmp_path;
	int errno;

	if((errno = malloc_name(path, &tmp_path)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_path, &i, NULL, FOLLOW_LINKS))) {
		free_name(tmp_path);
		return errno;
	}
	free_name(tmp_path);
	errno = do_listxattr(i, list, size);
	iput(i);
	return errno;
}

int sys_llistxattr(const char *path, char *list, __size_t size)
{
	struct inode *i;
	char *tmp_path;
	int errno;

	if((errno = malloc_name(path, &tmp_path)) < 0) {
		return errno;
	}
	if((errno = namei(tmp_path, &i, NULL, !FOLLOW_LINKS))) {
		free_name(tmp_path);
		return errno;
	}
	free_name(tmp_path);
	errno = do_listxattr(i, list, size);
	iput(i);
	return errno;
}

int sys_flistxattr(int ufd, char *list, __size_t size)
{
	struct inode *i;
	int errno;

	if((errno = xattr_fd(ufd, &i))) {
		return errno;
	}
	return do_listxattr(i, list, size);
}

/* ---- removexattr --------------------------------------------------- */

static int do_removexattr(struct inode *i, const char *name)
{
	int errno;

	if(!i->fsop || !i->fsop->removexattr) {
		return -EOPNOTSUPP;
	}
	if(IS_RDONLY_FS(i)) {
		return -EROFS;
	}
	if((errno = check_permission(TO_WRITE, i))) {
		return errno;
	}
	return i->fsop->removexattr(i, name);
}

int sys_removexattr(const char *path, const char *name)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_path(path, 1, &i))) {
		return errno;
	}
	errno = do_removexattr(i, kname);
	iput(i);
	return errno;
}

int sys_lremovexattr(const char *path, const char *name)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_path(path, 0, &i))) {
		return errno;
	}
	errno = do_removexattr(i, kname);
	iput(i);
	return errno;
}

int sys_fremovexattr(int ufd, const char *name)
{
	struct inode *i;
	char kname[XATTR_NAME_MAX + 1];
	int errno;

	if((errno = xattr_name(name, kname))) {
		return errno;
	}
	if((errno = xattr_fd(ufd, &i))) {
		return errno;
	}
	return do_removexattr(i, kname);
}
