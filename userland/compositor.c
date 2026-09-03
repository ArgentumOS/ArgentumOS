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
	size_t have;		/* bytes buffered */
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

/* ---- fb helpers ---------------------------------------------------- */

static int fb_open_device(void)
{
	size_t len;

	fb_fd = open("/dev/fb0", O_RDWR);
	if(fb_fd < 0) {
		perror("open /dev/fb0");
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

/* paint the whole screen with the desktop background */
static void composite_clear(void)
{
	static uint32_t *row;
	int i, y;

	if(!row) {
		row = malloc((size_t)fb_w * 4);
		if(!row) {
			return;
		}
		for(i = 0; i < fb_w; i++) {
			row[i] = 0x303030;
		}
	}
	for(y = 0; y < fb_h; y++) {
		fb_put_rect(0, y, fb_w, 1, row);
	}
}

/* blit window w's full visible area onto the screen */
static void composite_window(struct window *w)
{
	int dx0 = w->x < 0 ? 0 : w->x;
	int dy0 = w->y < 0 ? 0 : w->y;
	int dx1 = w->x + w->w > fb_w ? fb_w : w->x + w->w;
	int dy1 = w->y + w->h > fb_h ? fb_h : w->y + w->h;
	const uint32_t *src = w->backing;
	int y;

	if(!w->visible || !w->backing || dx1 <= dx0 || dy1 <= dy0) {
		return;
	}
	for(y = dy0; y < dy1; y++) {
		const uint32_t *row = src + (size_t)(y - w->y) * w->w +
				      (dx0 - w->x);
		fb_put_rect(dx0, y, dx1 - dx0, 1, row);
	}
}

/* bottom -> top over the visible windows */
static void composite_all(void)
{
	int i;

	composite_clear();
	for(i = 0; i < MAX_WINDOWS; i++) {
		if(windows[i].used && windows[i].visible) {
			composite_window(&windows[i]);
		}
	}
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

static int send_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;

	while(len) {
		ssize_t n = write(fd, p, len);

		if(n <= 0) {
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int msg_send(int fd, const void *payload, size_t len)
{
	uint32_t hdr = (uint32_t)len;

	if(send_all(fd, &hdr, sizeof(hdr)) || send_all(fd, payload, len)) {
		return -1;
	}
	return 0;
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
	msg_send(c->fd, &m, sizeof(m));
}

static void reply_ack(struct client *c, int req, int err, int shmid)
{
	gui_window_ack_t m;

	memset(&m, 0, sizeof(m));
	m.type = GUI_MSG_ACK;
	m.req = req;
	m.err = err;
	m.shmid = shmid;
	msg_send(c->fd, &m, sizeof(m));
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
			msg_send(c->fd, &m, sizeof(m));
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
		composite_all();
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
			break;
		case GUI_MSG_WINDOW_HIDE:
			w->visible = 0;
			printf("COMP: hide win %d\n", w->id);
			break;
		case GUI_MSG_WINDOW_MOVE:
			w->x = op->x;
			w->y = op->y;
			printf("COMP: move win %d to %d,%d\n", w->id,
			       w->x, w->y);
			break;
		case GUI_MSG_WINDOW_RESIZE: {
			/* v1: resize by re-allocating the backing; the new
			 * shmid travels in the ack so the client can
			 * re-attach */
			int nid = -1;
			void *nb = NULL;

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
			composite_all();
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
			break;
		}
		case GUI_MSG_WINDOW_CLOSE:
			printf("COMP: close win %d\n", w->id);
			window_destroy(w);
			composite_all();
			reply_ack(c, op->req, 0, 0);
			return;
		default:
			err = EINVAL;
			break;
		}
		if(!err) {
			composite_all();
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
	printf("COMP: listening on %s (%dx%d fb)\n", sockpath, fb_w, fb_h);

	while(1) {
		fd_set rfds;
		int maxfd = listen_fd;
		int nsel;

		FD_ZERO(&rfds);
		FD_SET(listen_fd, &rfds);
		for(i = 0; i < MAX_CLIENTS; i++) {
			if(clients[i].used) {
				FD_SET(clients[i].fd, &rfds);
				if(clients[i].fd > maxfd) {
					maxfd = clients[i].fd;
				}
			}
		}
		nsel = select(maxfd + 1, &rfds, NULL, NULL, NULL);
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
				for(i = 0; i < MAX_CLIENTS; i++) {
					if(!clients[i].used) {
						slot = i;
						break;
					}
				}
				if(slot >= 0) {
					clients[slot].used = 1;
					clients[slot].fd = cfd;
					clients[slot].have = 0;
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
		/* reap dead clients + their windows */
		for(i = 0; i < MAX_CLIENTS; i++) {
			if(clients[i].used && clients[i].dead) {
				int j;

				close(clients[i].fd);
				clients[i].used = 0;
				for(j = 0; j < MAX_WINDOWS; j++) {
					if(windows[j].used &&
					   windows[j].client == i) {
						printf("COMP: drop win %d "
						       "(client gone)\n",
						       windows[j].id);
						window_destroy(&windows[j]);
					}
				}
				composite_all();
			}
		}
	}
	return 0;
}
