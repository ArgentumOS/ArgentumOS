/*
 * gui_demo.c — a persistent, animated demo for the FNX session compositor.
 *
 * Run `gui_demo` from the shell of a graphical `make run-uefi` boot: it
 * spawns the compositor (if none is running), opens three windows on the
 * /dev/fb0 desktop and keeps them alive — a title-bar panel with a
 * bouncing square, a sweep-bar status panel, and a small accent window —
 * so the QEMU window shows live GUI output. Ctrl-C closes the windows and
 * stops the compositor.
 *
 * Built by `make userland64` into /bin/gui_demo (static musl x86_64).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <gui.h>
#include <gui_proto.h>

static int g_running = 1;

static void on_signal(int sig)
{
	(void)sig;
	g_running = 0;
}

/* spawn the compositor as a child (shell backgrounding is unreliable for
 * a long-running fb-owning process on FNX); returns the pid or -1 */
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

/* ---- tiny drawing helpers (windows are 32-bpp 0x00RRGGBB) ---------- */

static void fill(uint32_t *buf, int w, int h, uint32_t color)
{
	int n = w * h;
	int i;

	for(i = 0; i < n; i++) {
		buf[i] = color;
	}
}

static void fill_rect(uint32_t *buf, int stride, int x, int y, int w, int h,
		      uint32_t color)
{
	int yy;

	for(yy = y; yy < y + h; yy++) {
		int xx;

		for(xx = x; xx < x + w; xx++) {
			buf[(size_t)yy * stride + xx] = color;
		}
	}
}

/* vertical gradient between two colors */
static void vgrad(uint32_t *buf, int stride, int x, int y, int w, int h,
		  uint32_t top, uint32_t bottom)
{
	int yy;

	for(yy = 0; yy < h; yy++) {
		int t = yy * 255 / (h - 1 ? h - 1 : 1);
		uint32_t c = ((((top >> 16 & 0xFF) * (255 - t) +
			       (bottom >> 16 & 0xFF) * t) / 255) << 16) |
			     ((((top >> 8 & 0xFF) * (255 - t) +
				(bottom >> 8 & 0xFF) * t) / 255) << 8) |
			     (((top & 0xFF) * (255 - t) +
			       (bottom & 0xFF) * t) / 255);
		int xx;

		for(xx = x; xx < x + w; xx++) {
			buf[(size_t)(y + yy) * stride + xx] = c;
		}
	}
}

