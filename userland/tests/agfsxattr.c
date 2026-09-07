/*
 * fnx/userland/tests/agfsxattr.c
 *
 * M4d test: exercise the xattr syscalls on AGFS (small_data attributes).
 *
 * Usage:
 *	agfsxattr set		create the attributes + checks
 *	agfsxattr check		read back + list (after reopen)
 *
 * All results are printed to stdout AND appended to /mnt/XR so the host
 * can verify them independently of the serial.
 */

#include <sys/xattr.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static const char *path = "/Volumes/hello.txt";
static FILE *out;

static void report(const char *what, int ok)
{
	fprintf(out, "%s %s\n", what, ok ? "OK" : "FAIL");
	fprintf(stdout, "%s %s\n", what, ok ? "OK" : "FAIL");
}

static int set_one(const char *name, const void *val, size_t size, int flags)
{
	int r = setxattr(path, name, val, size, flags);
	if(r < 0) {
		fprintf(out, "setxattr(%s) errno=%d (%s)\n", name, errno,
			strerror(errno));
		fprintf(stdout, "setxattr(%s) errno=%d (%s)\n", name, errno,
			strerror(errno));
	}
	return r;
}

static int do_set(void)
{
	unsigned char num[4] = {1, 2, 3, 4};
	unsigned char big[30];
	char huge[60];
	int i, r;
	int ok = 1;

	for(i = 0; i < 30; i++) {
		big[i] = 'A' + (i % 26);
	}
	for(i = 0; i < 60; i++) {
		huge[i] = 'h';
	}

	/* the small_data budget is 792 bytes (Haiku: inode_size == block
	 * size); a first inline attribute always fits */
	r = set_one("user.c", "hello att", 9, 0);
	report("set user.c", r == 0);

	/* a second attribute fits inline too (user.n + user.c < 792) */
	errno = 0;
	r = set_one("user.n", num, 4, 0);
	report("set user.n", r == 0);

	/* XATTR_CREATE on an existing attr -> EEXIST */
	errno = 0;
	r = set_one("user.c", "x", 1, XATTR_CREATE);
	report("XATTR_CREATE existing -> EEXIST", r < 0 && errno == EEXIST);

	/* XATTR_REPLACE on a missing attr -> ENODATA */
	errno = 0;
	r = set_one("user.missing", "x", 1, XATTR_REPLACE);
	report("XATTR_REPLACE missing -> ENODATA", r < 0 && errno == ENODATA);

	/* replace-then-remove round trip on the single slot */
	r = set_one("user.c", "tmp", 3, 0);
	report("replace user.c -> tmp", r == 0);
	r = removexattr(path, "user.c");
	report("removexattr user.c", r == 0);
	errno = 0;
	r = getxattr(path, "user.c", NULL, 0);
	report("user.c gone -> ENODATA", r < 0 && errno == ENODATA);

	/* final value for the persistence check */
	r = set_one("user.c", "replaced", 8, 0);
	report("set user.c = replaced", r == 0);

	/* a 60-byte value still fits inline */
	errno = 0;
	r = set_one("user.huge", huge, 60, 0);
	report("set user.huge 60B", r == 0);
	{
		char hb[64];
		errno = 0;
		r = getxattr(path, "user.huge", hb, sizeof(hb));
		report("get user.huge", r == 60 && !memcmp(hb, huge, 60));
	}

	/* attributes on a DIRECTORY (short name to fit 24 bytes) */
	{
		char d[32];
		int rr = setxattr("/Volumes/subdir", "d", "dirvalue", 9, 0);
		report("setxattr on dir", rr == 0);
		errno = 0;
		rr = getxattr("/Volumes/subdir", "d", d, sizeof(d));
		report("getxattr dir value", rr == 9 && !memcmp(d, "dirvalue", 9));
	}

	/* attributes on a SYMLINK are ALLOWED (Haiku: every inode supports
	 * attributes; the small_data tail starts at 232, after the 144-byte
	 * symlink area, so there is no aliasing); lsetxattr must NOT follow
	 * the link */
	errno = 0;
	r = lsetxattr("/Volumes/short", "user.onlink", "x", 1, 0);
	report("lsetxattr on symlink", r == 0);
	{
		char d2[32];
		errno = 0;
		r = lgetxattr("/Volumes/short", "user.onlink", d2, sizeof(d2));
		report("lgetxattr on symlink", r == 1 && d2[0] == 'x');
	}

	/* empty name -> EINVAL */
	errno = 0;
	r = setxattr(path, "", "x", 1, 0);
	report("setxattr empty name -> EINVAL", r < 0 && errno == EINVAL);

	/* name with '/' -> EACCES */
	errno = 0;
	r = setxattr(path, "user/a/b", "x", 1, 0);
	report("setxattr name with / -> EACCES", r < 0 && errno == EACCES);

	/* getxattr of a missing attr -> ENODATA */
	errno = 0;
	r = getxattr(path, "user.nope", NULL, 0);
	report("getxattr missing -> ENODATA", r < 0 && errno == ENODATA);

	/* size query (value == NULL) returns the value length */
	r = getxattr(path, "user.c", NULL, 0);
	report("getxattr size query = 8", r == 8);

	/* too-small buffer -> ERANGE */
	errno = 0;
	r = getxattr(path, "user.c", (void *)&r, 4);
	report("getxattr small buffer -> ERANGE", r < 0 && errno == ERANGE);

	/* f* variant via an fd */
	{
		int fd = open(path, O_RDONLY);
		unsigned char v[16];
		ssize_t n;

		if(fd >= 0) {
			n = fgetxattr(fd, "user.c", v, sizeof(v));
			report("fgetxattr user.c", n == 8 && !memcmp(v, "replaced", 8));
			errno = 0;
			r = fsetxattr(fd, "user.c", "fd", 2, 0);
			report("fsetxattr user.c -> fd", r == 0);
			close(fd);
			/* restore for the persistence check */
			r = set_one("user.c", "replaced", 8, 0);
			report("restore user.c = replaced", r == 0);
		} else {
			report("open for f*", 0);
		}
	}

	return ok;
}

