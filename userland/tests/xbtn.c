/* S0.6 mouse-gate client: maps a window, polls the pointer position,
 * and prints every button event. Minimal core-protocol (Xlib) — isolates
 * whether Xfb delivers ButtonPress/Release when PS/2 packets arrive.
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    Display *dpy;
    Window win;
    XEvent ev;
    int tries;

    for (tries = 0; tries < 120; tries++) {
        dpy = XOpenDisplay(NULL);
        if (dpy) break;
        usleep(1000000);
    }
    if (!dpy) { printf("XBTN: no display\n"); return 1; }

    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
                              120, 90, 360, 240, 0, 0, 0x2288ee);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask);
    XMapWindow(dpy, win);
    XSync(dpy, False);
    printf("XBTN: window mapped\n");
    fflush(stdout);

    for (;;) {
        Window root_ret, child_ret;
        int rx, ry, wx, wy;
        unsigned int mask;
        static int n = 0;

        /* poll the pointer so we can see whether Xfb's cursor moved */
        if (XQueryPointer(dpy, DefaultRootWindow(dpy), &root_ret, &child_ret,
                          &rx, &ry, &wx, &wy, &mask)) {
            if ((n++ % 10) == 0) {
                printf("XBTN: ptr root=%d,%d child=%d,%d mask=%u\n",
                       rx, ry, wx, wy, mask);
                fflush(stdout);
            }
        }
        XSync(dpy, False);
        if (!XPending(dpy)) {
            usleep(100000);
            continue;
        }
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case Expose:
            printf("XBTN: expose\n"); fflush(stdout); break;
        case ButtonPress:
            printf("XBTN: button %d press at %d,%d\n",
                   ev.xbutton.button, ev.xbutton.x, ev.xbutton.y);
            fflush(stdout); break;
        case ButtonRelease:
            printf("XBTN: button %d release at %d,%d\n",
                   ev.xbutton.button, ev.xbutton.x, ev.xbutton.y);
            fflush(stdout); break;
        default:
            printf("XBTN: event %d\n", ev.type); fflush(stdout); break;
        }
    }
    return 0;
}
