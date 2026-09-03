// FNX minimal X11/Xlib.h compat (docs/fltk-port-plan.md M1).
//
// FLTK's FL/x11.H includes <X11/Xlib.h> / <X11/Xutil.h> / <X11/Xatom.h>.
// FNX has no Xlib; these headers define just enough opaque/leaf types for
// FLTK's platform-neutral core to compile (the X11 *driver* files are not
// part of the FNX build). Put this directory first on the include path so
// <X11/...> resolves here. The FLTK submodule stays pristine.
//
// FNX code (MIT); NOT part of FLTK.

#ifndef _FNX_X11_XLIB_H
#define _FNX_X11_XLIB_H

#include <stddef.h>

/* core ids / scalars (Xlib parity for the members FLTK uses) */
typedef unsigned long XID;
typedef XID Window;
typedef XID Colormap;
typedef XID Pixmap;
typedef XID Atom;
typedef XID Drawable;
typedef unsigned long Time;
typedef unsigned long VisualID;
typedef unsigned long KeySym;
typedef unsigned long XEvent_mask;	/* not Xlib; see masks below */

typedef unsigned long ulong;		/* Xlib's `ulong` */
typedef int Bool;

/* opaque objects */
struct _XDisplay;
typedef struct _XDisplay Display;
struct _XGC;
typedef struct _XGC *GC;
struct _XFontStruct;
typedef struct _XFontStruct XFontStruct;
struct _XVisual;
typedef struct _XVisual Visual;
struct _XEvent;
typedef struct _XEvent XEvent;

/* leaf structs the core touches */
typedef struct {
	int x, y;
} XPoint;

typedef struct {
	unsigned long pixel;
	unsigned short red, green, blue;
	char flags;
	char pad;
} XColor;

typedef struct {
	VisualID visualid;
	Visual *visual;
	int screen;
	int depth;
	int class_;			/* Xlib calls it `class` (C++-safe here) */
	unsigned long red_mask, green_mask, blue_mask;
	int colormap_size;
	int bits_per_rgb;
} XVisualInfo;

/* event-mask bits the core refers to by name */
#define KeyPressMask		(1L << 0)
#define KeyReleaseMask		(1L << 1)
#define ButtonPressMask		(1L << 2)
#define ButtonReleaseMask	(1L << 3)
#define EnterWindowMask		(1L << 4)
#define LeaveWindowMask		(1L << 5)
#define PointerMotionMask	(1L << 6)
#define ExposureMask		(1L << 15)
#define StructureNotifyMask	(1L << 17)
#define SubstructureNotifyMask	(1L << 19)

/* expose/notify event kinds the core names */
#define Expose		12
#define ConfigureNotify	22
#define MapNotify	19
#define UnmapNotify	18
#define DestroyNotify	17
#define KeyPress		2
#define KeyRelease		3
#define ButtonPress		4
#define ButtonRelease		5
#define MotionNotify		6
#define EnterNotify		7
#define LeaveNotify		8
#define FocusIn		9
#define FocusOut		10
#define ClientMessage		33

#define True	1
#define False	0

#define None	0L

#endif /* _FNX_X11_XLIB_H */
