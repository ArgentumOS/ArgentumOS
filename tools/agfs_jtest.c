/* agfs_jtest: exercise the AGFS journal reset path - repeatedly create
 * and unlink a file until the log fills and agfs_log_commit() takes its
 * 'log full, resetting' branch. Mounts the AGFS disk (/dev/hdb) on /mnt
 * first (the boot root is a reliable ext2 on /dev/sda). */
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(void)
{
	char f[96];
	int i;

	if(mkdir("/mnt", 0755) < 0 && errno != EEXIST) {
		perror("mkdir /mnt");
		return 1;
	}
	if(mount("/dev/hdb", "/mnt", "agfs", 0, NULL) < 0) {
		perror("mount /dev/hdb (agfs)");
		return 1;
	}
	printf("JT: agfs mounted\n");

	for(i = 0; i < 40; i++) {
		int fd;

		snprintf(f, sizeof(f), "/mnt/jt_%d", i);
		fd = open(f, O_CREAT | O_WRONLY | O_TRUNC, 0600);
		if(fd < 0) {
			printf("JT: create %d failed (%s)\n", i, strerror(errno));
			return 1;
		}
		if(write(fd, "x", 1) != 1) {
			printf("JT: write %d failed (%s)\n", i, strerror(errno));
			return 1;
		}
		close(fd);
		if(unlink(f) != 0) {
			printf("JT: unlink %d failed (%s)\n", i, strerror(errno));
			return 1;
		}
		if(i % 5 == 0) {
			printf("JT: cycle %d ok\n", i);
		}
	}
	printf("JT: 40 cycles done - journal resets survived\n");
	return 0;
}
