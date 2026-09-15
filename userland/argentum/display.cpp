/*
 * The display path (U2a): the X connection, a window's surface, the
 * drawing context, and the window's own chrome
 * (docs/design/cocoa-parity-plan.md).
 *
 * Three layers, and nothing above this file knows X exists:
 *
 *   Connection   one X display for the process, opened on first use.
 *   Surface      per window: an x8r8g8b8 image that goes straight to X,
 *                with a pixman image over the same bytes for drawing.
 *   Window       the chrome + the content view + the draw pass + damage.
 *
 * THE WINDOW DRAWS ITS OWN CHROME. No window manager decorates for us:
 * a titled window paints its titlebar, title and close box as part of its
 * own surface, and the content view is laid out inside contentRect().
 */
#include <argentum/argentum.h>

#include <pixman.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace argentum {

/* ---- the process's connection ---------------------------------------- */

static Display *gDpy = nullptr;
static double gPxPerPt = 1.0;

bool
displayOpen(const char *name)
{
	if (gDpy) {
		return true;
	}
	const char *dn = name;

	if (!dn || !dn[0]) {
		dn = std::getenv("DISPLAY");
		if (!dn || !dn[0]) {
			/* the session's Xfb is :0 (see init.c) */
			dn = ":0";
		}
	}
	gDpy = XOpenDisplay(dn);
	if (!gDpy) {
		return false;
	}
	/* the text engine shapes in PIXELS: tell it the session's factor */
	textEngineInit();
	textEngineSetPxPerPt(gPxPerPt);
	return true;
}

bool
displayIsOpen()
{
	return gDpy != nullptr;
}

void
displayClose()
{
	if (gDpy) {
		XCloseDisplay(gDpy);
		gDpy = nullptr;
	}
}

double
displayPxPerPt()
{
	return gPxPerPt;
}

void
displaySetPxPerPt(double pxPerPt)
{
	if (pxPerPt > 0) {
		gPxPerPt = pxPerPt;
		textEngineSetPxPerPt(pxPerPt);
	}
}

Color
Color::hex(unsigned int rgb, double a)
{
	return Color::rgb(((rgb >> 16) & 0xFF) / 255.0,
			  ((rgb >> 8) & 0xFF) / 255.0,
			  (rgb & 0xFF) / 255.0, a);
}

/* ---- the drawing context --------------------------------------------- */

struct Context::Impl {
	pixman_image_t *img = nullptr;
	unsigned int wPx = 0, hPx = 0;
	double pxPerPt = 1.0;
	int ox = 0, oy = 0;			/* the view's surface origin */
	int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;	/* the clip, in px */

	struct Saved { int ox, oy, cx0, cy0, cx1, cy1; };
	std::vector<Saved> stack;

	void save()
	{
		stack.push_back(Saved{ ox, oy, cx0, cy0, cx1, cy1 });
	}
	void restore()
	{
		if (!stack.empty()) {
			const Saved &s = stack.back();

			ox = s.ox; oy = s.oy;
			cx0 = s.cx0; cy0 = s.cy0;
			cx1 = s.cx1; cy1 = s.cy1;
			stack.pop_back();
		}
	}
};

static Context *gCurrent = nullptr;
static void markTreeForDisplay(View *v);

void
Context::pushFrame(int x0, int y0, int x1, int y1)
{
	if (!impl_) {
		return;
	}
	impl_->save();
	if (x0 > impl_->cx0) {
		impl_->cx0 = x0;
	}
	if (y0 > impl_->cy0) {
		impl_->cy0 = y0;
	}
	if (x1 < impl_->cx1) {
		impl_->cx1 = x1;
	}
	if (y1 < impl_->cy1) {
		impl_->cy1 = y1;
	}
	impl_->ox = x0;
	impl_->oy = y0;
}

void
Context::popFrame()
{
	if (impl_) {
		impl_->restore();
	}
}

Context *
Context::current()
{
	return gCurrent;
}

