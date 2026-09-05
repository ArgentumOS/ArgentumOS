/* compositor.c — the FNX session compositor (GUI M0).
 *
 * docs/gui-e-native-api.md: the compositor is the per-user display
 * server. It owns /dev/fb0, keeps the window tree + z-order, and
 * composites damaged regions from per-window shared-memory backing
 * stores. Clients link libgui (userland/libgui.c) and never speak the
 * protocol directly (include/gui_proto.h).
 *
 * v1 scope (M0): AF_UNIX clients, window create/show/hide/move/resize/
 * raise/close, SysV-shm backings, damage-triggered full recomposite
 * onto the 32-bpp framebuffer, sync replies. Input routing arrives in
 * a later step.
 *
 * Usage: compositor            (listen on $GUI_SOCKET or /tmp/gui.sock)
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/wait.h>

#include <gui.h>
#include <gui_proto.h>


/* /dev/fb0 geometry: the kernel exposes width/height via these custom
 * ioctls (drivers/char/fb.c); v1 assumes a 32-bpp framebuffer (the
 * UEFI GOP mapping used by FNX). */
#define FB_XRES	2
#define FB_YRES	3
#define FB_BPP	32

#define MAX_CLIENTS	8
#define MAX_WINDOWS	64

/* ---- one client connection ---------------------------------------- */

struct client {
	int fd;
	int used;
	unsigned char buf[GUI_PROTO_MAX + 4];
	size_t have;		/* bytes buffered (input) */
	unsigned char obuf[GUI_PROTO_MAX + 4];	/* pending output bytes */
	size_t olen;		/* unsent bytes in obuf (frame-coherent) */
	int dead;
};

/* ---- one window ---------------------------------------------------- */

struct window {
	int used;
	int id;
	int client;		/* owning client index, or -1 */
	char name[64];
	int x, y, w, h;
	unsigned int flags;
	int visible;
	int shmid;
	void *backing;		/* our shmat of the backing segment */
};

/* ---- compositor state ---------------------------------------------- */

static int fb_fd = -1;
static int fb_w, fb_h;
static uint32_t *fb_map;	/* userland mmap of /dev/fb0 (kernel-fixed) */
static int fb_map_ok;

static int listen_fd = -1;
static struct client clients[MAX_CLIENTS];
static struct window windows[MAX_WINDOWS];
static int next_id = 1;

/* ---- forward decls (defined below the fb helpers) ------------------ */
static struct window *window_find(int id);
static int flush_client(struct client *c);
static void client_send(struct client *c, const void *payload, size_t len,
			int drop);
static int fb_put_rect(int x, int y, int w, int h, const uint32_t *src);
static void present_rect(int rx, int ry, int rw, int rh);
static uint32_t *bbuf;

/* ---- pointer input (PS/2 mouse on /dev/psaux) ---------------------- */

static int mouse_fd = -1;
static int mouse_x, mouse_y;	/* pointer position (screen) */
static int mouse_buttons;	/* current button state */
static int mouse_grab;		/* window id capturing the pointer, -1 */
static int kbd_fd = -1;		/* /dev/kbd (key events) */
static int focus_win;		/* window id holding keyboard focus, -1 */
static unsigned char mouse_pkt[3];
static int mouse_pkt_n;

/* ---- the pointer cursor (an fb-only overlay, never in bbuf) ------- */

#define CUR_W	12
#define CUR_H	16

static int cur_vis;		/* a mouse is present */
static int cur_drawn;		/* the cursor is on the fb */
static int cur_ox, cur_oy;	/* where it was last painted */
/* per-pixel colors; 0xFFFFFFFF = transparent (show bbuf) */
static uint32_t cur_px[CUR_H][CUR_W];

static const char *cur_mask[CUR_H] = {
	"#...........",
	"##..........",
	"#.#.........",
	"#..#........",
	"#...#.......",
	"#....#......",
	"#.....#.....",
	"#......#....",
	"#.......#...",
	"#........#..",
	"#........#..",
	"#...#####...",
	"#..#........",
	"#.#.........",
	"##..........",
	"#...........",
};

static void cursor_init(void)
{
	int y, x;

	for(y = 0; y < CUR_H; y++) {
		for(x = 0; x < CUR_W; x++) {
			cur_px[y][x] = 0xFFFFFFFF;
		}
	}
	/* black body first */
	for(y = 0; y < CUR_H; y++) {
		for(x = 0; x < CUR_W; x++) {
			if(cur_mask[y][x] == '#') {
				cur_px[y][x] = 0x00000000;
			}
		}
	}
	/* then a white outline on the 4-neighbours of the body so the
	 * cursor reads on any background */
	for(y = 0; y < CUR_H; y++) {
		for(x = 0; x < CUR_W; x++) {
			int dy, dx2;

			if(cur_px[y][x] != 0xFFFFFFFF) {
				continue;
			}
			for(dy = -1; dy <= 1; dy++) {
				for(dx2 = -1; dx2 <= 1; dx2++) {
					int ny = y + dy, nx = x + dx2;

					if(dy == 0 && dx2 == 0) {
						continue;
					}
					if(ny >= 0 && ny < CUR_H &&
					   nx >= 0 && nx < CUR_W &&
					   cur_mask[ny][nx] == '#') {
						cur_px[y][x] = 0x00FFFFFF;
					}
				}
			}
		}
	}
}

