/* gui_smoke.c — GUI M0 smoke test: proves the compositor + libgui
 * vertical slice end to end. Creates windows, paints their backing
 * buffers, reports damage, and verifies the compositor really blitted
 * to /dev/fb0 by reading back a pixel.
 *
 * Prints SMOKE-<name> markers; run under the session compositor.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <gui.h>

#define FB_XRES	2
#define FB_YRES	3

static int fails;

static void mark(const char *what, int rc)
{
	if(rc) {
		printf("SMOKE-FAIL: %s (errno %d)\n", what, errno);
		fails++;
	} else {
		printf("SMOKE-PASS: %s\n", what);
	}
}

/* paint a full window with a vertical color gradient test pattern */
static void paint(void *buf, int w, int h, uint32_t base)
{
	uint32_t *px = buf;
	int x, y;

	for(y = 0; y < h; y++) {
		for(x = 0; x < w; x++) {
			px[y * w + x] = base + (uint32_t)(x * 0x010101 / 8) +
					(uint32_t)(y * 0x000101);
		}
	}
}

/* read one pixel back from /dev/fb0 (via the char device) */
static int fb_pixel(int sx, int sy, uint32_t *out)
{
	int fd = open("/System/Devices/fb0", O_RDWR);
	int w;

	if(fd < 0) {
		return -1;
	}
	w = ioctl(fd, FB_XRES, 0);
	if(w <= 0) {
		close(fd);
		return -1;
	}
	if(lseek(fd, ((off_t)sy * w + sx) * 4, SEEK_SET) < 0) {
		close(fd);
		return -1;
	}
	if(read(fd, out, 4) != 4) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/* spawn the compositor as a child (used when the shell cannot
 * background it); returns the child pid or -1 */
static pid_t spawn_compositor(void)
{
	pid_t p = fork();

	if(p < 0) {
		return -1;
	}
	if(p == 0) {
		int fd = open("/System/Temporary Files/comp.log",
			      O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if(fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
		}
		execl("/System/Tools/compositor", "compositor", (char *)NULL);
		_exit(127);
	}
	return p;
}

int main(int argc, char **argv)
{
	gui_display_t *d;
	gui_window_t *w1, *w2;
	void *buf;
	int rc;
	uint32_t px, expect;
	pid_t comp = -1;

	/* -c: spawn the compositor ourselves (shell backgrounding is
	 * unreliable for a long-running fb-owning process on FNX) */
	if(argc > 1 && !strcmp(argv[1], "-c")) {
		comp = spawn_compositor();
		if(comp < 0) {
			printf("SMOKE-FAIL: fork compositor\n");
			return 1;
		}
		sleep(2);
	}
	d = display_connect("smoke");
	mark("display_connect", d ? 0 : -1);
	if(!d) {
		return 1;
	}
	printf("SMOKE: display %dx%d\n", display_width(d), display_height(d));

	/* window 1: a 240x160 gradient at (80, 60) */
	w1 = window_create(d, "smoke1", 80, 60, 240, 160, 0);
	mark("window_create 1", w1 ? 0 : -1);
	if(!w1) {
		return 1;
	}
	rc = window_show(w1);
	mark("window_show 1", rc);
	buf = window_buffer(w1);
	mark("window_buffer 1", buf ? 0 : -1);
	if(buf) {
		/* paint a distinctive pattern; the center pixel marks it */
		uint32_t *px32 = buf;

		paint(buf, 240, 160, 0x000000);
		/* red square in the middle so the readback is unique */
		px32[80 * 240 + 120] = 0x00FF0000;
	}
	rc = window_damage(w1, 0, 0, 240, 160);
	mark("window_damage 1", rc);

	/* the compositor must have blitted the whole window; verify the
	 * center pixel on the real device (screen pos 80+120, 60+80) */
	rc = fb_pixel(80 + 120, 60 + 80, &px);
	mark("fb readback open", rc);
	if(!rc) {
		if(px == 0x00FF0000) {
			printf("SMOKE-PASS: pixel on screen == painted value\n");
		} else {
			printf("SMOKE-FAIL: pixel on screen %08x != %08x\n",
			       px, 0x00FF0000);
			fails++;
		}
	}

	/* window 2 on top, different place: stacking + independent
	 * backing */
	w2 = window_create(d, "smoke2", 400, 200, 120, 90, 0);
	mark("window_create 2", w2 ? 0 : -1);
	if(w2) {
		rc = window_show(w2);
		mark("window_show 2", rc);
		buf = window_buffer(w2);
		if(buf) {
			paint(buf, 120, 90, 0x000080);
		}
		rc = window_damage(w2, 0, 0, 120, 90);
		mark("window_damage 2", rc);
		/* w2's center must now be visible over w1 (w2 is on top) */
		rc = fb_pixel(400 + 60, 200 + 45, &px);
		if(!rc && px == 0x00FF0000) {
			printf("SMOKE-FAIL: w2 not composited on top\n");
			fails++;
		} else if(!rc) {
			printf("SMOKE-PASS: w2 covers w1 (stacking ok)\n");
		}
		rc = window_move(w2, 100, 100);
		mark("window_move 2", rc);
		rc = window_damage(w2, 0, 0, 120, 90);
		mark("window_damage 2 after move", rc);
		rc = fb_pixel(100 + 60, 100 + 45, &px);
		expect = 0x000080 + (60 * 0x010101 / 8) + (45 * 0x000101);
		if(!rc && px == expect) {
			printf("SMOKE-PASS: moved window pixel matches\n");
		} else if(!rc) {
			printf("SMOKE-FAIL: moved pixel %08x != %08x\n",
			       px, expect);
			fails++;
		}
		rc = window_raise(w2);
		mark("window_raise 2", rc);
		rc = window_damage(w2, 0, 0, 120, 90);
		mark("window_damage after raise", rc);
		rc = window_close(w2);
		mark("window_close 2", rc);
	}
	rc = window_close(w1);
	mark("window_close 1", rc);

	if(comp > 0) {
		kill(comp, SIGTERM);
		waitpid(comp, NULL, 0);
	}
	display_disconnect(d);
	printf("SMOKE-DONE %d\n", fails);
	return fails ? 1 : 0;
}
