/* shm_resize_test.c - resize-leak regression through the live compositor.
 *
 * Creates one window and resizes it 150x (each resize replaces the backing
 * SysV segment: the compositor IPC_RMIDs the old while this client still
 * holds its attach, and the client's shmdt on the ack is the last detach,
 * which must free the SHM_DEST segment). Before the sys_shmdt fixes every
 * resize leaked a segment and resize #~128 hit the 128-slot cap (ENOMEM).
 * Prints RSZ: <n> resizes OK on success.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gui.h>

int main(void)
{
	gui_display_t *d;
	gui_window_t *win;
	const int n = 150;
	int i;

	d = display_connect("rsz");
	if(!d) {
		/* the boot auto-start compositor should be listening; wait a
		 * bit for it, then give up rather than spawn a second one */
		printf("RSZ: no compositor: %s\n", strerror(errno));
		return 1;
	}
	win = window_create(d, "rsz/win", 20, 20, 60, 40, 0);
	if(!win) {
		printf("RSZ: window_create: %s\n", strerror(errno));
		return 1;
	}
	for(i = 0; i < n; i++) {
		if(window_resize(win, 40 + (i % 40), 40 + (i % 30))) {
			printf("RSZ: FAIL resize %d: %s\n", i, strerror(errno));
			return 1;
		}
	}
	printf("RSZ: %d resizes OK\n", n);
	window_close(win);
	display_disconnect(d);
	return 0;
}
