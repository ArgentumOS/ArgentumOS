/*
 * gui_proto.h — the internal libgui <-> compositor transport.
 *
 * Implementation detail of docs/gui-e-native-api.md: the compositor and
 * libgui ship together, so this protocol has no compatibility surface
 * and may change freely. Public API: include/gui.h.
 *
 * Transport: one AF_UNIX SOCK_STREAM connection per client to the
 * compositor's socket ($GUI_SOCKET or /tmp/gui.sock). Framing: every
 * message is [u32 len][payload...] where len = payload bytes (native
 * endian) and the payload begins with a u8 message type. Window
 * backing stores are System V shared memory segments created by the
 * compositor; the shmid travels in WINDOW_CREATED. The compositor and
 * the client both attach the same segment.
 */

#ifndef FNX_GUI_PROTO_H
#define FNX_GUI_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GUI_SOCKET_DEFAULT	"/tmp/gui.sock"
#define GUI_PROTO_MAX		4096	/* largest message payload */

/* Message types (both directions). */
enum {
	GUI_MSG_CONNECT = 1,	/* c->s: client name (utf8, NUL-terminated) */

	GUI_MSG_WINDOW_CREATE,	/* c->s: window_create_t */
	GUI_MSG_WINDOW_SHOW,	/* c->s: window_op_t (win) */
	GUI_MSG_WINDOW_HIDE,	/* c->s: window_op_t (win) */
	GUI_MSG_WINDOW_MOVE,	/* c->s: window_op_t (win, x, y) */
	GUI_MSG_WINDOW_RESIZE,	/* c->s: window_op_t (win, w, h) */
	GUI_MSG_WINDOW_RAISE,	/* c->s: window_op_t (win) */
	GUI_MSG_WINDOW_CLOSE,	/* c->s: window_op_t (win) */
	GUI_MSG_DAMAGE,		/* c->s: window_op_t (win, x, y, w, h) */

	GUI_MSG_WINDOW_CREATED,	/* s->c: window_created_t */
	GUI_MSG_ACK,		/* s->c: window_ack_t (result of an op) */
	GUI_MSG_EVENT,		/* s->c: gui_event_t bytes */
	GUI_MSG_CONNECTED,	/* s->c: connected_t (after CONNECT) */
};

typedef struct {
	uint8_t type;		/* GUI_MSG_CONNECTED */
	int32_t w, h;		/* display pixel size */
} gui_connected_t;

typedef struct {
	uint8_t type;		/* GUI_MSG_WINDOW_CREATE */
	int32_t req;		/* echo of the client's request id */
	char name[64];		/* window name */
	int32_t x, y, w, h;
	uint32_t flags;
} gui_window_create_t;

typedef struct {
	uint8_t type;		/* GUI_MSG_WINDOW_CREATED */
	int32_t req;		/* echo of the request id */
	int32_t win;		/* the new window id, or -1 */
	int32_t shmid;		/* SysV shm id of the backing store */
	int32_t err;		/* 0 or a positive errno */
} gui_window_created_t;

typedef struct {
	uint8_t type;		/* GUI_MSG_ACK */
	int32_t req;		/* echo of the request id */
	int32_t err;		/* 0 or a positive errno */
	int32_t shmid;		/* new backing shmid (resize), else 0 */
} gui_window_ack_t;

typedef struct {
	uint8_t type;		/* window op message types above */
	int32_t req;
	int32_t win;
	int32_t x, y, w, h;	/* per-op: move x/y, resize w/h, damage rect */
} gui_window_op_t;

#ifdef __cplusplus
}
#endif

#endif /* FNX_GUI_PROTO_H */
