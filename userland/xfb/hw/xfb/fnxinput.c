/* FNX real-input backend for the Xfb DDX.
 *
 * Xvfb creates core pointer/keyboard devices but (XTEST aside) feeds them
 * nothing. This file plugs FNX's input devices into the X event pipeline
 * the kdrive way: the os layer polls /System/Devices/mouse (the native
 * pointer device: 8-byte records, decoded by the kernel from the PS/2
 * port or USB HID) and /dev/kbd (4-byte {key, mods, state, pad}
 * records) through SetNotifyFd, and the notify
 * callbacks drain + parse the streams and call QueuePointerEvents /
 * QueueKeyboardEvents into mieq.
 *
 * Device paths are overridable (XFB_MOUSE / XFB_KBD) so test harnesses
 * can feed PS/2 packets over a raw serial line, exactly like the GUI
 * compositor's GUI_MOUSE seam.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include <X11/Xproto.h>
#include "input.h"
#include "inputstr.h"
#include "scrnintstr.h"
#include "os.h"
#include "mipointer.h"
#include "misc.h"
#include "events.h"
#include "inpututils.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static DeviceIntPtr vfbMouseDev;
static DeviceIntPtr vfbKbdDev;

static int vfbMouseFd = -1;
static int vfbKbdFd = -1;

/* absolute pointer tracking (we feed relative deltas) */
static int vfbPtrX = 0;
static int vfbPtrY = 0;

/* /dev/mouse record assembly state (partial reads are possible) */
#define FNX_MOUSE_EVENT_SIZE 8
static unsigned char vfbMouseRec[FNX_MOUSE_EVENT_SIZE];
static int vfbMouseRecN = 0;

/* current X button bitmask: bit0 left, bit1 middle, bit2 right */
static int vfbMouseButtons = 0;


/* ---- mouse ----------------------------------------------------------- */

static void
vfbMouseRecord(const unsigned char *rec)
{
    int buttons = rec[0] & 0x07;
    int dx = (short) (rec[2] | (rec[3] << 8));
    int dy = (short) (rec[4] | (rec[5] << 8));
    int changed = buttons ^ vfbMouseButtons;
    ValuatorMask mask;
    int btn;
    int i;

    /* record: bit0 left, bit1 right, bit2 middle (PS/2 + HID order).
     * X core: button 1 left, 2 middle, 3 right. */
    static const int xbtn[3] = { 1, 3, 2 };

    /* deltas are already screen convention (positive Y = down) */
    vfbPtrX += dx;
    vfbPtrY += dy;

    valuator_mask_zero(&mask);
    valuator_mask_set(&mask, 0, dx);
    valuator_mask_set(&mask, 1, dy);
    QueuePointerEvents(vfbMouseDev, MotionNotify, 0,
                       POINTER_RELATIVE, &mask);

    for (i = 0; i < 3; i++) {
        int bit = 1 << i;
        if (changed & bit) {
            if (buttons & bit)
                QueuePointerEvents(vfbMouseDev, ButtonPress,
                                   xbtn[i], 0, NULL);
            else
                QueuePointerEvents(vfbMouseDev, ButtonRelease,
                                   xbtn[i], 0, NULL);
        }
    }
    vfbMouseButtons = buttons;
}

/* assemble fixed 8-byte records (a read may split one) */
static void
vfbFeedMouseByte(unsigned char b)
{
    vfbMouseRec[vfbMouseRecN++] = b;
    if (vfbMouseRecN < FNX_MOUSE_EVENT_SIZE)
        return;
    vfbMouseRecN = 0;
    vfbMouseRecord(vfbMouseRec);
}

static void
vfbDrainMouse(int fd)
{
    unsigned char buf[64];
    ssize_t n, i;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++)
            vfbFeedMouseByte(buf[i]);
    }
}

static void
vfbMouseNotify(int fd, int readmask, void *data)
{
    vfbDrainMouse(fd);
}

/* ---- keyboard --------------------------------------------------------
 *
 * /dev/kbd (FNX) delivers RESOLVED keysyms: the kernel keymap already
 * folded shift/caps/ctrl into the value, so 'A' arrives as key='A' with
 * mods=SHIFT and ctrl+c as key=0x03 with mods=CTRL. X instead wants
 * physical keycodes + its own modifier state. We inverse-translate each
 * keysym to its US-layout keycode (evdev numbering: linux input-event
 * code + 8, matching the keymap the server compiles from
 * xkeyboard-config) and synthesize modifier press/releases around the
 * char so the X keymap reproduces the right keysym. Modifier keys
 * themselves arrive as KB_KEY_LSHIFT/... events and are forwarded
 * verbatim; vfbModDown tracks which X modifier keycodes are logically
 * down so chords are not double-sent. */

