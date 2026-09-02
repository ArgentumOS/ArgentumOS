/* acl.c: the 'acl' userland tool - the userland face of FNX's single
 * canonical permissions model (docs/permissions-acl.md, M4).
 *
 * Usage:
 *   acl get <path>               show the access ACL; a file without a
 *                                stored ACL shows the trivial projection
 *                                synthesized from its mode bits
 *   acl set <path> <entry>...    set the access ACL; when named entries
 *                                are given without a mask, the mask is
 *                                calculated as the union of the group
 *                                class (owning group + named groups)
 *   acl default <dir>            show the directory's default ACL
 *   acl default <dir> <entry>... set the default ACL (directories only)
 *   acl --mask <path>            recalculate the stored mask from the
 *                                group class
 *   acl remove <path>            drop the stored access ACL (the mode
 *                                bits, the projection, remain)
 *
 * Entry grammar (getfacl/setfacl style):
 *   user::rwx  user:kyle:rw-  group::r-x  group:devs:r--  mask::rwx  other::---
 * short forms u:, g:, m:, o:; the 'who' part is a user/group name or a
 * numeric id. The kernel validates every payload and compresses trivial
 * ACLs (those equivalent to plain mode bits) away, keeping the mode in
 * sync, so the tool always writes through setxattr and lets the kernel
 * apply the model.
 *
 * Wire format mirrors include/fnx/acl.h: 8-byte entries
 * { u16 tag, u16 perm, u32 id }, native endian, canonical order. */
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#define XA_ACCESS	"system.posix_acl_access"
#define XA_DEFAULT	"system.posix_acl_default"

#define ACL_USER_OBJ	0x01
#define ACL_USER	0x02
#define ACL_GROUP_OBJ	0x04
#define ACL_GROUP	0x08
#define ACL_MASK	0x10
#define ACL_OTHER	0x20

#define ACL_UNDEFINED_ID	0xFFFFFFFFUL
#define ACL_MAX_ENTRIES		32

struct aclx {
	unsigned short tag;
	unsigned short perm;
	unsigned int id;
};

static const char *prog;

static void usage(void)
{
	fprintf(stderr,
"usage:\n"
"  %s get <path>                show the access ACL (mode if trivial)\n"
"  %s set <path> <entry>...     set the access ACL\n"
"  %s default <dir>             show the directory default ACL\n"
"  %s default <dir> <entry>...  set the default ACL (directories only)\n"
"  %s --mask <path>             recalc the mask = group class union\n"
"  %s remove <path>             drop the stored access ACL\n"
"entries: user[::rwx | :name:rwx]  group[::r-x | :name:r-x]\n"
"         mask::rwx  other::---   (perms: rwx with '-' holes)\n",
		prog, prog, prog, prog, prog, prog);
}

/* ---- entry text <-> wire ------------------------------------------ */

/* perm string: exactly 3 chars, each position r/w/x or '-' */
static int parse_perm(const char *s, unsigned short *perm)
{
	static const char bits[3] = { 'r', 'w', 'x' };
	int i;

	if(strlen(s) != 3) {
		return -1;
	}
	*perm = 0;
	for(i = 0; i < 3; i++) {
		if(s[i] == '-') {
			continue;
		}
		if(s[i] != bits[i]) {
			return -1;
		}
		*perm |= (1 << (2 - i));
	}
	return 0;
}

static int parse_class(const char *s, int *tag)
{
	if(!strcmp(s, "user") || !strcmp(s, "u")) {
		*tag = ACL_USER_OBJ;	/* decided below (named vs obj) */
		return 1;
	}
	if(!strcmp(s, "group") || !strcmp(s, "g")) {
		*tag = ACL_GROUP_OBJ;
		return 1;
	}
	if(!strcmp(s, "mask") || !strcmp(s, "m")) {
		*tag = ACL_MASK;
		return 1;
	}
	if(!strcmp(s, "other") || !strcmp(s, "o")) {
		*tag = ACL_OTHER;
		return 1;
	}
	return 0;
}

/* resolve a user/group name to an id; all-digits is taken as an id */
static int resolve_id(const char *who, int is_group, unsigned int *id)
{
	char *end;
	unsigned long v;

	if(!*who) {
		return -1;	/* empty 'who' is only valid for the obj forms */
	}
	if(isdigit((unsigned char)who[0])) {
		v = strtoul(who, &end, 10);
		if(!*end && v <= 0xFFFFFFFFUL) {
			*id = (unsigned int)v;
			return 0;
		}
	}
	if(is_group) {
		struct group *g = getgrnam(who);
		if(g) {
			*id = g->gr_gid;
			return 0;
		}
	} else {
		struct passwd *p = getpwnam(who);
		if(p) {
			*id = p->pw_uid;
			return 0;
		}
	}
	return -2;
}

