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
#include <X11/extensions/XShm.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <unistd.h>

/* The events a window asks for. ONE list: XSelectInput() above and the pump's
 * XCheckWindowEvent() below must agree, or the pump will never see an event
 * the server was told to send. */
static const long kWindowEventMask =
	ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask
	| PointerMotionMask | EnterWindowMask | LeaveWindowMask | KeyPressMask
	| KeyReleaseMask | FocusChangeMask;

#include <ctime>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace argentum {

/* ---- the process's connection ---------------------------------------- */

/* --- timing the interactive path -------------------------------------
 *
 * A window's felt latency is two costs added together, and guessing at
 * them is how you "fix" the wrong one: the whole-window repaint this
 * toolkit does for ANY damage (displayIfNeeded), and the transport that
 * pushes the result to X. Both halves are timed, and the rasterisation is
 * timed apart from the flush, when ARGENTUM_PAINT_MS is set. Off by
 * default: a clock read per frame is not worth paying for unasked. */
static double
nowMs()
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1000000.0;
}

static bool
timingOn(const char *name)
{
	const char *v = std::getenv(name);

	return v && v[0] && v[0] != '0';
}

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
/* How many views the last paint pass actually walked. This is the number
 * that says whether damage narrowing works: a click must not paint a
 * screenful. Logged with ARGENTUM_PAINT_MS and asserted by the gate. */
static int gViewsDrawn;

static bool
rectsIntersectPx(int ax0, int ay0, int ax1, int ay1, int bx0, int by0,
		 int bx1, int by1)
{
	return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}

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

/* ---- shapes -----------------------------------------------------------
 *
 * Every shape is rasterized into an A8 coverage mask with pixman's
 * triangle rasterizer and then composited in the requested colour, so the
 * edges are anti-aliased at any px/pt factor. The mask is built in SURFACE
 * pixels for the shape's clipped bounding box: the triangle coordinates are
 * mapped (points -> pixels, through the view's origin) and then made
 * relative to that box, which is what makes clipping and scaling fall out
 * for free.
 */

struct MaskBox {
	pixman_image_t *mask = nullptr;
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

	bool valid() const { return mask != nullptr; }
};

/* allocate an A8 mask over the clipped box of `pts` (view points) */
static MaskBox
mask_begin(const Context::Impl &im, const Point *pts, int count)
{
	MaskBox b;
	double minx = pts[0].x, miny = pts[0].y;
	double maxx = minx, maxy = miny;

	for (int i = 1; i < count; i++) {
		if (pts[i].x < minx) minx = pts[i].x;
		if (pts[i].y < miny) miny = pts[i].y;
		if (pts[i].x > maxx) maxx = pts[i].x;
		if (pts[i].y > maxy) maxy = pts[i].y;
	}
	Rect r = { { minx, miny }, { maxx - minx, maxy - miny } };
	int x0, y0, x1, y1;

	if (!map_rect(im, r, &x0, &y0, &x1, &y1)) {
		return b;
	}
	b.x0 = x0; b.y0 = y0; b.x1 = x1; b.y1 = y1;
	b.mask = pixman_image_create_bits(PIXMAN_a8, x1 - x0, y1 - y0,
					  nullptr, 0);
	return b;
}

static void
mask_end(MaskBox &b)
{
	if (b.mask) {
		pixman_image_unref(b.mask);
		b.mask = nullptr;
	}
}

/* the shape's own point -> mask coordinates */
static void
mask_pt(const Context::Impl &im, const MaskBox &b, const Point &p,
	double *mx, double *my)
{
	*mx = (double) (im.ox + p.x * im.pxPerPt - b.x0);
	*my = (double) (im.oy + p.y * im.pxPerPt - b.y0);
}

/* rasterize `count` triangles (a multiple of 3) into `b.mask` */
static bool
mask_triangles(const Context::Impl &im, MaskBox &b, const Point *pts,
	       int count)
{
	if (!b.valid() || count < 3 || count % 3) {
		return false;
	}
	int n = count / 3;
	pixman_triangle_t *tris = (pixman_triangle_t *)
		std::calloc((size_t) n, sizeof(pixman_triangle_t));

	if (!tris) {
		return false;
	}
	for (int i = 0; i < n; i++) {
		double x, y;

		mask_pt(im, b, pts[i * 3 + 0], &x, &y);
		tris[i].p1.x = pixman_double_to_fixed(x);
		tris[i].p1.y = pixman_double_to_fixed(y);
		mask_pt(im, b, pts[i * 3 + 1], &x, &y);
		tris[i].p2.x = pixman_double_to_fixed(x);
		tris[i].p2.y = pixman_double_to_fixed(y);
		mask_pt(im, b, pts[i * 3 + 2], &x, &y);
		tris[i].p3.x = pixman_double_to_fixed(x);
		tris[i].p3.y = pixman_double_to_fixed(y);
	}
	pixman_color_t white = { 0xffff, 0xffff, 0xffff, 0xffff };
	pixman_image_t *wsrc = pixman_image_create_solid_fill(&white);
	bool ok = false;

	if (wsrc) {
		/* ADD: overlapping triangles accumulate coverage */
		pixman_composite_triangles(PIXMAN_OP_ADD, wsrc, b.mask,
					   PIXMAN_a8, 0, 0, 0, 0, n, tris);
		pixman_image_unref(wsrc);
		ok = true;
	}
	std::free(tris);
	return ok;
}