#define KB_KEY_UP 0x80
#define KB_KEY_DOWN 0x81
#define KB_KEY_LEFT 0x82
#define KB_KEY_RIGHT 0x83
#define KB_KEY_HOME 0x84
#define KB_KEY_END 0x85
#define KB_KEY_PGUP 0x86
#define KB_KEY_PGDN 0x87
#define KB_KEY_INS 0x88
#define KB_KEY_DEL 0x89
#define KB_KEY_F1 0x8A
#define KB_KEY_F12 (KB_KEY_F1 + 11)
#define KB_KEY_LSHIFT 0x96
#define KB_KEY_RSHIFT 0x97
#define KB_KEY_LCTRL 0x98
#define KB_KEY_RCTRL 0x99
#define KB_KEY_LALT 0x9A
#define KB_KEY_RALT 0x9B

#define KBD_MOD_SHIFT   0x01    /* mirrors fnx KB_MOD_* */
#define KBD_MOD_CTRL    0x02
#define KBD_MOD_ALT     0x04
#define KBD_MOD_ALTGR   0x08
#define KBD_MOD_CAPS    0x10
#define KBD_MOD_NUM     0x20

static int vfbModDown;          /* bitset of X modifiers sent down */

/* US inverse map: for every ASCII char 0x20..0x7e, the unshifted keycode
 * (high bit = the char is the SHIFT variant of its key) */
static const unsigned char fnxUs[0x60] = {
    /*      !   "   #   $   %   &   '   (   )   *   +   ,   -   .   / */
    0x41,0x8a,0xb0,0x8c,0x8d,0x8e,0x90,0x30,0x92,0x93,0x91,0x95,0x3b,0x14,0x3c,0x3d,
    /* 0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ? */
    0x13,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0xaf,0x2f,0xbb,0x15,0xbc,0xbd,
    /* @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O */
    0x8b,0xa6,0xb8,0xb6,0xa8,0x9a,0xa9,0xaa,0xab,0x9f,0xac,0xad,0xae,0xba,0xb9,0xa0,
    /* P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _ */
    0xa1,0x98,0x9b,0xa7,0x9c,0x9e,0xb7,0x99,0xb5,0x9d,0xb4,0x22,0x33,0x23,0x8f,0x94,
    /* `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o */
    0x31,0x26,0x38,0x36,0x28,0x1a,0x29,0x2a,0x2b,0x1f,0x2c,0x2d,0x2e,0x3a,0x39,0x20,
    /* p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~  DEL */
    0x21,0x18,0x1b,0x27,0x1c,0x1e,0x37,0x19,0x35,0x1d,0x34,0xa2,0xb3,0xa3,0xb1,0x16
};

static void
vfbKbdQueue(DeviceIntPtr dev, int isPress, int keycode)
{
    QueueKeyboardEvents(dev, isPress ? KeyPress : KeyRelease, keycode);
}

/* reconcile one synthesized modifier (keycode) with the down-state */
static void
vfbKbdMod(int bit, int keycode, int down)
{
    if (down && !(vfbModDown & bit)) {
        vfbModDown |= bit;
        vfbKbdQueue(vfbKbdDev, 1, keycode);
    }
    else if (!down && (vfbModDown & bit)) {
        vfbModDown &= ~bit;
        vfbKbdQueue(vfbKbdDev, 0, keycode);
    }
}

/* keycode + shift requirement for a /dev/kbd keysym (ASCII or KB_KEY_*);
 * returns -1 when unmapped. ctrlHeld disambiguates control chars. */
static int
vfbKbdKey(int key, int ctrlHeld, int *shifted)
{
    int kc;

    *shifted = 0;
    if (key >= 0x20 && key < 0x7f) {
        kc = fnxUs[key - 0x20];
        if (kc == 0)
            return -1;
        *shifted = kc & 0x80 ? 1 : 0;
        return kc & 0x7f;
    }
    switch (key) {
    case '\t':
        return ctrlHeld ? 31 : 23;  /* ctrl+i vs Tab */
    case 0x0d:
        return ctrlHeld ? 58 : 36;  /* ctrl+m vs Enter */
    case 0x1b:
        return ctrlHeld ? 34 : 9;   /* ctrl+[ vs Escape */
    case 0x7f:
        return ctrlHeld ? 43 : 22;  /* ctrl+h vs BackSpace */
    case 0x00:
        return 38;                  /* ctrl+space-ish: fall back to 'a' */
    }
    if (key < 0x20)                 /* ctrl+letter control chars */
        return 0;                   /* caller maps via the letter table */
    switch (key) {
    case KB_KEY_UP: return 111;
    case KB_KEY_DOWN: return 116;
    case KB_KEY_LEFT: return 113;
    case KB_KEY_RIGHT: return 114;
    case KB_KEY_HOME: return 110;
    case KB_KEY_END: return 115;
    case KB_KEY_PGUP: return 112;
    case KB_KEY_PGDN: return 117;
    case KB_KEY_INS: return 118;
    case KB_KEY_DEL: return 119;
    case KB_KEY_LSHIFT: return 50;
    case KB_KEY_RSHIFT: return 62;
    case KB_KEY_LCTRL: return 37;
    case KB_KEY_RCTRL: return 105;
    case KB_KEY_LALT: return 64;
    case KB_KEY_RALT: return 108;
    }
    if (key >= KB_KEY_F1 && key <= KB_KEY_F12) {
        static const unsigned char fk[12] = {
            67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 95, 96
        };
        return fk[key - KB_KEY_F1];
    }
    return -1;
}

