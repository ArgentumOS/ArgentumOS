/* OSS /dev/dsp test: a 440 Hz sine, 44100 Hz S16_LE stereo, ~0.5 s. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <sys/ioctl.h>

int main(void)
{
	int dsp, i, n;
	short buf[512];
	int nsamples = 22050;   /* 0.5 s */

	dsp = open("/System/Devices/dsp", O_WRONLY);
	if(dsp < 0) { printf("TONE-FAIL open: %m\n"); return 1; }
	if(ioctl(dsp, 0xc0045005, &(int){0x10})) { printf("TONE-FAIL setfmt: %m\n"); return 1; }
	if(ioctl(dsp, 0xc0045002, &(int){44100})) { printf("TONE-FAIL speed: %m\n"); return 1; }
	if(ioctl(dsp, 0xc0045006, &(int){2})) { printf("TONE-FAIL channels: %m\n"); return 1; }

	for(n = 0; n < nsamples / 512; n++) {
		for(i = 0; i < 256; i++) {
			short v = (short)(12000.0 * sin(2.0 * 3.14159 * 440.0 * (n * 256 + i) / 44100.0));
			buf[2 * i] = v;
			buf[2 * i + 1] = v;
		}
		if(write(dsp, buf, 512 * 2) != 512 * 2) { printf("TONE-FAIL write: %m\n"); return 1; }
	}
	close(dsp);
	printf("TONE-OK %d samples\n", nsamples);
	return 0;
}