/* composite `color` through `b.mask` onto the surface */
static void
mask_paint(const Context::Impl &im, const MaskBox &b, const Color &color)
{
	if (!b.valid() || color.a <= 0) {
		return;
	}
	pixman_color_t c;

	c.red = (unsigned short) (color.r * color.a * 65535.0 + 0.5);
	c.green = (unsigned short) (color.g * color.a * 65535.0 + 0.5);
	c.blue = (unsigned short) (color.b * color.a * 65535.0 + 0.5);
	c.alpha = (unsigned short) (color.a * 65535.0 + 0.5);
	pixman_image_t *src = pixman_image_create_solid_fill(&c);

	if (!src) {
		return;
	}
	pixman_image_composite32(PIXMAN_OP_OVER, src, b.mask, im.img, 0, 0,
				 0, 0, b.x0, b.y0, (unsigned int) (b.x1 - b.x0),
				 (unsigned int) (b.y1 - b.y0));
	pixman_image_unref(src);
}

void
Context::fillTriangles(const Point *pts, int count, const Color &color)
{
	if (!impl_ || !impl_->img || !pts || count < 3 || count % 3) {
		return;
	}
	MaskBox b = mask_begin(*impl_, pts, count);

	if (!b.valid()) {
		return;
	}
	if (mask_triangles(*impl_, b, pts, count)) {
		mask_paint(*impl_, b, color);
	}
	mask_end(b);
}

void
Context::fillPolygon(const Point *pts, int count, const Color &color)
{
	if (!impl_ || !pts || count < 3) {
		return;
	}
	/* a fan from the first vertex: exact for a CONVEX polygon, which is
	 * what the toolkit's chrome is made of */
	std::vector<Point> tris;

	tris.reserve((size_t) (count - 2) * 3);
	for (int i = 1; i + 1 < count; i++) {
		tris.push_back(pts[0]);
		tris.push_back(pts[i]);
		tris.push_back(pts[i + 1]);
	}
	fillTriangles(tris.data(), (int) tris.size(), color);
}

#define ARC_STEPS 12	/* samples per quarter arc */

void
Context::fillCircle(const Point &center, double radius, const Color &color)
{
	if (!impl_ || radius <= 0) {
		return;
	}
	Rect r = { { center.x - radius, center.y - radius },
		   { radius * 2, radius * 2 } };

	fillEllipse(r, color);
}

void
Context::fillEllipse(const Rect &rect, const Color &color)
{
	if (!impl_ || rect.size.w <= 0 || rect.size.h <= 0) {
		return;
	}
	double rx = rect.size.w / 2.0;
	double ry = rect.size.h / 2.0;
	double cx = rect.origin.x + rx;
	double cy = rect.origin.y + ry;
	int n = ARC_STEPS * 4;
	std::vector<Point> pts;
	std::vector<Point> tris;

	pts.reserve((size_t) n);
	for (int i = 0; i < n; i++) {
		double a = 2.0 * 3.14159265358979 * i / n;

		pts.push_back(Point{ cx + rx * std::cos(a),
				     cy + ry * std::sin(a) });
	}
	tris.reserve((size_t) n * 3);
	for (int i = 0; i < n; i++) {
		tris.push_back(Point{ cx, cy });
		tris.push_back(pts[(size_t) i]);
		tris.push_back(pts[(size_t) ((i + 1) % n)]);
	}
	fillTriangles(tris.data(), (int) tris.size(), color);
}

/* the outline points of a rounded rect, clockwise from the top-left arc */
static void
rounded_outline(const Rect &rect, double radius, std::vector<Point> &out)
{
	double x = rect.origin.x, y = rect.origin.y;
	double w = rect.size.w, h = rect.size.h;
	double rr = radius;

	if (rr > w / 2.0) {
		rr = w / 2.0;
	}
	if (rr > h / 2.0) {
		rr = h / 2.0;
	}
	if (rr < 0) {
		rr = 0;
	}
	struct Corner { double cx, cy, a0, a1; };
	const Corner corners[4] = {
		{ x + w - rr, y + rr, -1.5707963267948966, 0.0 },
		{ x + w - rr, y + h - rr, 0.0, 1.5707963267948966 },
		{ x + rr, y + h - rr, 1.5707963267948966, 3.141592653589793 },
		{ x + rr, y + rr, 3.141592653589793, 4.71238898038469 },
	};

	out.clear();
	for (const Corner &c : corners) {
		for (int i = 0; i <= ARC_STEPS; i++) {
			double a = c.a0 + (c.a1 - c.a0) * i / ARC_STEPS;

			out.push_back(Point{ c.cx + rr * std::cos(a),
					     c.cy + rr * std::sin(a) });
		}
	}
}

void
Context::fillRoundRect(const Rect &rect, double radius, const Color &color)
{
	if (!impl_ || rect.size.w <= 0 || rect.size.h <= 0) {
		return;
	}
	if (radius <= 0) {
		fillRect(rect, color);
		return;
	}
	std::vector<Point> per;

	rounded_outline(rect, radius, per);
	/* the centre, relative to the outline: a rounded rect is convex, so
	 * a fan from its centre covers it exactly */
	Point c = { rect.origin.x + rect.size.w / 2.0,
		    rect.origin.y + rect.size.h / 2.0 };
	std::vector<Point> tris;

	tris.reserve(per.size() * 3);
	for (size_t i = 0; i < per.size(); i++) {
		tris.push_back(c);
		tris.push_back(per[i]);
		tris.push_back(per[(i + 1) % per.size()]);
	}
	fillTriangles(tris.data(), (int) tris.size(), color);
}

void
Context::strokeRect(const Rect &rect, const Color &color, double width)
{
	if (width <= 0) {
		return;
	}
	fillRect(Rect{ rect.origin, { rect.size.w, width } }, color);
	fillRect(Rect{ { rect.origin.x, rect.origin.y + rect.size.h - width },
		       { rect.size.w, width } }, color);
	fillRect(Rect{ rect.origin, { width, rect.size.h } }, color);
	fillRect(Rect{ { rect.origin.x + rect.size.w - width, rect.origin.y },
		       { width, rect.size.h } }, color);
}

