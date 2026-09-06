/*
 * fnx/kernel/acl.c
 *
 * POSIX Access Control Lists - the single canonical permissions model
 * (docs/design/permissions-acl.md). See include/fnx/acl.h for the wire format.
 *
 * acl_validate() runs when an ACL xattr is written (kernel/syscalls/
 * xattr.c) so a bad ACL never reaches the filesystem.
 *
 * acl_permission() is the algorithm behind check_permission(): owner
 * entry, then a named-user entry, then the group class (limited by the
 * mask), then other. Inodes without a stored access ACL get the trivial
 * ACL projected from the mode bits, so the ACL algorithm is always the
 * check and the mode bits are only ever a view of the ACL.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/acl.h>
#include <fnx/process.h>
#include <fnx/stat.h>
#include <fnx/errno.h>
#include <fnx/limits.h>
#include <fnx/string.h>
#include <fnx/fs.h>
#include <fnx/syscalls.h>
#include <fnx/kernel.h>


/*
 * Validate an ACL payload. Canonical order is enforced so the parser in
 * acl_permission() can trust the layout:
 *
 *	USER_OBJ, USER(ascending id), GROUP_OBJ, GROUP(ascending id),
 *	MASK?, OTHER
 *
 * Rules: exactly one USER_OBJ / GROUP_OBJ / OTHER, at most one MASK, a
 * MASK is mandatory when any named user/group entry exists, perms only
 * use the rwx bits, named entries never carry the undefined id.
 */
int acl_validate(const char *buf, __size_t size)
{
	const struct acl_xattr_entry *e;
	int n, nents;
	int seen_user_obj = 0, seen_group_obj = 0, seen_mask = 0, seen_other = 0;
	int named_uid = 0, named_gid = 0;
	__u32 last_uid = 0, last_gid = 0;
	unsigned int tag;

	if(!buf) {
		return -EFAULT;
	}
	if(size == 0 || size % sizeof(struct acl_xattr_entry)) {
		return -EINVAL;
	}
	nents = size / sizeof(struct acl_xattr_entry);
	if(nents < 3 || nents > ACL_MAX_ENTRIES) {
		return -EINVAL;
	}

	for(n = 0; n < nents; n++) {
		e = &((const struct acl_xattr_entry *)buf)[n];
		tag = e->e_tag;
		if(e->e_perm & ~7) {
			return -EINVAL;		/* perms use only the rwx bits */
		}
		switch(tag) {
			case ACL_USER_OBJ:
				if(seen_user_obj || n != 0) {
					return -EINVAL;	/* must be first, unique */
				}
				seen_user_obj = 1;
				break;
			case ACL_USER:
				/* between USER_OBJ and GROUP_OBJ, ids ascending */
				if(!seen_user_obj || seen_group_obj) {
					return -EINVAL;
				}
				if(e->e_id == ACL_UNDEFINED_ID) {
					return -EINVAL;
				}
				if(named_uid && e->e_id <= last_uid) {
					return -EINVAL;	/* not ascending */
				}
				last_uid = e->e_id;
				named_uid = 1;
				break;
			case ACL_GROUP_OBJ:
				/* the first group entry: after the named users */
				if(!seen_user_obj || seen_group_obj) {
					return -EINVAL;
				}
				seen_group_obj = 1;
				break;
			case ACL_GROUP:
				/* between GROUP_OBJ and MASK, ids ascending */
				if(!seen_group_obj || seen_mask) {
					return -EINVAL;
				}
				if(e->e_id == ACL_UNDEFINED_ID) {
					return -EINVAL;
				}
				if(named_gid && e->e_id <= last_gid) {
					return -EINVAL;	/* not ascending */
				}
				last_gid = e->e_id;
				named_gid = 1;
				break;
			case ACL_MASK:
				if(seen_mask || !seen_group_obj) {
					return -EINVAL;	/* after the group class */
				}
				seen_mask = 1;
				break;
			case ACL_OTHER:
				if(seen_other || !seen_group_obj || n != nents - 1) {
					return -EINVAL;	/* last */
				}
				seen_other = 1;
				break;
			default:
				return -EINVAL;
		}
	}
	if(!seen_user_obj || !seen_group_obj || !seen_other) {
		return -EINVAL;
	}
	if((named_uid || named_gid) && !seen_mask) {
		return -EINVAL;		/* mask required with named entries */
	}
	return 0;
}

