/* OSS /dev/fb0 test: dump the framebuffer's non-zero content statistics */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(void)
{
	unsigned char buf[65536];
	int fd, n, total = 0, nonzero = 0;
	long long off = 0;

	fd = open("/System/Devices/fb0", O_RDONLY);
	if(fd < 0) { perror("open /dev/fb0"); return 1; }
	while((n = read(fd, buf, sizeof(buf))) > 0) {
		int i;
		for(i = 0; i < n; i++) {
			if(buf[i]) nonzero++;
		}
		total += n;
		off += n;
	}
	printf("FB-DUMP: read %d bytes, %d non-zero\n", total, nonzero);
	/* sample: how many distinct rows have any non-zero pixel */
	{
		unsigned char big[4096 * 4];
		int rows = 0, k;
		unsigned char *p = big;
		int fd2 = open("/System/Devices/fb0", O_RDONLY);
		if(fd2 >= 0) {
			for(k = 0; k < 48; k++) {
				if(read(fd2, big, 16 * 1024) != 16 * 1024) break;
				{
					int r, nonz = 0;
					for(r = 0; r < 16 * 1024; r++) if(big[r]) { nonz = 1; break; }
					if(nonz) rows++;
				}
			}
			printf("FB-ROWS: %d/48 text rows non-empty\n", rows);
			close(fd2);
		}
	}
	close(fd);
	return 0;
}