void
Context::strokeRoundRect(const Rect &rect, double radius, const Color &color,
			 double width)
{
	if (!impl_ || width <= 0 || rect.size.w <= 0 || rect.size.h <= 0) {
		return;
	}
	if (radius <= 0) {
		strokeRect(rect, color, width);
		return;
	}
	std::vector<Point> outer, inner;

	rounded_outline(rect, radius, outer);
	Rect in = { { rect.origin.x + width, rect.origin.y + width },
		    { rect.size.w - 2 * width, rect.size.h - 2 * width } };

	if (in.size.w < 0 || in.size.h < 0) {
		fillRoundRect(rect, radius, color);
		return;
	}
	rounded_outline(in, radius - width, inner);

	/* the ring between the two outlines, as a strip of quads (two
	 * triangles each): a ring is NOT convex, so no fan */
	size_t n = outer.size() < inner.size() ? inner.size() : outer.size();
	std::vector<Point> tris;

	tris.reserve(n * 6);
	for (size_t i = 0; i < n; i++) {
		const Point &o0 = outer[i % outer.size()];
		const Point &o1 = outer[(i + 1) % outer.size()];
		const Point &i0 = inner[i % inner.size()];
		const Point &i1 = inner[(i + 1) % inner.size()];

		tris.push_back(o0); tris.push_back(o1); tris.push_back(i1);
		tris.push_back(o0); tris.push_back(i1); tris.push_back(i0);
	}
	fillTriangles(tris.data(), (int) tris.size(), color);
}

void
Context::fillLinearGradient(const Rect &rect, const Color &top,
			    const Color &bottom, bool vertical)
{
	if (!impl_ || !impl_->img || rect.size.w <= 0 || rect.size.h <= 0) {
		return;
	}
	int x0, y0, x1, y1;

	if (!map_rect(*impl_, rect, &x0, &y0, &x1, &y1)) {
		return;
	}
	pixman_point_fixed_t p1, p2;

	/* the gradient's geometry is in the SOURCE image's space, and the
	 * composite below samples it from the source's origin - so these are
	 * relative to the box, NOT absolute surface pixels. (Absolute
	 * coordinates put the whole gradient off to one side, where PAD
	 * repeat clamped everything to a single colour.) */
	if (vertical) {
		p1.x = pixman_double_to_fixed(0);
		p1.y = pixman_double_to_fixed(0);
		p2.x = pixman_double_to_fixed(0);
		p2.y = pixman_double_to_fixed(y1 - y0);
	} else {
		p1.x = pixman_double_to_fixed(0);
		p1.y = pixman_double_to_fixed(0);
		p2.x = pixman_double_to_fixed(x1 - x0);
		p2.y = pixman_double_to_fixed(0);
	}
	pixman_color_t c0, c1;

	c0.red = (unsigned short) (top.r * top.a * 65535.0 + 0.5);
	c0.green = (unsigned short) (top.g * top.a * 65535.0 + 0.5);
	c0.blue = (unsigned short) (top.b * top.a * 65535.0 + 0.5);
	c0.alpha = (unsigned short) (top.a * 65535.0 + 0.5);
	c1.red = (unsigned short) (bottom.r * bottom.a * 65535.0 + 0.5);
	c1.green = (unsigned short) (bottom.g * bottom.a * 65535.0 + 0.5);
	c1.blue = (unsigned short) (bottom.b * bottom.a * 65535.0 + 0.5);
	c1.alpha = (unsigned short) (bottom.a * 65535.0 + 0.5);

	pixman_gradient_stop_t stops[2];

	stops[0].color = c0;
	stops[0].x = 0;
	stops[1].color = c1;
	stops[1].x = 0xffff;
	pixman_image_t *grad = pixman_image_create_linear_gradient(&p1, &p2,
								   stops, 2);

	if (!grad) {
		return;
	}
	pixman_image_set_repeat(grad, PIXMAN_REPEAT_PAD);
	pixman_image_composite32(PIXMAN_OP_OVER, grad, nullptr, impl_->img,
				 0, 0, 0, 0, x0, y0, (unsigned int) (x1 - x0),
				 (unsigned int) (y1 - y0));
	pixman_image_unref(grad);
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
	int level = WindowLevelNormal;	/* see WindowLevel */
	double pxPerPt = 1.0;
	unsigned int wPx = 0, hPx = 0;
	XImage *ximg = nullptr;
	pixman_image_t *pimg = nullptr;
	bool dirty = true;
	/* the flush region: what changed since the last frame, in SURFACE
	 * pixels. dmgAll is the whole surface (a resize, an expose, a window
	 * level change); otherwise the rect is the union of the view damage
	 * that arrived since. */
	bool dmgAll = true;
	int dmgX0 = 0, dmgY0 = 0, dmgX1 = 0, dmgY1 = 0;
	/* the pixels, in shared memory when the server has MIT-SHM */
	XShmSegmentInfo shm {};
	bool shmAttached = false;
};

/* ---- the window's pixels ---------------------------------------------
 *
 * One buffer holds the window: pixman rasterises into it and X is handed
 * the result. Both halves of that are measured (ARGENTUM_PAINT_MS),
 * because they are the two costs a slow control is made of. MIT-SHM takes
 * the push off the socket when the server has it, and the push is limited
 * to the region that actually changed. The PAINT stays coarse on purpose:
 * the whole tree repaints, so the buffer is always a complete frame and
 * any sub-rect of it is correct to send. */
static Bool gShmOk = False;