int main(int argc, char **argv)
{
	gui_display_t *d = NULL;
	gui_window_t *main_win = NULL, *status_win = NULL, *accent = NULL;
	uint32_t *main_buf = NULL, *status_buf = NULL, *accent_buf = NULL;
	pid_t comp = -1;
	int retries = 0;
	const int MW = 360, MH = 240, SW = 500, SH = 120, AW = 130, AH = 96;
	int sq_x = 24, sq_y = 44, sq_dx = 3, sq_dy = 2, sq_c = 0;
	int sweep = 0;
	int anim = 0;
	uint32_t accent_colors[] = { 0x00FF8800, 0x000088FF, 0x00FF00A0,
				     0x0000CC66 };
	unsigned int frame = 0;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	/* attach to a running compositor if there is one, else spawn
	 * (GUI_NO_SPAWN=1: attach-only - init has already started the
	 * compositor, so never fork a second one) */
	d = display_connect("demo");
	if(!d && !getenv("GUI_NO_SPAWN")) {
		comp = spawn_compositor();
		if(comp < 0) {
			printf("DEMO: could not start compositor\n");
			return 1;
		}
	}
	if(!d) {
		/* the compositor's startup (fb open + 4MB mmap + socket bind)
		 * can take several seconds under emulation; wait for its
		 * socket file to appear, then connect */
		{
			const char *sp = getenv("GUI_SOCKET");
			struct stat st;
			int waited = 0;

			if(!sp || !*sp) {
				sp = GUI_SOCKET_DEFAULT;
			}
			while(waited < 300) {	/* up to 30s */
				if(stat(sp, &st) == 0) {
					int k;

					/* the compositor has bound its socket
					 * but may not be in accept() yet;
					 * retry patiently until it is */
					for(k = 0; k < 200 && !d; k++) {
						struct timeval tv = {
							.tv_sec = 0,
							.tv_usec = 50000
						};

						d = display_connect("demo");
						if(!d) {
							select(0, NULL, NULL,
							       NULL, &tv);
						}
					}
					break;
				}
				/* FNX usleep/nanosleep is unreliable
				 * (timer granularity); a select timeout is
				 * the dependable way to pause */
				{
					struct timeval tv = {
						.tv_sec = 0,
						.tv_usec = 100000
					};
					select(0, NULL, NULL, NULL, &tv);
				}
				waited++;
			}
		}
	}
	if(!d) {
		printf("DEMO: no compositor (is /dev/fb0 present?)\n");
		if(comp > 0) {
			kill(comp, SIGTERM);
		}
		return 1;
	}
	printf("DEMO: connected to %dx%d display\n", display_width(d),
	       display_height(d));

	/* ---- window 1: main panel with a title bar ------------------- */
	main_win = window_create(d, "demo/main", 48, 48, MW, MH, 0);
	if(!main_win) {
		printf("DEMO: window_create(main): %s\n", strerror(errno));
		goto out;
	}
	window_show(main_win);
	main_buf = window_buffer(main_win);
	if(main_buf) {
		fill(main_buf, MW, MH, 0xC8C8C0);
		/* title bar */
		fill_rect(main_buf, MW, 0, 0, MW, 24, 0x24456E);
		fill_rect(main_buf, MW, 0, 24, MW, 1, 0x88A0C0);
		/* content: light gradient with a grid feel */
		vgrad(main_buf, MW, 0, 25, MW, MH - 25, 0xE8E8E0, 0xB0B8C0);
	}
	window_damage(main_win, 0, 0, MW, MH);

	/* ---- window 2: status sweep bar ------------------------------ */
	status_win = window_create(d, "demo/status", 48, 330, SW, SH, 0);
	if(!status_win) {
		printf("DEMO: window_create(status): %s\n", strerror(errno));
		goto out;
	}
	window_show(status_win);
	status_buf = window_buffer(status_win);
	if(status_buf) {
		fill(status_buf, SW, SH, 0x181818);
		fill_rect(status_buf, SW, 0, 0, SW, 18, 0x303030);
	}
	window_damage(status_win, 0, 0, SW, SH);

	/* ---- window 3: small accent overlapping the main panel ------- */
	accent = window_create(d, "demo/accent", 320, 150, AW, AH, 0);
	if(!accent) {
		printf("DEMO: window_create(accent): %s\n", strerror(errno));
		goto out;
	}
	window_show(accent);
	window_raise(accent);
	accent_buf = window_buffer(accent);
	if(accent_buf) {
		fill(accent_buf, AW, AH, accent_colors[0]);
	}
	window_damage(accent, 0, 0, AW, AH);

	printf("DEMO: three windows up — look at the display\n");
	fflush(stdout);

	/* ---- animation loop ------------------------------------------ */
	while(g_running) {
		gui_event_t ev;
		int dirty_x = -1, dirty_y = -1, dirty_w = 0, dirty_h = 0;

		/* pace the frame + drain any events */
		event_poll(d, &ev, 40);

		anim = (anim + 1) % 4;
		frame++;

		if(main_buf && (anim == 0 || frame < 3)) {
			/* erase the square's old spot + move it */
			int ox = sq_x, oy = sq_y;
			int i, j;

			fill_rect(main_buf, MW, ox, oy, 28, 28, 0xE8E8E0);
			sq_x += sq_dx;
			sq_y += sq_dy;
			if(sq_x < 4 || sq_x + 28 > MW - 4) {
				sq_dx = -sq_dx;
				sq_x += sq_dx;
				sq_c = (sq_c + 1) % 4;
			}
			if(sq_y < 30 || sq_y + 28 > MH - 4) {
				sq_dy = -sq_dy;
				sq_y += sq_dy;
				sq_c = (sq_c + 1) % 4;
			}
			{
				static const uint32_t sqc[4] = {
					0x00CC2020, 0x0020CC40,
					0x002040CC, 0x00CCCC20,
				};

				for(j = 0; j < 28; j++) {
					for(i = 0; i < 28; i++) {
						int border = i < 2 || j < 2 ||
							     i > 25 || j > 25;

						main_buf[(size_t)(sq_y + j) *
							 MW + sq_x + i] =
							border ? 0x00303030
							       : sqc[sq_c];
					}
				}
			}
			dirty_x = (sq_x < ox ? sq_x : ox) - 2;
			dirty_y = (sq_y < oy ? sq_y : oy) - 2;
			dirty_w = (sq_x > ox ? sq_x + 28 : ox + 28) -
				  dirty_x + 4;
			dirty_h = (sq_y > oy ? sq_y + 28 : oy + 28) -
				  dirty_y + 4;
			if(dirty_x < 0) {
				dirty_w += dirty_x;
				dirty_x = 0;
			}
			if(dirty_y < 25) {
				dirty_h += dirty_y - 25;
				dirty_y = 25;
			}
			window_damage(main_win, dirty_x, dirty_y, dirty_w,
				      dirty_h);
		}

		if(status_buf && anim == 1) {
			/* sweeping highlight inside the status bar */
			fill_rect(status_buf, SW, 4, 22, SW - 8, 26, 0x181818);
			sweep += 12;
			if(sweep + 90 > SW) {
				sweep = 4;
			}
			fill_rect(status_buf, SW, sweep, 22, 90, 26, 0x0A3A6A);
			fill_rect(status_buf, SW, sweep, 48, 90, 2, 0x2A6A9A);
			window_damage(status_win, 2, 20, SW - 4, 32);
		}

		if(accent_buf && frame % 30 == 0) {
			uint32_t c = accent_colors[(frame / 30) % 4];

			fill(accent_buf, AW, AH, c);
			window_damage(accent, 0, 0, AW, AH);
		}
	}

out:
	if(main_win) {
		window_close(main_win);
	}
	if(status_win) {
		window_close(status_win);
	}
	if(accent) {
		window_close(accent);
	}
	if(d) {
		display_disconnect(d);
	}
	if(comp > 0) {
		kill(comp, SIGTERM);
	}
	printf("DEMO: stopped\n");
	return 0;
}
