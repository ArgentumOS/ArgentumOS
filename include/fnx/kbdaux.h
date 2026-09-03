/*
 * fnx/include/fnx/kbdaux.h
 *
 * The GUI keyboard device (/dev/kbd): the kernel keyboard pipeline
 * (PS/2 IRQ1 and USB-HID alike, both through keyboard.c) delivers
 * normalized key-press events here when a userland reader (the session
 * compositor) has the device open, and the console emission is then
 * skipped. Mirrors the psaux design (a byte queue gated by open count;
 * state on the heap because FNX64 maps kernel objects at two VAs).
 *
 * Event record: 4 bytes { key, mods, state, pad }.
 *
 * key: 0x00-0x7F = the ASCII char/control the keymap produced
 *      ('a', 'A', '1', '\t' = 9, '\r' = 13, ESC = 27, BS = 127, ...);
 *      0x80+ = semantic non-ASCII keys (arrows, editing, F-keys).
 * mods: bitmask of the modifiers held when the key was pressed.
 * state: 1 = press (autorepeat also arrives as presses), 0 = release.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_KBDAUX_H
#define _FNX_KBDAUX_H

#include <fnx/fs.h>
#include <fnx/charq.h>
#include <fnx/sigcontext.h>

#define KBD_MAJOR	11	/* major number for /dev/kbd */
#define KBD_MINOR	0	/* minor number for /dev/kbd */

/* normalized keysyms (0x80+) */
#define KB_KEY_UP	0x80
#define KB_KEY_DOWN	0x81
#define KB_KEY_LEFT	0x82
#define KB_KEY_RIGHT	0x83
#define KB_KEY_HOME	0x84
#define KB_KEY_END	0x85
#define KB_KEY_PGUP	0x86
#define KB_KEY_PGDN	0x87
#define KB_KEY_INS	0x88
#define KB_KEY_DEL	0x89
#define KB_KEY_F1	0x8A
#define KB_KEY_F12	(KB_KEY_F1 + 11)

/* modifier bitmask */
#define KB_MOD_SHIFT	0x01
#define KB_MOD_CTRL	0x02
#define KB_MOD_ALT	0x04
#define KB_MOD_ALTGR	0x08
#define KB_MOD_CAPS	0x10
#define KB_MOD_NUM	0x20

struct kbdaux {
	int count;		/* open readers */
	struct clist read_q;
};
extern struct kbdaux *kbdaux_table;

int kbdaux_open(struct inode *, struct fd *);
int kbdaux_close(struct inode *, struct fd *);
int kbdaux_read(struct inode *, struct fd *, char *, __size_t);
int kbdaux_select(struct inode *, struct fd *, int);

/* queue one key event when a reader is open (0 = nobody, dropped);
 * active() reports whether the GUI keyboard is grabbed */
void kbdaux_event(int key, int mods, int state);
int kbdaux_active(void);
void kbdaux_init(void);

#endif /* _FNX_KBDAUX_H */