template <typename ImplT>
static void
surfaceDestroy(ImplT *impl)
{
	if (impl->pimg) {
		pixman_image_unref(impl->pimg);
		impl->pimg = nullptr;
	}
	if (impl->ximg) {
		if (impl->shmAttached) {
			/* those pixels belong to the shared segment, not to
			 * Xlib: detach and drop the pointer before Xlib's
			 * destroy tries to free memory it never allocated */
			XShmDetach(gDpy, &impl->shm);
			impl->shmAttached = False;
			impl->ximg->data = nullptr;
		}
		XDestroyImage(impl->ximg);
		impl->ximg = nullptr;
	}
	if (impl->shm.shmid >= 0) {
		if (impl->shm.shmaddr && impl->shm.shmaddr != (char *) -1) {
			shmdt(impl->shm.shmaddr);
		}
		shmctl(impl->shm.shmid, IPC_RMID, nullptr);
		impl->shm.shmid = -1;
		impl->shm.shmaddr = nullptr;
	}
}

template <typename ImplT>
static bool
surfaceCreate(ImplT *impl, unsigned int wPx, unsigned int hPx)
{
	Visual *vis = DefaultVisual(gDpy, DefaultScreen(gDpy));
	int depth = DefaultDepth(gDpy, DefaultScreen(gDpy));

	impl->shm.shmid = -1;
	impl->shm.shmaddr = nullptr;
	impl->shmAttached = False;

	if (gShmOk) {
		impl->ximg = XShmCreateImage(gDpy, vis, (unsigned int) depth,
					     ZPixmap, nullptr, &impl->shm,
					     wPx, hPx);
		if (impl->ximg) {
			size_t bytes = (size_t) impl->ximg->bytes_per_line
				       * (size_t) hPx;

			impl->shm.shmid = shmget(IPC_PRIVATE, bytes,
						 IPC_CREAT | 0666);
			if (impl->shm.shmid >= 0) {
				impl->shm.shmaddr = impl->ximg->data =
					(char *) shmat(impl->shm.shmid,
						       nullptr, 0);
				impl->shm.readOnly = False;
				if (impl->shm.shmaddr != (char *) -1
				    && XShmAttach(gDpy, &impl->shm)) {
					impl->shmAttached = true;
				}
			}
		}
		if (!impl->shmAttached) {
			surfaceDestroy(impl);	/* a private buffer then */
		}
	}
	if (!impl->ximg) {
		char *bits = (char *) std::malloc((size_t) wPx
						  * (size_t) hPx * 4);

		if (!bits) {
			return false;
		}
		/* XDestroyImage() frees `bits`: Xlib owns an image it made */
		impl->ximg = XCreateImage(gDpy, vis, (unsigned int) depth,
					  ZPixmap, 0, bits, wPx, hPx, 32, 0);
		if (!impl->ximg) {
			std::free(bits);
			return false;
		}
	}
	impl->pimg = pixman_image_create_bits(PIXMAN_x8r8g8b8, (int) wPx,
					      (int) hPx,
					      (std::uint32_t *) impl->ximg->data,
					      impl->ximg->bytes_per_line);
	if (!impl->pimg) {
		surfaceDestroy(impl);
		return false;
	}
	return true;
}

template <typename ImplT>
static void
damageAll(ImplT *impl)
{
	impl->dmgAll = true;
	impl->dirty = true;
}

/* Narrow the damage to include a rect given in WINDOW POINTS - what a view
 * knows about itself. Once the whole surface is damaged it stays that way:
 * a window level change is not undone by a control that also touched
 * itself. */