unsigned int
Context::widthPx() const
{
	return impl_ ? impl_->wPx : 0;
}

unsigned int
Context::heightPx() const
{
	return impl_ ? impl_->hPx : 0;
}

/* pixel bounds of a rect in the current view's points, clipped to the
 * surface and to the clip rect; false when nothing is left to draw */
static bool
map_rect(const Context::Impl &im, const Rect &r, int *x0, int *y0, int *x1,
	 int *y1)
{
	double pp = im.pxPerPt;
	int ax0 = im.ox + (int) std::floor(r.origin.x * pp);
	int ay0 = im.oy + (int) std::floor(r.origin.y * pp);
	int ax1 = im.ox + (int) std::ceil((r.origin.x + r.size.w) * pp);
	int ay1 = im.oy + (int) std::ceil((r.origin.y + r.size.h) * pp);

	if (ax0 < im.cx0) ax0 = im.cx0;
	if (ay0 < im.cy0) ay0 = im.cy0;
	if (ax1 > im.cx1) ax1 = im.cx1;
	if (ay1 > im.cy1) ay1 = im.cy1;
	if (ax0 < 0) ax0 = 0;
	if (ay0 < 0) ay0 = 0;
	if (ax1 > (int) im.wPx) ax1 = (int) im.wPx;
	if (ay1 > (int) im.hPx) ay1 = (int) im.hPx;
	if (ax1 <= ax0 || ay1 <= ay0) {
		return false;
	}
	*x0 = ax0; *y0 = ay0; *x1 = ax1; *y1 = ay1;
	return true;
}

void
Context::fillRect(const Rect &rect, const Color &color)
{
	if (!impl_ || !impl_->img || color.a <= 0) {
		return;
	}
	int x0, y0, x1, y1;

	if (!map_rect(*impl_, rect, &x0, &y0, &x1, &y1)) {
		return;
	}
	/* pixman_color_t is PREMULTIPLIED */
	pixman_color_t c;

	c.red = (unsigned short) (color.r * color.a * 65535.0 + 0.5);
	c.green = (unsigned short) (color.g * color.a * 65535.0 + 0.5);
	c.blue = (unsigned short) (color.b * color.a * 65535.0 + 0.5);
	c.alpha = (unsigned short) (color.a * 65535.0 + 0.5);

	pixman_image_t *src = pixman_image_create_solid_fill(&c);

	if (!src) {
		return;
	}
	pixman_image_composite32(PIXMAN_OP_OVER, src, nullptr, impl_->img,
				 0, 0, 0, 0, x0, y0, (unsigned int) (x1 - x0),
				 (unsigned int) (y1 - y0));
	pixman_image_unref(src);
}

