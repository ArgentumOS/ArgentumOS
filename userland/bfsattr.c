/* bfsattr.c: OpenBFS M6 test - attributes that overflow the inode's
 * small_data section must land in the per-file attributes B+tree
 * (Haiku's CreateAttribute), and come back through getxattr/listxattr.
 * Also verifies small+tree attributes coexist and that removing the
 * last tree attribute frees the attributes inode.
 *
 * Results are printed to stdout AND appended to /mnt/XR so the host
 * can cross-check the on-disk layout. Usage: bfsattr set|check|rm|unlink
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char *path = "/mnt/hello.txt";
static char g_buf[4096];

static void report(const char *name, int ok)
{
	printf("%s %s\n", name, ok ? "OK" : "FAIL");
	FILE *f = fopen("/mnt/XR", "a");
	if(f) {
		fprintf(f, "%s %s\n", name, ok ? "OK" : "FAIL");
		fclose(f);
	}
}

int main(int argc, char **argv)
{
	int r;

	if(argc < 2) {
		fprintf(stderr, "usage: bfsattr set|check|rm|unlink\n");
		return 1;
	}

	if(!strcmp(argv[1], "set")) {
		int fd = open(path, O_CREAT | O_RDWR, 0644);
		if(fd >= 0)
			close(fd);

		/* a value too big for the small_data section (792 bytes) */
		memset(g_buf, 'A', sizeof(g_buf));
		g_buf[900] = 0;
		r = setxattr(path, "big.attr", g_buf, 900, 0);
		if(r) fprintf(stderr, "SET-BIG errno=%d (%s)\n", errno, strerror(errno));
		report("SET-BIG", r == 0);

		/* a small attribute stays inline */
		r = setxattr(path, "small.attr", "v", 1, 0);
		report("SET-SMALL", r == 0);
		return 0;
	}

	if(!strcmp(argv[1], "check")) {
		memset(g_buf, 0, sizeof(g_buf));
		r = getxattr(path, "big.attr", g_buf, sizeof(g_buf));
		if(r != 900) fprintf(stderr, "GET-BIG r=%d errno=%d (%s)\n", r, errno, strerror(errno));
		report("GET-BIG", r == 900 && g_buf[0] == 'A' && g_buf[899] == 'A');

		r = getxattr(path, "small.attr", g_buf, sizeof(g_buf));
		report("GET-SMALL", r == 1 && g_buf[0] == 'v');

		ssize_t n = listxattr(path, g_buf, sizeof(g_buf));
		{
			/* NUL-separated names: walk the list, not strstr */
			char *p2 = g_buf, *e2 = g_buf + (n > 0 ? n : 0);
			int has_big = 0, has_small = 0, has_x13 = 0;
			while(p2 < e2) {
				if(!strcmp(p2, "big.attr")) has_big = 1;
				if(!strcmp(p2, "small.attr")) has_small = 1;
				if(p2[0] == '\x13') has_x13 = 1;
				p2 += strlen(p2) + 1;
			}
			if(!has_big) {
				fprintf(stderr, "LIST n=%zd hex:", n);
				for(int k2 = 0; k2 < n; k2++) fprintf(stderr, " %02x", (unsigned char)g_buf[k2]);
				fprintf(stderr, "\n");
			}
			report("LIST-BOTH", n > 0 && has_big && has_small && !has_x13);
		}

		/* size query for the tree attribute */
		r = getxattr(path, "big.attr", NULL, 0);
		report("SIZE-BIG", r == 900);
		return 0;
	}

	if(!strcmp(argv[1], "rm")) {
		r = removexattr(path, "big.attr");
		report("RM-BIG", r == 0);

		ssize_t n = listxattr(path, g_buf, sizeof(g_buf));
		report("LIST-AFTER-RM", n >= 0 && !strstr(g_buf, "big.attr")
			&& strstr(g_buf, "small.attr"));

		r = getxattr(path, "big.attr", NULL, 0);
		report("GONE-BIG", r < 0 && errno == ENODATA);
		return 0;
	}

	if(!strcmp(argv[1], "unlink")) {
		/* re-create a tree attribute, then unlink the file: the
		 * host verifies every attribute block is freed */
		memset(g_buf, 'B', sizeof(g_buf));
		g_buf[800] = 0;
		r = setxattr(path, "drop.attr", g_buf, 800, 0);
		report("SET-DROP", r == 0);
		r = unlink(path);
		report("UNLINK", r == 0);
		return 0;
	}

	return 1;
}