/* draw the cursor at the pointer position (hotspot = its top-left tip) */
static void cursor_paint(void)
{
	uint32_t rowbuf[CUR_W];
	int x0 = mouse_x;
	int y0 = mouse_y;
	int vw = CUR_W;
	int y, x;

	if(!cur_vis || !bbuf) {
		return;
	}
	if(x0 < 0) {
		x0 = 0;
	}
	if(y0 < 0) {
		y0 = 0;
	}
	if(x0 + vw > fb_w) {
		vw = fb_w - x0;
	}
	if(vw <= 0) {
		return;
	}
	for(y = 0; y < CUR_H; y++) {
		int sy = y0 + y;

		if(sy >= fb_h) {
			break;
		}
		for(x = 0; x < vw; x++) {
			uint32_t c = cur_px[y][x];

			if(c == 0xFFFFFFFF) {
				rowbuf[x] = bbuf[(size_t)sy * fb_w + x0 + x];
			} else {
				rowbuf[x] = c;
			}
		}
		fb_put_rect(x0, sy, vw, 1, rowbuf);
	}
	cur_ox = x0;
	cur_oy = y0;
	cur_drawn = 1;
}

/* restore the fb under the cursor from the back buffer */
static void cursor_restore(void)
{
	if(cur_drawn) {
		present_rect(cur_ox, cur_oy, CUR_W, CUR_H);
		cur_drawn = 0;
	}
}

/* after any screen damage, keep the cursor on top of the rect */
static void cursor_repaint_rect(int rx, int ry, int rw, int rh)
{
	if(!cur_drawn || !cur_vis) {
		return;
	}
	if(rx < cur_ox + CUR_W && rx + rw > cur_ox &&
	   ry < cur_oy + CUR_H && ry + rh > cur_oy) {
		cursor_paint();
	}
}

/* topmost visible window containing (x, y), or NULL */
static struct window *window_at(int x, int y)
{
	int i;

	for(i = MAX_WINDOWS - 1; i >= 0; i--) {
		if(windows[i].used && windows[i].visible &&
		   x >= windows[i].x && y >= windows[i].y &&
		   x < windows[i].x + windows[i].w &&
		   y < windows[i].y + windows[i].h) {
			return &windows[i];
		}
	}
	return NULL;
}

/* send a gui_event_t to the owner of window id */
static void send_event(int win_id, gui_event_type_t type,
		       int x, int y, int button, int state)
{
	struct window *w = window_find(win_id);
	unsigned char payload[1 + sizeof(gui_event_t)];

	if(!w || w->client < 0 || !clients[w->client].used) {
		return;
	}
	payload[0] = GUI_MSG_EVENT;
	{
		gui_event_t *e = (gui_event_t *)(payload + 1);

		memset(e, 0, sizeof(*e));
		e->type = type;
		e->win = win_id;
		e->x = x;
		e->y = y;
		e->button = button;
		e->state = state;
	}
	client_send(&clients[w->client], payload, 1 + sizeof(gui_event_t), 1);
}

/* send a KEY event (keysym + modifiers) to window id's owner */
static void send_key_event(int win_id, int key, int mods, int state)
{
	struct window *w = window_find(win_id);
	unsigned char payload[1 + sizeof(gui_event_t)];

	if(!w || w->client < 0 || !clients[w->client].used) {
		return;
	}
	payload[0] = GUI_MSG_EVENT;
	{
		gui_event_t *e = (gui_event_t *)(payload + 1);

		memset(e, 0, sizeof(*e));
		e->type = GUI_EVENT_KEY;
		e->win = win_id;
		e->key = key;
		e->mods = mods;
		e->state = state;
	}
	client_send(&clients[w->client], payload, 1 + sizeof(gui_event_t), 1);
}

/* move the keyboard focus to the window's owner (FOCUS events) */
static void set_focus_win(int win_id)
{
	if(focus_win == win_id) {
		return;
	}
	if(focus_win >= 0) {
		send_event(focus_win, GUI_EVENT_FOCUS, 0, 0, 0, 0);
	}
	focus_win = win_id;
	if(focus_win >= 0) {
		send_event(focus_win, GUI_EVENT_FOCUS, 0, 0, 0, 0);
	}
}

