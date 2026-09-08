/* argentum/graphics.cpp — S1.2 pixman offscreen context + shape set.
 *
 * BitmapImage owns a PIXMAN_x8r8g8b8 offscreen surface (the
 * NSBitmapImageRep analog). GraphicsContext draws the chrome shape set
 * into it through pixman (the NSGraphicsContext analog): solid rects,
 * rounded rects, two-stop linear/radial gradients, 1px lines — the
 * closed set UI chrome needs (plan §3). Pixman gives anti-aliased
 * coverage; flush() blits the finished bitmap to a Window with one
 * core-protocol XPutImage (same 0x00RRGGBB 32-bpp layout fill() and
 * drawText() use, so the x8r8g8b8 data goes straight to X).
 *
 * Rounded rects are not a pixman primitive: a shape mask (PIXMAN_a8)
 * is painted with the four quarter-disc corners (radial gradient,
 * hard 0..radius stop) plus the central cross, then the fill color is
 * composited over the destination through that mask. Radius is capped
 * at half the smaller side, so the mask is always well-formed.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace argentum {

#define PI 3.14159265358979323846

/* 0xRRGGBB -> premultiplied 16-bit pixman color (alpha 0xffff). */
static pixman_color_t
pixcolor(std::uint32_t rgb)
{
	pixman_color_t c;

	c.red = (uint16_t)((((rgb >> 16) & 0xff) * 0xffff) / 255);
	c.green = (uint16_t)((((rgb >> 8) & 0xff) * 0xffff) / 255);
	c.blue = (uint16_t)(((rgb & 0xff) * 0xffff) / 255);
	c.alpha = 0xffff;
	return c;
}

/* ------------------------------------------------------------------ */
/* BitmapImage                                                          */

struct bitmap_destroy_ctx {
};

BitmapImage::BitmapImage(unsigned int width, unsigned int height)
{
	impl_ = new Impl;
	impl_->width = width;
	impl_->height = height;
	impl_->img = pixman_image_create_bits(PIXMAN_x8r8g8b8,
					     (int) width, (int) height,
					     nullptr, 0);
	if (!impl_->img) {
		std::fprintf(stderr, "ARGENTUM: pixman surface %ux%u failed\n",
			     width, height);
	}
}

BitmapImage::~BitmapImage()
{
	if (impl_->img) {
		pixman_image_unref(impl_->img);
	}
	delete impl_;
}

unsigned int
BitmapImage::width() const
{
	return impl_->width;
}

unsigned int
BitmapImage::height() const
{
	return impl_->height;
}

/* ------------------------------------------------------------------ */
/* GraphicsContext                                                      */

/* Convert 0xRRGGBB to a pixman 32-bit a8r8g8b8 pixel (opaque). */
static std::uint32_t
argb_pixel(std::uint32_t rgb)
{
	return 0xff000000u | (rgb & 0xffffffu);
}

GraphicsContext::GraphicsContext(BitmapImage &image)
{
	impl_ = new Impl;
	impl_->bitmap = &image;
}

GraphicsContext::~GraphicsContext()
{
	delete impl_;
}

/* Composite a solid color over dest[x..x+w)[y..y+h) through an a8 mask
 * image (or NULL for a plain rectangle fill). The mask, when given, is
 * sampled 1:1 from (mask_x, mask_y). */
static void
composite_solid(pixman_image_t *dest, std::uint32_t rgb,
		pixman_image_t *mask, int mask_x, int mask_y,
		int x, int y, int w, int h)
{
	pixman_color_t c = pixcolor(rgb);
	pixman_image_t *src = pixman_image_create_solid_fill(&c);

	if (!src) {
		return;
	}
	pixman_image_composite32(PIXMAN_OP_OVER, src, mask, dest,
				 0, 0, mask_x, mask_y, x, y, w, h);
	pixman_image_unref(src);
}

void
GraphicsContext::fillRect(int x, int y, unsigned int w, unsigned int h,
			  std::uint32_t rgb)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;
	int r = (int) impl_->bitmap->width();
	int s = (int) impl_->bitmap->height();

	if (!b->img || w == 0 || h == 0) {
		return;
	}
	/* clip to the surface */
	if (x < 0) {
		w = (unsigned int) (x + (int) w);
		x = 0;
	}
	if (y < 0) {
		h = (unsigned int) (y + (int) h);
		y = 0;
	}
	if ((int) w > r - x) {
		w = (unsigned int) (r - x);
	}
	if ((int) h > s - y) {
		h = (unsigned int) (s - y);
	}
	if ((int) w <= 0 || (int) h <= 0) {
		return;
	}
	composite_solid(b->img, rgb, nullptr, 0, 0, x, y,
			(int) w, (int) h);
}