void
Context::drawText(const char *family, double sizePt, const Point &at,
		  const char *utf8, const Color &color, bool bold)
{
	if (!impl_ || !impl_->img || !utf8 || !utf8[0] || sizePt <= 0) {
		return;
	}
	unsigned int pixelSize = (unsigned int)
		(sizePt * impl_->pxPerPt + 0.5);

	if (pixelSize == 0) {
		pixelSize = 1;
	}
	TextRun *t = textRunPrepare(family && family[0] ? family : nullptr,
				    utf8, pixelSize, false, bold);
	if (!t) {
		return;
	}
	int boxW = textRunBoxW(t);
	int boxH = textRunBoxH(t);
	unsigned int capW = (unsigned int) boxW;
	unsigned int capH = (unsigned int) boxH;

	if (boxW <= 0 || boxH <= 0) {
		textRunFinish(t);
		return;
	}
	std::vector<unsigned char> cov((size_t) boxW * (size_t) boxH, 0);
	if (textRunComposeMask(t, cov.data()) == 0) {
		textRunFinish(t);
		return;
	}
	/* the run box, in px, at the requested point */
	Rect run = { at, { (double) boxW / impl_->pxPerPt,
			   (double) boxH / impl_->pxPerPt } };
	int x0, y0, x1, y1;

	if (!map_rect(*impl_, run, &x0, &y0, &x1, &y1)) {
		textRunFinish(t);
		return;
	}
	pixman_image_t *mask = pixman_image_create_bits(PIXMAN_a8, capW, capH,
						       nullptr, 0);
	if (!mask) {
		textRunFinish(t);
		return;
	}
	int stride = pixman_image_get_stride(mask);
	unsigned char *md = (unsigned char *) pixman_image_get_data(mask);

	for (int r = 0; r < boxH; r++) {
		std::memcpy(md + (size_t) r * (size_t) stride,
			    cov.data() + (size_t) r * (size_t) boxW,
			    (size_t) boxW);
	}
	pixman_color_t c;

	c.red = (unsigned short) (color.r * color.a * 65535.0 + 0.5);
	c.green = (unsigned short) (color.g * color.a * 65535.0 + 0.5);
	c.blue = (unsigned short) (color.b * color.a * 65535.0 + 0.5);
	c.alpha = (unsigned short) (color.a * 65535.0 + 0.5);
	pixman_image_t *src = pixman_image_create_solid_fill(&c);

	if (src) {
		int runX = impl_->ox + (int) std::floor(at.x * impl_->pxPerPt);
		int runY = impl_->oy + (int) std::floor(at.y * impl_->pxPerPt);

		pixman_image_composite32(PIXMAN_OP_OVER, src, mask, impl_->img,
					 0, 0, x0 - runX, y0 - runY,
					 x0, y0, (unsigned int) (x1 - x0),
					 (unsigned int) (y1 - y0));
		pixman_image_unref(src);
	}
	pixman_image_unref(mask);
	textRunFinish(t);
}

/* ---- the window ------------------------------------------------------
 *
 * The surface is an x8r8g8b8 image that goes straight to X, with a pixman
 * image over the same bytes for drawing. v1 uploads the damage with
 * XPutImage; the SHM fast path is a later optimisation of this one
 * function (the buffer the rest of the class sees does not change).
 */

static const double kTitlebarPt = 22.0;	/* the default chrome height */
static const double kCloseBoxPt = 10.0;
static const double kTitleInsetPt = 10.0;

struct Window::Impl {
	XID xwin = 0;
	bool open = false;
	double pxPerPt = 1.0;
	unsigned int wPx = 0, hPx = 0;
	XImage *ximg = nullptr;
	pixman_image_t *pimg = nullptr;
	bool dirty = true;
};

Window::Window()
	: impl_(new Impl())
{
}

Window::~Window()
{
	close();
	delete impl_;
	impl_ = nullptr;
}