/* one complete 3-byte PS/2 packet */
static void input_process_packet(unsigned char b0, unsigned char b1,
				 unsigned char b2)
{
	int dx, dy;
	int buttons = b0 & 7;
	struct window *w;

	dx = (int)(signed char)b1;
	dy = (int)(signed char)b2;
	/* PS/2 dy is positive down (toward the user) */
	{
		int px = mouse_x, py = mouse_y;

		mouse_x += dx;
		mouse_y += dy;
		if(mouse_x < 0) {
			mouse_x = 0;
		}
		if(mouse_y < 0) {
			mouse_y = 0;
		}
		if(mouse_x >= fb_w) {
			mouse_x = fb_w - 1;
		}
		if(mouse_y >= fb_h) {
			mouse_y = fb_h - 1;
		}
		if(mouse_x != px || mouse_y != py) {
			/* slide the overlay: uncover the old spot, draw
			 * the new one */
			cursor_restore();
			cursor_paint();
		}
	}

	if(buttons != mouse_buttons) {
		/* a button changed: press focuses + grabs the window;
		 * release goes to the grab even off-window */
		if(mouse_grab >= 0) {
			send_event(mouse_grab, GUI_EVENT_MOUSE, mouse_x,
				   mouse_y, buttons & 7,
				   (buttons & 7) ? 1 : 0);
			if(!(buttons & 7)) {
				mouse_grab = -1;
			}
		} else if(buttons & 7) {
			w = window_at(mouse_x, mouse_y);
			if(w) {
				set_focus_win(w->id);
				mouse_grab = w->id;
				send_event(w->id, GUI_EVENT_MOUSE, mouse_x,
					   mouse_y, buttons & 7, 1);
			}
		}
		mouse_buttons = buttons & 7;
		return;
	}
	/* pure motion: route to the grab, else the window under the
	 * pointer (only bother clients when the pointer is over them) */
	if(mouse_grab >= 0) {
		send_event(mouse_grab, GUI_EVENT_MOUSE, mouse_x, mouse_y,
			   mouse_buttons, 2);
		return;
	}
	w = window_at(mouse_x, mouse_y);
	if(w) {
		send_event(w->id, GUI_EVENT_MOUSE, mouse_x, mouse_y, 0, 2);
	}
}

/* feed raw bytes from /dev/psaux through the packet assembler */
static void input_feed(const unsigned char *buf, int n)
{
	int i;

	for(i = 0; i < n; i++) {
		unsigned char b = buf[i];

		if(mouse_pkt_n == 0) {
			/* first byte of a packet has bit 3 set; drop
			 * stray bytes until we find it */
			if(!(b & 0x08)) {
				continue;
			}
			mouse_pkt[0] = b;
			mouse_pkt_n = 1;
		} else if(mouse_pkt_n == 1) {
			mouse_pkt[1] = b;
			mouse_pkt_n = 2;
		} else {
			mouse_pkt[2] = b;
			mouse_pkt_n = 0;
			input_process_packet(mouse_pkt[0], mouse_pkt[1],
					     mouse_pkt[2]);
		}
	}
}

static void input_drain(void)
{
	unsigned char buf[64];
	ssize_t n;

	while((n = read(mouse_fd, buf, sizeof(buf))) > 0) {
		input_feed(buf, (int)n);
	}
}

static void input_open_mouse(void)
{
	const char *src = getenv("GUI_MOUSE");

	/* GUI_MOUSE overrides the source (test harnesses feed PS/2
	 * packets through a serial line); default = the PS/2 mouse */
	if(!src || !*src) {
		src = "/System/Devices/PS2/Mouse";
	}
	mouse_fd = open(src, O_RDONLY | O_NONBLOCK);
	if(mouse_fd >= 0 && isatty(mouse_fd)) {
		struct termios raw;

		/* test sources ride a serial line: no echo, no
		 * canonical buffering - raw 8-bit bytes */
		if(tcgetattr(mouse_fd, &raw) == 0) {
			cfmakeraw(&raw);
			tcsetattr(mouse_fd, TCSANOW, &raw);
		}
	}
	mouse_grab = -1;
	focus_win = -1;
	mouse_buttons = 0;
	mouse_pkt_n = 0;
	cursor_init();
	if(mouse_fd >= 0) {
		mouse_x = fb_w / 2;
		mouse_y = fb_h / 2;
		cur_vis = 1;
		printf("COMP: mouse on %s\n", src);
	} else {
		mouse_fd = -1;
		cur_vis = 0;
		printf("COMP: no mouse (%s)\n", strerror(errno));
	}
}

/* ---- keyboard input (/dev/kbd: 4-byte {key, mods, state, pad}) ----- */

static void kbd_process_record(unsigned char *r)
{
	int key = r[0];
	int mods = r[1];
	int state = r[2];

	/* keys go to the window holding the keyboard focus */
	if(focus_win >= 0) {
		send_key_event(focus_win, key, mods, state);
	}
}

static void kbd_drain(void)
{
	unsigned char buf[64];
	ssize_t n;

	while((n = read(kbd_fd, buf, sizeof(buf))) > 0) {
		int i;

		for(i = 0; i + 3 < n; i += 4) {
			kbd_process_record(buf + i);
		}
	}
}

static void input_open_keyboard(void)
{
	const char *src = getenv("GUI_KBD");

	if(!src || !*src) {
		src = "/System/Devices/PS2/Keyboard";
	}
	kbd_fd = open(src, O_RDONLY | O_NONBLOCK);
	if(kbd_fd >= 0 && isatty(kbd_fd)) {
		struct termios raw;

		/* test sources ride a serial line: raw 8-bit bytes */
		if(tcgetattr(kbd_fd, &raw) == 0) {
			cfmakeraw(&raw);
			tcsetattr(kbd_fd, TCSANOW, &raw);
		}
	}
	if(kbd_fd >= 0) {
		printf("COMP: keyboard on %s\n", src);
	} else {
		kbd_fd = -1;
		printf("COMP: no keyboard (%s)\n", strerror(errno));
	}
}

/* ---- fb helpers ---------------------------------------------------- */

