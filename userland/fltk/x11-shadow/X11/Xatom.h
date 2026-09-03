// FNX minimal X11/Xatom.h compat — see X11/Xlib.h (docs/fltk-port-plan.md M1).
#ifndef _FNX_X11_XATOM_H
#define _FNX_X11_XATOM_H

#include <X11/Xlib.h>

/* standard atom constants FLTK's X11 code may reference by name */
#define XA_PRIMARY	1
#define XA_SECONDARY	2
#define XA_ARC		3
#define XA_ATOM		4
#define XA_BITMAP	5
#define XA_CARDINAL	6
#define XA_COLORMAP	7
#define XA_CURSOR	8
#define XA_CUT_BUFFER0	9
#define XA_DRAWABLE	10
#define XA_FONT		11
#define XA_INTEGER	12
#define XA_PIXMAP	13
#define XA_POINT	14
#define XA_RECTANGLE	15
#define XA_RESOURCE_MANAGER 16
#define XA_RGB_COLOR_MAP 17
#define XA_RGB_BEST_MAP 18
#define XA_RGB_BLUE_MAP 19
#define XA_RGB_DEFAULT_MAP 20
#define XA_RGB_GRAY_MAP 21
#define XA_RGB_GREEN_MAP 22
#define XA_STRING	31
#define XA_VISUALID	33
#define XA_WINDOW	33
#define XA_WM_COMMAND	34
#define XA_WM_HINTS	35
#define XA_WM_CLIENT_MACHINE 36
#define XA_WM_ICON_NAME 37
#define XA_WM_NAME	39
#define XA_WM_NORMAL_HINTS 40
#define XA_WM_TRANSIENT_FOR 68

#endif /* _FNX_X11_XATOM_H */