bool
Window::open(const char *title, int xPt, int yPt, unsigned int wPt,
	     unsigned int hPt)
{
	if (impl_->open) {
		return true;
	}
	if (!displayOpen()) {
		return false;
	}
	Display *dpy = gDpy;
	double pp = displayPxPerPt();

	/* the frame is the window's size in POINTS: the surface, the chrome
	 * and the content rect are all derived from it */
	frame_ = Rect{ { (double) xPt, (double) yPt },
		       { (double) wPt, (double) hPt } };
	impl_->pxPerPt = pp;
	impl_->wPx = (unsigned int) (wPt * pp + 0.5);
	impl_->hPx = (unsigned int) (hPt * pp + 0.5);
	if (impl_->wPx < 1 || impl_->hPx < 1) {
		return false;
	}
	impl_->xwin = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
					  xPt, yPt, impl_->wPx, impl_->hPx,
					  0, 0, 0);
	if (!impl_->xwin) {
		return false;
	}
	XSelectInput(dpy, impl_->xwin, ExposureMask | StructureNotifyMask
		     | ButtonPressMask | ButtonReleaseMask
		     | PointerMotionMask | EnterWindowMask | LeaveWindowMask);
	if (title) {
		XStoreName(dpy, impl_->xwin, title);
		title_ = title;
	}
	/* the window draws its own chrome: a real WM must not add any */
	Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, impl_->xwin, &wmDelete, 1);
	{
		Atom pidA = XInternAtom(dpy, "_NET_WM_PID", False);
		long pid = (long) getpid();

		XChangeProperty(dpy, impl_->xwin, pidA, XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *) &pid, 1);
	}

	char *bits = (char *) std::malloc((size_t) impl_->wPx
					  * (size_t) impl_->hPx * 4);

	if (!bits) {
		XDestroyWindow(dpy, impl_->xwin);
		impl_->xwin = 0;
		return false;
	}
	/* XDestroyImage() frees `bits` (Xlib owns an image it created) */
	impl_->ximg = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)),
				   DefaultDepth(dpy, DefaultScreen(dpy)),
				   ZPixmap, 0, bits, impl_->wPx, impl_->hPx, 32, 0);
	if (!impl_->ximg) {
		std::free(bits);
		XDestroyWindow(dpy, impl_->xwin);
		impl_->xwin = 0;
		return false;
	}
	impl_->pimg = pixman_image_create_bits(PIXMAN_x8r8g8b8,
					       (int) impl_->wPx,
					       (int) impl_->hPx,
					       (std::uint32_t *) impl_->ximg->data,
					       impl_->ximg->bytes_per_line);
	impl_->open = true;
	XMapWindow(dpy, impl_->xwin);
	XSync(dpy, False);
	layoutContent();
	setNeedsDisplay();
	return true;
}

void
Window::close()
{
	if (!impl_ || !impl_->open) {
		return;
	}
	if (impl_->pimg) {
		pixman_image_unref(impl_->pimg);
		impl_->pimg = nullptr;
	}
	if (impl_->ximg) {
		XDestroyImage(impl_->ximg);	/* frees the pixel buffer */
		impl_->ximg = nullptr;
	}
	if (gDpy && impl_->xwin) {
		XDestroyWindow(gDpy, impl_->xwin);
		XSync(gDpy, False);
	}
	impl_->xwin = 0;
	impl_->open = false;
}

bool
Window::isOpen() const
{
	return impl_ && impl_->open;
}

void
Window::show()
{
	if (impl_->open) {
		XMapWindow(gDpy, impl_->xwin);
		XSync(gDpy, False);
	}
}

void
Window::hide()
{
	if (impl_->open) {
		XUnmapWindow(gDpy, impl_->xwin);
		XSync(gDpy, False);
	}
}

const char *
Window::title() const
{
	return title_.c_str();
}

void
Window::setTitle(const char *utf8)
{
	title_ = utf8 ? utf8 : "";
	if (impl_->open) {
		XStoreName(gDpy, impl_->xwin, title_.c_str());
	}
	setChromeDirty();
}

WindowStyle
Window::style() const
{
	return style_;
}

void
Window::setStyle(WindowStyle style)
{
	style_ = style;
	if (impl_->open) {
		layoutContent();
		setNeedsDisplay();
	}
}

double
Window::chromeHeightPt() const
{
	return style_ == WindowStyle::Titled ? chromePt_ : 0.0;
}

void
Window::setChromeHeightPt(double pt)
{
	if (pt < 0) {
		pt = 0;
	}
	chromePt_ = pt;
	if (impl_->open) {
		layoutContent();
		setNeedsDisplay();
	}
}

Rect
Window::contentRect() const
{
	double ch = chromeHeightPt();

	return Rect{ { 0, ch }, { frame_.size.w, frame_.size.h - ch } };
}

/* put the content view at the content rect's origin, at its size */
void
Window::layoutContent()
{
	if (!content_) {
		return;
	}
	Rect cr = contentRect();

	content_->setFrame(Rect{ { 0, 0 }, cr.size });
}

