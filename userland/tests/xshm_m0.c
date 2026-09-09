/* xshm_m0.c — MIT-SHM M0 acceptance (docs/design/mit-shm-plan.md):
 * prove the Xfb server answers MIT-SHM over the XLIB path (the UIKit
 * client transport). XShmQueryExtension, attach a SysV segment via
 * XShmCreateImage/XShmAttach, paint two bands into the segment and
 * XShmPutImage them into a window. The gate screendump pixel-probes
 * the bands. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <sys/shm.h>
#include <sys/ipc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

int
main(void)
{
	Display *dpy = XOpenDisplay(NULL);

	if (!dpy) {
		printf("SHMM0: no display\n");
		return 1;
	}
	if (!XShmQueryExtension(dpy)) {
		printf("SHMM0: MIT-SHM not supported by the server\n");
		return 1;
	}
	printf("SHMM0: MIT-SHM extension present (xlib)\n");
	fflush(stdout);

	int scr = DefaultScreen(dpy);
	Window win = XCreateSimpleWindow(
		dpy, RootWindow(dpy, scr), 150, 100, 400, 300, 0,
		BlackPixel(dpy, scr), WhitePixel(dpy, scr));

	XSelectInput(dpy, win, ExposureMask);
	XMapWindow(dpy, win);

	int w = 400, h = 300;
	XShmSegmentInfo shm;
	XImage *img = XShmCreateImage(dpy, DefaultVisual(dpy, scr),
				      DefaultDepth(dpy, scr), ZPixmap,
				      NULL, &shm, (unsigned) w,
				      (unsigned) h);

	if (!img) {
		printf("SHMM0: XShmCreateImage failed\n");
		return 1;
	}
	shm.shmid = shmget(IPC_PRIVATE,
			   (size_t) img->bytes_per_line * img->height,
			   IPC_CREAT | 0666);
	if (shm.shmid < 0) {
		printf("SHMM0: shmget failed\n");
		return 1;
	}
	/* replicate what the server's ProcShmAttach does: IPC_STAT fill */
	struct shmid_ds ds;

	if (shmctl(shm.shmid, IPC_STAT, &ds) < 0) {
		printf("SHMM0: shmctl IPC_STAT errno=%d\n", errno);
	} else {
		printf("SHMM0: ipc uid=%d cuid=%d gid=%d mode=0%o size=%ld euid=%d\n",
		       ds.shm_perm.uid, ds.shm_perm.cuid, ds.shm_perm.gid,
		       ds.shm_perm.mode, (long) ds.shm_segsz, geteuid());
	}
	shm.shmaddr = img->data = shmat(shm.shmid, NULL, 0);
	shm.readOnly = False;
	if (shm.shmaddr == (char *) -1) {
		printf("SHMM0: shmat failed\n");
		return 1;
	}
	if (!XShmAttach(dpy, &shm)) {
		printf("SHMM0: XShmAttach failed\n");
		return 1;
	}
	/* paint: words 0x00RRGGBB (x8r8g8b8); top band green,
	 * bottom band blue */
	for (int y = 0; y < h; y++) {
		uint32_t color = (y < h / 2) ? 0x001fa84d : 0x002288ee;
		uint32_t *row = (uint32_t *) (img->data +
					      (size_t) y * img->bytes_per_line);

		for (int x = 0; x < w; x++) {
			row[x] = color;
		}
	}
	printf("SHMM0: put 400x300 via shm seg %d\n", shm.shmid);
	fflush(stdout);

	XSync(dpy, False);
	XShmPutImage(dpy, win, DefaultGC(dpy, scr), img, 0, 0, 0, 0,
		     (unsigned) w, (unsigned) h, False);
	XSync(dpy, False);
	printf("SHMM0-DONE\n");
	fflush(stdout);

	/* hold the window up for the gate's screendump */
	sleep(6);
	XShmDetach(dpy, &shm);
	shmdt(shm.shmaddr);
	img->data = NULL;	/* XDestroyImage would free the segment */
	XDestroyImage(img);
	XCloseDisplay(dpy);
	return 0;
}