template <typename ImplT>
static void
damagePt(ImplT *impl, const Rect &r)
{
	int x0, y0, x1, y1;

	impl->dirty = true;
	if (impl->dmgAll) {
		return;
	}
	x0 = (int) std::floor(r.origin.x * impl->pxPerPt);
	y0 = (int) std::floor(r.origin.y * impl->pxPerPt);
	x1 = (int) std::ceil((r.origin.x + r.size.w) * impl->pxPerPt);
	y1 = (int) std::ceil((r.origin.y + r.size.h) * impl->pxPerPt);
	if (x0 < 0) {
		x0 = 0;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (x1 > (int) impl->wPx) {
		x1 = (int) impl->wPx;
	}
	if (y1 > (int) impl->hPx) {
		y1 = (int) impl->hPx;
	}
	if (x1 <= x0 || y1 <= y0) {
		return;		/* outside the window: nothing to send */
	}
	if (impl->dmgX1 <= impl->dmgX0 || impl->dmgY1 <= impl->dmgY0) {
		impl->dmgX0 = x0;	/* the first damage defines it */
		impl->dmgY0 = y0;
		impl->dmgX1 = x1;
		impl->dmgY1 = y1;
		return;
	}
	if (x0 < impl->dmgX0) {
		impl->dmgX0 = x0;
	}
	if (y0 < impl->dmgY0) {
		impl->dmgY0 = y0;
	}
	if (x1 > impl->dmgX1) {
		impl->dmgX1 = x1;
	}
	if (y1 > impl->dmgY1) {
		impl->dmgY1 = y1;
	}
}

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
	{
		/* XCreateWindow rather than the simple form, because
		 * override_redirect CANNOT BE CHANGED AFTER CREATION: a level above
		 * normal has to be decided here (see WindowLevel). */
		XSetWindowAttributes attrs;

		attrs.override_redirect =
			(impl_->level != WindowLevelNormal) ? True : False;
		attrs.background_pixel = 0;
		attrs.border_pixel = 0;
		impl_->xwin = XCreateWindow(
			dpy, DefaultRootWindow(dpy), xPt, yPt, impl_->wPx,
			impl_->hPx, 0, CopyFromParent, InputOutput,
			CopyFromParent,
			CWOverrideRedirect | CWBackPixel | CWBorderPixel, &attrs);
	if (getenv("ARGENTUM_KEYLOG")) {
		/* xwin so this can be matched against ARGENTUM-PUMP: one window id
		 * in the pumps, or two, says which window the press reached */
		std::printf("ARGENTUM-WINOPEN %s x=%d y=%d %ux%u override=%d "
			    "xwin=%lu\n", title ? title : "?", xPt, yPt, impl_->wPx,
			    impl_->hPx, attrs.override_redirect ? 1 : 0,
			    (unsigned long) impl_->xwin);
		std::fflush(stdout);
	}
	}
	if (!impl_->xwin) {
		return false;
	}
	XSelectInput(dpy, impl_->xwin, kWindowEventMask);
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

	/* MIT-SHM if this server has it: it removes the per-frame socket
	 * copy, which was 250ms of a 300ms repaint */
	gShmOk = XShmQueryExtension(dpy) ? True : False;
	if (!surfaceCreate(impl_, impl_->wPx, impl_->hPx)) {
		XDestroyWindow(dpy, impl_->xwin);
		impl_->xwin = 0;
		return false;
	}
	impl_->open = true;
	XMapRaised(dpy, impl_->xwin);
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
	surfaceDestroy(impl_);
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

int
Window::level() const
{
	return impl_ ? impl_->level : WindowLevelNormal;
}

void
Window::setLevel(int l)
{
	if (impl_) {
		impl_->level = l;
	}
}

void
Window::show()
{
	if (impl_->open) {
		/* A POP-UP (a menu, a colour panel) must come up ABOVE the window it
		 * belongs to, and with no window manager there is nobody else to
		 * raise it. An ordinary window just maps where X puts it. */
		if (impl_->level != WindowLevelNormal) {
			XMapRaised(gDpy, impl_->xwin);
		} else {
			XMapWindow(gDpy, impl_->xwin);
		}
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
		 * move in the model and stay put on screen.
		 *
		 * XFlush, NOT XSync: a move happens once per motion event, and
		 * XSync is a ROUND TRIP - one per event, with the client waiting
		 * for the server to answer each time. A flush hands the request
		 * over and returns. (Measured: the server is fine with either,
		 * but a drag is a stream of these and only one of them scales.) */
		XMoveWindow(gDpy, impl_->xwin, (int) r.origin.x,
			    (int) r.origin.y);
		XFlush(gDpy);
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
	surfaceDestroy(impl_);
	if (!surfaceCreate(impl_, impl_->wPx, impl_->hPx)) {
		close();
		return;
	}
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
	damageAll(impl_);
}

bool
Window::focusFromClick(View *hit)
{
	if (!hit) {
		return false;
	}
	/* a click gives the focus to a view that wants it (Cocoa: the field's
	 * mouseDown makes itself the first responder), so an editable field
	 * starts taking keys from the click, with no separate "focus" API for
	 * the app to call */
	if (hit->acceptsFirstResponder()) {
		return makeFirstResponder(hit);
	}
	return false;
}

void
Window::noteViewDamage()
{
	/* a caller that cannot say WHAT changed damages the whole surface.
	 * Conservative, and correct: the erase then covers exactly the region
	 * the pass is allowed to draw, so nothing is missed. */
	damageAll(impl_);
}

void
Window::setNeedsDisplayInRect(const Rect &r)
{
	/* Damage is COARSE for the PAINT and NARROW for the PUSH. The whole
	 * content tree repaints (so the buffer is a complete frame and this
	 * sub-rect of it is correct to send), but the flush sends only the
	 * union of what changed - which is what stops one click from
	 * shipping a megabyte: a click damages one control's frame. */
	damagePt(impl_, r);
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
draw_view(View *v, Context &ctx, int oxPx, int oyPx, double pxPerPt,
	  int dx0, int dy0, int dx1, int dy1)
{
	if (!v || v->isHidden()) {
		return;
	}
	const Rect &f = v->frame();
	int vx = oxPx + (int) std::floor(f.origin.x * pxPerPt);
	int vy = oyPx + (int) std::floor(f.origin.y * pxPerPt);
	int vw = (int) std::ceil(f.size.w * pxPerPt);
	int vh = (int) std::ceil(f.size.h * pxPerPt);

	/* a subtree that cannot touch the damage contributes no pixel, so it
	 * is not walked: this is where the cost of a repaint is decided */
	if (!rectsIntersectPx(vx, vy, vx + vw, vy + vh, dx0, dy0, dx1, dy1)) {
		return;
	}
	gViewsDrawn++;
	ctx.pushFrame(vx, vy, vx + vw, vy + vh);
	/* THE DAMAGE IS THE ERASE'S CONTRACT. The region was just cleared to
	 * the background, so EVERY view intersecting it has to draw - a view
	 * skipped here would leave a HOLE, not a stale pixel. That is why this
	 * is deliberately NOT gated on needsDisplay(): the flag is the app's
	 * record of what it changed, the damage rect is the renderer's. What
	 * the view is told is the part of itself the damage covers. */
	int ix0 = vx > dx0 ? vx : dx0;
	int iy0 = vy > dy0 ? vy : dy0;
	int ix1 = vx + vw < dx1 ? vx + vw : dx1;
	int iy1 = vy + vh < dy1 ? vy + vh : dy1;
	Rect local{ { (ix0 - vx) / pxPerPt, (iy0 - vy) / pxPerPt },
		    { (ix1 - ix0) / pxPerPt, (iy1 - iy0) / pxPerPt } };

	v->drawRect(local);
	v->clearNeedsDisplay();
	/* children after their superview: painter's order */
	for (View *c : v->subviews()) {
		draw_view(c, ctx, vx, vy, pxPerPt, dx0, dy0, dx1, dy1);
	}
	ctx.popFrame();
}

void
Window::displayIfNeeded()
{
	if (!impl_->open || !impl_->dirty) {
		return;
	}
	bool timing = timingOn("ARGENTUM_PAINT_MS");
	double tPaint0 = timing ? nowMs() : 0.0;
	/* THE DAMAGE, in surface pixels: the whole surface for a window-level
	 * change, otherwise the union of what the views reported. It is both
	 * the clip the pass draws through and the region erased first, and
	 * that is what removes the need for per-view bookkeeping - anything
	 * the erase touched must be redrawn, anything outside it is already
	 * right on screen. */
	int dx0 = 0, dy0 = 0, dx1 = (int) impl_->wPx, dy1 = (int) impl_->hPx;

	if (!impl_->dmgAll) {
		dx0 = impl_->dmgX0;
		dy0 = impl_->dmgY0;
		dx1 = impl_->dmgX1;
		dy1 = impl_->dmgY1;
		if (dx1 <= dx0 || dy1 <= dy0) {
			impl_->dirty = false;	/* nothing changed, nothing to do */
			return;
		}
	}
	Context::Impl ci;

	ci.img = impl_->pimg;
	ci.wPx = impl_->wPx;
	ci.hPx = impl_->hPx;
	ci.pxPerPt = impl_->pxPerPt;
	ci.cx0 = dx0;
	ci.cy0 = dy0;
	ci.cx1 = dx1;
	ci.cy1 = dy1;
	Context ctx;

	ctx.impl_ = &ci;
	gCurrent = &ctx;
	gViewsDrawn = 0;

	double pp = impl_->pxPerPt;
	/* 1. the background over the damage, outward-rounded so no sliver of
	 * the previous frame survives at the edges (the clip keeps it in) */
	double bx0 = std::floor(dx0 / pp), by0 = std::floor(dy0 / pp);
	double bx1 = std::ceil(dx1 / pp), by1 = std::ceil(dy1 / pp);

	ctx.fillRect(Rect{ { bx0, by0 }, { bx1 - bx0, by1 - by0 } }, bg_);
	/* 2. the chrome, when the damage reaches it (the window's own: no
	 * window manager draws it) */
	int chPx = (int) std::ceil(chromeHeightPt() * pp);

	if (style_ == WindowStyle::Titled
	    && rectsIntersectPx(dx0, dy0, dx1, dy1, 0, 0, (int) impl_->wPx,
				chPx)) {
		drawChrome(ctx);
	}
	/* 3. the content tree, from the content origin, damage-limited */
	if (content_) {
		Rect cr = contentRect();

		draw_view(content_, ctx,
			  (int) std::floor(cr.origin.x * pp),
			  (int) std::floor(cr.origin.y * pp), pp, dx0, dy0,
			  dx1, dy1);
	}
	gCurrent = nullptr;
	ctx.impl_ = nullptr;
	double tPaint1 = timing ? nowMs() : 0.0;

	flush();
	impl_->dirty = false;
	if (timing) {
		/* paint = our own rasterisation; flush = the XPutImage to X.
		 * The split is what tells drawing apart from transport. */
		std::printf("ARGENTUM-PAINT paint=%.1f flush=%.1f ms %ldx%ld "
			    "views=%d dmg=%dx%d\n",
			    tPaint1 - tPaint0, nowMs() - tPaint1,
			    (long) impl_->wPx, (long) impl_->hPx, gViewsDrawn,
			    dx1 - dx0, dy1 - dy0);
		std::fflush(stdout);
	}
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
Window::dispatchToContent(const Point &contentPt, const Event &e)
{
	if (!content_) {
		return nullptr;
	}
	Rect cr = contentRect();
	bool log = getenv("ARGENTUM_KEYLOG") != nullptr;

	if (contentPt.x < 0 || contentPt.y < 0 || contentPt.x >= cr.size.w
	    || contentPt.y >= cr.size.h) {
		if (log) {
			const ObjectClass *oc = content_->objectClass();

			std::printf("ARGENTUM-DISPATCH p=%.0f,%.0f REJECTED "
				    "cr=%.0fx%.0f content=%s\n",
				    contentPt.x, contentPt.y, cr.size.w, cr.size.h,
				    oc ? oc->name : "?");
			std::fflush(stdout);
		}
		return nullptr;
	}
	View *hit = content_->hitTest(contentPt);

	if (log) {
		const ObjectClass *oc = content_->objectClass();
		const ObjectClass *hc = hit ? hit->objectClass() : nullptr;

		std::printf("ARGENTUM-DISPATCH p=%.0f,%.0f cr=%.0fx%.0f content=%s "
			    "hit=%s\n",
			    contentPt.x, contentPt.y, cr.size.w, cr.size.h,
			    oc ? oc->name : "?", hc ? hc->name : "(none)");
		std::fflush(stdout);
	}
	return hit;
}

bool
Window::pumpEvent()
{
	if (!impl_->open || !gDpy) {
		return false;
	}
	XEvent ev;

	/* THIS WINDOW'S EVENTS ONLY, and this is load-bearing. A plain
	 * XPending()/XNextEvent() takes whatever is at the head of the queue, so
	 * the FIRST window in the pass swallows every event - including ones
	 * addressed to another window, whose xbutton.x/y are relative to THAT
	 * window. It was invisible while only one window was ever pumped, which is
	 * exactly what runModal() does (it pumps one window), and it silently
	 * breaks the moment a second window exists: a menu, a colour panel.
	 *
	 * XCheckWindowEvent() SEARCHES the queue for an event for this window and
	 * leaves the rest, so a modal loop cannot wedge behind another window's
	 * event the way a peek-and-bail would. */
	if (!XCheckWindowEvent(gDpy, impl_->xwin, kWindowEventMask, &ev)) {
		return false;
	}
	if (!XPending(gDpy)) {
		/* nothing else waiting: let the caller pause */
	}
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
		if (getenv("ARGENTUM_KEYLOG")
		    && (ev.type == ButtonPress || ev.type == ButtonRelease)) {
			/* WHICH WINDOW'S PUMP SAW THE PRESS: the board's or the
			 * menu's. Neither seeing it means the coordinates are wrong. */
			std::printf("ARGENTUM-PUMP xwin=%lu %s\n",
				    (unsigned long) impl_->xwin,
				    ev.type == ButtonPress ? "press" : "release");
			std::fflush(stdout);
		}
		Point winPt = { ev.xbutton.x / pp, ev.xbutton.y / pp };
		bool pressed = (ev.type == ButtonPress);
		bool released = (ev.type == ButtonRelease);
		unsigned int mods = 0;
		int xbutton = ev.xbutton.button ? (int) ev.xbutton.button : 1;
		EventType kind;

		if (ev.xbutton.state & ShiftMask) {
			mods |= ModifierShift;
		}
		if (ev.xbutton.state & ControlMask) {
			mods |= ModifierControl;
		}
		if (ev.xbutton.state & Mod1Mask) {
			mods |= ModifierOption;
		}
		if (ev.xbutton.state & LockMask) {
			mods |= ModifierCapsLock;
		}
		/* the KIND comes from the X event and the button, as Cocoa's does: a
		 * motion with a button held is a DRAG, not a move */
		if (pressed) {
			kind = (xbutton == 3) ? EventType::RightMouseDown
					      : EventType::LeftMouseDown;
		} else if (released) {
			kind = (xbutton == 3) ? EventType::RightMouseUp
					      : EventType::LeftMouseUp;
		} else if (ev.xbutton.state & (Button1Mask | Button3Mask)) {
			kind = EventType::LeftMouseDragged;
		} else {
			kind = EventType::MouseMoved;
		}
		/* the name is Cocoa's; the SPACE is this toolkit's. The point is
		 * converted into the receiving view's space by the dispatch below,
		 * where Cocoa's locationInWindow is window space and its
		 * convertPoint:fromView: does the rest - a later fidelity step. */
		Event me = Event::mouseEvent(kind, winPt, mods,
					     (double) ev.xbutton.time / 1000.0, 0,
					     0, 1, 0.0);

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

				if (me.buttonNumber() == 0) {
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
				if (ev.xbutton.button == 1 && me.buttonNumber() == 0) {
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

			focusFromClick(hit);
			pressView_ = hit;
			if (hit) {
				hit->setTrackingMouse(true);
			}
			/* offer it to the view, then up the parent chain */
			for (View *v = hit; v; v = v->superview()) {
				me.setLocationInWindow(Point{ contentPt.x
						     - v->rectInWindow(
							       Rect{ { 0, 0 },
								     { 0, 0 } })
							       .origin.x,
						     contentPt.y
						     - v->rectInWindow(
							       Rect{ { 0, 0 },
								     { 0, 0 } })
							       .origin.y });

				if (v->mouseDown(me)) {
					break;
				}
			}
			return true;
		}
		if (me.buttonNumber() != 1 && pressView_ == nullptr) {
			return true;
		}
		/* drags and releases go to the view that captured the press */
		if (pressView_) {
			View *v = pressView_;
			Rect off = v->rectInWindow(Rect{ { 0, 0 }, { 0, 0 } });

			me.setLocationInWindow(Point{ contentPt.x - off.origin.x,
					     contentPt.y - off.origin.y });
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

	case KeyPress: {
		/* X's key events carry a keycode; the KEYSYM and the TEXT come
		 * from the server's keymap (Xfb loads the host's), so the
		 * toolkit never has to know a layout */
		char buf[32] = { 0 };
		KeySym ks = NoSymbol;
		Status st = 0;
		int n = XLookupString((XKeyEvent *) &ev, buf,
				      (int) sizeof(buf) - 1, &ks, nullptr);

		(void) st;
		/* COCOA'S RULE: AN EVENT KEEPS ITS CHARACTERS. This block used to
		 * erase them for Return, Tab and the arrows and set a bool for each
		 * instead, which is why every control asked what key it was. The
		 * binding table (Responder::commandForEvent) reads the characters
		 * now, and the keysyms for the keys that type nothing. */
		unsigned int flags = 0;

		if (ev.xkey.state & ShiftMask) {
			flags |= ModifierShift;
		}
		if (ev.xkey.state & ControlMask) {
			flags |= ModifierControl;
		}
		if (ev.xkey.state & Mod1Mask) {
			flags |= ModifierOption;
		}
		if (ev.xkey.state & LockMask) {
			flags |= ModifierCapsLock;
		}
		if (ev.xkey.state & Mod4Mask) {
			flags |= ModifierCommand;
		}
		/* XLookupString gives the MODIFIED text; Cocoa separates the two,
		 * and the producing side of that split is not here yet, so both
		 * carry the same string for now. */
		Event ke = Event::keyEvent(
			EventType::KeyDown,
			Point{ (double) ev.xkey.x, (double) ev.xkey.y }, flags,
			(double) ev.xkey.time / 1000.0, 0, buf, buf, false,
			(unsigned short) ks);

		dispatchKey(ke);
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

/* the keyboard's dispatch: Tab walks the focus, everything else goes to
 * the first responder and, if it does not handle it, up the chain */
void
Window::dispatchKey(const Event &ke)
{
	const char *cmd = Responder::commandForEvent(ke);
	/* ARGENTUM_KEYLOG: the key path's instrument, the same shape that settled
	 * the button hit test (ARGENTUM_HITLOG) and the DatePicker
	 * (ARGENTUM-DATE). It answers the whole question in one run: what the
	 * event carries, what the table makes of it, WHO the first responder is,
	 * and whether that responder took it. */
	bool log = getenv("ARGENTUM_KEYLOG") != nullptr;

	if (log) {
		std::printf("ARGENTUM-KEY chars=\"%s\" keysym=%u cmd=%s\n",
			    ke.characters(), (unsigned) ke.keyCode(),
			    cmd ? cmd : "(text)");
		std::fflush(stdout);
	}

	/* Tab moves the focus, and Cocoa reaches that through the command
	 * chain: an unhandled insertTab: ends editing and the window takes it.
	 * This toolkit has no field editors yet, so the window answers it here. */
	if (cmd && std::strcmp(cmd, "insertTab") == 0) {
		advanceFirstResponder(
			(ke.modifierFlags() & ModifierShift) != 0);
		return;
	}
	for (View *v = firstResponder_; v; v = v->nextResponder()) {
		bool took = v->keyDown(ke);

		if (log) {
			const ObjectClass *oc = v->objectClass();

			std::printf("ARGENTUM-KEYTRY %s took=%d\n",
				    oc ? oc->name : "?", took ? 1 : 0);
			std::fflush(stdout);
		}
		if (took) {
			return;
		}
	}
	if (log) {
		std::printf("ARGENTUM-KEY-UNHANDLED\n");
		std::fflush(stdout);
	}
}

bool
Window::makeFirstResponder(View *v)
{
	if (v == firstResponder_) {
		return true;
	}
	if (v == nullptr) {
		if (firstResponder_) {
			/* its caret goes with the focus: the outgoing view has to be
			 * repainted, or it keeps drawing a cursor it no longer has */
			firstResponder_->setNeedsDisplay();
		}
		firstResponder_ = nullptr;
		return true;
	}
	/* the view must be IN this window's tree (walk up to the content
	 * view) and must want the job */
	View *root = v;

	while (root->superview()) {
		root = root->superview();
	}
	if (root != content_) {
		return false;
	}
	if (!v->acceptsFirstResponder()) {
		return false;
	}
	/* BOTH ENDS ARE REPAINTED. Marking only the view that GAINS the focus
	 * left the one that lost it drawing its caret, so clicking out of a
	 * field left a stray vertical line in it - the caret the field no longer
	 * had. advanceFirstResponder() (the Tab path) always marked the outgoing
	 * view; this path did not. */
	if (firstResponder_ && firstResponder_ != v) {
		firstResponder_->setNeedsDisplay();
	}
	if (std::getenv("ARGENTUM_CARET_DEBUG")) {
		std::fprintf(stderr, "ARGENTUM-FOCUS -> %p\n", (void *) v);
	}
	firstResponder_ = v;
	v->setNeedsDisplay();
	return true;
}

bool
Window::advanceFirstResponder(bool backwards)
{
	/* a pre-order walk of the content tree, the same order hit-testing
	 * and drawing use, so Tab visits views in the order they are built */
	std::vector<View *> order;

	collectResponders(content_, order);
	if (order.empty()) {
		return false;
	}
	int at = -1;

	for (size_t i = 0; i < order.size(); i++) {
		if (order[i] == firstResponder_) {
			at = (int) i;
			break;
		}
	}
	int n = (int) order.size();
	int next = backwards ? (at <= 0 ? n - 1 : at - 1)
			     : (at < 0 || at + 1 >= n ? 0 : at + 1);

	if (firstResponder_) {
		firstResponder_->setNeedsDisplay();
	}
	return makeFirstResponder(order[(size_t) next]);
}

void
Window::collectResponders(View *v, std::vector<View *> &out)
{
	if (!v || v->isHidden()) {
		return;
	}
	if (v->acceptsFirstResponder()) {
		out.push_back(v);
	}
	for (View *c : v->subviews()) {
		collectResponders(c, out);
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
	int x = 0, y = 0, w = (int) impl_->wPx, h = (int) impl_->hPx;

	if (!impl_->dmgAll) {
		x = impl_->dmgX0;
		y = impl_->dmgY0;
		w = impl_->dmgX1 - x;
		h = impl_->dmgY1 - y;
	}
	if (timingOn("ARGENTUM_PAINT_MS")) {
		/* what actually goes to the server, per frame: the whole point
		 * of narrowing the damage is this number */
		std::printf("ARGENTUM-PUSH %dx%d at %d,%d%s\n", w, h, x, y,
			    impl_->shmAttached ? " shm" : " noshm");
		std::fflush(stdout);
	}
	if (w <= 0 || h <= 0) {
		XFlush(gDpy);		/* nothing changed on screen */
		return;
	}
	GC gc = XCreateGC(gDpy, impl_->xwin, 0, nullptr);

	if (gc) {
		if (impl_->shmAttached) {
			XShmPutImage(gDpy, impl_->xwin, gc, impl_->ximg,
				     x, y, x, y, (unsigned int) w,
				     (unsigned int) h, False);
		} else {
			XPutImage(gDpy, impl_->xwin, gc, impl_->ximg, x, y,
				  x, y, (unsigned int) w, (unsigned int) h);
		}
		XFreeGC(gDpy, gc);
	}
	/* the sync is what makes the shared buffer safe to reuse: the server
	 * reads it, and only this says it is done */
	XSync(gDpy, False);
	impl_->dmgAll = false;
	impl_->dmgX0 = impl_->dmgY0 = impl_->dmgX1 = impl_->dmgY1 = 0;
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