void
Window::setContentView(View *v)
{
	if (v == content_) {
		return;
	}
	delete content_;
	content_ = v;
	if (v) {
		v->setWindow(this);
	}
	layoutContent();
	setNeedsDisplay();
}

View *
Window::contentView() const
{
	return content_;
}

Rect
Window::frame() const
{
	return frame_;
}

void
Window::setFrame(const Rect &r)
{
	bool resized = (r.size.w != frame_.size.w || r.size.h != frame_.size.h);

	frame_ = r;
	if (!impl_->open) {
		return;
	}
	if (!resized) {
		/* a move is still a move: without telling X the window would
		 * move in the model and stay put on screen */
		XMoveWindow(gDpy, impl_->xwin, (int) r.origin.x,
			    (int) r.origin.y);
		XSync(gDpy, False);
		return;
	}
	double pp = impl_->pxPerPt;

	impl_->wPx = (unsigned int) (r.size.w * pp + 0.5);
	impl_->hPx = (unsigned int) (r.size.h * pp + 0.5);
	if (impl_->wPx < 1) {
		impl_->wPx = 1;
	}
	if (impl_->hPx < 1) {
		impl_->hPx = 1;
	}
	XMoveResizeWindow(gDpy, impl_->xwin, (int) r.origin.x,
			  (int) r.origin.y, impl_->wPx, impl_->hPx);
	/* the surface is a new size: a fresh image over a fresh buffer */
	if (impl_->pimg) {
		pixman_image_unref(impl_->pimg);
		impl_->pimg = nullptr;
	}
	if (impl_->ximg) {
		XDestroyImage(impl_->ximg);
	}
	char *bits = (char *) std::malloc((size_t) impl_->wPx
					  * (size_t) impl_->hPx * 4);

	if (!bits) {
		close();
		return;
	}
	impl_->ximg = XCreateImage(gDpy, DefaultVisual(gDpy,
						       DefaultScreen(gDpy)),
				   DefaultDepth(gDpy, DefaultScreen(gDpy)),
				   ZPixmap, 0, bits, impl_->wPx, impl_->hPx,
				   32, 0);
	impl_->pimg = pixman_image_create_bits(PIXMAN_x8r8g8b8,
					       (int) impl_->wPx,
					       (int) impl_->hPx,
					       (std::uint32_t *) impl_->ximg->data,
					       impl_->ximg->bytes_per_line);
	layoutContent();
	setNeedsDisplay();
}

Color
Window::backgroundColor() const
{
	return bg_;
}

void
Window::setBackgroundColor(const Color &c)
{
	bg_ = c;
	setNeedsDisplay();
}

void
Window::setChromeDirty()
{
	setNeedsDisplay();
}

void
Window::setNeedsDisplay()
{
	impl_->dirty = true;
	markTreeForDisplay(content_);
}

void
Window::noteViewDamage()
{
	impl_->dirty = true;
}

void
Window::setNeedsDisplayInRect(const Rect &r)
{
	(void) r;
	/* v1: damage is coarse (the whole content tree repaints); the rect
	 * is still the right call for a caller to make */
	setNeedsDisplay();
}

bool
Window::needsDisplay() const
{
	return impl_ && impl_->dirty;
}

unsigned int
Window::widthPx() const
{
	return impl_->wPx;
}

unsigned int
Window::heightPx() const
{
	return impl_->hPx;
}

double
Window::pxPerPt() const
{
	return impl_->pxPerPt;
}

/* ---- the draw pass --------------------------------------------------- */

static void
markTreeForDisplay(View *v)
{
	if (!v) {
		return;
	}
	v->setNeedsDisplay();
	for (View *c : v->subviews()) {
		markTreeForDisplay(c);
	}
}