static int fb_open_device(void)
{
	size_t len;

	fb_fd = open("/System/Devices/Display/fb0", O_RDWR);
	if(fb_fd < 0) {
		perror("open Display/fb0");
		return -1;
	}
	fb_w = ioctl(fb_fd, FB_XRES, 0);
	fb_h = ioctl(fb_fd, FB_YRES, 0);
	if(fb_w <= 0 || fb_h <= 0) {
		fprintf(stderr, "compositor: bad fb geometry %dx%d\n",
			fb_w, fb_h);
		return -1;
	}
	printf("COMP: fb0 %dx%d %d-bpp\n", fb_w, fb_h, FB_BPP);
	/* map the framebuffer for direct pixel access (skip when
	 * GUI_PWRITE=1 forces the char-device path - kernel-copy blit) */
	if(!getenv("GUI_PWRITE")) {
		len = (size_t)fb_w * fb_h * 4;
		fb_map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
			      fb_fd, 0);
		if(fb_map != MAP_FAILED) {
			fb_map_ok = 1;
			printf("COMP: fb mapped at %p\n", (void *)fb_map);
			return 0;
		}
		fb_map = NULL;
		fb_map_ok = 0;
		printf("COMP: fb mmap failed (%s); using pwrite\n",
		       strerror(errno));
	} else {
		fb_map_ok = 0;
		printf("COMP: fb blit forced to pwrite (GUI_PWRITE)\n");
	}
	return 0;
}

/* blit a w x h pixel rect to the screen at (x, y): direct memcpy into
 * the mmap'd framebuffer, falling back to per-row writes on the fb
 * device when the mmap is unavailable */
static int fb_put_rect(int x, int y, int w, int h, const uint32_t *src)
{
	int dy;

	if(fb_map_ok) {
		for(dy = 0; dy < h; dy++) {
			memcpy(fb_map + (size_t)(y + dy) * fb_w + x,
			       src + (size_t)dy * w, (size_t)w * 4);
		}
		return 0;
	}
	for(dy = 0; dy < h; dy++) {
		off_t off = ((off_t)(y + dy) * fb_w + x) * 4;
		ssize_t n;

		if(lseek(fb_fd, off, SEEK_SET) < 0) {
			return -1;
		}
		n = write(fb_fd, src + (size_t)dy * w, (size_t)w * 4);
		if(n != (ssize_t)w * 4) {
			return -1;
		}
	}
	return 0;
}

/* ---- back-buffered compositing ------------------------------------
 *
 * The compositor keeps a full-screen back buffer (system RAM). A frame is
 * assembled there and only the damaged rects are pushed to the LFB, so a
 * damage no longer repaints the whole screen and each present is a single
 * coherent copy of the changed area. The UEFI GOP framebuffer itself has
 * no hardware double buffering - this is the software equivalent. */

#define DESKTOP_COLOR	0x00303030

/* bbuf: full-screen composited back buffer (forward-declared above) */

/* clip a screen rect to the framebuffer; returns 0 if empty */
static int rect_clip(int *rx, int *ry, int *rw, int *rh)
{
	if(*rx < 0) {
		*rw += *rx;
		*rx = 0;
	}
	if(*ry < 0) {
		*rh += *ry;
		*ry = 0;
	}
	if(*rx + *rw > fb_w) {
		*rw = fb_w - *rx;
	}
	if(*ry + *rh > fb_h) {
		*rh = fb_h - *ry;
	}
	return *rw > 0 && *rh > 0;
}

/* recomposite a screen rect into the back buffer: desktop fill, then
 * every visible window bottom-to-top (the array order is the z-order,
 * higher windows overwrite lower ones - correct occlusion) */
static void composite_rect(int rx, int ry, int rw, int rh)
{
	int i, y;

	if(!bbuf || !rect_clip(&rx, &ry, &rw, &rh)) {
		return;
	}
	for(y = ry; y < ry + rh; y++) {
		uint32_t *dst = bbuf + (size_t)y * fb_w + rx;
		int n;

		for(n = 0; n < rw; n++) {
			dst[n] = DESKTOP_COLOR;
		}
	}
	for(i = 0; i < MAX_WINDOWS; i++) {
		struct window *w = &windows[i];
		int ix0, iy0, ix1, iy1;

		if(!w->used || !w->visible || !w->backing) {
			continue;
		}
		ix0 = w->x > rx ? w->x : rx;
		iy0 = w->y > ry ? w->y : ry;
		ix1 = (w->x + w->w < rx + rw) ? (w->x + w->w) : (rx + rw);
		iy1 = (w->y + w->h < ry + rh) ? (w->y + w->h) : (ry + rh);
		if(ix1 <= ix0 || iy1 <= iy0) {
			continue;
		}
		for(y = iy0; y < iy1; y++) {
			const uint32_t *src = (const uint32_t *)w->backing +
					      (size_t)(y - w->y) * w->w +
					      (ix0 - w->x);
			uint32_t *dst = bbuf + (size_t)y * fb_w + ix0;

			memcpy(dst, src, (size_t)(ix1 - ix0) * 4);
		}
	}
}

/* push a back-buffer rect to the LFB */
static void present_rect(int rx, int ry, int rw, int rh)
{
	int y;

	if(!bbuf || !rect_clip(&rx, &ry, &rw, &rh)) {
		return;
	}
	for(y = 0; y < rh; y++) {
		fb_put_rect(rx, ry + y, rw, 1,
			    bbuf + (size_t)(ry + y) * fb_w + rx);
	}
}

