/* bfsdir.c: OpenBFS M4b test - create N files in /mnt/big, then verify
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
		snprintf(path, sizeof(path), "/mnt/big/%s", name);
		if((fd = open(path, O_CREAT | O_WRONLY, 0644)) < 0) {
			fprintf(stderr, "create %s: %s\n", path, strerror(errno));
			return 1;
		}
		close(fd);
	}
	printf("BFSDIR-CREATED %d (%d..%d)\n", n, base, base + n - 1);

	if(!(d = opendir("/mnt/big"))) {
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
	printf("BFSDIR-READDIR-COUNT %d (expect %d)\n", count, n);

	{
		struct stat st;
		int probes[] = { base, base + n / 2, base + n - 1 };
		int ok = 1;

		for(i = 0; i < 3; i++) {
			snprintf(name, sizeof(name), "f%05d", probes[i]);
			snprintf(path, sizeof(path), "/mnt/big/%s", name);
			if(stat(path, &st) < 0) {
				printf("BFSDIR-STAT-MISS %s (%s)\n", name,
				       strerror(errno));
				ok = 0;
			}
		}
		if(ok) {
			printf("BFSDIR-STAT-OK\n");
		}

		/* spread lookups: stat every n/100-th entry (exercises the
		 * descend + binary search across the whole key range) */
		ok = 1;
		for(i = 0; i < n; i += (n > 100 ? n / 100 : 1)) {
			snprintf(name, sizeof(name), "f%05d", base + i);
			snprintf(path, sizeof(path), "/mnt/big/%s", name);
			if(stat(path, &st) < 0) {
				printf("BFSDIR-SPREAD-MISS %s (%s)\n", name,
				       strerror(errno));
				ok = 0;
				break;
			}
		}
		if(ok) {
			printf("BFSDIR-SPREAD-OK\n");
		}
	}
	return 0;
}

/* mode "trunc": write 8 blocks of marker data, ftruncate to 5 blocks,
 * read back and verify the tail is gone and the first 5 blocks survive */
int main2(void)
{
	int fd, i, rd;
	char buf[1024];
	struct stat st;

	if((fd = open("/mnt/trunc", O_CREAT | O_WRONLY, 0644)) < 0) {
		printf("TRUNC-OPEN-FAIL %s\n", strerror(errno));
		return 1;
	}
	for(i = 0; i < 8; i++) {
		memset(buf, 'A' + i, sizeof(buf));
		if(write(fd, buf, sizeof(buf)) != sizeof(buf)) {
			printf("TRUNC-WRITE-FAIL %s\n", strerror(errno));
			return 1;
		}
	}
	close(fd);
	if((fd = open("/mnt/trunc", O_WRONLY)) < 0 ||
			ftruncate(fd, 5 * 1024) < 0) {
		printf("TRUNC-FTRUNC-FAIL %s\n", strerror(errno));
		return 1;
	}
	close(fd);
	if(stat("/mnt/trunc", &st) < 0) {
		printf("TRUNC-STAT-FAIL %s\n", strerror(errno));
		return 1;
	}
	printf("TRUNC-SIZE %d (expect 5120)\n", (int)st.st_size);
	if((fd = open("/mnt/trunc", O_RDONLY)) < 0) {
		printf("TRUNC-REOPEN-FAIL %s\n", strerror(errno));
		return 1;
	}
	for(i = 0; i < 5; i++) {
		rd = read(fd, buf, sizeof(buf));
		if(rd != sizeof(buf)) {
			printf("TRUNC-READ-FAIL at %d rd=%d %s\n",
			       i, rd, strerror(errno));
			return 1;
		}
		if(buf[0] != 'A' + i) {
			printf("TRUNC-CONTENT-BAD at %d got %c\n", i, buf[0]);
			return 1;
		}
	}
	if(read(fd, buf, sizeof(buf)) != 0) {
		printf("TRUNC-TAIL-FAIL (read past end)\n");
		return 1;
	}
	close(fd);
	printf("TRUNC-OK\n");
	return 0;
}