/* the window's own chrome: the titlebar, its title and the close box */
void
Window::drawChrome(Context &ctx)
{
	double ch = chromeHeightPt();
	double wPt = frame_.size.w;
	double pp = impl_->pxPerPt;

	/* the bar */
	ctx.fillRect(Rect{ { 0, 0 }, { wPt, ch } }, chromeColor_);
	/* a hairline under it */
	ctx.fillRect(Rect{ { 0, ch - 1.0 / pp }, { wPt, 1.0 / pp } },
		     borderColor_);
	/* the title, vertically centred in the bar */
	if (!title_.empty()) {
		ctx.drawText(nullptr, 12.0,
			     Point{ kTitleInsetPt, ch * 0.5 - 8.0 },
			     title_.c_str(), titleColor_);
	}
	/* the close box: a placeholder square until the theme layer lands */
	double bs = kCloseBoxPt;

	ctx.fillRect(Rect{ { wPt - bs - 6.0, (ch - bs) * 0.5 },
			   { bs, bs } }, closeColor_);
}

static void
draw_view(View *v, Context &ctx, int oxPx, int oyPx, double pxPerPt)
{
	if (!v || v->isHidden()) {
		return;
	}
	const Rect &f = v->frame();
	int vx = oxPx + (int) std::floor(f.origin.x * pxPerPt);
	int vy = oyPx + (int) std::floor(f.origin.y * pxPerPt);
	int vw = (int) std::ceil(f.size.w * pxPerPt);
	int vh = (int) std::ceil(f.size.h * pxPerPt);

	ctx.pushFrame(vx, vy, vx + vw, vy + vh);
	if (v->needsDisplay()) {
		v->drawRect(v->needsDisplayRect());
		v->clearNeedsDisplay();
	}
	/* children after their superview: painter's order */
	for (View *c : v->subviews()) {
		draw_view(c, ctx, vx, vy, pxPerPt);
	}
	ctx.popFrame();
}

void
Window::displayIfNeeded()
{
	if (!impl_->open || !impl_->dirty) {
		return;
	}
	Context::Impl ci;

	ci.img = impl_->pimg;
	ci.wPx = impl_->wPx;
	ci.hPx = impl_->hPx;
	ci.pxPerPt = impl_->pxPerPt;
	ci.cx0 = 0;
	ci.cy0 = 0;
	ci.cx1 = (int) impl_->wPx;
	ci.cy1 = (int) impl_->hPx;
	Context ctx;

	ctx.impl_ = &ci;
	gCurrent = &ctx;

	/* 1. the background covers the whole surface */
	ctx.fillRect(Rect{ { 0, 0 }, frame_.size }, bg_);
	/* 2. the chrome (the window's own: no window manager draws it) */
	if (style_ == WindowStyle::Titled) {
		drawChrome(ctx);
	}
	/* 3. the content tree, inside the content rect */
	Rect cr = contentRect();

	if (content_) {
		ci.cx0 = (int) std::floor(cr.origin.x * ci.pxPerPt);
		ci.cy0 = (int) std::floor(cr.origin.y * ci.pxPerPt);
		ci.cx1 = ci.cx0
			+ (int) std::ceil(cr.size.w * ci.pxPerPt);
		ci.cy1 = ci.cy0
			+ (int) std::ceil(cr.size.h * ci.pxPerPt);
		draw_view(content_, ctx, ci.cx0, ci.cy0, ci.pxPerPt);
	}
	gCurrent = nullptr;
	ctx.impl_ = nullptr;
	flush();
	impl_->dirty = false;
}

/* ---- input (U2b) ------------------------------------------------------
 *
 * X hands us an event in the window's own pixel coordinates; the window
 * converts that to POINTS, works out whether the chrome or the content was
 * hit, and dispatches. A press is captured: the same view gets the drags
 * and the release (X's implicit pointer grab does the same job at the
 * protocol level while a button is down).
 *
 * The chrome's drag uses the event's ROOT coordinates, not the window-
 * relative ones: while the window moves, the window-relative point under
 * the pointer stays put, so subtracting two of them yields a delta of
 * zero and the window stops following the pointer.
 */