/* parse one 'class[:who]:perms' argument into an entry. Returns 0 and
 * fills *e; a negative errno-style value on error (-1 malformed,
 * -2 unknown name, -3 duplicate handled by the caller). */
static int parse_entry(const char *arg, struct aclx *e)
{
	char buf[160];
	char *cls, *who, *perms, *rest;
	int tag;

	if(strlen(arg) >= sizeof(buf)) {
		return -1;
	}
	strcpy(buf, arg);
	cls = buf;
	rest = strchr(buf, ':');
	if(!rest) {
		return -1;
	}
	*rest++ = '\0';
	if(!parse_class(cls, &tag)) {
		return -1;
	}
	perms = strchr(rest, ':');
	if(!perms) {
		return -1;
	}
	*perms++ = '\0';
	who = rest;
	if(tag == ACL_USER_OBJ && *who) {
		tag = ACL_USER;
	} else if(tag == ACL_GROUP_OBJ && *who) {
		tag = ACL_GROUP;
	} else if(*who) {
		return -1;	/* mask:: / other:: never carry a name */
	}
	e->tag = (unsigned short)tag;
	e->id = ACL_UNDEFINED_ID;
	if(tag == ACL_USER || tag == ACL_GROUP) {
		int rc = resolve_id(who, tag == ACL_GROUP, &e->id);
		if(rc == -2) {
			fprintf(stderr, "%s: unknown %s '%s'\n", prog,
				tag == ACL_USER ? "user" : "group", who);
			return -2;
		}
		if(rc) {
			return -1;
		}
	}
	if(parse_perm(perms, &e->perm)) {
		fprintf(stderr, "%s: bad permission string '%s' in '%s'\n",
			prog, perms, arg);
		return -1;
	}
	return 0;
}

/* ---- assembling + validating an ACL buffer ------------------------ */

static int sort_key(const struct aclx *e)
{
	switch(e->tag) {
	case ACL_USER_OBJ:
		return 0;
	case ACL_USER:
		return 1;
	case ACL_GROUP_OBJ:
		return 2;
	case ACL_GROUP:
		return 3;
	case ACL_MASK:
		return 4;
	default:
		return 5;	/* ACL_OTHER */
	}
}

static int acl_cmp(const void *a, const void *b)
{
	const struct aclx *x = a, *y = b;
	int k = sort_key(x) - sort_key(y);

	if(k) {
		return k;
	}
	if(x->tag == ACL_USER || x->tag == ACL_GROUP) {
		if(x->id < y->id) {
			return -1;
		}
		if(x->id > y->id) {
			return 1;
		}
	}
	return 0;
}

/* collect entries into e/n (cap ACL_MAX_ENTRIES), enforcing the shape
 * the kernel requires: exactly one owner/group/other, unique named ids,
 * at most one mask. When named entries exist and no mask was given,
 * calculate it as the union of the group class. Order is canonicalized. */
static int build_acl(struct aclx *e, int *n, const char *const *args,
		     int nargs)
{
	int i, rc, has_obj[3] = { 0, 0, 0 }, has_mask = 0, has_named = 0;

	for(i = 0; i < nargs; i++) {
		struct aclx t;

		if(*n >= ACL_MAX_ENTRIES) {
			fprintf(stderr, "%s: too many entries (max %d)\n",
				prog, ACL_MAX_ENTRIES);
			return -1;
		}
		rc = parse_entry(args[i], &t);
		if(rc) {
			return -1;
		}
		switch(t.tag) {
		case ACL_USER_OBJ:
			if(has_obj[0]++) {
				fprintf(stderr, "%s: duplicate owner entry\n", prog);
				return -1;
			}
			break;
		case ACL_GROUP_OBJ:
			if(has_obj[1]++) {
				fprintf(stderr, "%s: duplicate group entry\n", prog);
				return -1;
			}
			break;
		case ACL_OTHER:
			if(has_obj[2]++) {
				fprintf(stderr, "%s: duplicate other entry\n", prog);
				return -1;
			}
			break;
		case ACL_MASK:
			if(has_mask++) {
				fprintf(stderr, "%s: duplicate mask entry\n", prog);
				return -1;
			}
			break;
		default:
			has_named = 1;
			break;
		}
		e[(*n)++] = t;
	}
	if(!has_obj[0] || !has_obj[1] || !has_obj[2]) {
		fprintf(stderr,
			"%s: an ACL needs exactly one user, group and other entry\n",
			prog);
		return -1;
	}
	if(has_mask && !has_named) {
		fprintf(stderr,
			"%s: a mask without named entries is pointless "
			"(the ACL is trivial)\n", prog);
		return -1;
	}
	if(has_named && !has_mask) {
		/* calculate the mask: union of the group class (owning
		 * group + named groups; named users do not contribute) */
		unsigned short m = 0;
		struct aclx mk;

		for(i = 0; i < *n; i++) {
			if(e[i].tag == ACL_GROUP_OBJ || e[i].tag == ACL_GROUP) {
				m |= e[i].perm;
			}
		}
		mk.tag = ACL_MASK;
		mk.perm = m;
		mk.id = ACL_UNDEFINED_ID;
		e[(*n)++] = mk;
	}
	/* duplicate named ids (after the possible mask insertion) */
	for(i = 0; i < *n; i++) {
		int j;

		if(e[i].tag != ACL_USER && e[i].tag != ACL_GROUP) {
			continue;
		}
		for(j = i + 1; j < *n; j++) {
			if(e[j].tag == e[i].tag && e[j].id == e[i].id) {
				fprintf(stderr, "%s: duplicate entry\n", prog);
				return -1;
			}
		}
	}
	qsort(e, *n, sizeof(struct aclx), acl_cmp);
	return 0;
}

