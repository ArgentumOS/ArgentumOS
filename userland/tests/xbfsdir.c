/* xbfsdir.c: OpenXBFS M4b test - create N files in /mnt/big, then verify
 * the directory reads them all back (readdir count + stat spot checks).
 * The point is to overflow the btree interior nodes (~60+ leaf children),
 * forcing recursive interior splits (depth-3+ trees). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

int main2(void);

static char g_buf[1024];

int main(int argc, char **argv)
{
	int n = 3000, base = 0, i, fd, count = 0;

	if(argc > 1 && !strcmp(argv[1], "trunc")) {
		return main2();
	}
	char name[32], path[64];
	DIR *d;
	struct dirent *de;

	if(argc > 1) {
		n = atoi(argv[1]);
	}
	if(argc > 2) {
		base = atoi(argv[2]);
	}

	for(i = base; i < base + n; i++) {
		snprintf(name, sizeof(name), "f%05d", i);
		snprintf(path, sizeof(path), "/Volumes/big/%s", name);
		if((fd = open(path, O_CREAT | O_WRONLY, 0644)) < 0) {
			fprintf(stderr, "create %s: %s\n", path, strerror(errno));
			return 1;
		}
		close(fd);
	}
	printf("XBFSDIR-CREATED %d (%d..%d)\n", n, base, base + n - 1);

	if(!(d = opendir("/Volumes/big"))) {
		fprintf(stderr, "opendir: %s\n", strerror(errno));
		return 1;
	}
	while((de = readdir(d))) {
		if(de->d_name[0] == '.') {
			continue;
		}
		count++;
	}
	closedir(d);
	printf("XBFSDIR-READDIR-COUNT %d (expect %d)\n", count, n);

	{
		struct stat st;
		int probes[] = { base, base + n / 2, base + n - 1 };
		int ok = 1;

		for(i = 0; i < 3; i++) {
			snprintf(name, sizeof(name), "f%05d", probes[i]);
			snprintf(path, sizeof(path), "/Volumes/big/%s", name);
			if(stat(path, &st) < 0) {
				printf("XBFSDIR-STAT-MISS %s (%s)\n", name,
				       strerror(errno));
				ok = 0;
			}
		}
		if(ok) {
			printf("XBFSDIR-STAT-OK\n");
		}

		/* spread lookups: stat every n/100-th entry (exercises the
		 * descend + binary search across the whole key range) */
		ok = 1;
		for(i = 0; i < n; i += (n > 100 ? n / 100 : 1)) {
			snprintf(name, sizeof(name), "f%05d", base + i);
			snprintf(path, sizeof(path), "/Volumes/big/%s", name);
			if(stat(path, &st) < 0) {
				printf("XBFSDIR-SPREAD-MISS %s (%s)\n", name,
				       strerror(errno));
				ok = 0;
				break;
			}
		}
		if(ok) {
			printf("XBFSDIR-SPREAD-OK\n");
		}
	}
	return 0;
}

/* mode "trunc": create an 8-block file (blocks 0-3, 6-7, then 4-5),
 * ftruncate to 7 blocks, read back and verify blocks 0-6 survive and the
 * tail is gone. The straddling run (blocks 6-7 vs the 7-block EOF)
 * exercises the truncate whole-run-vs-partial free logic: a run starting
 * before the new length must keep its in-range blocks (the old disk-addr
 * bug dropped block 6 and the read-back failed on zeros). */
static void put_blk(int fd, int blk, char c)
{
	if(lseek(fd, (off_t)blk * 1024, SEEK_SET) < 0) {
		printf("TRUNC-SEEK-FAIL %s\n", strerror(errno));
		exit(1);
	}
	memset(g_buf, c, sizeof(g_buf));
	if(write(fd, g_buf, sizeof(g_buf)) != sizeof(g_buf)) {
		printf("TRUNC-WRITE-FAIL %s\n", strerror(errno));
		exit(1);
	}
}

int main2(void)
{
	int fd, i, rd;
	struct stat st;

	if((fd = open("/Volumes/trunc", O_CREAT | O_WRONLY, 0644)) < 0) {
		printf("TRUNC-OPEN-FAIL %s\n", strerror(errno));
		return 1;
	}
	for(i = 0; i < 4; i++) {
		put_blk(fd, i, 'A' + i);
	}
	for(i = 6; i < 8; i++) {
		put_blk(fd, i, 'A' + i);
	}
	for(i = 4; i < 6; i++) {
		put_blk(fd, i, 'A' + i);
	}
	close(fd);
	/* truncate to 7 blocks: run1 (blocks 6-7) straddles the new EOF, so
	 * the old bug (whole-free by disk address) would drop block 6 and the
	 * read-back below fails on zeros */
	if((fd = open("/Volumes/trunc", O_WRONLY)) < 0 ||
			ftruncate(fd, 7 * 1024) < 0) {
		printf("TRUNC-FTRUNC-FAIL %s\n", strerror(errno));
		return 1;
	}
	close(fd);
	if(stat("/Volumes/trunc", &st) < 0) {
		printf("TRUNC-STAT-FAIL %s\n", strerror(errno));
		return 1;
	}
	printf("TRUNC-SIZE %d (expect 7168)\n", (int)st.st_size);
	if((fd = open("/Volumes/trunc", O_RDONLY)) < 0) {
		printf("TRUNC-REOPEN-FAIL %s\n", strerror(errno));
		return 1;
	}
	for(i = 0; i < 7; i++) {
		rd = read(fd, g_buf, sizeof(g_buf));
		if(rd != sizeof(g_buf)) {
			printf("TRUNC-READ-FAIL at %d rd=%d %s\n",
			       i, rd, strerror(errno));
			return 1;
		}
		if(g_buf[0] != 'A' + i) {
			printf("TRUNC-CONTENT-BAD at %d got %c\n", i, g_buf[0]);
			return 1;
		}
	}
	if(read(fd, g_buf, sizeof(g_buf)) != 0) {
		printf("TRUNC-TAIL-FAIL (read past end)\n");
		return 1;
	}
	close(fd);
	printf("TRUNC-OK\n");
	return 0;
}