Rect
Window::closeBoxRect() const
{
	double ch = chromeHeightPt();
	double wPt = frame_.size.w;
	double bs = kCloseBoxPt;

	return Rect{ { wPt - bs - 6.0, (ch - bs) * 0.5 }, { bs, bs } };
}

bool
Window::inChrome(const Point &p) const
{
	return style_ == WindowStyle::Titled && chromeHeightPt() > 0
		&& p.y < chromeHeightPt();
}

View *
Window::dispatchToContent(const Point &contentPt, const MouseEvent &e)
{
	if (!content_) {
		return nullptr;
	}
	Rect cr = contentRect();

	if (contentPt.x < 0 || contentPt.y < 0 || contentPt.x >= cr.size.w
	    || contentPt.y >= cr.size.h) {
		return nullptr;
	}
	return content_->hitTest(contentPt);
}

bool
Window::pumpEvent()
{
	if (!impl_->open || !gDpy) {
		return false;
	}
	if (!XPending(gDpy)) {
		return false;
	}
	XEvent ev;

	XNextEvent(gDpy, &ev);
	double pp = impl_->pxPerPt;

	switch (ev.type) {
	case Expose:
		if (ev.xexpose.count == 0) {
			setNeedsDisplay();
		}
		return true;

	case ConfigureNotify:
		if (ev.xconfigure.width != (int) impl_->wPx
		    || ev.xconfigure.height != (int) impl_->hPx) {
			frame_.origin.x = ev.xconfigure.x;
			frame_.origin.y = ev.xconfigure.y;
			setFrame(Rect{ frame_.origin,
				       { ev.xconfigure.width / pp,
					 ev.xconfigure.height / pp } });
		}
		return true;

	case ButtonPress:
	case ButtonRelease:
	case MotionNotify: {
		Point winPt = { ev.xbutton.x / pp, ev.xbutton.y / pp };
		bool pressed = (ev.type == ButtonPress);
		bool released = (ev.type == ButtonRelease);
		MouseEvent me;

		me.location = winPt;
		me.button = ev.xbutton.button ? ev.xbutton.button : 1;
		me.clickCount = 1;
		me.shift = (ev.xbutton.state & ShiftMask) != 0;
		me.control = (ev.xbutton.state & ControlMask) != 0;
		me.alt = (ev.xbutton.state & Mod1Mask) != 0;

		/* 0. a drag in progress is CAPTURED: it follows the pointer
		 * wherever it goes. Testing the chrome first would stop the drag
		 * the moment the pointer left the titlebar - which a downward
		 * drag does immediately. */
		if (dragging_) {
			if (released) {
				dragging_ = false;
				return true;
			}
			if (ev.type == MotionNotify) {
				setFrame(Rect{ { dragWinX_
						 + (ev.xbutton.x_root - dragRootX_)
						 / pp,
						 dragWinY_
						 + (ev.xbutton.y_root - dragRootY_)
						 / pp },
					       frame_.size });
				return true;
			}
			return true;
		}

		/* 1. the chrome: the window's own, so the window handles it */
		if (inChrome(winPt)) {
			if (pressed) {
				Point p = winPt;

				if (me.button == 1) {
					Rect cb = closeBoxRect();

					if (p.x >= cb.origin.x
					    && p.x < cb.origin.x + cb.size.w
					    && p.y >= cb.origin.y
					    && p.y < cb.origin.y + cb.size.h) {
						requestClose();
						return true;
					}
				}
				/* anywhere else in the bar starts a drag, and the
				 * delta comes from the ROOT coordinates */
				if (ev.xbutton.button == 1 && me.button == 1) {
					dragging_ = true;
					dragRootX_ = ev.xbutton.x_root;
					dragRootY_ = ev.xbutton.y_root;
					dragWinX_ = frame_.origin.x;
					dragWinY_ = frame_.origin.y;
				}
				return true;
			}
			if (released) {
				dragging_ = false;
				return true;
			}
			if (dragging_) {
				/* XMoveWindow keeps the pointer in the right
				 * place; the frame follows it */
				setFrame(Rect{ { dragWinX_
						 + (ev.xbutton.x_root - dragRootX_)
						 / pp,
						 dragWinY_
						 + (ev.xbutton.y_root - dragRootY_)
						 / pp },
					       frame_.size });
				return true;
			}
			return true;
		}

		/* 2. the content: hit test, then capture on the press */
		Rect cr = contentRect();
		Point contentPt = { winPt.x - cr.origin.x,
				    winPt.y - cr.origin.y };

		if (pressed) {
			View *hit = dispatchToContent(contentPt, me);

			pressView_ = hit;
			if (hit) {
				hit->setTrackingMouse(true);
			}
			/* offer it to the view, then up the parent chain */
			for (View *v = hit; v; v = v->superview()) {
				me.location = Point{ contentPt.x
						     - v->rectInWindow(
							       Rect{ { 0, 0 },
								     { 0, 0 } })
							       .origin.x,
						     contentPt.y
						     - v->rectInWindow(
							       Rect{ { 0, 0 },
								     { 0, 0 } })
							       .origin.y };

				if (v->mouseDown(me)) {
					break;
				}
			}
			return true;
		}
		if (me.button != 1 && pressView_ == nullptr) {
			return true;
		}
		/* drags and releases go to the view that captured the press */
		if (pressView_) {
			View *v = pressView_;
			Rect off = v->rectInWindow(Rect{ { 0, 0 }, { 0, 0 } });

			me.location = Point{ contentPt.x - off.origin.x,
					     contentPt.y - off.origin.y };
			if (released) {
				v->mouseUp(me);
				v->setTrackingMouse(false);
				pressView_ = nullptr;
				dragging_ = false;
			} else {
				v->mouseDragged(me);
			}
			return true;
		}
		/* plain motion: hover bookkeeping, so a control can react */
		View *under = dispatchToContent(contentPt, me);

		if (under != hoverView_) {
			if (hoverView_) {
				hoverView_->setHovered(false);
			}
			hoverView_ = under;
			if (under) {
				under->setHovered(true);
			}
		}
		return true;
	}

	case ClientMessage:
		/* the close-box protocol: a window manager would send this */
		requestClose();
		return true;

	default:
		return true;
	}
}

