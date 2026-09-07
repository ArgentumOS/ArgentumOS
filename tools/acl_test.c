/* acl_test.c — POSIX ACL kernel probe for FNX (M0/M1).
 *
 * Verifies on a AGFS root (the only filesystem with xattr support):
 *   - setxattr/getxattr/removexattr of system.posix_acl_access observe
 *     chmod-style ownership (owner/root) and validate the payload;
 *   - check_permission() runs the ACL algorithm: a named-user entry
 *     grants access a 0600 root-owned file would otherwise deny, a
 *     stranger is still denied, and the mask limits the group class;
 *   - inodes without an ACL xattr behave exactly per their mode bits
 *     (the trivial-ACL projection).
 * Wire-format definitions mirror include/fnx/acl.h (8-byte entries).
 * Prints PASS/FAIL per check; exit code = number of failures.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>

#define XA_ACCESS	"system.posix_acl_access"
#define XA_DEFAULT	"system.posix_acl_default"

#define ACL_USER_OBJ	0x01
#define ACL_USER	0x02
#define ACL_GROUP_OBJ	0x04
#define ACL_GROUP	0x08
#define ACL_MASK	0x10
#define ACL_OTHER	0x20

struct aclx {
	unsigned short tag;
	unsigned short perm;
	unsigned int id;
};

static int fails;

static void fail(const char *what)
{
	printf("FAIL: %s (errno %d)\n", what, errno);
	fails++;
}

static void ok(const char *what)
{
	printf("PASS: %s\n", what);
}

/* ---- helpers ------------------------------------------------------- */

/* run fn in a child that has dropped to uid/gid 1001 */
static int as_user(unsigned int uid, unsigned int gid, int (*fn)(void))
{
	pid_t p;
	int st;

	p = fork();
	if(p < 0) {
		perror("fork");
		return -1;
	}
	if(p == 0) {
		if(setgid(gid) || setuid(uid)) {
			printf("child setuid/setgid failed: %d\n", errno);
			_exit(2);
		}
		_exit(fn());
	}
	waitpid(p, &st, 0);
	if(!WIFEXITED(st)) {
		printf("child crashed\n");
		return -1;
	}
	return WEXITSTATUS(st);
}

/* the AGFS disk persists across runs. The old test directory may linger
 * with a wrong mode or stale ACL xattrs, so: force 0755 (traversable by
 * the test's non-root children) and drop any stale ACL on the fixed
 * file names instead of deleting (recursive deletes flood the AGFS
 * journal's small log and trip its reset path). */
static void reset_dir(const char *dir)
{
	chmod(dir, 0755);
	removexattr(dir, XA_DEFAULT);
	removexattr(dir, XA_ACCESS);
}

static void reset_file(const char *path)
{
	removexattr(path, XA_ACCESS);
	removexattr(path, XA_DEFAULT);
}

static int make_file(const char *path, mode_t mode)
{	int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);

	if(fd < 0) {
		return -1;
	}
	close(fd);
	chmod(path, mode);	/* ensure the mode survived umask */
	return 0;
}

static int set_acl(const char *path, const char *name,
		   const struct aclx *e, int n)
{
	return setxattr(path, name, e, n * sizeof(struct aclx), 0);
}

/* ---- checks -------------------------------------------------------- */

static int check_deny(void)		/* uid 1001: no access */
{
	if(access("/mnt/aclt_d/grant", R_OK) == 0) {
		printf("child: uid 1001 unexpectedly allowed\n");
		return 1;
	}
	if(errno != EACCES) {
		printf("child: expected EACCES got %d\n", errno);
		return 1;
	}
	return 0;
}

static int check_readable(void)		/* uid 1000: named-user rw */
{
	if(access("/mnt/aclt_d/grant", R_OK | W_OK) != 0) {
		printf("child: uid 1000 denied by ACL: %d\n", errno);
		return 1;
	}
	return 0;
}

static int check_r_only(void)		/* uid 1000: read only */
{
	if(access("/mnt/aclt_d/grant", R_OK) != 0) {
		printf("child: uid 1000 read denied by ACL: %d\n", errno);
		return 1;
	}
	if(access("/mnt/aclt_d/grant", W_OK) == 0) {
		printf("child: uid 1000 write unexpectedly allowed\n");
		return 1;
	}
	return 0;
}

