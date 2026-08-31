/*
 * fs/bfs/indices.c - Haiku's indices tree.
 *
 * The superblock's 'indices' run points at the indices directory: a
 * container inode (mode S_INDEX_DIR|S_STR_INDEX|S_IFDIR) whose stream is
 * a STRING B+tree mapping index name -> index file inode. Each index
 * file is itself a container inode (S_INDEX_DIR|S_IFDIR|S_*_INDEX) whose
 * stream is a B+tree over the indexed values: the "name" index keys are
 * file names (STRING), the "size" and "last_modified" index keys are
 * INT64 (bytes of the signed value). Many files share a key, so the
 * index trees use the duplicate-key machinery (fragments / duplicate
 * nodes) in fs/bfs/btree.c.
 *
 * The driver maintains the indices on create/rename/write/truncate/
 * unlink only when the volume has them (sb.indices.len != 0); volumes
 * built without indices are left untouched, exactly like Haiku (which
 * logs "volume doesn't have indices!" and carries on).
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/bfs.h>
#include <fnx/buffer.h>
#include <fnx/stat.h>
#include <fnx/string.h>
#include <fnx/time.h>

/*
 * Add 'value' to the index 'idx' under 'key' (a no-op when the volume
 * has no indices).
 */
static struct inode *bfs_index_dir(struct superblock *sb)
{
	if(!sb->u.bfs.indices_inode) {
		return NULL;
	}
	return iget(sb, sb->u.bfs.indices_inode);
}

static int bfs_index_put(struct superblock *sb, const char *idx, int dtype,
			 const char *key, int keylen, __u64 value)
{
	struct inode *dir, *ti = NULL;
	__ino_t ino;
	int res = -ENOENT;

	if(!(dir = bfs_index_dir(sb))) {
		return -ENOENT;
	}
	if(bfs_btree_find(dir, idx, &ino) == 0) {
		ti = iget(sb, ino);
	}
	iput(dir);
	if(!ti) {
		return -ENOENT;
	}
	res = bfs_btree_insert_value(ti, key, keylen, dtype, value);
		iput(ti);
	return res;
}

static int bfs_index_del(struct superblock *sb, const char *idx, int dtype,
			 const char *key, int keylen, __u64 value)
{
	struct inode *dir, *ti = NULL;
	__ino_t ino;
	int res = -ENOENT;

	if(!(dir = bfs_index_dir(sb))) {
		return -ENOENT;
	}
	if(bfs_btree_find(dir, idx, &ino) == 0) {
		ti = iget(sb, ino);
	}
	iput(dir);
	if(!ti) {
		return -ENOENT;
	}
		res = bfs_btree_delete_value(ti, key, keylen, dtype, value);
		iput(ti);
	return res;
}

/* the "name" + "size" + "last_modified" index keys for an inode */
static __s64 bfs_index_mtime_key(struct inode *i)
{
	return (__s64)((__u64)i->i_mtime << 16);
}

/*
 * Index a freshly created/renamed entry (Haiku's Inode::_AddIndexes).
 */
void bfs_index_add(struct superblock *sb, struct inode *i, const char *name)
{
	__s64 sz = (__s64)i->i_size;
	__s64 mt = bfs_index_mtime_key(i);

	if(!sb->u.bfs.indices_inode) {
		return;
	}
	bfs_index_put(sb, "name", BFS_BTREE_STRING_TYPE, name, strlen(name),
		      i->inode);
	bfs_index_put(sb, "size", BFS_BTREE_INT64_TYPE, (char *)&sz, 8,
		      i->inode);
	bfs_index_put(sb, "last_modified", BFS_BTREE_INT64_TYPE,
		      (char *)&mt, 8, i->inode);
}

/*
 * Remove 'i' from every index (Haiku's Inode::_RemoveIndexes).
 */
void bfs_index_remove(struct superblock *sb, struct inode *i,
		      const char *name)
{
	__s64 sz = (__s64)i->i_size;
	__s64 mt = bfs_index_mtime_key(i);

	if(!sb->u.bfs.indices_inode) {
		return;
	}
	bfs_index_del(sb, "name", BFS_BTREE_STRING_TYPE, name, strlen(name),
		      i->inode);
	bfs_index_del(sb, "size", BFS_BTREE_INT64_TYPE, (char *)&sz, 8,
		      i->inode);
	bfs_index_del(sb, "last_modified", BFS_BTREE_INT64_TYPE,
		      (char *)&mt, 8, i->inode);
}

/*
 * Move the size + last_modified index entries after a write/truncate
 * (the keys changed; the "name" entry is untouched).
 */
void bfs_index_resize(struct superblock *sb, struct inode *i,
		      __off_t old_size, __u64 old_mtime)
{
	__s64 osz = (__s64)old_size;
	__s64 nsz = (__s64)i->i_size;
	__s64 omt = (__s64)((__u64)old_mtime << 16);
	__s64 nmt = bfs_index_mtime_key(i);

	if(!sb->u.bfs.indices_inode) {
		return;
	}
	/* an unlinked inode's entries were already removed by the unlink;
	 * the final truncate-to-0 (from the last iput) must not re-add
	 * them, or the deleted file's size/mtime entries resurrect */
	if(i->i_nlink == 0) {
		return;
	}
	/* only touch an index when its value actually changed: a
	 * same-value resize would otherwise remove + re-insert the same
	 * key (wasted churn, and on the last_modified index it can
	 * resurrect an entry removed by the unlink) */
	if(osz != nsz) {
		bfs_index_del(sb, "size", BFS_BTREE_INT64_TYPE, (char *)&osz, 8,
			      i->inode);
		bfs_index_put(sb, "size", BFS_BTREE_INT64_TYPE, (char *)&nsz, 8,
			      i->inode);
	}
	if(omt != nmt) {
		bfs_index_del(sb, "last_modified", BFS_BTREE_INT64_TYPE,
			      (char *)&omt, 8, i->inode);
		bfs_index_put(sb, "last_modified", BFS_BTREE_INT64_TYPE,
			      (char *)&nmt, 8, i->inode);
	}
}
