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
#include <sys/ioctl.h>

/* the kernel's BFS attribute-type ioctl (include/fnx/bfs.h) */
#define BFS_ATTR_NAME_MAX	255

struct bfs_attr_info {
	char name[BFS_ATTR_NAME_MAX + 1];
	unsigned int type;
	unsigned long long size;
};

#define BFS_IOC_GET_ATTR_INFO	0x42530001	/* 'BS' + 1 */
#define BFS_IOC_SET_ATTR_TYPE	0x42530002

static const char *path = "/Volumes/hello.txt";
static char g_buf[4096];

static void report(const char *name, int ok)
{
	printf("%s %s\n", name, ok ? "OK" : "FAIL");
	FILE *f = fopen("/Volumes/XR", "a");
	if(f) {
		fprintf(f, "%s %s\n", name, ok ? "OK" : "FAIL");
		fclose(f);
	}
}

int main(int argc, char **argv)
{
	int r;

	if(argc < 2) {
		fprintf(stderr, "usage: bfsattr set|check|rm|unlink|type|mk|val\n");
		return 1;
	}

	if(!strcmp(argv[1], "type")) {
		/* bfsattr type <name> [TYPE]: query (or set) the on-disk
		 * attribute type via the BFS ioctl (the Linux xattr ABI
		 * carries no type; Haiku's fs_stat_attr / WriteAttr do) */
		struct bfs_attr_info info;
		int fd, ok;

		if(argc < 3) {
			fprintf(stderr, "usage: bfsattr type <name> [TYPE]\n");
			return 1;
		}
		if((fd = open(path, O_RDONLY)) < 0) {
			report("ATTR-INFO", 0);
			return 1;
		}
		memset(&info, 0, sizeof(info));
		strncpy(info.name, argv[2], BFS_ATTR_NAME_MAX);
		if(argc >= 4) {
			info.type = (unsigned int)strtoul(argv[3], NULL, 0);
			r = ioctl(fd, BFS_IOC_SET_ATTR_TYPE, &info);
			ok = (r == 0);
			printf("TYPE-SET %s type=%u %s\n", argv[2], info.type,
			       ok ? "OK" : "FAIL");
			close(fd);
			return ok ? 0 : 1;
		}
		memset(&info, 0, sizeof(info));
		strncpy(info.name, argv[2], BFS_ATTR_NAME_MAX);
		r = ioctl(fd, BFS_IOC_GET_ATTR_INFO, &info);
		ok = (r == 0);
		printf("ATTR-INFO %s type=%u size=%llu %s\n", argv[2],
		       info.type, info.size, ok ? "OK" : "FAIL");
		report("ATTR-INFO", ok);
		close(fd);
		return ok ? 0 : 1;
	}

	if(!strcmp(argv[1], "mk")) {
		/* bfsattr mk <name> <size>: setxattr name to 'x'*size */
		int i, size = argc >= 4 ? atoi(argv[3]) : 0;
		if(size > 4096)
			size = 4096;
		for(i = 0; i < size; i++)
			g_buf[i] = 'x';
		r = setxattr(path, argv[2], g_buf, size, 0);
		printf("MK %s %d %s\n", argv[2], size, r == 0 ? "OK" : "FAIL");
		return r == 0 ? 0 : 1;
	}

	if(!strcmp(argv[1], "val")) {
		/* bfsattr val <name>: getxattr and print the size */
		r = getxattr(path, argv[2], g_buf, sizeof(g_buf));
		printf("VAL %s %d %s\n", argv[2], r < 0 ? -1 : r,
		       r >= 0 ? "OK" : "FAIL");
		return r >= 0 ? 0 : 1;
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
