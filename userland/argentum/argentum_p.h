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
#include <X11/extensions/XShm.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <pixman.h>

#include <map>
#include <string>
#include <vector>

namespace argentum {

/* Channel-wise mix towards another colour. theme.cpp's own blend helpers
 * (lighten/darken/mix) are file-static, so a derived tone that two
 * controls have to agree on - the well a track or a trough is cut into -
 * needs one both can reach. It lives here rather than being copied into
 * each of them, which is what happened once already. */
inline std::uint32_t
mixTone(std::uint32_t c, std::uint32_t to, double f)
{
	int r0 = (int) ((c >> 16) & 0xff);
	int g0 = (int) ((c >> 8) & 0xff);
	int b0 = (int) (c & 0xff);
	int r1 = (int) ((to >> 16) & 0xff);
	int g1 = (int) ((to >> 8) & 0xff);
	int b1 = (int) (to & 0xff);
	int r = (int) (r0 + (r1 - r0) * f + 0.5);
	int g = (int) (g0 + (g1 - g0) * f + 0.5);
	int b = (int) (b0 + (b1 - b0) * f + 0.5);

	return ((std::uint32_t) r << 16) | ((std::uint32_t) g << 8) |
	       (std::uint32_t) b;
}

class Window;
class GraphicsContext;
class View;

/* Application session state. */
struct Application::Impl {
	Display *dpy = nullptr;		/* the X connection */
	int screen = 0;			/* DefaultScreen(dpy) */
	bool running = false;		/* init() succeeded */
	bool stopping = false;		/* terminate() requested */

	/* S2.3c: root px of the last dispatched mouse press (popup
	 * anchoring). Written by Window::dispatchMouseToContent. */
	int lastRootX = 0;
	int lastRootY = 0;

	/* S4.1a: optional raw-X event hook (Kestrel). null = off. */
	Application::EventHook eventHook = nullptr;
	/* S4.1c: optional idle beat (Kestrel housekeeping). null = off. */
	Application::IdleHook idleHook = nullptr;

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

	/* S2.2b: the session Theme (lazy; owned here, deleted in ~Impl).
	 * Loaded from the system.theme domain on first theme() access. */
	Theme *theme = nullptr;
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

	/* S4.1c: WM_DELETE_WINDOW close hook (null = quit the app). */
	std::function<void()> onClose;

	/* S2.2c minimal focus: the first responder receives key events
	 * (null = the content view). pressed tracks the view that got
	 * the ButtonPress so the release reaches it even after a drag
	 * out; motionTarget is the last view the pointer entered (for
	 * mouseExited on leave). */
	View *firstResponder = nullptr;
	View *pressed = nullptr;
	View *motionTarget = nullptr;

	/* S2.6 redraw: per-rect damage. A persistent backing store holds
	 * the last full composite; the tree is re-composited into it and
	 * ONLY the damaged rect is XPutImage'd (a full-window put of a
	 * 960x720 backing through Xfb is ~850ms; a small dirty rect is
	 * microseconds of server work). damageScheduled coalesces the
	 * XClearArea requests that generate the Expose pass. */
	BitmapImage *back = nullptr;	/* window-px backing store */
	bool damageScheduled = false;	/* a rect is pending an Expose */
	int dmgX0 = 0;
	int dmgY0 = 0;
	int dmgX1 = 0;
	int dmgY1 = 0;			/* px bounds, exclusive */
	bool painted = false;		/* the backing holds full, current
					 * content (set by draw()) */

	/* MIT-SHM (docs/design/mit-shm-plan.md M2): when the server
	 * answers XShm, the damaged rect is memcpy'd into a persistent
	 * SysV segment and XShmPutImage'd — the X11 wire carries the
	 * segment id, not the image bytes (a 960x720 flush stops being a
	 * multi-MB socket write). Geometry must equal the backing
	 * (width*4 stride) or flushBacking falls back to XPutImage. */
	bool shmUp = false;		/* the transport is attached */
	int shmW = 0, shmH = 0;		/* segment geometry (px) */
	XShmSegmentInfo shm;
	XImage *shmImg = nullptr;
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
	/* the window that owns this view's content root (set only on the
	 * root view by Window::setContentView) — damage reports through
	 * it so a redraw only flushes the dirty rect */
	class Window *hostWindow = nullptr;