void
Window::requestClose()
{
	closeRequested_ = true;
}

void
Window::flush()
{
	if (!impl_->open || !impl_->ximg) {
		return;
	}
	GC gc = XCreateGC(gDpy, impl_->xwin, 0, nullptr);

	if (gc) {
		XPutImage(gDpy, impl_->xwin, gc, impl_->ximg, 0, 0, 0, 0,
			  impl_->wPx, impl_->hPx);
		XFreeGC(gDpy, gc);
	}
	XSync(gDpy, False);
}

/* the class record */
static const Property Window_PROPS[] = {
	{ "title",
	  [](const Object *o) {
		  return Value::of(static_cast<const Window *>(o)->title()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Text) {
			  return false;
		  }
		  static_cast<Window *>(o)->setTitle(v.text.c_str());
		  return true; } },
	{ "contentView",
	  [](const Object *o) {
		  View *v = static_cast<const Window *>(o)->contentView();

		  return v ? Value::of(static_cast<Object *>(v))
			   : Value::nil(); },
	  nullptr },
	{ "pxPerPt",
	  [](const Object *o) {
		  return Value::of(static_cast<const Window *>(o)->pxPerPt()); },
	  nullptr },
	{ "closeRequested",
	  [](const Object *o) {
		  return Value::of(static_cast<const Window *>(o)
				   ->isCloseRequested()); },
	  nullptr },
};

const ObjectClass Window::kClass = {
	"Window", &Object::kClass, Window_PROPS,
	(int) (sizeof(Window_PROPS) / sizeof(Window_PROPS[0])), nullptr, 0
};

} /* namespace argentum */
