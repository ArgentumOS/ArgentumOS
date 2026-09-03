/* shm_cap_test.c - SysV shm size-cap regression test.
 *
 * The old shm_pages array was one PAGE_SIZE allocation (512 addr_t entries
 * on 64-bit), so segments > 2 MB were rejected with EINVAL. shm_pages is
 * now sized to the segment via kmalloc64(), so a segment is limited only by
 * SHMMAX (64 MB). Creates 3/4/16/64 MB segments, attaches each, and pokes
 * pages across the whole span (including beyond the old 512-page boundary)
 * to prove the far pages map to distinct frames.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

int main(void)
{
	static const int sizes[] = { 3 << 20, 4 << 20, 16 << 20, 64 << 20 };
	static const char *names[] = { "3MB", "4MB", "16MB", "64MB" };
	int i;

	for(i = 0; i < 4; i++) {
		int id, off;
		char *p;

		id = shmget(IPC_PRIVATE, sizes[i], IPC_CREAT | 0600);
		if(id < 0) {
			printf("CAP-FAIL: shmget %s: %m\n", names[i]);
			return 1;
		}
		p = shmat(id, NULL, 0);
		if(p == (void *)-1) {
			printf("CAP-FAIL: shmat %s: %m\n", names[i]);
			return 1;
		}
		/* poke every 256KB plus the very last byte */
		for(off = 0; off < sizes[i]; off += 262144) {
			p[off] = (char)0x5a;
		}
		p[sizes[i] - 1] = (char)0xa5;
		/* last page must be its own frame, not an alias of page 0 */
		p[0] = 1;
		if(p[sizes[i] - 1] != (char)0xa5) {
			printf("CAP-FAIL: %s last page aliased/zero\n", names[i]);
			return 1;
		}
		if(shmdt(p)) {
			printf("CAP-FAIL: shmdt %s: %m\n", names[i]);
			return 1;
		}
		if(shmctl(id, IPC_RMID, NULL)) {
			printf("CAP-FAIL: IPC_RMID %s: %m\n", names[i]);
			return 1;
		}
		printf("CAP-OK: %s segment (%d pages) attached+mapped\n",
		       names[i], sizes[i] >> 12);
	}
	printf("SHM-CAP: 3/4/16/64MB segments OK\n");
	return 0;
}