/* Update i_mode's rwx bits from a stored access ACL (Linux inode_set_acl
 * semantics): owner bits from the owner entry, the group-class bits from
 * the MASK when one exists (named entries are present then) else from the
 * owning-group entry, other bits from other. The type/suid/sgid/sticky
 * bits are preserved. Called after a validated access-ACL set so that
 * stat() and the mode bits stay a true view of the ACL. */
void acl_sync_mode(struct inode *i, const void *buf, __size_t size)
{
	const struct acl_xattr_entry *e;
	int n, nents;
	unsigned int owner = 0, group = 0, mask = 0, other = 0;
	int have_mask = 0;

	if(!buf || !size || size % sizeof(struct acl_xattr_entry)) {
		return;
	}
	nents = size / sizeof(struct acl_xattr_entry);
	for(n = 0; n < nents; n++) {
		e = &((const struct acl_xattr_entry *)buf)[n];
		switch(e->e_tag) {
			case ACL_USER_OBJ: owner = e->e_perm; break;
			case ACL_GROUP_OBJ: group = e->e_perm; break;
			case ACL_MASK: mask = e->e_perm; have_mask = 1; break;
			case ACL_OTHER: other = e->e_perm; break;
		}
	}
	if(!have_mask) {
		mask = group;
	}
	i->i_mode = (i->i_mode & ~(S_IRWXU | S_IRWXG | S_IRWXO)) |
		(owner << 6) | (mask << 3) | other;
}

/* 1 when an access ACL carries no information beyond the mode bits: no
 * named user/group entries, and a MASK (if present) equal to the
 * owning-group perms. Such an ACL is never stored - setxattr compresses
 * it away and applies the mode change. *mode receives the projection. */
int acl_equiv_mode(const void *buf, __size_t size, __u16 *mode)
{
	const struct acl_xattr_entry *e;
	int n, nents;
	__u16 owner = 0, group = 0, mask = 0, other = 0;
	int have_mask = 0;

	if(!buf || !size || size % sizeof(struct acl_xattr_entry) ||
	   size < 3 * sizeof(struct acl_xattr_entry)) {
		return 0;
	}
	nents = size / sizeof(struct acl_xattr_entry);
	for(n = 0; n < nents; n++) {
		e = &((const struct acl_xattr_entry *)buf)[n];
		switch(e->e_tag) {
			case ACL_USER_OBJ: owner = e->e_perm; break;
			case ACL_GROUP_OBJ: group = e->e_perm; break;
			case ACL_MASK: mask = e->e_perm; have_mask = 1; break;
			case ACL_OTHER: other = e->e_perm; break;
			default: return 0;	/* a named entry: not trivial */
		}
	}
	if(have_mask && mask != group) {
		return 0;
	}
	if(mode) {
		*mode = (owner << 6) | (group << 3) | other;
	}
	return 1;
}

/* chmod on an inode that has a stored access ACL edits those same
 * entries: the new owner bits go to USER_OBJ, the new other bits to
 * OTHER, and the new group-class bits to the MASK when one exists (the
 * mask is what stat() shows as the group class) else to GROUP_OBJ.
 * Mode-only inodes (no stored ACL) need no work: the mode change itself
 * is the ACL change. Returns 0 or a negative errno; the mode bits are
 * left untouched on failure. */