/* ---- name display -------------------------------------------------- */

static void fmt_name(struct aclx *e, char *out, size_t outsz)
{
	if(e->tag == ACL_USER) {
		struct passwd *p = getpwuid(e->id);
		if(p) {
			snprintf(out, outsz, "user:%s:", p->pw_name);
			return;
		}
		snprintf(out, outsz, "user:%u:", e->id);
		return;
	}
	if(e->tag == ACL_GROUP) {
		struct group *g = getgrgid(e->id);
		if(g) {
			snprintf(out, outsz, "group:%s:", g->gr_name);
			return;
		}
		snprintf(out, outsz, "group:%u:", e->id);
	}
}

static void fmt_perm(unsigned short p, char out[4])
{
	out[0] = (p & 4) ? 'r' : '-';
	out[1] = (p & 2) ? 'w' : '-';
	out[2] = (p & 1) ? 'x' : '-';
	out[3] = '\0';
}

static const char *tag_name(int tag)
{
	switch(tag) {
	case ACL_USER_OBJ:
		return "user::";
	case ACL_GROUP_OBJ:
		return "group::";
	case ACL_MASK:
		return "mask::";
	case ACL_OTHER:
		return "other::";
	}
	return "";
}

/* ---- subcommands --------------------------------------------------- */

static int show_acl(const char *path, const char *name, int show_mode)
{
	struct aclx buf[ACL_MAX_ENTRIES];
	struct stat st;
	ssize_t sz;
	int i;

	if(stat(path, &st)) {
		perror(path);
		return 1;
	}
	sz = getxattr(path, name, buf, sizeof(buf));
	if(sz < 0 && (errno == ENODATA || errno == EOPNOTSUPP)) {
		if(show_mode) {
			/* nothing stored: synthesize the trivial projection */
			sz = 0;
			buf[sz].tag = ACL_USER_OBJ;
			buf[sz].perm = (st.st_mode >> 6) & 7;
			buf[sz++].id = ACL_UNDEFINED_ID;
			buf[sz].tag = ACL_GROUP_OBJ;
			buf[sz].perm = (st.st_mode >> 3) & 7;
			buf[sz++].id = ACL_UNDEFINED_ID;
			buf[sz].tag = ACL_OTHER;
			buf[sz].perm = st.st_mode & 7;
			buf[sz++].id = ACL_UNDEFINED_ID;
		} else {
			printf("%s: no default ACL\n", path);
			return 0;
		}
	} else if(sz < 0) {
		fprintf(stderr, "%s: getxattr: ", prog);
		perror("");
		return 1;
	}
	if(sz % sizeof(struct aclx) || sz == 0) {
		fprintf(stderr, "%s: %s: malformed stored ACL (%d bytes)\n",
			prog, path, (int)sz);
		return 1;
	}
	printf("# file: %s\n", path);
	if(show_mode) {
		char uo[4], gc[4], ot[4];

		fmt_perm((st.st_mode >> 6) & 7, uo);
		fmt_perm((st.st_mode >> 3) & 7, gc);
		fmt_perm(st.st_mode & 7, ot);
		printf("# mode: %04o (owner %s, group class %s, other %s)\n",
			st.st_mode & 0777, uo, gc, ot);
	}
	for(i = 0; i < (int)(sz / (int)sizeof(struct aclx)); i++) {
		struct aclx *e = &buf[i];
		char prefix[80], perms[4];

		if(e->tag == ACL_USER || e->tag == ACL_GROUP) {
			fmt_name(e, prefix, sizeof(prefix));
		} else {
			snprintf(prefix, sizeof(prefix), "%s", tag_name(e->tag));
		}
		fmt_perm(e->perm, perms);
		printf("%s%s\n", prefix, perms);
	}
	return 0;
}