void
GraphicsContext::fillRoundedRect(int x, int y, unsigned int w,
				 unsigned int h, unsigned int radius,
				 std::uint32_t rgb)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;

	if (!b->img || w == 0 || h == 0) {
		return;
	}
	/* radius capped at half the smaller side */
	unsigned int half = (w < h ? w : h) / 2;
	if (radius > half) {
		radius = half;
	}
	if (radius == 0) {
		fillRect(x, y, w, h, rgb);
		return;
	}
	/* clip to the surface */
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + (int) w;
	int y1 = y + (int) h;
	int bw = (int) impl_->bitmap->width();
	int bh = (int) impl_->bitmap->height();
	if (x0 >= bw || y0 >= bh || x1 <= 0 || y1 <= 0) {
		return;
	}
	if (x1 > bw) {
		x1 = bw;
	}
	if (y1 > bh) {
		y1 = bh;
	}

	/* Mask: a8, sized to the rounded rect, cleared to 0. The rounded
	 * rect is triangulated (center fan over the perimeter, arcs
	 * sampled finely) and pixman rasterizes the triangles with AA
	 * coverage into the mask; the fill color is then composited over
	 * the destination through the mask. This is pixman's native
	 * mechanism for non-rectangular shapes (plan §3: "rounded rects
	 * ... are a thin layer on top"). */
	int mw = x1 - x0;
	int mh = y1 - y0;
	pixman_image_t *mask = pixman_image_create_bits(PIXMAN_a8,
							 mw, mh, nullptr, 0);
	if (!mask) {
		return;
	}
	int ox = x - x0;	/* shape origin in mask space */
	int oy = y - y0;
	int rr = (int) radius;

	/* perimeter vertices: the four straight edges + each arc sampled
	 * at ARC_STEPS per quadrant. Center fan: every vertex pairs with
	 * the center -> all triangles are inside the convex shape. */
#define ARC_STEPS 12
	double cx = ox + (double) w / 2.0;
	double cy = oy + (double) h / 2.0;
	struct { double px, py; } per[4 * (ARC_STEPS + 2)];
	int nv = 0;
	int q;

	/* helper: append the arc of a corner. Corner circle centers at
	 * the inner corners; arcs run clockwise from the top/right side. */
#define APPEND(cx_, cy_, a0, a1)					\
	do {								\
		int s_;							\
		for (s_ = 0; s_ <= ARC_STEPS; s_++) {			\
			double a = (a0) + ((a1) - (a0)) * s_ / ARC_STEPS; \
			per[nv].px = (cx_) + rr * __builtin_cos(a);	\
			per[nv].py = (cy_) + rr * __builtin_sin(a);	\
			nv++;						\
		}							\
	} while (0)

	/* top-left corner: circle center (ox+rr, oy+rr); arc from 180deg
	 * (left edge) to 270deg (top edge), i.e. the corner round. */
	APPEND(ox + rr, oy + rr, PI, PI * 1.5);
	/* top-right: center (ox+w-rr, oy+rr); 270deg -> 360deg */
	APPEND(ox + (int) w - rr, oy + rr, PI * 1.5, PI * 2.0);
	/* bottom-right: center (ox+w-rr, oy+h-rr); 0deg -> 90deg */
	APPEND(ox + (int) w - rr, oy + (int) h - rr, 0, PI * 0.5);
	/* bottom-left: center (ox+rr, oy+h-rr); 90deg -> 180deg */
	APPEND(ox + rr, oy + (int) h - rr, PI * 0.5, PI);
#undef APPEND

	/* center fan — closed: nv triangles, each (center, per[i],
	 * per[(i+1) % nv]) so the straight left/right/top/bottom sides
	 * between the arc endpoints are covered too. */
	pixman_triangle_t *tris = (pixman_triangle_t *)
		std::calloc((size_t) (nv > 0 ? nv : 0),
			    sizeof(pixman_triangle_t));
	if (!tris) {
		pixman_image_unref(mask);
		return;
	}
	int i;
	for (i = 0; i < nv; i++) {
		int j = (i + 1) % nv;

		tris[i].p1.x = pixman_double_to_fixed(cx);
		tris[i].p1.y = pixman_double_to_fixed(cy);
		tris[i].p2.x = pixman_double_to_fixed(per[i].px);
		tris[i].p2.y = pixman_double_to_fixed(per[i].py);
		tris[i].p3.x = pixman_double_to_fixed(per[j].px);
		tris[i].p3.y = pixman_double_to_fixed(per[j].py);
	}
	pixman_color_t white = { 0xffff, 0xffff, 0xffff, 0xffff };
	pixman_image_t *wsrc = pixman_image_create_solid_fill(&white);
	if (wsrc) {
		/* rasterize into the mask: triangles accumulate coverage
		 * (PIXMAN_OP_ADD keeps overlaps additive). */
		pixman_composite_triangles(PIXMAN_OP_ADD, wsrc, mask,
					   PIXMAN_a8, 0, 0, 0, 0,
					   nv, tris);
		pixman_image_unref(wsrc);
	}
	std::free(tris);

	/* paint the fill color over the destination through the mask.
	 * mask pixel 0 corresponds to dest (x0,y0): composite at dest
	 * (x0,y0) with mask_x=0. */
	composite_solid(b->img, rgb, mask, 0, 0, x0, y0, mw, mh);
	pixman_image_unref(mask);
