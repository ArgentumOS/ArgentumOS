/* shm_leak_test.c - SysV shm SHM_DEST leak regression test.
 *
 * Reproduces the compositor resize leak: IPC_RMID a segment while two
 * attaches are held (compositor + client), then detach both. The kernel
 * must free the SHM_DEST segment on the LAST detach; without that,
 * every cycle leaks one of the 128 slots and shmget() fails with ENOSPC
 * around cycle 128. Prints SHM-LEAK: <n> cycles OK on success.
 */
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>

int main(void)
{
	const int n = 400;
	int i;

	for(i = 0; i < n; i++) {
		int id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
		void *a, *b;

		if(id < 0) {
			printf("SHM-LEAK: FAIL shmget at cycle %d: %m\n", i);
			return 1;
		}
		a = shmat(id, NULL, 0);
		b = shmat(id, NULL, 0);
		if(a == (void *)-1 || b == (void *)-1) {
			printf("SHM-LEAK: FAIL shmat at cycle %d: %m\n", i);
			return 1;
		}
		if(shmctl(id, IPC_RMID, NULL)) {
			printf("SHM-LEAK: FAIL IPC_RMID at cycle %d: %m\n", i);
			return 1;
		}
		/* both attaches still held; the LAST detach must free the
		 * SHM_DEST segment */
		if(shmdt(a) || shmdt(b)) {
			printf("SHM-LEAK: FAIL shmdt at cycle %d: %m\n", i);
			return 1;
		}
	}
	printf("SHM-LEAK: %d cycles OK\n", n);
	return 0;
}
