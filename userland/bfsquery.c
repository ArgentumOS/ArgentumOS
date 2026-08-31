/* bfsquery.c: OpenBFS query tool - the userland face of Haiku's BQuery.
 * Usage: bfsquery <mountpoint> '<query>'
 * The query expression follows Haiku's QueryParser syntax:
 *   attr op value  with op = | != > >= < <=
 *   combined with && (tighter) and ||, parentheses, and !( ... )
 *   values quoted with ' or " or bare; * ? [ are wildcards for
 *   = and != on STRING indices.
 * The tool issues the BFS_IOC_QUERY ioctl on the mountpoint, then
 * resolves the returned inode numbers to paths by walking the volume
 * tree (stat st_ino == inode number) and prints the matches. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define BFS_QUERY_MAX_LEN	512
#define BFS_QUERY_MAX_RESULTS	65536
struct bfs_query {
	char query[BFS_QUERY_MAX_LEN];
	unsigned int count;
	unsigned int inodes[BFS_QUERY_MAX_RESULTS];
};
#define BFS_IOC_QUERY		0x42530003	/* 'BS' + 3 */

static unsigned int *g_match;
static unsigned int g_nmatch;
static unsigned int g_printed;

static int in_match(unsigned int ino)
{
	unsigned int lo = 0, hi = g_nmatch;

	while(lo < hi) {
		unsigned int mid = (lo + hi) >> 1;
		if(g_match[mid] < ino) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return (lo < g_nmatch && g_match[lo] == ino);
}

static void walk(const char *dirpath)
{
	DIR *d;
	struct dirent *de;

	if(!(d = opendir(dirpath))) {
		return;
	}
	while((de = readdir(d))) {
		char path[1024];
		struct stat st;

		if(!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", dirpath, de->d_name);
		if(stat(path, &st) < 0) {
			continue;
		}
		if(in_match((unsigned int)st.st_ino)) {
			printf("%s\n", path);
			g_printed++;
		}
		if(S_ISDIR(st.st_mode)) {
			walk(path);
		}
	}
	closedir(d);
}

int main(int argc, char **argv)
{
	const char *mnt, *expr;
	int fd, total, n, i;
	struct bfs_query *q;
	size_t sz;

	if(argc != 3) {
		fprintf(stderr,
			"usage: %s <mountpoint> '<query>'\n", argv[0]);
		fprintf(stderr, "  e.g. %s /mnt \"size>1024 && name=f00*\"\n",
			argv[0]);
		return 1;
	}
	mnt = argv[1];
	expr = argv[2];

	if((fd = open(mnt, O_RDONLY)) < 0) {
		fprintf(stderr, "open %s: %s\n", mnt, strerror(errno));
		return 1;
	}

	sz = sizeof(struct bfs_query);
	if(!(q = (struct bfs_query *)malloc(sz))) {
		return 1;
	}

	/* probe: count the matches */
	memset(q, 0, sz);
	strncpy(q->query, expr, BFS_QUERY_MAX_LEN - 1);
	q->count = 0;
	if(ioctl(fd, BFS_IOC_QUERY, q) < 0) {
		fprintf(stderr, "query '%s': %s\n", expr, strerror(errno));
		return 1;
	}
	total = q->count;
	if(total == 0) {
		printf("(no matches)\n");
		return 0;
	}
	if(total > BFS_QUERY_MAX_RESULTS) {
		fprintf(stderr, "warning: %d matches, showing the first %d\n",
			total, BFS_QUERY_MAX_RESULTS);
		total = BFS_QUERY_MAX_RESULTS;
	}

	/* fetch the matches */
	memset(q, 0, sz);
	strncpy(q->query, expr, BFS_QUERY_MAX_LEN - 1);
	q->count = total;
	if(ioctl(fd, BFS_IOC_QUERY, q) < 0) {
		fprintf(stderr, "query '%s': %s\n", expr, strerror(errno));
		return 1;
	}
	n = q->count;
	if(n > total) {
		n = total;
	}
	g_match = q->inodes;
	g_nmatch = (unsigned int)n;
	g_printed = 0;

	printf("BFSQUERY: %d match(es) for: %s\n", n, expr);
	walk(mnt);

	close(fd);
	free(q);
	return g_printed ? 0 : 0;
}