#undef ARC_STEPS
}

void
GraphicsContext::fillLinearGradient(int x, int y, unsigned int w,
				    unsigned int h, std::uint32_t rgb0,
				    std::uint32_t rgb1, bool vertical)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;

	if (!b->img || w == 0 || h == 0) {
		return;
	}
	/* clip to the surface */
	int r = (int) impl_->bitmap->width();
	int s = (int) impl_->bitmap->height();
	if (x < 0) {
		w = (unsigned int) (x + (int) w);
		x = 0;
	}
	if (y < 0) {
		h = (unsigned int) (y + (int) h);
		y = 0;
	}
	if ((int) w > r - x) {
		w = (unsigned int) (r - x);
	}
	if ((int) h > s - y) {
		h = (unsigned int) (s - y);
	}
	if ((int) w <= 0 || (int) h <= 0) {
		return;
	}

	/* stops 0 and 1.0; p1..p2 span the gradient axis */
	pixman_gradient_stop_t stops[2];
	pixman_color_t c0 = pixcolor(rgb0);
	pixman_color_t c1 = pixcolor(rgb1);
	stops[0].x = 0;
	stops[0].color = c0;
	stops[1].x = pixman_fixed_1;
	stops[1].color = c1;

	pixman_point_fixed_t p1, p2;
	pixman_int_to_fixed(x);	/* (unused) */
	if (vertical) {
		p1.x = pixman_int_to_fixed(x);
		p1.y = pixman_int_to_fixed(y);
		p2.x = pixman_int_to_fixed(x);
		p2.y = pixman_int_to_fixed(y + (int) h);
	} else {
		p1.x = pixman_int_to_fixed(x);
		p1.y = pixman_int_to_fixed(y);
		p2.x = pixman_int_to_fixed(x + (int) w);
		p2.y = pixman_int_to_fixed(y);
	}

	pixman_image_t *grad = pixman_image_create_linear_gradient(
		&p1, &p2, stops, 2);
	if (!grad) {
		return;
	}
	pixman_image_set_repeat(grad, PIXMAN_REPEAT_PAD);
	pixman_image_composite32(PIXMAN_OP_SRC, grad, nullptr, b->img,
				 x, y, 0, 0, x, y, (int) w, (int) h);
	pixman_image_unref(grad);
}

void
GraphicsContext::fillRadialGradient(int cx, int cy, unsigned int radius,
				    std::uint32_t rgb0, std::uint32_t rgb1)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;

	if (!b->img || radius == 0) {
		return;
	}
	pixman_gradient_stop_t stops[2];
	pixman_color_t c0 = pixcolor(rgb0);
	pixman_color_t c1 = pixcolor(rgb1);
	stops[0].x = 0;
	stops[0].color = c0;
	stops[1].x = pixman_fixed_1;
	stops[1].color = c1;

	pixman_point_fixed_t pc = { pixman_int_to_fixed(cx),
				    pixman_int_to_fixed(cy) };
	pixman_image_t *grad = pixman_image_create_radial_gradient(
		&pc, &pc, 0, pixman_int_to_fixed((int) radius), stops, 2);
	if (!grad) {
		return;
	}
	pixman_image_set_repeat(grad, PIXMAN_REPEAT_PAD);

	/* clip the composite to the bounding box of the disc */
	int x0 = cx - (int) radius;
	int y0 = cy - (int) radius;
	int w = (int) radius * 2;
	int h = w;
	pixman_box32_t bb = { x0, y0, x0 + w, y0 + h };
	pixman_region32_t clip;
	pixman_region32_init_rect(&clip, x0, y0, (unsigned int) w,
				  (unsigned int) h);
	pixman_image_set_clip_region32(b->img, &clip);
	pixman_region32_fini(&clip);
	pixman_image_composite32(PIXMAN_OP_SRC, grad, nullptr, b->img,
				 x0, y0, 0, 0, x0, y0, w, h);
	pixman_image_unref(grad);

	/* clear the clip for subsequent draws */
	pixman_region32_t all;
	pixman_region32_init_rect(&all, 0, 0,
				  (unsigned int) impl_->bitmap->width(),
				  (unsigned int) impl_->bitmap->height());
	pixman_image_set_clip_region32(b->img, &all);
	pixman_region32_fini(&all);
}