static int set_acl(const char *path, const char *name,
		   const struct aclx *e, int n)
{
	if(setxattr(path, name, e, n * sizeof(struct aclx), 0)) {
		fprintf(stderr, "%s: %s: ", prog, path);
		perror("setxattr");
		return 1;
	}
	return 0;
}

/* recompute the mask of a stored ACL (union of the group class) and
 * write it back; a file without a stored ACL has no mask to recalc */
static int recalc_mask(const char *path)
{
	struct aclx buf[ACL_MAX_ENTRIES];
	struct aclx mk;
	char lbuf[512];
	ssize_t lsz, sz;
	int i, stored = 0;
	unsigned short m = 0;

	lsz = listxattr(path, lbuf, sizeof(lbuf));
	for(i = 0; i < lsz; i += strlen(&lbuf[i]) + 1) {
		if(!strcmp(&lbuf[i], XA_ACCESS)) {
			stored = 1;
		}
	}
	if(!stored) {
		printf("%s: no stored ACL (the mode bits are the projection)\n",
			path);
		return 0;
	}
	sz = getxattr(path, XA_ACCESS, buf, sizeof(buf));
	if(sz <= 0 || sz % sizeof(struct aclx)) {
		fprintf(stderr, "%s: %s: cannot read the stored ACL\n", prog, path);
		return 1;
	}
	for(i = 0; i < (int)(sz / (int)sizeof(struct aclx)); i++) {
		if(buf[i].tag == ACL_GROUP_OBJ || buf[i].tag == ACL_GROUP) {
			m |= buf[i].perm;
		}
	}
	mk.tag = ACL_MASK;
	mk.perm = m;
	mk.id = ACL_UNDEFINED_ID;
	for(i = 0; i < (int)(sz / (int)sizeof(struct aclx)); i++) {
		if(buf[i].tag == ACL_MASK) {
			if(buf[i].perm == m) {
				char p[4];
				fmt_perm(m, p);
				printf("%s: mask unchanged (mask::%s)\n", path, p);
				return 0;
			}
			buf[i] = mk;
			if(!set_acl(path, XA_ACCESS, buf, (int)(sz / 8))) {
				printf("%s: mask recalculated\n", path);
			}
			return 0;
		}
	}
	/* a stored ACL always has a mask when named entries exist; a
	 * stored trivial shape would have been compressed, so reaching
	 * here means the mask is genuinely missing - add it */
	buf[sz / 8] = mk;
	if(!set_acl(path, XA_ACCESS, buf, (int)(sz / 8) + 1)) {
		printf("%s: mask added\n", path);
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd;
	struct aclx acl[ACL_MAX_ENTRIES];
	int n = 0;

	prog = argv[0];
	if(argc < 3) {
		usage();
		return 2;
	}
	cmd = argv[1];
	if(!strcmp(cmd, "get")) {
		return show_acl(argv[2], XA_ACCESS, 1);
	}
	if(!strcmp(cmd, "remove")) {
		if(removexattr(argv[2], XA_ACCESS)) {
			if(errno == ENODATA) {
				printf("%s: no stored ACL\n", argv[2]);
				return 0;
			}
			fprintf(stderr, "%s: %s: ", prog, argv[2]);
			perror("removexattr");
			return 1;
		}
		printf("%s: access ACL removed\n", argv[2]);
		return 0;
	}
	if(!strcmp(cmd, "--mask")) {
		return recalc_mask(argv[2]);
	}
	if(!strcmp(cmd, "default")) {
		struct stat st;

		if(argc == 3) {
			return show_acl(argv[2], XA_DEFAULT, 0);
		}
		if(stat(argv[2], &st) || !S_ISDIR(st.st_mode)) {
			fprintf(stderr, "%s: %s: default ACLs live on directories\n",
				prog, argv[2]);
			return 1;
		}
		if(build_acl(acl, &n, (const char *const *)&argv[3],
			     argc - 3)) {
			return 1;
		}
		/* the kernel also enforces the directory rule */
		return set_acl(argv[2], XA_DEFAULT, acl, n);
	}
	if(!strcmp(cmd, "set")) {
		if(build_acl(acl, &n, (const char *const *)&argv[3],
			     argc - 3)) {
			return 1;
		}
		return set_acl(argv[2], XA_ACCESS, acl, n);
	}
	usage();
	return 2;
}
