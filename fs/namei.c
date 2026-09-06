/*
 * fnx/fs/namei.c
 *
 * Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/types.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/stat.h>
#include <fnx/mm.h>
#include <fnx/mman.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

static int do_namei(char *path, struct inode *dir, struct inode **i_res, struct inode **d_res, int follow_links)
{
	char *name, *ptr_name;
	struct inode *i;
	struct superblock *sb;

	int errno;

	*i_res = dir;
	for(;;) {
		while(*path == '/') {
			path++;
		}
		if(*path == '\0') {
			return 0;
		}

		/* extracts the next component of the path */
		if(!(name = (char *)kmalloc(NAME_MAX + 1))) {
			return -ENOMEM;
		}
		ptr_name = name;
		while(*path != '\0' && *path != '/') {
			if(ptr_name > (name + NAME_MAX)) {
				/* B_FILE_NAME_LENGTH = 255 (Haiku); reject, do
				 * not silently truncate */
				kfree((addr_t)name);
				return -ENAMETOOLONG;
			}
			*ptr_name++ = *path++;
		}
		*ptr_name = 0;

		/*
		 * If the inode is the root of a file system, then return the
		 * inode on which the file system was mounted.
		 */
		if(name[0] == '.' && name[1] == '.' && name[2] == '\0') {
			if(dir == dir->sb->root) {
				sb = dir->sb;
				iput(dir);
				dir = sb->dir;
				dir->count++;
			}
		}

		if((errno = check_permission(TO_EXEC, dir))) {
			break;
		}

		dir->count++;
		if((errno = dir->fsop->lookup(name, dir, &i))) {
			break;
		}

		kfree((addr_t)name);
		if(*path == '/') {
			if(!S_ISDIR(i->i_mode) && !S_ISLNK(i->i_mode)) {
				iput(dir);
				iput(i);
				return -ENOTDIR;
			}
			if(S_ISLNK(i->i_mode)) {
				if(i->fsop->followlink) {
					if((errno = i->fsop->followlink(dir, i, &i))) {
						iput(dir);
						return errno;
					}
				}
			}
		} else {
			if(i->fsop->followlink && follow_links) {
				if((errno = i->fsop->followlink(dir, i, &i))) {
					iput(dir);
					return errno;
				}
			}
		}

		if(d_res) {
			if(*d_res) {
				iput(*d_res);
			}
			*d_res = dir;
		} else {
			iput(dir);
		}
		dir = i;
		*i_res = i;
	}

	kfree((addr_t)name);
	if(d_res) {
		if(*d_res) {
			iput(*d_res);
		}
		/*
		 * If that was the last component of the path,
		 * then return the directory.
		 */
		if(*path == '\0') {
			*d_res = dir;
			dir->count++;
		} else {
			/* that's a non-existent directory */
			*d_res = NULL;
			errno = -ENOENT;
		}
		iput(dir);
		*i_res = NULL;
	} else {
		iput(dir);
	}

	return errno;
}

int parse_namei(char *path, struct inode *base_dir, struct inode **i_res, struct inode **d_res, int follow_links)
{
	struct inode *dir;
	char *scratch;
	int s, errno;

	/*
	 * do_namei() treats a non-NULL *d_res as the previous component's
	 * directory and iputs it, so the result slots MUST start empty.
	 * Some callers relied on zeroed kernel stacks (and sys_mkdirat
	 * even seeded *d_res with its base_dir, draining the dirfd
	 * inode's reference on every mkdir of an existing name - the
	 * 'already freed inode' storm under cp -R). Null both here so
	 * every caller is safe regardless of how it initializes them.
	 */
	if(i_res) {
		*i_res = NULL;
	}
	if(d_res) {
		*d_res = NULL;
	}

	if(!path) {
		return -EFAULT;
	}
	if(*path == '\0') {
		return -ENOENT;
	}

	/*
	 * FSH device shorthand: a leading '@' as the very first component
	 * of the path (absolute or relative) resolves under
	 * /System/Devices — e.g. "@null" -> /System/Devices/null,
	 * "/@console" -> /System/Devices/console, "@" alone is the
	 * devices root itself. Purpose: a short, namespace-pollution-free
	 * way to name devices without typing /System/Devices each time.
	 *
	 * The magic is position-1-only, so it can never collide with real
	 * files: mid-path components ("a/@b") and escaped forms
	 * ("./@x") are ordinary names. Resolution is a fixed redirect to
	 * the real /System/Devices directory under the process root, so
	 * it is chroot-safe (a chroot without that tree gets ENOENT, never
	 * an escape) and requires no per-device table.
	 */
	for(s = 0; path[s] == '/'; s++) {
	}
	if(path[s] == '@') {
		/* scratch = "/System/Devices" + path after the '@' */
		if(!(scratch = (char *)kmalloc(strlen(path) + 16))) {
			return -ENOMEM;
		}
		strcpy(scratch, "/System/Devices");
		if(path[s + 1]) {
			strcat(scratch, "/");
			strcat(scratch, path + s + 1);
		}
		dir = current->root;
		dir->count++;
		errno = do_namei(scratch, dir, i_res, d_res, follow_links);
		kfree((addr_t)scratch);
		return errno;
	}

	if(!(dir = base_dir)) {
		dir = current->pwd;
	}

	/* it is definitely an absolute path */
	if(path[0] == '/') {
		dir = current->root;
	}
	dir->count++;
	errno = do_namei(path, dir, i_res, d_res, follow_links);
	return errno;
}

/*
 * namei() returns:
 * i_res -> the inode of the last component of the path, or NULL.
 * d_res -> the inode of the directory where i_res resides, or NULL.
 */
int namei(char *path, struct inode **i_res, struct inode **d_res, int follow_links)
{
	*i_res = NULL;
	if(d_res) {
		*d_res = NULL;
	}
	return parse_namei(path, NULL, i_res, d_res, follow_links);
}