/* the single screen-write op: recomposite a rect + present it */
static void screen_damage(int rx, int ry, int rw, int rh)
{
	if(!rect_clip(&rx, &ry, &rw, &rh)) {
		return;
	}
	composite_rect(rx, ry, rw, rh);
	present_rect(rx, ry, rw, rh);
	/* the pointer cursor floats above the damage */
	cursor_repaint_rect(rx, ry, rw, rh);
}

/* ---- shm backings -------------------------------------------------- */

static void *shm_attach(int shmid)
{
	void *p = shmat(shmid, NULL, 0);

	return p == (void *)-1 ? NULL : p;
}

/* ---- window lifecycle ---------------------------------------------- */

static struct window *window_find(int id)
{
	int i;

	for(i = 0; i < MAX_WINDOWS; i++) {
		if(windows[i].used && windows[i].id == id) {
			return &windows[i];
		}
	}
	return NULL;
}

static struct window *window_slot(void)
{
	int i;

	for(i = 0; i < MAX_WINDOWS; i++) {
		if(!windows[i].used) {
			memset(&windows[i], 0, sizeof(windows[i]));
			windows[i].client = -1;
			return &windows[i];
		}
	}
	return NULL;
}

static void window_destroy(struct window *w)
{
	if(w->backing && w->shmid >= 0) {
		shmdt(w->backing);
	}
	if(w->shmid >= 0) {
		shmctl(w->shmid, IPC_RMID, NULL);
	}
	w->used = 0;
	w->shmid = -1;
	w->backing = NULL;
}

/* ---- socket plumbing ----------------------------------------------- */
/* All compositor -> client traffic is queued per client and flushed when
 * the socket is writable, so the compositor NEVER blocks writing to a
 * client (that would stall the input sources and deadlock against a
 * client busy in a request/ack exchange). Bytes are written in order
 * from obuf and a frame is never partially discarded, so the client's
 * [len][payload] framing can never desync. */

/* write as much pending output as the socket will take; nonblocking */
static int flush_client(struct client *c)
{
	while(c->olen) {
		ssize_t w = write(c->fd, c->obuf, c->olen);

		if(w > 0) {
			memmove(c->obuf, c->obuf + w, c->olen - (size_t)w);
			c->olen -= (size_t)w;
			continue;
		}
		if(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return 0;	/* retried when the fd is writable */
		}
		if(w < 0 && errno == EINTR) {
			continue;
		}
		c->dead = 1;		/* EPIPE etc.: client gone */
		return -1;
	}
	return 0;
}

/* Queue one [u32 len][payload] frame for c. Events (drop=1) may be
 * dropped whole when the client is not draining and the queue is full;
 * replies (drop=0) are never dropped: the queue always drains because a
 * client is reading while it awaits its reply. */
static void client_send(struct client *c, const void *payload, size_t len,
			int drop)
{
	uint32_t hdr = (uint32_t)len;
	size_t framelen = len + sizeof(hdr);

	if(!c->used || c->dead || len > GUI_PROTO_MAX) {
		return;
	}
	flush_client(c);
	if(framelen > sizeof(c->obuf)) {
		return;
	}
	if(c->olen + framelen > sizeof(c->obuf)) {
		if(drop) {
			return;		/* whole-frame drop */
		}
		/* reply: wait for queue space (the client is reading) */
		while(c->olen + framelen > sizeof(c->obuf) && !c->dead) {
			fd_set wfds;

			FD_ZERO(&wfds);
			FD_SET(c->fd, &wfds);
			select(c->fd + 1, NULL, &wfds, NULL, NULL);
			flush_client(c);
		}
		if(c->dead) {
			return;
		}
	}
	memcpy(c->obuf + c->olen, &hdr, sizeof(hdr));
	memcpy(c->obuf + c->olen + sizeof(hdr), payload, len);
	c->olen += framelen;
	flush_client(c);
}

static void reply_created(struct client *c, int req, int win, int shmid,
			  int err)
{
	gui_window_created_t m;

	memset(&m, 0, sizeof(m));
	m.type = GUI_MSG_WINDOW_CREATED;
	m.req = req;
	m.win = win;
	m.shmid = shmid;
	m.err = err;
	client_send(c, &m, sizeof(m), 0);
}

static void reply_ack(struct client *c, int req, int err, int shmid)
{
	gui_window_ack_t m;

	memset(&m, 0, sizeof(m));
	m.type = GUI_MSG_ACK;
	m.req = req;
	m.err = err;
	m.shmid = shmid;
	client_send(c, &m, sizeof(m), 0);
}

/* ---- message handling ---------------------------------------------- */

