/* psaux_probe.c - open /dev/psaux, read for a few seconds, print bytes.
 * Used to verify the PS/2 mouse -> kernel -> psaux path while the
 * compositor also holds the device open. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/dev/psaux", O_RDONLY | O_NONBLOCK);
	unsigned char b;
	int i, got = 0;

	if(fd < 0) {
		printf("PSAUX-PROBE: open failed: %s\n", strerror(errno));
		return 1;
	}
	printf("PSAUX-PROBE: open ok, reading 6s\n");
	fflush(stdout);
	for(i = 0; i < 600; i++) {
		ssize_t n = read(fd, &b, 1);

		if(n > 0) {
			printf("PSAUX-PROBE: byte %02x\n", b);
			fflush(stdout);
			if(++got >= 24) {
				break;
			}
		}
		usleep(10000);
	}
	printf("PSAUX-PROBE: done (%d bytes)\n", got);
	return 0;
}
