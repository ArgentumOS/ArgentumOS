/*
 * fnx/include/fnx/acl.h
 *
 * POSIX Access Control Lists - the single canonical permissions model
 * (docs/permissions-acl.md). One ACL per object is the source of truth;
 * the classic mode bits are its trivial projection, kept in sync by the
 * POSIX rules.
 *
 * Wire format: each entry is 8 bytes, stored native-endian inside the
 * xattr value named below. The layout intentionally matches the classic
 * posix_acl_xattr_entry shape (tag, perm, id), which keeps the format
 * compact and familiar; FNX is the only producer/consumer.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_ACL_H
#define _FNX_ACL_H

#include <fnx/types.h>
#include <fnx/fs.h>

/* xattr names carrying the ACLs (see docs/permissions-acl.md) */
#define XATTR_ACL_ACCESS	"system.posix_acl_access"
#define XATTR_ACL_DEFAULT	"system.posix_acl_default"

/* one ACL entry, 8 bytes on the wire */
struct acl_xattr_entry {
	__u16 e_tag;
	__u16 e_perm;		/* rwx bits (0..7) */
	__u32 e_id;
};

/* entry tags */
#define ACL_USER_OBJ	0x01	/* the object owner */
#define ACL_USER	0x02	/* a named user (e_id = uid) */
#define ACL_GROUP_OBJ	0x04	/* the object's owning group */
#define ACL_GROUP	0x08	/* a named group (e_id = gid) */
#define ACL_MASK	0x10	/* limits all group-class entries */
#define ACL_OTHER	0x20	/* everyone else */

#define ACL_UNDEFINED_ID	0xFFFFFFFF	/* USER_OBJ/GROUP_OBJ/MASK/OTHER */

/* largest sane ACL: comfortably inside the BFS small_data cap */
#define ACL_MAX_ENTRIES	32

/* Validate an ACL xattr payload: canonical entry order, exactly one
 * owner/owning-group/other, at most one mask (mandatory when any named
 * entry exists), valid tags/perms. Returns 0 or a negative errno. */
int acl_validate(const char *buf, __size_t size);

/* Run the POSIX ACL permission algorithm for the current process on
 * inode i: owner entry -> named-user entry -> group-class (through the
 * mask) -> other. Inodes without a stored access ACL get the trivial
 * ACL projected from i_mode, so the algorithm is always the check.
 * Returns 0 if the requested mask is granted, else -EACCES. */
int acl_permission(struct inode *i, int mask);

/* Keep i_mode a true view of a stored access ACL after a validated
 * setxattr: owner bits from the owner entry, the group-class bits from
 * the MASK when one exists else the owning-group entry, other bits from
 * other. The type/suid/sgid/sticky bits are preserved. */
void acl_sync_mode(struct inode *i, const void *buf, __size_t size);

/* 1 when an access ACL is equivalent to plain mode bits (no named
 * entries, and a MASK, if present, equal to the owning-group perms) -
 * such an ACL carries no information beyond the mode and is compressed
 * away on set. When trivial, *mode receives the projection
 * (owner << 6 | group << 3 | other). */
int acl_equiv_mode(const void *buf, __size_t size, __u16 *mode);

/* chmod on an inode that has a stored access ACL edits those same
 * entries (Linux posix_acl_chmod semantics): the new owner bits go to
 * USER_OBJ, the new other bits to OTHER, and the new group-class bits
 * to the MASK when one exists (the mask is what stat() shows as the
 * group class) else to GROUP_OBJ. Inodes without a stored ACL need no
 * work (the mode change itself is the ACL change). Returns 0 or a
 * negative errno; on failure the mode bits are left untouched. */
int acl_chmod(struct inode *i, __mode_t mode);

#endif /* _FNX_ACL_H */