/* handle one complete message payload (payload[0] = type) */
static void handle_msg(struct client *c, const unsigned char *p, size_t len)
{
	uint8_t type = p[0];
	gui_window_op_t *op;
	struct window *w;
	int err = 0;

	switch(type) {
	case GUI_MSG_CONNECT: {
		char name[64];

		snprintf(name, sizeof(name), "%s",
			 len > 1 ? (const char *)p + 1 : "?");
		printf("COMP: client connected: %s\n", name);
		{
			gui_connected_t m;

			memset(&m, 0, sizeof(m));
			m.type = GUI_MSG_CONNECTED;
			m.w = fb_w;
			m.h = fb_h;
			client_send(c, &m, sizeof(m), 0);
		}
		return;
	}
	case GUI_MSG_WINDOW_CREATE: {
		gui_window_create_t *cr = (gui_window_create_t *)p;
		struct window *w2;
		int shmid;

		if(len < sizeof(*cr) || cr->w <= 0 || cr->h <= 0 ||
		   cr->w > 4096 || cr->h > 4096) {
			reply_created(c, cr->req, -1, -1, EINVAL);
			return;
		}
		w2 = window_slot();
		if(!w2) {
			reply_created(c, cr->req, -1, -1, ENOMEM);
			return;
		}
		shmid = shmget(IPC_PRIVATE,
				(size_t)cr->w * cr->h * sizeof(uint32_t),
				IPC_CREAT | 0600);
		if(shmid < 0) {
			reply_created(c, cr->req, -1, -1, ENOMEM);
			return;
		}
		w2->used = 1;
		w2->id = next_id++;
		w2->client = (int)(c - clients);
		snprintf(w2->name, sizeof(w2->name), "%s", cr->name);
		w2->x = cr->x;
		w2->y = cr->y;
		w2->w = cr->w;
		w2->h = cr->h;
		w2->flags = cr->flags;
		w2->visible = 0;
		w2->shmid = shmid;
		w2->backing = shm_attach(shmid);
		if(!w2->backing) {
			window_destroy(w2);
			reply_created(c, cr->req, -1, -1, ENOMEM);
			return;
		}
		printf("COMP: window %d '%s' %dx%d at %d,%d (shm %d)\n",
		       w2->id, w2->name, w2->w, w2->h, w2->x, w2->y, shmid);
		reply_created(c, cr->req, w2->id, shmid, 0);
		return;
	}
	case GUI_MSG_DAMAGE: {
		op = (gui_window_op_t *)p;
		w = window_find(op->win);
		if(!w || w->client != (int)(c - clients)) {
			reply_ack(c, op->req, EINVAL, 0);
			return;
		}
		printf("COMP: damage win %d rect %d,%d %dx%d\n",
		       op->win, op->x, op->y, op->w, op->h);
		/* damage rects are window-relative: composite only the
		 * affected screen area instead of repainting everything */
		screen_damage(w->x + op->x, w->y + op->y, op->w, op->h);
		reply_ack(c, op->req, 0, 0);
		return;
	}
	default:
		/* window ops that need a valid owned window */
		op = (gui_window_op_t *)p;
		if(len < sizeof(*op)) {
			reply_ack(c, op->req, EINVAL, 0);
			return;
		}
		w = window_find(op->win);
		if(!w || w->client != (int)(c - clients)) {
			reply_ack(c, op->req, EINVAL, 0);
			return;
		}
		switch(type) {
		case GUI_MSG_WINDOW_SHOW:
			w->visible = 1;
			printf("COMP: show win %d\n", w->id);
			screen_damage(w->x, w->y, w->w, w->h);
			/* a newly shown window takes the keyboard focus
			 * when nothing else has it (the desktop is
			 * keyboard-usable without a prior mouse click) */
			if(focus_win < 0) {
				set_focus_win(w->id);
			}
			break;
		case GUI_MSG_WINDOW_HIDE:
			w->visible = 0;
			printf("COMP: hide win %d\n", w->id);
			/* restore what is beneath the window */
			screen_damage(w->x, w->y, w->w, w->h);
			break;
		case GUI_MSG_WINDOW_MOVE: {
			int ox = w->x, oy = w->y;

			w->x = op->x;
			w->y = op->y;
			printf("COMP: move win %d to %d,%d\n", w->id,
			       w->x, w->y);
			/* restore the old spot, paint the new one */
			screen_damage(ox, oy, w->w, w->h);
			screen_damage(w->x, w->y, w->w, w->h);
			break;
		}
		case GUI_MSG_WINDOW_RESIZE: {
			/* v1: resize by re-allocating the backing; the new
			 * shmid travels in the ack so the client can
			 * re-attach */
			int nid = -1;
			void *nb = NULL;
			int ox, oy, ow, oh;

			ox = w->x;
			oy = w->y;
			ow = w->w;
			oh = w->h;

			if(op->w > 0 && op->h > 0 && op->w <= 4096 &&
			   op->h <= 4096) {
				nid = shmget(IPC_PRIVATE,
					     (size_t)op->w * op->h * 4,
					     IPC_CREAT | 0600);
				if(nid >= 0) {
					nb = shm_attach(nid);
				}
			}
			if(nid < 0 || !nb) {
				if(nid >= 0) {
					shmctl(nid, IPC_RMID, NULL);
				}
				reply_ack(c, op->req, ENOMEM, 0);
				return;
			}
			shmdt(w->backing);
			shmctl(w->shmid, IPC_RMID, NULL);
			w->shmid = nid;
			w->backing = nb;
			w->w = op->w;
			w->h = op->h;
			printf("COMP: resize win %d to %dx%d (shm %d)\n",
			       w->id, w->w, w->h, w->shmid);
			screen_damage(ox, oy, ow, oh);
			screen_damage(w->x, w->y, w->w, w->h);
			reply_ack(c, op->req, 0, nid);
			return;
		}
		case GUI_MSG_WINDOW_RAISE: {
			/* the array order is the z-order (bottom = low
			 * index); raise = move this window above all
			 * others by relocating it to the highest used
			 * index */
			int hi = -1, i;

			for(i = MAX_WINDOWS - 1; i >= 0; i--) {
				if(windows[i].used) {
					hi = i;
					break;
				}
			}
			if(hi >= 0 && hi != (int)(w - windows)) {
				struct window tmp = *w;

				/* shift windows above w down one slot */
				for(i = (int)(w - windows); i < hi; i++) {
					windows[i] = windows[i + 1];
					if(!windows[i].used) {
						windows[i].shmid = -1;
					}
				}
				windows[hi] = tmp;
				w = &windows[hi];
			}
			printf("COMP: raise win %d\n", w->id);
			/* only the raised window's rect can change: it now
			 * stacks above whatever it overlaps */
			screen_damage(w->x, w->y, w->w, w->h);
			break;
		}
		case GUI_MSG_WINDOW_CLOSE: {
			int cx = w->x, cy = w->y, cw = w->w, ch = w->h;

			printf("COMP: close win %d\n", w->id);
			window_destroy(w);
			screen_damage(cx, cy, cw, ch);
			reply_ack(c, op->req, 0, 0);
			return;
		}
		default:
			err = EINVAL;
			break;
		}
		reply_ack(c, op->req, err, 0);
		return;
	}
}