int acl_chmod(struct inode *i, __mode_t mode)
{
	char buf[ACL_XATTR_SZ];
	struct acl_xattr_entry *e;
	__u16 uperm = (mode >> 6) & 7;
	__u16 gperm = (mode >> 3) & 7;
	__u16 operm = mode & 7;
	int n, nents, err, have_mask = 0, i2;

	if(!i->fsop || !i->fsop->getxattr || !i->fsop->setxattr) {
		return 0;	/* no xattr storage: the mode is the ACL */
	}
	n = i->fsop->getxattr(i, XATTR_ACL_ACCESS, buf, sizeof(buf));
	if(n < 0) {
		if(n == -ENODATA || n == -EOPNOTSUPP) {
			return 0;	/* no stored ACL: mode-only */
		}
		return n;
	}
	if(n == 0 || n % sizeof(struct acl_xattr_entry)) {
		return -EIO;
	}
	nents = n / sizeof(struct acl_xattr_entry);
	if(nents > ACL_MAX_ENTRIES) {
		return -EIO;
	}
	if((err = acl_validate(buf, n))) {
		return -EIO;	/* corrupted on disk - do not guess */
	}
	for(i2 = 0; i2 < nents; i2++) {
		e = &((struct acl_xattr_entry *)buf)[i2];
		if(e->e_tag == ACL_MASK) {
			have_mask = 1;
			break;
		}
	}
	for(i2 = 0; i2 < nents; i2++) {
		e = &((struct acl_xattr_entry *)buf)[i2];
		switch(e->e_tag) {
			case ACL_USER_OBJ: e->e_perm = uperm; break;
			case ACL_GROUP_OBJ:
				if(!have_mask) {
					e->e_perm = gperm;
				}
				break;
			case ACL_MASK: e->e_perm = gperm; break;
			case ACL_OTHER: e->e_perm = operm; break;
		}
	}
	return i->fsop->setxattr(i, XATTR_ACL_ACCESS, buf, n, 0);
}

/* ---- default-ACL inheritance (M3) ---------------------------------- */

/* POSIX default-ACL inheritance for a freshly created object (Linux
 * posix_acl_create_masq): start from the parent directory's default
 * ACL and intersect each class's entry with the create mode's bits,
 * folding the result back into the mode so the two agree:
 *   owner := default USER_OBJ & mode-owner bits (mode-owner := that)
 *   other := default OTHER & mode-other bits  (mode-other := that)
 *   group := default MASK (or GROUP_OBJ when no mask) & mode-group
 *           (mode-group := that)
 * Named entries are inherited unchanged - the (intersected) mask is
 * what limits them. *nontrivial is set when the result carries named
 * entries or a mask and therefore must be stored rather than folded
 * into the mode. The suid/sgid/sticky bits of the mode are preserved.
 * out must hold at least dflsz bytes. */
void acl_default_masq(const void *dfl, __size_t dflsz, __mode_t mode,
		      __mode_t *mode_out, void *out, __size_t outsz,
		      int *nontrivial)
{
	struct acl_xattr_entry *e, *group_obj = NULL, *mask_obj = NULL;
	int n, nents;
	__mode_t rwx;

	*nontrivial = 0;
	if(!dfl || dflsz < 3 * sizeof(struct acl_xattr_entry) ||
	   dflsz % sizeof(struct acl_xattr_entry) ||
	   outsz < dflsz) {
		*mode_out = mode;
		return;
	}
	memcpy_b(out, dfl, dflsz);
	nents = dflsz / sizeof(struct acl_xattr_entry);
	rwx = mode & (S_IRWXU | S_IRWXG | S_IRWXO);

	for(n = 0; n < nents; n++) {
		e = &((struct acl_xattr_entry *)out)[n];
		switch(e->e_tag) {
			case ACL_USER_OBJ:
				e->e_perm &= (rwx >> 6) & 7;
				rwx = (rwx & ~S_IRWXU) | (e->e_perm << 6);
				break;
			case ACL_OTHER:
				e->e_perm &= rwx & 7;
				rwx = (rwx & ~S_IRWXO) | e->e_perm;
				break;
			case ACL_GROUP_OBJ:
				group_obj = e;
				break;
			case ACL_MASK:
				mask_obj = e;
				*nontrivial = 1;
				break;
			case ACL_USER:
			case ACL_GROUP:
				*nontrivial = 1;	/* inherited, unmasked here */
				break;
		}
	}
	if(mask_obj) {
		mask_obj->e_perm &= (rwx >> 3) & 7;
		rwx = (rwx & ~S_IRWXG) | (mask_obj->e_perm << 3);
	} else if(group_obj) {
		group_obj->e_perm &= (rwx >> 3) & 7;
		rwx = (rwx & ~S_IRWXG) | (group_obj->e_perm << 3);
	}
	*mode_out = (mode & ~(S_IRWXU | S_IRWXG | S_IRWXO)) | rwx;
}