void
GraphicsContext::drawLine(int x0, int y0, int x1, int y1, std::uint32_t rgb)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;

	if (!b->img) {
		return;
	}
	if (x0 == x1 && y0 == y1) {
		/* single pixel */
		fillRect(x0, y0, 1, 1, rgb);
		return;
	}
	/* 1px line as two triangles (a thin quad of width 1.0 with AA).
	 * Perpendicular unit vector, half-width 0.5. */
	double dx = (double) (x1 - x0);
	double dy = (double) (y1 - y0);
	double len = dx * dx + dy * dy;
	if (len == 0) {
		return;
	}
	len = __builtin_sqrt(len);
	double px = -dy / len * 0.5;
	double py = dx / len * 0.5;

	pixman_triangle_t tris[2];
	/* quad corners (order: p0+p, p0-p, p1-p, p1+p) -> two tris */
	double q[4][2] = {
		{ x0 + px, y0 + py }, { x0 - px, y0 - py },
		{ x1 - px, y1 - py }, { x1 + px, y1 + py },
	};
	tris[0].p1 = pixman_point_fixed_t { pixman_double_to_fixed(q[0][0]),
					    pixman_double_to_fixed(q[0][1]) };
	tris[0].p2 = pixman_point_fixed_t { pixman_double_to_fixed(q[1][0]),
					    pixman_double_to_fixed(q[1][1]) };
	tris[0].p3 = pixman_point_fixed_t { pixman_double_to_fixed(q[2][0]),
					    pixman_double_to_fixed(q[2][1]) };
	tris[1].p1 = pixman_point_fixed_t { pixman_double_to_fixed(q[0][0]),
					    pixman_double_to_fixed(q[0][1]) };
	tris[1].p2 = pixman_point_fixed_t { pixman_double_to_fixed(q[2][0]),
					    pixman_double_to_fixed(q[2][1]) };
	tris[1].p3 = pixman_point_fixed_t { pixman_double_to_fixed(q[3][0]),
					    pixman_double_to_fixed(q[3][1]) };

	pixman_color_t c = pixcolor(rgb);
	pixman_image_t *src = pixman_image_create_solid_fill(&c);
	if (!src) {
		return;
	}
	pixman_composite_triangles(PIXMAN_OP_OVER, src, b->img, PIXMAN_a8,
				   0, 0, 0, 0, 2, tris);
	pixman_image_unref(src);
}

void
GraphicsContext::flush(Window &window, int x, int y)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;

	if (!b->img) {
		return;
	}
	Window::Impl *w = window.impl_;
	if (!w->dpy || !w->xwin) {
		return;
	}
	int screen = DefaultScreen(w->dpy);
	Visual *vis = DefaultVisual(w->dpy, screen);
	unsigned int depth = (unsigned int) DefaultDepth(w->dpy, screen);

	/* 32-bpp x8r8g8b8 words == the XRGB8888 ZPixmap layout fill()
	 * blits; XPutImage straight from the pixman data pointer. */
	pixman_image_t *img = b->img;
	unsigned int bw = impl_->bitmap->width();
	unsigned int bh = impl_->bitmap->height();
	XImage *ximg = XCreateImage(w->dpy, vis, depth, ZPixmap, 0,
				    (char *) pixman_image_get_data(img),
				    bw, bh, 32, pixman_image_get_stride(img));
	if (!ximg) {
		return;
	}
	GC gc = XCreateGC(w->dpy, w->xwin, 0, nullptr);
	if (gc) {
		XPutImage(w->dpy, w->xwin, gc, ximg, 0, 0, x, y, bw, bh);
		XFreeGC(w->dpy, gc);
	}
	/* XDestroyImage frees ximg->data — but that is pixman's buffer,
	 * which the BitmapImage still owns. Detach the data pointer
	 * first so only the XImage wrapper is freed (text.cpp keeps the
	 * box and frees via XDestroyImage, so it must NOT detach). */
	ximg->data = nullptr;
	XDestroyImage(ximg);
	XSync(w->dpy, False);
}

} /* namespace argentum */
