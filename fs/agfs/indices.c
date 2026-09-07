/*
 * fs/agfs/indices.c - Haiku's indices tree.
 *
 * The superblock's 'indices' run points at the indices directory: a
 * container inode (mode S_INDEX_DIR|S_STR_INDEX|S_IFDIR) whose stream is
 * a STRING B+tree mapping index name -> index file inode. Each index
 * file is itself a container inode (S_INDEX_DIR|S_IFDIR|S_*_INDEX) whose
 * stream is a B+tree over the indexed values: the "name" index keys are
 * file names (STRING), the "size" and "last_modified" index keys are
 * INT64 (bytes of the signed value). Many files share a key, so the
 * index trees use the duplicate-key machinery (fragments / duplicate
 * nodes) in fs/agfs/btree.c.
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
#include <fnx/agfs.h>
#include <fnx/buffer.h>
#include <fnx/stat.h>
#include <fnx/string.h>
#include <fnx/time.h>

/*
 * Add 'value' to the index 'idx' under 'key' (a no-op when the volume
 * has no indices).
 */
static struct inode *agfs_index_dir(struct superblock *sb)
{
	if(!sb->u.agfs.indices_inode) {
		return NULL;
	}
	return iget(sb, sb->u.agfs.indices_inode);
}

static int agfs_index_put(struct superblock *sb, const char *idx, int dtype,
			 const char *key, int keylen, __u64 value)
{
	struct inode *dir, *ti = NULL;
	__ino_t ino;
	int res = -ENOENT;

	if(!(dir = agfs_index_dir(sb))) {
		return -ENOENT;
	}
	if(agfs_btree_find(dir, idx, &ino) == 0) {
		ti = iget(sb, ino);
	}
	iput(dir);
	if(!ti) {
		return -ENOENT;
	}
	res = agfs_btree_insert_value(ti, key, keylen, dtype, value);
		iput(ti);
	return res;
}

static int agfs_index_del(struct superblock *sb, const char *idx, int dtype,
			 const char *key, int keylen, __u64 value)
{
	struct inode *dir, *ti = NULL;
	__ino_t ino;
	int res = -ENOENT;

	if(!(dir = agfs_index_dir(sb))) {
		return -ENOENT;
	}
	if(agfs_btree_find(dir, idx, &ino) == 0) {
		ti = iget(sb, ino);
	}
	iput(dir);
	if(!ti) {
		return -ENOENT;
	}
		res = agfs_btree_delete_value(ti, key, keylen, dtype, value);
		iput(ti);
	return res;
}

/* the "name" + "size" + "last_modified" index keys for an inode.
 * The mtime key is the FULL stored value (seconds << 16 | subsecond),
 * set by agfs_touch_mtime() into the in-memory raw inode — the same
 * value agfs_write_inode() puts on disk, so Haiku's index and our
 * verifier agree. */
static __s64 agfs_index_mtime_key(struct inode *i)
{
	return (__s64)i->u.agfs.raw.last_modified_time;
}

/*
 * Index a freshly created/renamed entry (Haiku's Inode::_AddIndexes).
 */
void agfs_index_add(struct superblock *sb, struct inode *i, const char *name)
{
	__s64 sz = (__s64)i->i_size;
	__s64 mt = agfs_index_mtime_key(i);

	if(!sb->u.agfs.indices_inode) {
		return;
	}
	/* one transaction: a kill between the per-tree puts would replay a
	 * partially indexed file (e.g. in "size" but not "last_modified"),
	 * which the verifier flags as an index mismatch */
	agfs_log_begin(sb);
	agfs_index_put(sb, "name", AGFS_BTREE_STRING_TYPE, name, strlen(name),
		      i->inode);
	agfs_index_put(sb, "size", AGFS_BTREE_INT64_TYPE, (char *)&sz, 8,
		      i->inode);
	agfs_index_put(sb, "last_modified", AGFS_BTREE_INT64_TYPE,
		      (char *)&mt, 8, i->inode);
	agfs_log_commit(sb);
}

/*
 * Remove 'i' from every index (Haiku's Inode::_RemoveIndexes).
 */
void agfs_index_remove(struct superblock *sb, struct inode *i,
		      const char *name)
{
	__s64 sz = (__s64)i->i_size;
	__s64 mt = agfs_index_mtime_key(i);

	if(!sb->u.agfs.indices_inode) {
		return;
	}
	agfs_log_begin(sb);
	agfs_index_del(sb, "name", AGFS_BTREE_STRING_TYPE, name, strlen(name),
		      i->inode);
	agfs_index_del(sb, "size", AGFS_BTREE_INT64_TYPE, (char *)&sz, 8,
		      i->inode);
	agfs_index_del(sb, "last_modified", AGFS_BTREE_INT64_TYPE,
		      (char *)&mt, 8, i->inode);
	agfs_log_commit(sb);
}

/*
 * Move the size + last_modified index entries after a write/truncate
 * (the keys changed; the "name" entry is untouched).
 */
void agfs_index_resize(struct superblock *sb, struct inode *i,
		      __off_t old_size, __u64 old_mtime)
{
	/* old_mtime is the FULL old stored key (sec << 16 | subsecond),
	 * captured from the raw inode before the update */
	__s64 osz = (__s64)old_size;
	__s64 nsz = (__s64)i->i_size;
	__s64 omt = (__s64)old_mtime;
	__s64 nmt = agfs_index_mtime_key(i);

	if(!sb->u.agfs.indices_inode) {
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
	 * resurrect an entry removed by the unlink).
	 *
	 * Remove the old key, then insert the new one unconditionally —
	 * exactly Haiku's Index::Update(): a missing old key (an inode
	 * that was never indexed: the root and the mkagfs/foreign-created
	 * files whose indices the image builder starts empty) is
	 * tolerated, and the inode is silently added on its first
	 * modification ("index-on-modify"). Gating the put on the del
	 * succeeding left such inodes unindexed forever even after a
	 * session modified them, which agfscheck flagged (and which Haiku
	 * would not). Unlinked inodes are already excluded above (the
	 * i_nlink == 0 early return), so this cannot resurrect a deleted
	 * file's entries. */
	/* Remove the old key, then insert the new one for every change —
	 * and also when the value is unchanged but the entry is missing
	 * (a never-indexed inode — the root and foreign-created files the
	 * image builder starts unindexed — must be added on its first
	 * session modification, or it lands in the mtime/name indices but
	 * not the size one). A same-value del+put of an entry that IS
	 * present is a harmless no-op net; the i_nlink == 0 guard above is
	 * what prevents an unlinked inode's entries from resurrecting. */
	agfs_log_begin(sb);
	/* write the inode in the same transaction as its index move: the
	 * size/mtime change and the index del+put are otherwise separate
	 * txs (the inode itself is flushed at the fd's close/iput), and a
	 * kill between them replays an inode whose on-disk size/mtime does
	 * not match its index entries ("index size/last_modified
	 * mismatch"). The record is idempotent with the later close-time
	 * flush. */
	agfs_write_inode(i);
	agfs_index_del(sb, "size", AGFS_BTREE_INT64_TYPE, (char *)&osz,
		       8, i->inode);
	agfs_index_put(sb, "size", AGFS_BTREE_INT64_TYPE, (char *)&nsz,
		       8, i->inode);
	agfs_index_del(sb, "last_modified", AGFS_BTREE_INT64_TYPE,
		       (char *)&omt, 8, i->inode);
	agfs_index_put(sb, "last_modified", AGFS_BTREE_INT64_TYPE,
		       (char *)&nmt, 8, i->inode);
	agfs_log_commit(sb);
}
