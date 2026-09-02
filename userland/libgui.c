/* libgui.c — the client library of the FNX windowing API (GUI M0).
 *
 * docs/gui-e-native-api.md: the public contract is the C API in
 * include/gui.h; libgui talks to the session compositor over the
 * internal AF_UNIX + SysV-shm transport (include/gui_proto.h).
 * v1 is synchronous: every request blocks for its reply, so a client
 * is single-threaded and events arrive only between requests.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <gui.h>
#include <gui_proto.h>

struct gui_display {
	int fd;
	int w, h;
};

struct gui_window {
	gui_display_t *display;
	int id;
	int x, y, w, h;
	void *backing;		/* shmat of the compositor's segment */
	int shmid;
	unsigned int flags;
	char name[64];
};

/* ---- transport helpers --------------------------------------------- */

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

/* read exactly len bytes (blocking) */
static int read_full(int fd, void *buf, size_t len)
{
	char *p = buf;

	while(len) {
		ssize_t n = read(fd, p, len);

		if(n <= 0) {
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

/* read one message; *payload malloc'd (starts with the u8 type).
 * Returns 0 or -1. */
static int msg_recv(int fd, unsigned char **payload, size_t *len)
{
	uint32_t hdr;
	unsigned char *p;

	if(read_full(fd, &hdr, sizeof(hdr))) {
		return -1;
	}
	if(hdr > GUI_PROTO_MAX) {
		errno = EPROTO;
		return -1;
	}
	p = malloc(hdr ? hdr : 1);
	if(!p) {
		errno = ENOMEM;
		return -1;
	}
	if(hdr && read_full(fd, p, hdr)) {
		free(p);
		return -1;
	}
	*payload = p;
	*len = hdr;
	return 0;
}

/* request()/reply: send a payload whose second field is the request
 * id, then wait for the matching reply message type. */
static int request_msg(gui_display_t *d, const void *payload, size_t plen,
		       int want_type, unsigned char **reply, size_t *rlen)
{
	if(msg_send(d->fd, payload, plen)) {
		return -1;
	}
	while(1) {
		unsigned char *m;
		size_t ml;

		if(msg_recv(d->fd, &m, &ml)) {
			return -1;
		}
		if(ml >= 1 && m[0] == (unsigned char)want_type) {
			*reply = m;
			*rlen = ml;
			return 0;
		}
		if(ml >= 1 && m[0] == GUI_MSG_EVENT) {
			/* no event queue yet in v1; ignore */
		}
		free(m);
	}
}

/* ---- session ------------------------------------------------------- */

gui_display_t *display_connect(const char *client_name)
{
	const char *sockpath = getenv("GUI_SOCKET");
	struct sockaddr_un addr;
	gui_display_t *d;
	unsigned char *m;
	size_t ml;
	uint8_t req[GUI_PROTO_MAX];

	if(!sockpath || !*sockpath) {
		sockpath = GUI_SOCKET_DEFAULT;
	}
	d = calloc(1, sizeof(*d));
	if(!d) {
		return NULL;
	}
	d->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(d->fd < 0) {
		free(d);
		return NULL;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sockpath);
	if(connect(d->fd, (struct sockaddr *)&addr, sizeof(addr))) {
		close(d->fd);
		free(d);
		return NULL;
	}
	req[0] = GUI_MSG_CONNECT;
	snprintf((char *)req + 1, sizeof(req) - 1, "%s",
		 client_name ? client_name : "app");
	if(msg_send(d->fd, req, 1 + strlen((char *)req + 1) + 1)) {
		close(d->fd);
		free(d);
		return NULL;
	}
	if(msg_recv(d->fd, &m, &ml) || ml < sizeof(gui_connected_t) ||
	   m[0] != GUI_MSG_CONNECTED) {
		close(d->fd);
		free(d);
		return NULL;
	}
	{
		gui_connected_t *cn = (gui_connected_t *)m;

		d->w = cn->w;
		d->h = cn->h;
	}
	free(m);
	return d;
}

void display_disconnect(gui_display_t *display)
{
	if(!display) {
		return;
	}
	close(display->fd);
	free(display);
}

int display_width(gui_display_t *display)
{
	return display ? display->w : 0;
}

int display_height(gui_display_t *display)
{
	return display ? display->h : 0;
}

/* ---- windows ------------------------------------------------------- */

gui_window_t *window_create(gui_display_t *display, const char *name,
			    int x, int y, int w, int h, unsigned int flags)
{
	gui_window_create_t req;
	unsigned char *m;
	size_t ml;
	gui_window_t *win;

	if(!display || w <= 0 || h <= 0) {
		errno = EINVAL;
		return NULL;
	}
	memset(&req, 0, sizeof(req));
	req.type = GUI_MSG_WINDOW_CREATE;
	req.req = 1;
	snprintf(req.name, sizeof(req.name), "%s", name ? name : "");
	req.x = x;
	req.y = y;
	req.w = w;
	req.h = h;
	req.flags = flags;
	if(request_msg(display, &req, sizeof(req), GUI_MSG_WINDOW_CREATED,
		       &m, &ml) || ml < sizeof(gui_window_created_t)) {
		return NULL;
	}
	{
		gui_window_created_t *cr = (gui_window_created_t *)m;

		if(cr->err || cr->win < 0) {
			errno = cr->err ? cr->err : EIO;
			free(m);
			return NULL;
		}
		win = calloc(1, sizeof(*win));
		if(!win) {
			errno = ENOMEM;
			free(m);
			return NULL;
		}
		win->display = display;
		win->id = cr->win;
		win->x = x;
		win->y = y;
		win->w = w;
		win->h = h;
		win->shmid = cr->shmid;
		win->flags = flags;
		snprintf(win->name, sizeof(win->name), "%s",
			 req.name);
	}
	free(m);
	win->backing = shmat(win->shmid, NULL, 0);
	if(win->backing == (void *)-1) {
		win->backing = NULL;
		window_close(win);
		return NULL;
	}
	return win;
}

/* send a window-op and wait for its ack; returns 0 or -errno. When
 * shmid_out is non-NULL and the ack carries a new backing shmid
 * (resize), *shmid_out receives it. */
static int window_op(gui_window_t *win, int type, int x, int y, int w, int h,
		     int *shmid_out)
{
	gui_window_op_t req;
	unsigned char *m;
	size_t ml;
	int err;

	if(!win || !win->display) {
		return -EINVAL;
	}
	memset(&req, 0, sizeof(req));
	req.type = (uint8_t)type;
	req.req = 1;
	req.win = win->id;
	req.x = x;
	req.y = y;
	req.w = w;
	req.h = h;
	if(request_msg(win->display, &req, sizeof(req), GUI_MSG_ACK,
		       &m, &ml) || ml < sizeof(gui_window_ack_t)) {
		return -EIO;
	}
	err = ((gui_window_ack_t *)m)->err;
	if(!err && shmid_out) {
		*shmid_out = ((gui_window_ack_t *)m)->shmid;
	}
	free(m);
	return err ? -err : 0;
}

int window_show(gui_window_t *win)
{
	return window_op(win, GUI_MSG_WINDOW_SHOW, 0, 0, 0, 0, NULL);
}

int window_hide(gui_window_t *win)
{
	return window_op(win, GUI_MSG_WINDOW_HIDE, 0, 0, 0, 0, NULL);
}

int window_move(gui_window_t *win, int x, int y)
{
	int r = window_op(win, GUI_MSG_WINDOW_MOVE, x, y, 0, 0, NULL);

	if(!r) {
		win->x = x;
		win->y = y;
	}
	return r;
}

int window_resize(gui_window_t *win, int w, int h)
{
	void *nb;
	int r;
	int nid = 0;
	r = window_op(win, GUI_MSG_WINDOW_RESIZE, 0, 0, w, h, &nid);

	if(r) {
		return r;
	}
	/* re-attach: the compositor replaced the backing segment */
	if(win->backing) {
		shmdt(win->backing);
	}
	win->w = w;
	win->h = h;
	if(nid > 0) {
		win->shmid = nid;
	}
	nb = shmat(win->shmid, NULL, 0);
	win->backing = (nb == (void *)-1) ? NULL : nb;
	return win->backing ? 0 : -ENOMEM;
}

int window_raise(gui_window_t *win)
{
	return window_op(win, GUI_MSG_WINDOW_RAISE, 0, 0, 0, 0, NULL);
}

int window_close(gui_window_t *win)
{
	int r;

	if(!win) {
		return -EINVAL;
	}
	/* detach first: the compositor destroys the backing segment on
	 * close, and a later shmdt of a destroyed segment is a kernel
	 * warning (no vma region found) */
	if(win->backing) {
		shmdt(win->backing);
		win->backing = NULL;
	}
	r = window_op(win, GUI_MSG_WINDOW_CLOSE, 0, 0, 0, 0, NULL);
	free(win);
	return r;
}

void *window_buffer(gui_window_t *window)
{
	return window ? window->backing : NULL;
}

int window_damage(gui_window_t *window, int x, int y, int w, int h)
{
	if(!window) {
		return -EINVAL;
	}
	if(x < 0) {
		x = 0;
	}
	if(y < 0) {
		y = 0;
	}
	if(x + w > window->w) {
		w = window->w - x;
	}
	if(y + h > window->h) {
		h = window->h - y;
	}
	if(w <= 0 || h <= 0) {
		return 0;
	}
	return window_op(window, GUI_MSG_DAMAGE, x, y, w, h, NULL);
}

/* ---- input --------------------------------------------------------- */

int event_poll(gui_display_t *display, gui_event_t *event, int timeout_ms)
{
	fd_set rfds;
	struct timeval tv, *ptv = NULL;

	if(!display || !event) {
		errno = EINVAL;
		return -1;
	}
	event->type = GUI_EVENT_NONE;
	if(timeout_ms >= 0) {
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		ptv = &tv;
	}
	FD_ZERO(&rfds);
	FD_SET(display->fd, &rfds);
	{
		int n = select(display->fd + 1, &rfds, NULL, NULL, ptv);

		if(n <= 0) {
			return n;	/* 0 = timeout, -1 = error */
		}
	}
	{
		unsigned char *m;
		size_t ml;

		if(msg_recv(display->fd, &m, &ml) || ml < 1) {
			return -1;
		}
		if(m[0] == GUI_MSG_EVENT && ml >= sizeof(gui_event_t) + 1) {
			memcpy(event, m + 1, sizeof(gui_event_t));
		} else if(m[0] == GUI_MSG_EVENT) {
			event->type = GUI_EVENT_NONE;
		}
		free(m);
	}
	return 1;
}