/* ctrl+letter control chars map to the letter keycode */
static int
vfbCtrlLetterKey(int c)
{
    static const char row1[] = "qwertyuiop";
    static const char row2[] = "asdfghjkl";
    static const char row3[] = "zxcvbnm";
    const char *p;

    if (c >= 1 && c <= 26)
        c += 'a' - 1;
    else
        return -1;
    if ((p = strchr(row1, c)))
        return 24 + (p - row1);
    if ((p = strchr(row2, c)))
        return 38 + (p - row2);
    if ((p = strchr(row3, c)))
        return 52 + (p - row3);
    return -1;
}

static void
vfbKbdRecord(const unsigned char *r)
{
    int key = r[0];
    int mods = r[1];
    int state = r[2];
    int isModKey = (key >= KB_KEY_LSHIFT && key <= KB_KEY_RALT);
    int shifted = 0;
    int keycode;

    if (isModKey) {
        int bit;

        keycode = vfbKbdKey(key, 0, &shifted);
        switch (key) {
        case KB_KEY_LSHIFT: case KB_KEY_RSHIFT: bit = KBD_MOD_SHIFT; break;
        case KB_KEY_LCTRL: case KB_KEY_RCTRL: bit = KBD_MOD_CTRL; break;
        case KB_KEY_LALT: bit = KBD_MOD_ALT; break;
        default: bit = KBD_MOD_ALTGR; break;
        }
        if (state) {
            vfbModDown |= bit;
            vfbKbdQueue(vfbKbdDev, 1, keycode);
        }
        else {
            vfbModDown &= ~bit;
            vfbKbdQueue(vfbKbdDev, 0, keycode);
        }
        return;
    }

    /* reconcile the modifiers the char implies */
    vfbKbdMod(KBD_MOD_CTRL, 37, (mods & KBD_MOD_CTRL) != 0);
    vfbKbdMod(KBD_MOD_ALT, 64, (mods & KBD_MOD_ALT) != 0);
    vfbKbdMod(KBD_MOD_ALTGR, 108, (mods & KBD_MOD_ALTGR) != 0);

    keycode = vfbKbdKey(key, (mods & KBD_MOD_CTRL) != 0, &shifted);
    if (keycode == 0 && key < 0x20)
        keycode = vfbCtrlLetterKey(key);
    if (keycode < 0)
        return;
    /* shifted chars (kernel folded shift/caps in) need Shift_L down */
    vfbKbdMod(KBD_MOD_SHIFT, 50, shifted != 0);
    vfbKbdQueue(vfbKbdDev, state, keycode);
    {
        static int dbq;
        if (dbq < 12) {
            dbq++;
            ErrorF("XQR: key=%d state=%d kc=%d sh=%d\n", key, state, keycode,
                   shifted);
        }
    }
}

static void
vfbDrainKbd(int fd)
{
    unsigned char buf[64];
    ssize_t n, i;
    static int dbd;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (dbd < 10) {
            dbd++;
            ErrorF("XKB: drain n=%d first=%02x\n", (int) n, buf[0]);
        }
        for (i = 0; i + 3 < n; i += 4)
            vfbKbdRecord(buf + i);
    }
}

static void
vfbKbdNotify(int fd, int readmask, void *data)
{
    (void) data;
    vfbDrainKbd(fd);
}

/* ---- setup ----------------------------------------------------------- */

static void
vfbSetRaw(int fd)
{
    struct termios raw;

    if (tcgetattr(fd, &raw) == 0) {
        cfmakeraw(&raw);
        tcsetattr(fd, TCSANOW, &raw);
    }
}


void
vfbFnxInputInit(DeviceIntPtr pMouse, DeviceIntPtr pKbd)
{
    const char *src;

    vfbMouseDev = pMouse;
    vfbKbdDev = pKbd;

    src = getenv("XFB_MOUSE");
    if (!src || !*src)
        src = "/System/Devices/mouse";   /* by-role alias (PS2/Mouse or USB/Mouse) */
    vfbMouseFd = open(src, O_RDONLY | O_NONBLOCK);
    if (vfbMouseFd >= 0) {
        vfbSetRaw(vfbMouseFd);
        SetNotifyFd(vfbMouseFd, vfbMouseNotify, X_NOTIFY_READ, NULL);
        ErrorF("Xfb: mouse on %s\n", src);
    }
    else {
        ErrorF("Xfb: no mouse (%s) - XTEST only\n", strerror(errno));
    }

    src = getenv("XFB_KBD");
    if (!src || !*src)
        src = "/System/Devices/PS2/Keyboard";
    vfbKbdFd = open(src, O_RDONLY | O_NONBLOCK);
    if (vfbKbdFd >= 0) {
        vfbSetRaw(vfbKbdFd);
        SetNotifyFd(vfbKbdFd, vfbKbdNotify, X_NOTIFY_READ, NULL);
        ErrorF("Xfb: keyboard on %s\n", src);
    }
    else {
        ErrorF("Xfb: no keyboard (%s)\n", strerror(errno));
    }
}
