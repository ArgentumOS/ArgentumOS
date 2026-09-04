/* FNX real-input backend for the Xfb DDX.
 *
 * Xvfb creates core pointer/keyboard devices but (XTEST aside) feeds them
 * nothing. This file plugs FNX's input devices into the X event pipeline
 * the kdrive way: the os layer polls /dev/psaux (3-byte PS/2 packets,
 * driven by the kernel's usb-mouse/ps2 synthesis) and /dev/kbd (4-byte
 * {key, mods, state, pad} records) through SetNotifyFd, and the notify
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

/* PS/2 packet assembly state */
static int vfbMousePkt[3];
static int vfbMousePktN = 0;

/* current X button bitmask: bit0 left, bit1 middle, bit2 right */
static int vfbMouseButtons = 0;


/* ---- direct-fb pointer cursor ----------------------------------------
 * The X sprite machinery is cursorless in this server, so the pointer
 * cursor is an overlay drawn straight into the /dev/fb0 mapping (same
 * memory the X server renders into). fnxinput tracks the absolute pointer
 * from the same PS/2 stream it feeds mieq, so the overlay follows X's
 * pointer. No-op when there is no real framebuffer. */
#define CUR_SZ 13
static uint32_t vfbCurSaved[CUR_SZ * CUR_SZ];
static int vfbCurX = -1, vfbCurY = -1;

int vfbFbdevGet(int *w, int *h);
uint32_t *vfbFbdevBase(void);

static void
vfbCurRestore(void)
{
    uint32_t *base;
    int w, h, i, j;

    if (vfbCurX < 0 || !vfbFbdevGet(&w, &h))
        return;
    base = vfbFbdevBase();
    if (!base)
        return;
    for (j = 0; j < CUR_SZ; j++)
        for (i = 0; i < CUR_SZ; i++)
            base[(vfbCurY + j) * w + (vfbCurX + i)] =
                vfbCurSaved[j * CUR_SZ + i];
    vfbCurX = -1;
}

static void
vfbCurDraw(int px, int py)
{
    uint32_t *base;
    int w, h, i, j, x0, y0;

    if (!vfbFbdevGet(&w, &h))
        return;
    base = vfbFbdevBase();
    if (!base)
        return;
    vfbCurRestore();
    x0 = px - CUR_SZ / 2;
    y0 = py - CUR_SZ / 2;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x0 + CUR_SZ > w)
        x0 = w - CUR_SZ;
    if (y0 + CUR_SZ > h)
        y0 = h - CUR_SZ;
    if (x0 < 0 || y0 < 0)
        return;
    for (j = 0; j < CUR_SZ; j++)
        for (i = 0; i < CUR_SZ; i++)
            vfbCurSaved[j * CUR_SZ + i] =
                base[(y0 + j) * w + (x0 + i)];
    vfbCurX = x0;
    vfbCurY = y0;
    for (j = 0; j < CUR_SZ; j++) {
        for (i = 0; i < CUR_SZ; i++) {
            uint32_t c;
            if (i == 0 || j == 0 || i == CUR_SZ - 1 || j == CUR_SZ - 1)
                c = 0x000000;   /* 1px black outline */
            else if (i == j || i == CUR_SZ - 1 - j)
                c = 0xffffff;   /* white X */
            else
                continue;
            base[(y0 + j) * w + (x0 + i)] = c;
        }
    }
}

/* ---- mouse ----------------------------------------------------------- */

static void
vfbFeedMouseByte(unsigned char b)
{
    int i;

    if (vfbMousePktN == 0) {
        /* first byte of a PS/2 packet has bit 3 set; drop stray bytes */
        if (!(b & 0x08))
            return;
        vfbMousePkt[0] = b;
        vfbMousePktN = 1;
        return;
    }
    if (vfbMousePktN == 1) {
        vfbMousePkt[1] = b;
        vfbMousePktN = 2;
        return;
    }
    vfbMousePkt[2] = b;
    vfbMousePktN = 0;

    /* decode: PS/2 3-byte packet (same as the GUI compositor) */
    {
        int b0 = vfbMousePkt[0];
        int dx = (int) (signed char) vfbMousePkt[1];
        int dy = (int) (signed char) vfbMousePkt[2];
        int buttons = b0 & 0x07;
        int changed = buttons ^ vfbMouseButtons;
        ValuatorMask mask;
        int btn;

        /* PS/2 dy is positive down (toward the user): feed straight
         * through as relative motion */
        vfbPtrX += dx;
        vfbPtrY += dy;

        if (dx || dy)
            vfbCurDraw(vfbPtrX, vfbPtrY);

        valuator_mask_zero(&mask);
        valuator_mask_set(&mask, 0, dx);
        valuator_mask_set(&mask, 1, dy);
        QueuePointerEvents(vfbMouseDev, MotionNotify, 0,
                           POINTER_RELATIVE, &mask);

        for (btn = 0; btn < 3; btn++) {
            int bit = 1 << btn;
            if (changed & bit) {
                if (buttons & bit)
                    QueuePointerEvents(vfbMouseDev, ButtonPress,
                                       btn + 1, 0, NULL);
                else
                    QueuePointerEvents(vfbMouseDev, ButtonRelease,
                                       btn + 1, 0, NULL);
            }
        }
        vfbMouseButtons = buttons;
    }
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
 * /dev/kbd delivers RESOLVED keysyms (the kernel keymap already applied
 * shift etc), but X wants physical keycodes + modifier state. The full
 * US-layout inverse translation + modifier synthesis is the next input
 * increment; until then the keyboard device stays closed. */

static void
vfbDrainKbd(int fd)
{
    (void) fd;
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

    {
        int fbw = 0, fbh = 0;

        if (vfbFbdevGet(&fbw, &fbh)) {
            vfbPtrX = fbw / 2;
            vfbPtrY = fbh / 2;
            vfbCurDraw(vfbPtrX, vfbPtrY);
        }
    }

    src = getenv("XFB_MOUSE");
    if (!src || !*src)
        src = "/dev/psaux";
    vfbMouseFd = open(src, O_RDONLY | O_NONBLOCK);
    if (vfbMouseFd >= 0) {
        vfbSetRaw(vfbMouseFd);
        SetNotifyFd(vfbMouseFd, vfbMouseNotify, X_NOTIFY_READ, NULL);
        ErrorF("Xfb: mouse on %s\n", src);
    }
    else {
        ErrorF("Xfb: no mouse (%s) - XTEST only\n", strerror(errno));
    }

    (void) vfbKbdFd;
    (void) vfbKbdNotify;
}
