/*
 * fnx/include/fnx/mousedev.h
 *
 * The native mouse device (/System/Devices/mouse, "the pointer"): the
 * mouse analogue of kbdaux's /dev/kbd. The kernel mouse pipeline - the
 * PS/2 8042 aux port and USB HID alike - decodes its own wire format
 * here into fixed records whenever a reader has the device open; no
 * consumer ever sees a PS/2 byte stream, so there is no framing to
 * resync and nothing to desync.
 *
 * Event record: 8 bytes
 *
 *   struct mouse_event {
 *       u8  buttons;   bit0 left, bit1 right, bit2 middle, bit3.. extra
 *       i8  wheel;     signed vertical clicks (+ = away/up)
 *       i16 dx;        signed relative X (little-endian)
 *       i16 dy;        signed relative Y (little-endian, + = down)
 *       i8  hwheel;    signed horizontal clicks
 *       u8  pad[3];
 *   };
 *
 * State lives on the heap: FNX64 maps kernel code/data at both the low
 * identity and high-half VAs, so static objects get two addresses and
 * cannot serve as sleep/wakeup keys (the same rule as psaux/kbdaux).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_MOUSEDEV_H
#define _FNX_MOUSEDEV_H

#include <fnx/fs.h>
#include <fnx/charq.h>
#include <fnx/sigcontext.h>

#define MOUSE_MAJOR	10	/* major number for the native mouse device */
#define MOUSE_MINOR	0	/* minor number */

#define MOUSE_EVENT_SIZE 8

/* button bits (mouse_event.buttons) */
#define MOUSE_BTN_LEFT		0x01
#define MOUSE_BTN_RIGHT		0x02
#define MOUSE_BTN_MIDDLE	0x04

struct mousedev {
	int count;
	struct clist read_q;
};
extern struct mousedev *mousedev_table;

int mousedev_open(struct inode *, struct fd *);
int mousedev_close(struct inode *, struct fd *);
int mousedev_read(struct inode *, struct fd *, char *, __size_t);
int mousedev_select(struct inode *, struct fd *, int);

/* emit one normalized pointer event (deltas are relative, +y = down) */
void mousedev_event(int buttons, int wheel, int dx, int dy, int hwheel);

/* 1 while a userland reader (the session server) owns the pointer */
int mousedev_active(void);

/* feed one byte from the 8042 aux port; the driver frames PS/2 packets
 * and decodes them into records */
void mousedev_ps2_byte(unsigned char b);

/* register the device (call once at boot) */
void mousedev_init(void);

#endif /* _FNX_MOUSEDEV_H */