/* Apply POSIX default-ACL inheritance after dir->fsop->create/mkdir
 * produced the new object i. When the parent carries a
 * system.posix_acl_default, the new access ACL is that default
 * intersected with the create mode (acl_default_masq) and the mode
 * bits are folded to match - this also overrides the umask the
 * filesystem applied, which POSIX says is ignored when a default ACL
 * exists. A new directory additionally copies the parent's default ACL
 * verbatim so the inheritance continues. Storage is best-effort: a
 * failed xattr write only loses named grants, never over-grants (the
 * folded mode stays correct). Returns 1 when an ACL was inherited,
 * else 0 (no default ACL: umask rules apply). */
int acl_inherit_default(struct inode *dir, struct inode *i, __mode_t mode,
			int is_dir)
{
	char dfl[ACL_XATTR_SZ], acc[ACL_XATTR_SZ];
	__mode_t fmode;
	int n, non_trivial;

	/* symlinks never carry ACLs; only an xattr-capable parent can
	 * have a default ACL */
	if(S_ISLNK(i->i_mode) || !dir->fsop || !dir->fsop->getxattr) {
		return 0;
	}
	n = dir->fsop->getxattr(dir, XATTR_ACL_DEFAULT, dfl, sizeof(dfl));
	if(n <= 0 || n % sizeof(struct acl_xattr_entry)) {
		/* -ENODATA/-EOPNOTSUPP or an unreadable payload: nothing
		 * to inherit - the filesystem's umask result stands */
		return 0;
	}
	acl_default_masq(dfl, n, mode, &fmode, acc, sizeof(acc), &non_trivial);

	i->i_mode = (i->i_mode & ~(S_IRWXU | S_IRWXG | S_IRWXO)) |
		(fmode & (S_IRWXU | S_IRWXG | S_IRWXO));
	i->i_ctime = CURRENT_TIME;
	i->state |= INODE_DIRTY;

	if(i->fsop && i->fsop->setxattr) {
		if(non_trivial) {
			i->fsop->setxattr(i, XATTR_ACL_ACCESS, acc, n, 0);
		}
		if(is_dir) {
			i->fsop->setxattr(i, XATTR_ACL_DEFAULT, dfl, n, 0);
		}
	}
	return 1;
}

/* ---- the permission algorithm -------------------------------------- */

/* one in-memory ACL entry (the wire entries are already in canonical
 * order, so the check walks them linearly) */
struct acl_entry {
	unsigned int tag;
	unsigned int perm;
	__u32 id;
};

/* caller's group-class id (fsgid, or the real gid under PF_USEREAL) */
static __u32 acl_caller_gid(void)
{
	if(current->flags & PF_USEREAL) {
		return current->gid;
	}
	return current->fsgid;
}

/* is gid one of the caller's groups (owner/supplementary)? */
static int acl_in_groups(__u32 gid)
{
	int n;

	if(acl_caller_gid() == gid) {
		return 1;
	}
	for(n = 0; n < NGROUPS_MAX; n++) {
		if(current->groups[n] == -1) {
			break;
		}
		if((__u32)current->groups[n] == gid) {
			return 1;
		}
	}
	return 0;
}

static int acl_check_entries(const struct acl_entry *e, int nents,
			     struct inode *i, int mask)
{
	__u32 uid;
	int n;
	int mperm = 7;		/* no mask entry: group class unmasked */
	int matched_groups, gperms;

	uid = (current->flags & PF_USEREAL) ? current->uid : current->fsuid;

	/* the MASK limits the named users AND the group class (acl(5): it
	 * is the maximum that ACL_USER, ACL_GROUP_OBJ and ACL_GROUP can
	 * grant). The owner is not masked. */
	for(n = 0; n < nents; n++) {
		if(e[n].tag == ACL_MASK) {
			mperm = e[n].perm;
			break;
		}
	}