int main(void)
{
	struct aclx acl[8];
	int n;

	/* Boot runs from a reliable ext2 root; the AGFS filesystem (the
	 * only one with xattr support) is attached as an IDE slave and
	 * mounted here so the checks below run against real AGFS inodes. */
	if(mkdir("/mnt", 0755) < 0 && errno != EEXIST) {
		perror("mkdir /mnt");
		return 1;
	}
	if(mount("/dev/hdb", "/mnt", "agfs", 0, NULL) < 0) {
		perror("mount /dev/hdb (agfs)");
		return 1;
	}
	printf("ACLTEST: agfs mounted on /mnt\n");

	reset_dir("/mnt/aclt_d");
	if(mkdir("/mnt/aclt_d", 0755) < 0 && errno != EEXIST) {
		perror("mkdir /mnt/aclt_d");
		return 1;
	}
	reset_dir("/mnt/aclt_d");
	if(make_file("/mnt/aclt_d/grant", 0600) < 0) {
		perror("create grant");
		return 1;
	}
	reset_file("/mnt/aclt_d/grant");
	if(make_file("/mnt/aclt_d/plain", 0644) < 0) {
		perror("create plain");
		return 1;
	}
	reset_file("/mnt/aclt_d/plain");
	if(make_file("/mnt/aclt_d/nope", 0600) < 0) {
		perror("create nope");
		return 1;
	}
	reset_file("/mnt/aclt_d/nope");

	/* 1. trivial projection: mode 0644 grants uid 1001 read, mode 0600
	 * does not (both without any stored ACL) */
	if(fork() == 0) {
		setgid(1001);
		setuid(1001);
		if(access("/mnt/aclt_d/plain", R_OK) != 0) {
			printf("FAIL: trivial 0644 read denied (%d)\n", errno);
			_exit(1);
		}
		if(access("/mnt/aclt_d/plain", W_OK) == 0) {
			printf("FAIL: trivial 0644 write allowed\n");
			_exit(1);
		}
		if(access("/mnt/aclt_d/nope", R_OK) == 0) {
			printf("FAIL: trivial 0600 read allowed\n");
			_exit(1);
		}
		_exit(0);
	} else {
		int st;
		wait(&st);
		if(st) {
			fails++;
		} else {
			ok("trivial mode projection (0644 r, no w; 0600 denied)");
		}
	}

	/* 2. bad ACLs are rejected with -EINVAL */
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_USER, 7, 1000 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	errno = 0;
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != -1 || errno != EINVAL) {
		fail("missing mask must be EINVAL");
	} else {
		ok("missing mask rejected");
	}

	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
	acl[n++] = (struct aclx){ ACL_MASK, 7, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 0, 0 };	/* duplicate owner */
	errno = 0;
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != -1 || errno != EINVAL) {
		fail("duplicate owner must be EINVAL");
	} else {
		ok("duplicate owner rejected");
	}

	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_USER, 8, 1000 };	/* perm bit 3 set */
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
	acl[n++] = (struct aclx){ ACL_MASK, 7, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	errno = 0;
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != -1 || errno != EINVAL) {
		fail("perm>7 must be EINVAL");
	} else {
		ok("out-of-range perm rejected");
	}

	/* 3. a valid ACL granting uid 1000 rw on a 0600 root file (exec is
	 * governed by the mode's x bits, so rw is the clean demonstration) */
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_USER, 6, 1000 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
	acl[n++] = (struct aclx){ ACL_MASK, 7, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != 0) {
		fail("setxattr valid ACL");
		return 1;
	}
	ok("setxattr valid ACL");

	if(as_user(1000, 1000, check_readable) != 0) {
		fail("uid 1000 access via named-user entry");
	} else {
		ok("named-user entry grants uid 1000 rw");
	}
	if(as_user(1001, 1001, check_deny) != 0) {
		fail("uid 1001 must stay denied");
	} else {
		ok("stranger still denied");
	}

	/* 4. the mask limits the group class: group::rwx but mask::r --
	 * a member of the owning group gets only r */
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_MASK, 4, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	chmod("/mnt/aclt_d/grant", 0000);
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != 0) {
		fail("setxattr mask ACL");
	} else {
		ok("setxattr mask-limited ACL");
	}
	/* child stays in root's group (gid 0): owning-group candidate */
	if(fork() == 0) {
		setuid(1001);	/* keep gid 0: in the owning group */
		if(access("/mnt/aclt_d/grant", R_OK) != 0) {
			printf("FAIL: group read denied by mask (errno %d)\n", errno);
			_exit(1);
		}
		if(access("/mnt/aclt_d/grant", W_OK) == 0) {
			printf("FAIL: mask::r should deny group write\n");
			_exit(1);
		}
		_exit(0);
	} else {
		int st;
		wait(&st);
		if(st) {
			fails++;
		} else {
			ok("mask limits the group class (r allowed, w denied)");
		}
	}
	chmod("/mnt/aclt_d/grant", 0600);

	/* 4b. a group-class denial must NOT fall through to OTHER: the
	 * owning group is masked out (MASK 0) while OTHER is permissive -
	 * a member of the owning group gets nothing, not OTHER's access */
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_MASK, 0, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 7, 0 };
	chmod("/mnt/aclt_d/grant", 0000);
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != 0) {
		fail("setxattr mask-deny ACL");
		return 1;
	}
	if(fork() == 0) {		/* keep gid 0: in the owning group */
		setuid(1001);
		if(access("/mnt/aclt_d/grant", R_OK) == 0) {
			printf("FAIL: masked group must not reach OTHER (errno %d)\n", errno);
			_exit(1);
		}
		_exit(0);
	} else {
		int st;
		wait(&st);
		if(st) {
			fails++;
		} else {
			ok("masked group class denied (no OTHER fall-through)");
		}
	}
	chmod("/mnt/aclt_d/grant", 0600);

	/* 4c. a supplementary group grants group-class access: child joins
	 * group 2000 via setgroups, a named ACL group entry for 2000 with
	 * rwx grants it (mask 4 limits to r) */
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
	acl[n++] = (struct aclx){ ACL_GROUP, 7, 2000 };
	acl[n++] = (struct aclx){ ACL_MASK, 4, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	chmod("/mnt/aclt_d/grant", 0000);
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != 0) {
		fail("setxattr supplementary-group ACL");
		return 1;
	}
	if(fork() == 0) {
		gid_t g = 2000;
		if(setgroups(1, &g)) {
			printf("child: setgroups failed (%d)\n", errno);
			_exit(1);
		}
		setuid(1001);	/* keep gid 0; group 2000 now supplementary */
		if(access("/mnt/aclt_d/grant", R_OK) != 0) {
			printf("FAIL: supplementary group denied (errno %d)\n", errno);
			_exit(1);
		}
		if(access("/mnt/aclt_d/grant", W_OK) == 0) {
			printf("FAIL: mask::r should deny supplementary write\n");
			_exit(1);
		}
		_exit(0);
	} else {
		int st;
		wait(&st);
		if(st) {
			fails++;
		} else {
			ok("supplementary group honored through the mask");
		}
	}
	chmod("/mnt/aclt_d/grant", 0600);

	/* 5. getxattr needs no read permission on the object */
	if(make_file("/mnt/aclt_d/metadata", 0600) < 0) {
		perror("create metadata");
		return 1;
	}
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_USER, 4, 1000 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
	acl[n++] = (struct aclx){ ACL_MASK, 7, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	if(set_acl("/mnt/aclt_d/metadata", XA_ACCESS, acl, n) != 0) {
		fail("setxattr metadata ACL");
		return 1;
	}
	if(fork() == 0) {
		char buf[256];
		setgid(1001);
		setuid(1001);
		if(getxattr("/mnt/aclt_d/metadata", XA_ACCESS, buf, sizeof(buf)) < 0) {
			printf("FAIL: getxattr denied on 0600 file (%d)\n", errno);
			_exit(1);
		}
		_exit(0);
	} else {
		int st;
		wait(&st);
		if(st) {
			fails++;
		} else {
			ok("getxattr readable without read permission");
		}
	}

	/* 6. a non-owner cannot set or remove an ACL */
	if(fork() == 0) {
		struct aclx a2[4];
		setgid(1001);
		setuid(1001);
		a2[0] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
		a2[1] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
		a2[2] = (struct aclx){ ACL_OTHER, 0, 0 };
		a2[3] = (struct aclx){ ACL_MASK, 7, 0 };
		if(setxattr("/mnt/aclt_d/metadata", XA_ACCESS, a2, sizeof(a2), 0) != -1 ||
		   errno != EPERM) {
			printf("FAIL: non-owner setxattr (errno %d)\n", errno);
			_exit(1);
		}
		if(removexattr("/mnt/aclt_d/metadata", XA_ACCESS) != -1 ||
		   errno != EPERM) {
			printf("FAIL: non-owner removexattr (errno %d)\n", errno);
			_exit(1);
		}
		_exit(0);
	} else {
		int st;
		wait(&st);
		if(st) {
			fails++;
		} else {
			ok("non-owner set/remove rejected (-EPERM)");
		}
	}

	/* 7. default ACLs are directory-only */
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	errno = 0;
	if(set_acl("/mnt/aclt_d/metadata", XA_DEFAULT, acl, n) == 0 ||
	   errno != EINVAL) {
		fail("default ACL on a regular file must be EINVAL");
	} else {
		ok("default ACL rejected on a regular file (-EINVAL)");
	}
	if(set_acl("/mnt/aclt_d", XA_DEFAULT, acl, n) != 0) {
		fail("default ACL on a directory");
	} else {
		ok("default ACL accepted on a directory");
	}
	removexattr("/mnt/aclt_d", XA_DEFAULT);

	/* 8. owner can remove the ACL, restoring the trivial projection */
	if(removexattr("/mnt/aclt_d/grant", XA_ACCESS) != 0) {
		fail("owner removexattr");
	} else if(as_user(1000, 1000, check_deny) != 0) {
		fail("uid 1000 not denied after ACL removal");
	} else {
		ok("removexattr restores trivial projection (uid 1000 denied)");
	}

	/* ---- M2: chmod <-> ACL two-way sync --------------------------- */

	/* 9. chmod edits the stored ACL's entries: set a named-user ACL on
	 * a 0600 file, then chmod 0640 -> the MASK becomes 4 (the new
	 * group-class bits), so the named user is limited to read even
	 * though its own entry still says rw; chmod 0600 -> mask 0 */
	n = 0;
	acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
	acl[n++] = (struct aclx){ ACL_USER, 6, 1000 };
	acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
	acl[n++] = (struct aclx){ ACL_MASK, 7, 0 };
	acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
	chmod("/mnt/aclt_d/grant", 0600);
	if(set_acl("/mnt/aclt_d/grant", XA_ACCESS, acl, n) != 0) {
		fail("M2 setup: setxattr chmod-target ACL");
		return 1;
	}
	if(chmod("/mnt/aclt_d/grant", 0640) != 0) {
		fail("chmod 0640 on ACL'd file");
	} else {
		struct stat st;
		stat("/mnt/aclt_d/grant", &st);
		if((st.st_mode & 0777) != 0640) {
			printf("FAIL: mode not 0640 after chmod (got %o)\n",
				st.st_mode & 0777);
			fails++;
		} else {
			ok("chmod 0640 updates the mode projection");
		}
		if(as_user(1000, 1000, check_r_only) != 0) {
			fail("uid 1000 denied after chmod 0640 (mask 4)");
		} else {
			ok("chmod 0640 limits the named user via the mask (rw -> r)");
		}
		if(fork() == 0) {
			setgid(1001);
			setuid(1001);
			if(access("/mnt/aclt_d/grant", W_OK) == 0) {
				printf("FAIL: uid 1001 write after chmod 0640\n");
				_exit(1);
			}
			_exit(0);
		} else {
			int st2;
			wait(&st2);
			if(st2) {
				fails++;
			}
		}
	}
	/* the stored ACL must still carry the named user (chmod does not
	 * drop it); its rw entry is now masked down to the chmod bits */
	{
		char abuf[8 * 8];
		ssize_t sz = getxattr("/mnt/aclt_d/grant", XA_ACCESS, abuf, sizeof(abuf));
		int named = 0;

		if(sz != 5 * 8) {
			printf("FAIL: ACL size after chmod (%d)\n", (int)sz);
			fails++;
		} else {
			int k;
			for(k = 0; k < (int)(sz / 8); k++) {
				struct aclx *e = (struct aclx *)&abuf[k * 8];
				if(e->tag == ACL_USER && e->id == 1000 && e->perm == 6) {
					named = 1;	/* entry kept; mask now limits */
				}
				if(e->tag == ACL_MASK && e->perm != 4) {
					printf("FAIL: MASK not 4 after chmod 0640 (got %d)\n",
						e->perm);
					fails++;
				}
				if(e->tag == ACL_OTHER && e->perm != 0) {
					printf("FAIL: OTHER not 0 after chmod 0640\n");
					fails++;
				}
			}
			if(!named) {
				printf("FAIL: chmod dropped the named-user entry\n");
				fails++;
			} else {
				ok("chmod rewrites MASK/OTHER, keeps the named entry");
			}
		}
	}
	if(chmod("/mnt/aclt_d/grant", 0600) != 0) {
		fail("chmod 0600 on ACL'd file");
	} else if(fork() == 0) {
		setuid(1000);
		if(access("/mnt/aclt_d/grant", R_OK) == 0) {
			printf("FAIL: uid 1000 read after chmod 0600 (mask 0)\n");
			_exit(1);
		}
		_exit(0);
	} else {
		int st2;
		wait(&st2);
		if(st2) {
			fails++;
		} else {
			ok("chmod 0600 masks the named user out (mask 0)");
		}
	}
	removexattr("/mnt/aclt_d/grant", XA_ACCESS);
	chmod("/mnt/aclt_d/grant", 0600);

	/* 10. getxattr on a trivial (mode-only) file synthesizes the
	 * three-entry ACL from the mode bits */
	chmod("/mnt/aclt_d/plain", 0644);
	{
		struct aclx e[8];
		ssize_t sz = getxattr("/mnt/aclt_d/plain", XA_ACCESS, e, sizeof(e));
		int good = (sz == 3 * 8);

		if(good) {
			good = e[0].tag == ACL_USER_OBJ && e[0].perm == 6 &&
				e[1].tag == ACL_GROUP_OBJ && e[1].perm == 4 &&
				e[2].tag == ACL_OTHER && e[2].perm == 4;
		}
		if(!good) {
			printf("FAIL: trivial getxattr (sz %d)\n", (int)sz);
			fails++;
		} else {
			ok("getxattr synthesizes the trivial ACL from the mode");
		}
	}
	/* and the size query works on a mode-only file (even on the ext2
	 * root, which has no xattr storage) */
	if(getxattr("/etc/passwd", XA_ACCESS, NULL, 0) != 3 * 8) {
		printf("FAIL: trivial size query on ext2 root (%d)\n",
			(int)getxattr("/etc/passwd", XA_ACCESS, NULL, 0));
		fails++;
	} else {
		ok("trivial size query works without xattr storage");
	}

	/* 11. setting a trivial ACL compresses it away: after setxattr with
	 * the mode-equivalent ACL the file has no stored ACL, and a later
	 * non-trivial set that is then compressed by a mode-equal ACL set
	 * removes the stored one (mode becomes the projection) */
	{
		struct aclx e[8];
		int k;

		n = 0;
		acl[n++] = (struct aclx){ ACL_USER_OBJ, 6, 0 };
		acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 4, 0 };
		acl[n++] = (struct aclx){ ACL_OTHER, 4, 0 };
		if(set_acl("/mnt/aclt_d/plain", XA_ACCESS, acl, n) != 0) {
			fail("set trivial ACL (0644-equivalent)");
		} else {
			/* compressed: a getxattr still sees the trivial ACL, but a
			 * listxattr must not show a stored system.posix_acl_access */
			char lbuf[512];
			ssize_t lsz = listxattr("/mnt/aclt_d/plain", lbuf, sizeof(lbuf));
			int stored = 0;

			for(k = 0; k < lsz; k += strlen(&lbuf[k]) + 1) {
				if(!strcmp(&lbuf[k], XA_ACCESS)) {
					stored = 1;
				}
			}
			if(stored) {
				printf("FAIL: trivial ACL was stored (listxattr shows it)\n");
				fails++;
			} else if(getxattr("/mnt/aclt_d/plain", XA_ACCESS, e, sizeof(e))
				  != 3 * 8) {
				fail("trivial getxattr after compression");
			} else {
				ok("trivial ACL set is compressed away (not stored)");
			}
		}
	}

	/* ---- M3: default-ACL inheritance ------------------------------ */

	/* 12. a default ACL on a directory seeds files created there: the
	 * default intersected with the create mode; umask is bypassed */
	umask(022);
	{
		char d[80], f[96];
		struct stat st;
		int fd, i2, mkattempt;

		/* the AGFS disk persists across runs: pick a dir name that
		 * cannot collide with a stale one from a crashed run */
		for(mkattempt = 0; mkattempt < 16; mkattempt++) {
			snprintf(d, sizeof(d), "/mnt/aclt_d/m3_%d_%d",
				(int)getpid(), mkattempt);
			if(mkdir(d, 0755) == 0) {
				break;
			}
			if(errno != EEXIST) {
				break;
			}
		}
		if(access(d, F_OK)) {
			fail("M3 mkdir parent");
		} else {
			/* access ACL on the parent (root-owned): named user 1000
			 * gets rwx HERE so it can traverse the directory to reach
			 * the inherited files; the DEFAULT ACL (below) grants the
			 * files rw */
			n = 0;
			acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
			acl[n++] = (struct aclx){ ACL_USER, 7, 1000 };
			acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
			acl[n++] = (struct aclx){ ACL_MASK, 7, 0 };
			acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
			if(set_acl(d, XA_ACCESS, acl, n) != 0) {
				fail("M3 set parent access ACL");
			} else {
				n = 0;
				acl[n++] = (struct aclx){ ACL_USER_OBJ, 7, 0 };
				acl[n++] = (struct aclx){ ACL_USER, 6, 1000 };
				acl[n++] = (struct aclx){ ACL_GROUP_OBJ, 0, 0 };
				acl[n++] = (struct aclx){ ACL_MASK, 7, 0 };
				acl[n++] = (struct aclx){ ACL_OTHER, 0, 0 };
				if(set_acl(d, XA_DEFAULT, acl, n) != 0) {
					fail("M3 set parent default ACL");
				} else {
				struct aclx e2[8];
				int got_mask = -1, got_user = -1;
				ssize_t sz;

				/* create with mode 0666: owner 7&6=6, other 0,
				 * mask 7&6=6 (umask 022 must NOT bite -> group
				 * class stays 6) */
				snprintf(f, sizeof(f), "%s/inh", d);
				fd = open(f, O_CREAT | O_WRONLY | O_TRUNC, 0666);
				if(fd < 0) {
					fail("M3 open inherited file");
				} else {
					close(fd);
					stat(f, &st);
					if((st.st_mode & 0777) != 0660) {
						printf("FAIL: inherited mode %o != 0660\n",
							st.st_mode & 0777);
						fails++;
					} else {
						ok("M3 file inherits mode 0660 (umask bypassed)");
					}
					sz = getxattr(f, XA_ACCESS, e2, sizeof(e2));
					for(i2 = 0; i2 < (int)(sz / 8) && sz > 0; i2++) {
						if(e2[i2].tag == ACL_MASK) {
							got_mask = e2[i2].perm;
						}
						if(e2[i2].tag == ACL_USER &&
						   e2[i2].id == 1000) {
							got_user = e2[i2].perm;
						}
					}
					if(sz != 5 * 8 || got_mask != 6 || got_user != 6) {
						printf("FAIL: inherited ACL (sz %d mask %d user %d)\n",
							(int)sz, got_mask, got_user);
						fails++;
					} else {
						ok("M3 file inherits the default ACL (mask 6, named user kept)");
					}
					/* uid 1000 can read+write via the named entry */
					if(fork() == 0) {
						setgid(1001);
						setuid(1000);
						if(access(f, R_OK | W_OK) != 0) {
							printf("FAIL: uid 1000 denied on inherited file (%d)\n",
								errno);
							_exit(1);
						}
						_exit(0);
					} else {
						int ws;
						wait(&ws);
						if(ws) {
							fails++;
						} else {
							ok("M3 named user 1000 has rw on the inherited file");
						}
					}
					unlink(f);
				}

				/* a restrictive create mode (0600) intersects the
				 * default: mask -> 0, the named user gets nothing */
				snprintf(f, sizeof(f), "%s/restrict", d);
				fd = open(f, O_CREAT | O_WRONLY | O_TRUNC, 0600);
				if(fd >= 0) {
					close(fd);
					stat(f, &st);
					if((st.st_mode & 0777) != 0600) {
						printf("FAIL: restrictive mode %o != 0600\n",
							st.st_mode & 0777);
						fails++;
					}
					if(fork() == 0) {
						setuid(1000);
						if(access(f, R_OK) == 0) {
							printf("FAIL: uid 1000 read on 0600-created file\n");
							_exit(1);
						}
						_exit(0);
					} else {
						int ws;
						wait(&ws);
						if(ws) {
							fails++;
						} else {
							ok("M3 restrictive create mode masks the named user out");
						}
					}
					unlink(f);
				} else {
					fail("M3 open restrictive file");
				}

				/* 13. a subdirectory inherits the access ACL AND copies
				 * the default ACL so the inheritance continues */
				{
					char sub[96], subf[110];
					snprintf(sub, sizeof(sub), "%s/sub", d);
					if(mkdir(sub, 0777) != 0) {
						fail("M3 mkdir subdir");
					} else {
						sz = getxattr(sub, XA_ACCESS, e2, sizeof(e2));
						got_mask = -1;
						for(i2 = 0; i2 < (int)(sz / 8) && sz > 0; i2++) {
							if(e2[i2].tag == ACL_MASK) {
								got_mask = e2[i2].perm;
							}
						}
						if(sz != 5 * 8 || got_mask != 7) {
							printf("FAIL: subdir access ACL (sz %d mask %d)\n",
								(int)sz, got_mask);
							fails++;
						} else {
							ok("M3 subdir inherits the access ACL");
						}
						sz = getxattr(sub, XA_DEFAULT, e2, sizeof(e2));
						if(sz != 5 * 8) {
							printf("FAIL: subdir default ACL not copied (sz %d)\n",
								(int)sz);
							fails++;
						} else {
							ok("M3 subdir copies the default ACL");
						}
						/* a file in the subdir inherits again */
						snprintf(subf, sizeof(subf), "%s/grand", sub);
						fd = open(subf, O_CREAT | O_WRONLY | O_TRUNC, 0666);
						if(fd >= 0) {
							close(fd);
							stat(subf, &st);
							if((st.st_mode & 0777) != 0660) {
								printf("FAIL: grandchild mode %o != 0660\n",
									st.st_mode & 0777);
								fails++;
							} else {
								ok("M3 inheritance continues one level down");
							}
							unlink(subf);
						} else {
							fail("M3 open grandchild");
						}
						rmdir(sub);
					}
				}

				/* 14. removing the default ACL restores umask semantics */
				if(removexattr(d, XA_DEFAULT) != 0) {
					fail("M3 removexattr default");
				} else {
					snprintf(f, sizeof(f), "%s/plain", d);
					fd = open(f, O_CREAT | O_WRONLY | O_TRUNC, 0666);
					if(fd >= 0) {
						close(fd);
						stat(f, &st);
						if((st.st_mode & 0777) != 0644) {
							printf("FAIL: no-default mode %o != 0644\n",
								st.st_mode & 0777);
							fails++;
						} else {
							ok("M3 no default ACL: umask applies (0644)");
						}
						unlink(f);
					} else {
						fail("M3 open plain file");
					}
				}
				removexattr(d, XA_ACCESS);
				rmdir(d);
				}
			}
		}
	}

	printf("ACLTEST: %d failure(s)\n", fails);
	return fails ? 1 : 0;
}