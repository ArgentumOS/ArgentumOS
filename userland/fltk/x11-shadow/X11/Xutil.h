// FNX minimal X11/Xutil.h compat — see X11/Xlib.h (docs/fltk-port-plan.md M1).
#ifndef _FNX_X11_XUTIL_H
#define _FNX_X11_XUTIL_H

#include <X11/Xlib.h>

/* the only Xutil content FLTK's core references at declaration level:
 * XVisualInfo (already in Xlib.h above) and a couple of constants. */

/* window-manager hints kinds the core names */
#define XSizeHints		1
#define XWMHints		2
#define XA_WM_NAME		39
#define XA_WM_HINTS		35
#define XA_WM_CLASS		67

#endif /* _FNX_X11_XUTIL_H */
