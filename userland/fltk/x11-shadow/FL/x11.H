// FNX shadow of FLTK's FL/x11.H (docs/fltk-port-plan.md M1).
//
// FLTK's platform-neutral core includes FL/x11.H via FL/platform.H (X11 is
// the assumed Unix default). The real header drags in <X11/Xlib.h> etc.,
// which FNX does not have. This shadow satisfies the same include surface
// with minimal opaque X11-ish types plus the exact fl_* externs the core
// declares — the FNX platform driver (userland/fltk/fnx/) will provide the
// real implementations at link time.
//
// Shadowed by putting this directory FIRST on the include path
// (-I userland/fltk/x11-shadow), so <FL/x11.H> resolves here, not to the
// submodule's. The submodule stays pristine.
//
// This file is FNX code (MIT); it is NOT part of FLTK.

#ifdef FL_DOXYGEN
#else // ! FL_DOXYGEN

#ifndef FL_PLATFORM_H
#  error "Never use <FL/x11.H> directly; include <FL/platform.H> instead."
#endif // !FL_PLATFORM_H

#include <FL/Enumerations.H>

/* ---- minimal opaque X11-ish types (FNX compat) -------------------- */

typedef unsigned long XID;
typedef XID Window;
typedef unsigned long Colormap;
typedef unsigned long Pixmap;
typedef unsigned long Atom;
typedef unsigned long Time;
typedef unsigned long VisualID;

struct _XDisplay;		/* opaque */
typedef struct _XDisplay Display;
struct _XGC;			/* opaque */
typedef struct _XGC *GC;
struct _XFontStruct;		/* opaque */
typedef struct _XFontStruct XFontStruct;
struct _XEvent;			/* opaque */
typedef struct _XEvent XEvent;

typedef struct _XVisualInfo {
	VisualID visualid;
	unsigned long visual;	/* opaque Visual* placeholder */
	int screen;
	int depth;
	int class_;
} XVisualInfo;

#ifndef ulong
#define ulong unsigned long
#endif

/* ---- the exact fl_* surface FLTK core declares (from real x11.H) -- */

extern FL_EXPORT Display *fl_display;
extern FL_EXPORT Display *fl_x11_display();
extern FL_EXPORT void fl_x11_use_display(Display *);
extern FL_EXPORT Window fl_x11_xid(const Fl_Window *win);
extern FL_EXPORT Fl_Window *fl_x11_find(Window);
extern FL_EXPORT int fl_screen;
extern FL_EXPORT XVisualInfo *fl_visual;
extern FL_EXPORT Colormap fl_colormap;

extern FL_EXPORT GC fl_gc;
extern FL_EXPORT GC fl_x11_gc();
FL_EXPORT ulong fl_xpixel(Fl_Color i);
FL_EXPORT ulong fl_xpixel(uchar r, uchar g, uchar b);

FL_EXPORT int fl_handle(const XEvent &);

extern FL_EXPORT const XEvent *fl_xevent;
extern FL_EXPORT ulong fl_event_time;

#if defined(FL_LIBRARY) || defined(FL_INTERNALS)
extern FL_EXPORT Window fl_message_window;

class FL_EXPORT Fl_XFont_On_Demand {
public:
	Fl_XFont_On_Demand(XFontStruct *p = NULL) : ptr(p) {}
	Fl_XFont_On_Demand &operator=(const Fl_XFont_On_Demand &x)
	{
		ptr = x.ptr;
		return *this;
	}
	Fl_XFont_On_Demand &operator=(XFontStruct *p)
	{
		ptr = p;
		return *this;
	}
	XFontStruct *value();
	operator XFontStruct *() { return value(); }
	XFontStruct &operator*() { return *value(); }
	XFontStruct *operator->() { return value(); }
	bool operator==(const Fl_XFont_On_Demand &x) { return ptr == x.ptr; }
	bool operator!=(const Fl_XFont_On_Demand &x) { return ptr != x.ptr; }
private:
	XFontStruct *ptr;
};
extern FL_EXPORT Fl_XFont_On_Demand fl_xfont;

extern FL_EXPORT char fl_override_redirect;

#endif // FL_LIBRARY || FL_INTERNALS

#endif // FL_DOXYGEN
