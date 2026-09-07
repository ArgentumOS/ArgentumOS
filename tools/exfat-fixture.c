/*
 * tools/exfat-fixture.c — host fixture author for the FNX exFAT driver
 * tests. Uses the VENDORED FatFs (third_party/fatfs, reference only) to
 * write files into a mkfs.exfat-formatted image, so the fixture is
 * authored by the exact reference implementation we derive format facts
 * from.
 *
 * Build: cc -Ithird_party/fatfs tools/exfat-fixture.c third_party/fatfs/ff.c \
 *        third_party/fatfs/ffunicode.c -o .build/exfat-fixture
 * Run:   .build/exfat-fixture <image>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"

static FILE *g_fp;

DSTATUS disk_initialize(BYTE pdrv) { (void)pdrv; return 0; }
DSTATUS disk_status(BYTE pdrv) { (void)pdrv; return 0; }
DRESULT disk_read(BYTE pdrv, BYTE *buf, LBA_t sector, UINT count)
{
	(void)pdrv;
	if(fseek(g_fp, (long)sector * 512, SEEK_SET)) return RES_ERROR;
	if(fread(buf, 512, count, g_fp) != count) return RES_ERROR;
	return RES_OK;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buf, LBA_t sector, UINT count)
{
	(void)pdrv;
	if(fseek(g_fp, (long)sector * 512, SEEK_SET)) return RES_ERROR;
	if(fwrite(buf, 512, count, g_fp) != count) return RES_ERROR;
	return RES_OK;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
	(void)pdrv; (void)cmd; (void)buff;
	return RES_OK;
}

static void fail(FRESULT r, const char *what)
{
	printf("FATFS %s: rc=%d\n", what, r);
	exit(1);
}

static int verify(const char *path)
{
	FATFS fs;
	DIR dj;
	FILINFO fi;
	FIL f;
	FRESULT r;
	char line[256];

	if(!(g_fp = fopen(path, "r+b"))) {
		return 1;
	}
	if((r = f_mount(&fs, "", 1)) != FR_OK) {
		printf("VERIFY mount rc=%d\n", r);
		return 1;
	}
	if((r = f_opendir(&dj, "/")) != FR_OK) {
		printf("VERIFY opendir rc=%d\n", r);
		return 1;
	}
	printf("VERIFY root:\n");
	for(;;) {
		if((r = f_readdir(&dj, &fi)) != FR_OK || !fi.fname[0]) {
			break;
		}
		printf("  %c %9lu %s\n", (fi.fattrib & AM_DIR) ? 'd' : 'f',
		       (unsigned long)fi.fsize, fi.fname);
	}
	f_closedir(&dj);
	if((r = f_open(&f, "fnx-written.txt", FA_READ)) != FR_OK) {
		printf("VERIFY fnx-written.txt rc=%d\n", r);
		return 1;
	}
	{
		unsigned int got = 0;

		f_read(&f, line, sizeof(line) - 1, &got);
		line[got] = 0;
		printf("VERIFY fnx-written.txt: %.60s\n", line);
	}
	f_close(&f);
	if((r = f_open(&f, "sub2/deep.txt", FA_READ)) == FR_OK) {
		unsigned int got = 0;

		f_read(&f, line, sizeof(line) - 1, &got);
		line[got] = 0;
		printf("VERIFY sub2/deep.txt: %.60s\n", line);
		f_close(&f);
	} else {
		printf("VERIFY sub2/deep.txt rc=%d\n", r);
		return 1;
	}
	f_mount(NULL, "", 0);
	fclose(g_fp);
	printf("VERIFY OK\n");
	return 0;
}

int main(int argc, char **argv)
{
	FATFS fs;
	FIL f;
	unsigned int n;
	FRESULT r;
	char big[5000];
	int i;

	if(argc >= 2 && argv[2] && !strcmp(argv[2], "verify")) {
		return verify(argv[1]);
	}
	if(argc < 2 || !(g_fp = fopen(argv[1], "r+b"))) {
		fprintf(stderr, "usage: exfat-fixture <image> [verify]\n");
		return 1;
	}
	if((r = f_mount(&fs, "", 1)) != FR_OK) fail(r, "mount");
	if((r = f_mkdir("subdir")) != FR_OK) fail(r, "mkdir subdir");
	if((r = f_open(&f, "hello.txt", FA_CREATE_NEW | FA_WRITE)) != FR_OK)
		fail(r, "open hello.txt");
	if((r = f_write(&f, "hello-exfat\n", 12, &n)) != FR_OK) fail(r, "write hello");
	f_close(&f);
	if((r = f_open(&f, "a-very-long-file-name-for-exfat-test.txt",
		       FA_CREATE_NEW | FA_WRITE)) != FR_OK)
		fail(r, "open long name");
	f_close(&f);
	if((r = f_open(&f, "subdir/nested.bin", FA_CREATE_NEW | FA_WRITE)) != FR_OK)
		fail(r, "open nested");
	for(i = 0; i < 5000; i++) big[i] = (char)('a' + (i % 26));
	if((r = f_write(&f, big, sizeof(big), &n)) != FR_OK) fail(r, "write nested");
	f_close(&f);
	f_mount(NULL, "", 0);	/* unmount + flush */
	fclose(g_fp);
	printf("fixture written: hello.txt, a-very-long-file-name-for-exfat-test.txt, subdir/nested.bin (5000 B)\n");
	return 0;
}
/* FatFs requires get_fattime() when not FF_FS_READONLY */
DWORD get_fattime(void)
{
	/* 2026-09-06 23:00:00 local as a DOS timestamp */
	return (DWORD)(((2026 - 1980) << 25) | (9 << 21) | (6 << 16) |
		       (23 << 11) | (0 << 5) | (0 >> 1));
}