	AccessibilityRole a11yRole = AccessibilityRole::Unknown;
	bool a11yEnabled = true;
	char a11yLabel[128] = { 0 };
	char a11yHelp[128] = { 0 };
	char a11yValue[128] = { 0 };
};

/* S2.2b Control state: action + enabled + the flags the chrome state
 * derives from (hover/armed/focused are event-driven from S2.2c). */
struct Control::Impl {
	std::function<void(Control *)> action;
	bool enabled = true;
	bool hovered = false;
	bool armed = false;
	bool focused = false;
};

/* S2.4a Box state (see argentum.h): title + layout mode + spacing. */
struct Box::Impl {
	char title[64] = { 0 };
	BoxLayout layout = BoxLayout::Free;
	double spacing = 6.0;		/* pt between arranged children */
	bool logged = false;		/* BOX-A arrange log printed once */
};
/* S2.4b ScrollBar state (see argentum.h). */
struct ScrollBar::Impl {
	ScrollBar::Orientation orient = ScrollBar::Orientation::Vertical;
	double range = 0;		/* content extent (pt) */
	double page = 0;		/* viewport extent (pt) */
	double value = 0;		/* scroll offset (pt) */
	double lineStep = 16;		/* one arrow step (pt) */
	double thickness = 16;		/* cross size (pt) */
	std::function<void(double)> action;
	/* interaction: which part the pointer is on (0 none, 1 arrow-min,
	 * 2 arrow-max, 3 track, 4 scroller), the hovered part, and the
	 * grab offset inside the scroller while dragging (pt) */
	int part = 0;
	int hot = 0;
	bool dragging = false;
	double grab = 0;
};

/* S2.4b ScrollView state (see argentum.h). */
struct ScrollView::Impl {
	View *doc = nullptr;		/* document view (in the viewport) */
	View *viewport = nullptr;	/* internal clip container (owned) */
	ScrollBar *vbar = nullptr;	/* right gutter (owned) */
	ScrollBar *hbar = nullptr;	/* bottom gutter (owned) */
	View *frame = nullptr;		/* 1px border ring, paints last (owned) */
	double ox = 0;			/* scroll offset (pt) */
	double oy = 0;
};
/* S2.4c SplitView state (see argentum.h). */
struct SplitView::Impl {
	bool vertical = true;		/* axis: true = side-by-side */
	double thickness = 6.0;		/* divider band width (pt) */
	double minPane = 40.0;		/* smallest pane length (pt) */
	std::vector<double> divs;	/* divider positions (pt), n-1 */
	double lastAxis = -1;		/* frame length at the last reset */
	int dragging = -1;		/* divider being dragged, -1 none */
	double downPos = 0;		/* pointer pos at press (pt) */
	double dragBase = 0;		/* divider pos at press (pt) */
};
/* S2.4d TabViewItem state: the borrowed title + page. */
struct TabViewItem::Impl {
	char title[128] = { 0 };
	View *page = nullptr;
};
/* S2.4e TableView state (see argentum.h). */
struct TableView::Impl {
	TableViewDataSource *dataSource = nullptr;
	TableViewDelegate *delegate = nullptr;
	std::vector<std::string> titles;	/* column titles */
	double headerH = 0;			/* 0 = auto (rowH + 2 pt) */
	int selected = -1;
};

/* S2.4d TabView state (see argentum.h). */
struct TabView::Impl {
	std::vector<TabViewItem *> items;	/* borrowed */
	int selected = 0;
	bool logged = false;			/* TAB-C rect log printed once */
	std::function<void(TabView *, int)> onSelect;
};

/* S2.2b Label state: text + colour/size overrides (0 = theme). The
 * a11y label mirrors the text (kept in View::Impl). */
struct Label::Impl {
	char text[256] = { 0 };
	std::uint32_t color = 0;	/* 0 = theme text colour */
	double sizePt = 0;		/* 0 = theme font size */
};

/* S2.3a Menu model state. Menu owns NO items (borrowed). */
struct MenuItem::Impl {
	char title[128] = { 0 };
	bool enabled = true;
	std::function<void()> action;
	Menu *submenu = nullptr;
};

struct Menu::Impl {
	char title[128] = { 0 };
	std::vector<MenuItem *> items;
};

/* S2.3a Slider state. */
struct Slider::Impl {
	double minValue = 0.0;
	double maxValue = 1.0;
	double value = 0.5;
};

/* S2.3a Stepper state. */
struct Stepper::Impl {
	double value = 0.0;
	double increment = 1.0;
	bool upZone = false;		/* armed zone from mouseDown */
};

/* S2.3b SegmentedControl state. */
struct SegmentedControl::Impl {
	static const int kMax = 8;
	char titles[kMax][64] = { { 0 } };
	int count = 0;
	int selected = 0;
	int armed = -1;			/* segment pressed, -1 none */
};

/* S2.3b ProgressIndicator state. */
struct ProgressIndicator::Impl {
	double progress = 0.0;
};

/* S2.3b LevelIndicator state. */
struct LevelIndicator::Impl {
	double level = 0.0;
	int cells = 8;
};

/* S2.3c ImageView state. */
struct ImageView::Impl {
	BitmapImage *image = nullptr;	/* borrowed */
	ImageContentMode mode = ImageContentMode::ScaleToFit;
};

/* S2.3c internal popup window (popup.cpp). */
class PopupWindow;

/* S2.3c PopUpButton state. */
struct PopUpButton::Impl {
	char title[128] = { 0 };
	Menu *menu = nullptr;		/* borrowed */
	PopupWindow *popup = nullptr;	/* the transient popup, ours */
};

/* S2.3c internal popup bookkeeping (popup.cpp): if a popup is open and
 * `windowXid` is not it, dismiss it. Called from Application::run()
 * before a ButtonPress dispatches. */
void _popupDismissOther(unsigned long windowXid);

/* S2.2c Button state: type + title + toggle state. */
struct Button::Impl {
	Button::Type type = Button::Type::Push;
	char title[256] = { 0 };
	bool on = false;
};

/* S2.2d TextField state: the single-line value, the caret index, the
 * selection anchor (start of the selection; == caret when none). */
struct TextField::Impl {
	char text[256] = { 0 };
	unsigned int caret = 0;		/* byte index, utf8-safe */
	unsigned int anchor = 0;	/* selection anchor (byte) */
	bool selectAll = false;		/* first responder select-all */
	bool secure = false;		/* S3.2 bullets instead of glyphs */
	bool editing = false;		/* S3.2 open edit session */
	std::function<void(TextField *)> onEndEdit; /* commit callback */
};

/* TXT-a TextView internals. The layout memoizes the visual lines
 * (byte ranges over the document) and is recomputed when the text or
 * the wrap width changes. */
struct TextViewLine {
	unsigned int start;		/* bytes over the document */
	unsigned int end;
};

struct TextView::Impl {
	std::string text;
	bool dirty = true;		/* layout stale */
	double wrapPt = 0;		/* wrap width of the last layout */
	std::vector<TextViewLine> lines;	/* top to bottom */
	/* TXT-b edit state: byte caret/anchor over the document; the
	 * goal column (px, -1 = unset) anchors vertical moves */
	unsigned int caret = 0;
	unsigned int anchor = 0;
	double goalXPx = -1.0;
};

/* S2.2a text core: shared run internals (implemented in text.cpp).
 * The S0.4 Window::drawText and the S2.2a GraphicsContext::drawText
 * share match -> shape -> metrics; each sink then rasterizes the
 * glyphs itself (RGB blend over a background box vs A8 coverage into
 * an offscreen surface). A TextRun is single-shot: prepare, inspect,
 * compose at most once, finish. */
struct TextRun;

TextRun *textRunPrepare(const char *family, const char *utf8,
			unsigned int pixelSize, bool quiet = false);
void textRunFinish(TextRun *t);
unsigned int textRunGlyphCount(const TextRun *t);
/* run box geometry (px), matching the legacy drawText box */
int textRunBoxW(const TextRun *t);
int textRunBoxH(const TextRun *t);
/* ascent (px above the baseline) used to position the baseline in the
 * box: baseline sits textRunAscent()+PADY below the top */
int textRunAscent(const TextRun *t);
/* total 26.6 advance of the shaped run (px at the prepared size) */
long textRunAdvance26(const TextRun *t);
/* rasterize every glyph fg-over-bg into an opaque RGB32 box
 * (boxW*boxH words, 0x00RRGGBB). Returns the glyph count rasterized
 * and prints the legacy ARGENTUM-TEXT: rasterized/blitted lines. */
unsigned int textRunComposeRgb(TextRun *t, std::uint32_t *box,
			       std::uint32_t fg, std::uint32_t bg);
/* rasterize every glyph's AA coverage into an A8 mask (boxW*boxH
 * bytes, 0 = empty). Returns the glyph count rasterized. The caller
 * owns the buffer. */
unsigned int textRunComposeMask(TextRun *t, unsigned char *cov);

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_P_H */
