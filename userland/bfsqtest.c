/* bfsqtest.c: OpenBFS query-engine test (gap-3/4 verification).
 * Runs after the battery (bfsdir 2000 on /mnt/big): asserts exact
 * match counts for STRING/INT64 queries over the driver-populated
 * indices and for the mkbfs-built typed demo indices (keyed on the
 * inode number, so the expected counts are fixed). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define BFS_QUERY_MAX_LEN	512
#define BFS_QUERY_MAX_RESULTS	65536
struct bfs_query {
	char query[BFS_QUERY_MAX_LEN];
	unsigned int count;
	unsigned int inodes[BFS_QUERY_MAX_RESULTS];
};
#define BFS_IOC_QUERY		0x42530003

static int fd;

static int run_query(const char *q, unsigned int *inos, unsigned int cap)
{
	struct bfs_query *bq;
	int total;

	bq = malloc(sizeof(struct bfs_query));
	memset(bq, 0, sizeof(struct bfs_query));
	strncpy(bq->query, q, BFS_QUERY_MAX_LEN - 1);
	bq->count = cap;
	if(ioctl(fd, BFS_IOC_QUERY, bq) < 0) {
		fprintf(stderr, "ioctl(%s): %s\n", q, strerror(errno));
		free(bq);
		return -1;
	}
	total = (int)bq->count;
	if(inos && total) {
		unsigned int n = (unsigned int)total < cap ? (unsigned int)total : cap;
		memcpy(inos, bq->inodes, n * sizeof(unsigned int));
	}
	free(bq);
	return total;
}

static int check(const char *q, int expect)
{
	int n = run_query(q, NULL, 0);
	int ok = (n == expect);
	printf("Q %-40s -> %4d (expect %4d) %s\n", q, n, expect,
	       ok ? "OK" : "FAIL");
	return ok ? 0 : 1;
}

int main(void)
{
	int fails = 0;

	if((fd = open("/mnt", O_RDONLY)) < 0) {
		fprintf(stderr, "open /mnt: %s\n", strerror(errno));
		return 1;
	}
	printf("BFSQTEST: query engine tests\n");

	/* driver-populated indices (after bfsdir 2000) */
	fails += check("name=f0000*", 10);	/* f00000..f00009 */
	fails += check("name=f00001", 1);
	fails += check("name=f00001 && size=0", 1);
	fails += check("name=f00001 || name=f00002", 2);
	fails += check("size=0", 2000);
	fails += check("size>0", 1);		/* /mnt/big (dir size 69632) */
	fails += check("last_modified>0", 2001);	/* big + 2000 files */
	fails += check("last_modified>=0", 2001);
	fails += check("name=f0* && size=0", 2000);
	fails += check("name=f* && name=*999", 2);	/* f00999 + f01999 */
	fails += check("!(name=f0*)", 1);	/* only 'big' in the index */
	fails += check("name=nosuchfile*", 0);

	/* typed demo indices (mkbfs-built, keyed on inode number;
	 * image inodes 19 (root) .. 22 (data.bin)) */
	fails += check("qint32=19", 1);
	fails += check("qint32>=19", 4);
	fails += check("qint32>=21", 2);
	fails += check("qint32>=100", 0);
	fails += check("quint32<21", 2);
	fails += check("qint64>=20", 3);
	fails += check("qint64>22", 0);
	fails += check("quint64>19", 3);
	fails += check("qfloat>=20.5", 2);
	fails += check("qdouble<20.5", 2);
	fails += check("qdouble>=21", 2);

	printf("BFSQTEST: %s\n", fails ? "FAIL" : "ALL-OK");
	close(fd);
	return fails ? 1 : 0;
}
