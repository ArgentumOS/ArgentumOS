/* xkey: print every keyboard event (keycode, keysym, char) to stdout.
 * Sets input focus to its own window so it receives the core keyboard
 * events. Pure event loop - no raw reads. */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

static void
pr(int kind, XKeyEvent *e)
{
    KeySym ks0 = XLookupKeysym(e, 0);
    KeySym ks1 = XLookupKeysym(e, 1);
    KeySym ks = XLookupKeysym(e, e->state & ShiftMask ? 1 : 0);
    char buf[16] = { 0 };
    int n = 0;

    if (ks >= 0x20 && ks < 0x7f) {
        buf[0] = (char) ks;
        n = 1;
    }
    else if (ks == XK_Return) {
        strcpy(buf, "RET");
        n = 3;
    }
    else if (ks == XK_Tab) {
        strcpy(buf, "TAB");
        n = 3;
    }
    else if (ks == XK_Escape) {
        strcpy(buf, "ESC");
        n = 3;
    }
    else if (ks == XK_BackSpace) {
        strcpy(buf, "BS");
        n = 2;
    }
    else if (ks == XK_Up || ks == XK_Down || ks == XK_Left || ks == XK_Right) {
        strcpy(buf, "ARR");
        n = 3;
    }
    printf("XKEY: %s kc=%u sym=0x%lx txt=%.*s\n",
           kind ? "press " : "release", e->keycode, (unsigned long) ks, n,
           buf);
    fflush(stdout);
}

int
main(void)
{
    Display *d = 0;
    Window w;
    XEvent ev;
    int tries;

    /* Xfb takes a while to come up under TCG; retry until it accepts */
    for (tries = 0; tries < 120 && !(d = XOpenDisplay(NULL)); tries++)
        sleep(1);
    if (!d) {
        printf("XKEY: open failed\n");
        return 1;
    }
    w = XCreateSimpleWindow(d, DefaultRootWindow(d), 10, 10, 300, 100, 0,
                            BlackPixel(d, 0), WhitePixel(d, 0));
    XMapWindow(d, w);
    XSelectInput(d, w, KeyPressMask | KeyReleaseMask | FocusChangeMask |
                        ButtonPressMask | EnterWindowMask | LeaveWindowMask);
    XSetInputFocus(d, w, RevertToParent, CurrentTime);
    XSync(d, False);
    printf("XKEY: ready win=0x%lx fd=%d\n", (unsigned long) w,
           ConnectionNumber(d));
    fflush(stdout);
    while (1) {
        XNextEvent(d, &ev);
        if (ev.type == KeyPress)
            pr(1, &ev.xkey);
        else if (ev.type == KeyRelease)
            pr(0, &ev.xkey);
        else if (ev.type == FocusIn || ev.type == FocusOut)
            printf("XKEY: focus %s\n", ev.type == FocusIn ? "in" : "out");
        else if (ev.type == ButtonPress)
            printf("XKEY: button %d at %d,%d\n", ev.xbutton.button,
                   ev.xbutton.x_root, ev.xbutton.y_root);
        else if (ev.type == EnterNotify || ev.type == LeaveNotify)
            printf("XKEY: %s at %d,%d\n",
                   ev.type == EnterNotify ? "enter" : "leave",
                   ev.xcrossing.x_root, ev.xcrossing.y_root);
        fflush(stdout);
    }
}