static int check_one(const char *name, const void *exp, size_t size)
{
	unsigned char v[256];
	ssize_t n;
	int r;

	n = getxattr(path, name, v, sizeof(v));
	if(n != (ssize_t)size || memcmp(v, exp, size)) {
		fprintf(out, "check %s FAIL (n=%ld)\n", name, (long)n);
		fprintf(stdout, "check %s FAIL (n=%ld)\n", name, (long)n);
		return 0;
	}
	fprintf(out, "check %s OK (%ld bytes)\n", name, (long)n);
	fprintf(stdout, "check %s OK (%ld bytes)\n", name, (long)n);
	(void)r;
	return 1;
}

static int do_check(void)
{
	char list[256];
	unsigned char nbuf[4] = {1, 2, 3, 4};
	char hbuf[60];
	int ok = 1;
	ssize_t n;
	int i;

	for(i = 0; i < 60; i++) {
		hbuf[i] = 'h';
	}

	ok &= check_one("user.c", "replaced", 8);
	ok &= check_one("user.n", nbuf, 4);
	ok &= check_one("user.huge", hbuf, 60);

	/* all three attributes fit the 792-byte small_data section, so
	 * they survive the reopen */
	n = listxattr(path, list, sizeof(list));
	fprintf(out, "listxattr len=%ld\n", (long)n);
	fprintf(stdout, "listxattr len=%ld\n", (long)n);
	if(n > 0) {
		char *p = list;
		char *end = list + n;
		while(p < end) {
			fprintf(out, "  attr: %s\n", p);
			fprintf(stdout, "  attr: %s\n", p);
			p += strlen(p) + 1;
		}
		ok &= (n == 24);	/* user.c(6) + user.n(6) + user.huge(9) + 3 NULs */
		fprintf(out, "list total %s\n", (n == 24) ? "OK" : "FAIL");
		fprintf(stdout, "list total %s\n", (n == 24) ? "OK" : "FAIL");
	}

	/* too-small list buffer -> ERANGE (the 24-byte list does not fit
	 * in 4 bytes) */
	errno = 0;
	n = listxattr(path, list, 4);
	report("listxattr small buffer -> ERANGE", n < 0 && errno == ERANGE);

	/* size query */
	n = listxattr(path, NULL, 0);
	report("listxattr size query = 24", n == 24);

	return ok;
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "set";
	int r;

	if(!(out = fopen("/Volumes/XR", "a"))) {
		perror("fopen /mnt/XR");
		return 1;
	}
	fprintf(out, "== mode %s ==\n", mode);
	fprintf(stdout, "== mode %s ==\n", mode);
	if(!strcmp(mode, "set")) {
		r = do_set();
	} else {
		r = do_check();
	}
	fclose(out);
	return r ? 0 : 1;
}
