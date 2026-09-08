/* argentum/argentum_p.h — private Argentum internals (compiled into
 * libargentum.so.1; NOT installed / NOT part of the public API).
 *
 * The Impl structs declared in the public header are defined here so
 * the .cpp files can touch the X11 state without leaking Xlib types
 * into argentum.h. Apps never include this file.
 */
#ifndef FNX_ARGENTUM_ARGENTUM_P_H
#define FNX_ARGENTUM_ARGENTUM_P_H

#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <pixman.h>

#include <map>
#include <vector>

namespace argentum {

class Window;
class GraphicsContext;
class View;

/* Application session state. */
struct Application::Impl {
	Display *dpy = nullptr;		/* the X connection */
	int screen = 0;			/* DefaultScreen(dpy) */
	bool running = false;		/* init() succeeded */
	bool stopping = false;		/* terminate() requested */

	/* S0.4 text stack. fontconfig is process-global (FcInit once);
	 * FreeType needs one library handle shared by every face. */
	bool ftInited = false;
	FT_Library ft = nullptr;	/* FT_Init_FreeType result */

	/* S0.5 session values, resolved from the system.argentum domain at
	 * init() (libconfig; system -> user -> shared precedence). */
	bool confLoaded = false;	/* domain read attempted */
	std::uint32_t winBg = 0x2288ee;	/* window.background default */
	char fontFamily[96];		/* font.family */
	unsigned int fontPx = 26;	/* font.size */

	/* S1.1 points->pixels session factor: PPI/72 resolved once at
	 * init() from the display physical size (system.display domain
	 * display.width_mm/height_mm when set; else the X server's
	 * DisplayWidthMM/HeightMM when the display domain is absent;
	 * else the 96 dpi fallback = 4/3 px/pt). All Argentum screen
	 * units are points; multiply by pxPerPt to blit. */
	double pxPerPt = 4.0 / 3.0;	/* px/pt (96 dpi fallback) */

	/* X window id -> the argentum::Window that owns it (S0.3 event
	 * dispatch). Window registers on init, unregisters on destroy. */
	std::map<unsigned long, Window *> windows;
};

/* Window X11 state. */
struct Window::Impl {
	Display *dpy = nullptr;		/* borrowed from the session */
	::Window xwin = 0;		/* the X window id */
	int x = 0;			/* root position */
	int y = 0;
	unsigned int width = 0;
	unsigned int height = 0;
	bool mapped = false;

	/* S2.1a: the content view rooting the view tree drawn in this
	 * window (non-owning; may be null). */
	View *contentView = nullptr;
};

/* S1.2 BitmapImage state: the pixman offscreen surface. x8r8g8b8 is the
 * 32-bpp XRGB layout fill()/drawText blit, so flush() can XPutImage the
 * pixman data pointer straight to the window. */
struct BitmapImage::Impl {
	pixman_image_t *img = nullptr;	/* PIXMAN_x8r8g8b8 surface */
	unsigned int width = 0;
	unsigned int height = 0;
};

/* S1.2 GraphicsContext state: which bitmap it draws into, plus the
 * S2.1a state stack (transform origin + clip). One frame per pushed
 * view during the tree composite. */
struct GraphicsContext::Impl {
	BitmapImage *bitmap = nullptr;	/* the target surface */

	/* current frame: draw coordinates are offset by (ox,oy) and
	 * clipped to clipW/H (when clipping) before hitting the surface */
	int ox = 0;			/* current translate x (px) */
	int oy = 0;			/* current translate y (px) */
	bool clipOn = false;		/* clipToRect in effect */
	int clipX = 0;			/* clip rect (in translated space) */
	int clipY = 0;
	int clipW = 0;
	int clipH = 0;
	struct SavedFrame {
		int ox, oy;
		bool clipOn;
		int clipX, clipY, clipW, clipH;
	};
	std::vector<SavedFrame> stack;	/* save() push / restore() pop */
};

/* S1.3 Theme state: parsed params of the active theme file. */
struct Theme::Impl {
	bool loaded = false;		/* load() parsed a theme file */
	char name[64];			/* e.g. "Argentum" */
	std::uint32_t accent;		/* design accent (0xRRGGBB) */
	std::uint32_t chromeTop;	/* chrome surface top stop */
	std::uint32_t chromeBottom;	/* chrome surface bottom stop */
	std::uint32_t page;		/* document surface */
	std::uint32_t text;		/* text colour */
	double smallRadius;		/* pt */
	double baseRadius;
	double bevel;
	double outline;
	char fontFamily[96];
	double fontPt;
	std::uint32_t chromeOutline;	/* derived dark-accent edge */
	struct StateParams {
		std::uint32_t fillTop, fillBottom, outline, label;
	} states[5];			/* Index: (int)ControlState */
};

/* S2.1 View state: frame (pt, superview space), tree links, flags,
 * a11y metadata. Non-owning tree (addSubview does not take ownership). */
struct View::Impl {
	View *superview = nullptr;
	std::vector<View *> subviews;	/* draw order; last = topmost */
	Rect frame;			/* pt, superview space */
	bool hidden = false;
	bool needsDisplay = false;
	unsigned int autoresizeMask = View::AutoresizingNone;

	AccessibilityRole a11yRole = AccessibilityRole::Unknown;
	bool a11yEnabled = true;
	char a11yLabel[128] = { 0 };
	char a11yHelp[128] = { 0 };
	char a11yValue[128] = { 0 };
};

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_P_H */