/* drain one client: read whatever is available, handle full messages */
static void service_client(struct client *c)
{
	ssize_t n;
	size_t need;

	if(c->have < sizeof(uint32_t)) {
		need = sizeof(uint32_t) - c->have;
	} else {
		uint32_t len;

		memcpy(&len, c->buf, sizeof(len));
		if(len > GUI_PROTO_MAX) {
			c->dead = 1;
			return;
		}
		need = sizeof(uint32_t) + len - c->have;
	}
	if(need > sizeof(c->buf) - c->have) {
		c->dead = 1;
		return;
	}
	n = read(c->fd, c->buf + c->have, need);
	if(n <= 0) {
		c->dead = 1;
		return;
	}
	c->have += (size_t)n;
	if(c->have >= sizeof(uint32_t)) {
		uint32_t len;

		memcpy(&len, c->buf, sizeof(len));
		if(c->have >= sizeof(uint32_t) + len) {
			handle_msg(c, c->buf + sizeof(uint32_t), len);
			/* keep any pipelined remainder */
			{
				size_t total = sizeof(uint32_t) + len;
				size_t left = c->have - total;

				if(left) {
					memmove(c->buf, c->buf + total, left);
				}
				c->have = left;
			}
		}
	}
}

/* ---- main ---------------------------------------------------------- */

