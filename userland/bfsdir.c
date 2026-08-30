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

int main(int argc, char **argv)
{
	int n = 3000, base = 0, i, fd, count = 0;
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