	/* 1. the object owner -> owner entry */
	if(i->i_uid == uid) {
		for(n = 0; n < nents; n++) {
			if(e[n].tag == ACL_USER_OBJ) {
				return ((e[n].perm & mask) == mask) ? 0 : -EACCES;
			}
		}
		return -EACCES;
	}

	/* 2. a named-user entry matching the caller (masked: chmod of the
	 * group-class bits must also shrink what a named user can do) */
	for(n = 0; n < nents; n++) {
		if(e[n].tag == ACL_USER && e[n].id == uid) {
			return ((mperm & e[n].perm & mask) == mask) ? 0 : -EACCES;
		}
	}

	/* 3. the group class, through the mask: the owning group (its
	 * entry) plus any named group the caller belongs to. POSIX sums
	 * every matching group entry (their perms OR together), then the
	 * MASK limits the class; OTHER applies only when NO group entry
	 * matched. In particular a masked-out group class must NOT fall
	 * through to OTHER, or the mask could be bypassed by setting OTHER
	 * permissive while blocking the group class. */
	matched_groups = 0;
	gperms = 0;
	for(n = 0; n < nents; n++) {
		if(e[n].tag == ACL_GROUP_OBJ && acl_in_groups(i->i_gid)) {
			matched_groups = 1;
			gperms |= e[n].perm;
		} else if(e[n].tag == ACL_GROUP && acl_in_groups(e[n].id)) {
			matched_groups = 1;
			gperms |= e[n].perm;
		}
	}
	if(matched_groups) {
		return ((mperm & gperms & mask) == mask) ? 0 : -EACCES;
	}
	/* the group class matched nobody: fall through to other */

	/* 4. everyone else -> other entry */
	for(n = 0; n < nents; n++) {
		if(e[n].tag == ACL_OTHER) {
			return ((e[n].perm & mask) == mask) ? 0 : -EACCES;
		}
	}
	return -EACCES;
}

/* the trivial ACL projected from the mode bits: owner/group-obj/other,
 * no mask, no named entries */
static int acl_trivial_permission(struct inode *i, int mask)
{
	struct acl_entry e[3];

	e[0].tag = ACL_USER_OBJ;
	e[0].perm = (i->i_mode >> 6) & 7;
	e[0].id = ACL_UNDEFINED_ID;
	e[1].tag = ACL_GROUP_OBJ;
	e[1].perm = (i->i_mode >> 3) & 7;
	e[1].id = ACL_UNDEFINED_ID;
	e[2].tag = ACL_OTHER;
	e[2].perm = i->i_mode & 7;
	e[2].id = ACL_UNDEFINED_ID;
	return acl_check_entries(e, 3, i, mask);
}

int acl_permission(struct inode *i, int mask)
{
	char buf[ACL_XATTR_SZ];
	struct acl_entry e[ACL_MAX_ENTRIES];
	const struct acl_xattr_entry *we;
	int nents, n, err;

	if(i->fsop && i->fsop->getxattr) {
		n = i->fsop->getxattr(i, XATTR_ACL_ACCESS, buf, sizeof(buf));
	} else {
		n = -ENODATA;
	}
	if(n < 0) {
		if(n == -ENODATA || n == -EOPNOTSUPP) {
			/* no stored ACL: the mode bits are the ACL */
			return acl_trivial_permission(i, mask);
		}
		return n;	/* -ERANGE etc: report rather than guess */
	}

	/* a stored ACL: it was validated at write time; parse defensively */
	if(n == 0 || n % sizeof(struct acl_xattr_entry)) {
		return -EIO;
	}
	nents = n / sizeof(struct acl_xattr_entry);
	if((err = acl_validate(buf, n))) {
		return -EIO;	/* corrupted on disk - do not guess */
	}
	for(n = 0; n < nents; n++) {
		we = &((const struct acl_xattr_entry *)buf)[n];
		e[n].tag = we->e_tag;
		e[n].perm = we->e_perm;
		e[n].id = we->e_id;
	}
	return acl_check_entries(e, nents, i, mask);
}