int main(void)
{
	const char *sockpath = getenv("GUI_SOCKET");
	struct sockaddr_un addr;
	int i;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if(getenv("GUI_SELFCHECK")) {
		printf("COMP: selfcheck ok\n");
		return 0;
	}
	if(getenv("GUI_FBTEST")) {
		int r = fb_open_device();
		uint32_t px = 0x00FF0000;
		uint32_t rb;

		printf("COMP: fbtest rc=%d (%dx%d)\n", r, fb_w, fb_h);
		if(!r) {
			r = fb_put_rect(0, 0, 1, 1, &px);
			printf("COMP: fbtest blit rc=%d (%s)\n", r,
			       fb_map_ok ? "direct-mmap" : "pwrite");
			if(!r && fb_map_ok) {
				rb = fb_map[0];
				printf("COMP: fbtest readback %06x %s\n", rb,
				       rb == px ? "MATCH" : "MISMATCH");
			}
		}
		return r ? 1 : 0;
	}
	if(getenv("GUI_FBMMAPTEST")) {
		/* exercise the userland fb mmap path (kernel debug);
		 * mode = which teardown steps to run */
		const char *mode = getenv("GUI_FBMMAPTEST");
		int r = fb_open_device();
		void *m;

		printf("COMP: fbmmap mode=%s rc=%d (%dx%d)\n", mode, r,
		       fb_w, fb_h);
		m = mmap(NULL, (size_t)fb_w * fb_h * 4, PROT_READ | PROT_WRITE,
			 MAP_SHARED, fb_fd, 0);
		printf("COMP: fbmmap mmap=%p errno=%d\n", m, errno);
		if(m == MAP_FAILED) {
			return 1;
		}
		if(strchr(mode, 'w')) {
			uint32_t *p = m;

			p[0] = 0x00FF0000;
			p[100] = 0x0000FF00;
			printf("COMP: fbmmap wrote pixels\n");
		}
		if(strchr(mode, 'r')) {
			uint32_t *p = m;

			printf("COMP: fbmmap readback %08x\n", p[0]);
		}
		if(strchr(mode, 'F')) {
			/* fork while the fb is mapped: the child must survive
			 * the fork's pml4 copy + its own exit teardown */
			pid_t c = fork();
			uint32_t *p = m;

			if(c == 0) {
				printf("COMP: fbmmap child sees %06x\n",
				       p[0]);
				_exit(0);
			}
			if(c > 0) {
				int st;

				waitpid(c, &st, 0);
				printf("COMP: fbmmap fork child reaped\n");
			}
		}
		if(strchr(mode, 'u')) {
			if(munmap(m, (size_t)fb_w * fb_h * 4)) {
				printf("COMP: fbmmap munmap errno=%d\n", errno);
			} else {
				printf("COMP: fbmmap munmap ok\n");
			}
		}
		printf("COMP: fbmmap done\n");
		return 0;
	}
	if(!sockpath || !*sockpath) {
		sockpath = GUI_SOCKET_DEFAULT;
	}
	if(fb_open_device()) {
		return 1;
	}
	listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(listen_fd < 0) {
		perror("socket");
		return 1;
	}
	unlink(sockpath);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sockpath);
	if(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr))) {
		perror("bind");
		return 1;
	}
	if(listen(listen_fd, 4)) {
		perror("listen");
		return 1;
	}
	for(i = 0; i < MAX_CLIENTS; i++) {
		clients[i].fd = -1;
	}
	for(i = 0; i < MAX_WINDOWS; i++) {
		windows[i].shmid = -1;
	}
	bbuf = malloc((size_t)fb_w * fb_h * 4);
	if(!bbuf) {
		perror("compositor: back buffer");
		return 1;
	}
	printf("COMP: listening on %s (%dx%d fb)\n", sockpath, fb_w, fb_h);

	/* own the display: clear it to the desktop before any client */
	screen_damage(0, 0, fb_w, fb_h);

	/* the pointer: /dev/psaux (raw PS/2 stream); the cursor starts
	 * at the display centre */
	input_open_mouse();
	cursor_paint();
	input_open_keyboard();

	while(1) {
		fd_set rfds, wfds;
		int maxfd = listen_fd;
		int nsel;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(listen_fd, &rfds);
		if(mouse_fd >= 0) {
			FD_SET(mouse_fd, &rfds);
			if(mouse_fd > maxfd) {
				maxfd = mouse_fd;
			}
		}
		if(kbd_fd >= 0) {
			FD_SET(kbd_fd, &rfds);
			if(kbd_fd > maxfd) {
				maxfd = kbd_fd;
			}
		}
		for(i = 0; i < MAX_CLIENTS; i++) {
			if(clients[i].used) {
				FD_SET(clients[i].fd, &rfds);
				if(clients[i].fd > maxfd) {
					maxfd = clients[i].fd;
				}
			}
			if(clients[i].used && clients[i].olen) {
				FD_SET(clients[i].fd, &wfds);
			}
		}
		nsel = select(maxfd + 1, &rfds, &wfds, NULL, NULL);
		if(nsel < 0) {
			if(errno == EINTR) {
				continue;
			}
			perror("select");
			break;
		}
		if(FD_ISSET(listen_fd, &rfds)) {
			int cfd = accept(listen_fd, NULL, NULL);
			int slot = -1;

			if(cfd >= 0) {
				int fl = fcntl(cfd, F_GETFL, 0);

				if(fl >= 0) {
					fcntl(cfd, F_SETFL,
					      fl | O_NONBLOCK);
				}
				for(i = 0; i < MAX_CLIENTS; i++) {
					if(!clients[i].used) {
						slot = i;
						break;
					}
				}
				if(slot >= 0) {
					clients[slot].used = 1;
					clients[slot].dead = 0;
					clients[slot].fd = cfd;
					clients[slot].have = 0;
					clients[slot].olen = 0;
				} else {
					close(cfd);
				}
			}
		}
		for(i = 0; i < MAX_CLIENTS; i++) {
			if(clients[i].used && FD_ISSET(clients[i].fd, &rfds)) {
				service_client(&clients[i]);
			}
		}
		/* service requests first so a client waiting on an ack can
		 * make progress; only then generate new events from the
		 * mouse and keyboard (non-blocking sends cannot deadlock) */
		if(kbd_fd >= 0 && FD_ISSET(kbd_fd, &rfds)) {
			kbd_drain();
		}
		if(mouse_fd >= 0 && FD_ISSET(mouse_fd, &rfds)) {
			input_drain();
		}
		/* flush queued output to any client whose socket is writable
		 * (all writes are nonblocking; the select on POLLOUT above
		 * wakes us when a client that was not draining starts to) */
		for(i = 0; i < MAX_CLIENTS; i++) {
			if(clients[i].used && clients[i].olen &&
			   FD_ISSET(clients[i].fd, &wfds)) {
				flush_client(&clients[i]);
			}
		}
		/* reap dead clients + their windows */
		for(i = 0; i < MAX_CLIENTS; i++) {
			if(clients[i].used && clients[i].dead) {
				int j;

				close(clients[i].fd);
				clients[i].used = 0;
				clients[i].dead = 0;
				for(j = 0; j < MAX_WINDOWS; j++) {
					if(windows[j].used &&
					   windows[j].client == i) {
						int dx = windows[j].x;
						int dy = windows[j].y;
						int dw = windows[j].w;
						int dh = windows[j].h;

						printf("COMP: drop win %d "
						       "(client gone)\n",
						       windows[j].id);
						window_destroy(&windows[j]);
						screen_damage(dx, dy, dw, dh);
					}
				}
			}
		}
	}
	return 0;
}
