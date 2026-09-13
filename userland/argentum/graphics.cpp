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
#include <cstring>

#include <sys/mman.h>

/* Surfaces at least this big get their pixels from a shared anonymous
 * mapping instead of pixman's own allocator (see the constructor).
 * Window backings are hundreds of KB; the per-draw masks and ramps are a
 * few KB, and they must stay on the cheap path — one mmap syscall per
 * mask would cost more than it saves. */
#define BITMAP_SHARED_MIN_BYTES (64u * 1024u)

namespace argentum {

#define PI 3.14159265358979323846
/* arc sample density for the rounded-rect perimeter fan (per quadrant) */
#define ARC_STEPS 12

/* clip a rect to the surface; returns false when fully outside */
static bool clip_rect(int bw, int bh, int x, int y, int w, int h,
		      int *x0, int *y0, int *x1, int *y1);

/* The current frame state (translate origin + clip). Member functions
 * copy it out of the private Impl and hand it to map_frame() below. */
struct frame_state {
	int ox, oy;
	bool clipOn;
	int clipX, clipY, clipW, clipH;
};

/* Map a shape rect (x,y,w,h), given in the current frame's translated
 * space, through the frame origin + clip into surface coordinates.
 * On true, *x0..*y1 hold the clipped exclusive bounds (already within
 * the bitmap) and the shape was not empty. On false the shape is
 * fully clipped away. clip_rect() (surface) is then applied by the
 * caller as before. */
static bool
map_frame(const frame_state &g, int x, int y, int w, int h,
	  int *x0, int *y0, int *x1, int *y1)
{
	x += g.ox;
	y += g.oy;
	*x0 = x;
	*y0 = y;
	*x1 = x + w;
	*y1 = y + h;
	if (g.clipOn) {
		/* clip stored in SURFACE space (fixed at clipToRect) */
		int cx1 = g.clipX + g.clipW;
		int cy1 = g.clipY + g.clipH;

		if (*x0 < g.clipX) {
			*x0 = g.clipX;
		}
		if (*y0 < g.clipY) {
			*y0 = g.clipY;
		}
		if (*x1 > cx1) {
			*x1 = cx1;
		}
		if (*y1 > cy1) {
			*y1 = cy1;
		}
	}
	return *x1 > *x0 && *y1 > *y0;
}


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
	/* SECURITY (audit 2026-09): the one allocation every surface goes
	 * through, so the bound lives here as well as at the wire entry
	 * (Window::handleResize).  `width * height * 4` must not wrap: the
	 * product is computed in 64 bits, and a surface over the cap is
	 * refused outright (a null surface, which every draw already
	 * tolerates) rather than allocated small and written past.  A
	 * caller that clamps at the wire never reaches this; a caller that
	 * does not still cannot overflow the heap. */
	if (width == 0 || height == 0 ||
	    width > ARGENTUM_MAX_WINDOW_PX || height > ARGENTUM_MAX_WINDOW_PX ||
	    (std::uint64_t) width * height >
		    (std::uint64_t) ARGENTUM_MAX_WINDOW_PX * ARGENTUM_MAX_WINDOW_PX) {
		std::fprintf(stderr,
			     "ARGENTUM: refusing a %ux%u surface (bound)\n",
			     width, height);
		impl_->img = nullptr;
		return;
	}
	/* WHY A SHARED MAPPING (measured 2026-09): a MAPPED_SHARED vma is
	 * skipped when the kernel marks pages copy-on-write for a fork
	 * (create_pml4_64 skips MAP_SHARED), so a window's pixels never take
	 * the COW fault path. That path cost ~250us PER 4KB PAGE and fired
	 * on every repaint of a heap-backed surface: instrumenting
	 * mm/fault.c gave cow=4999 of the first 5000 user faults, and the
	 * backdrop fill of a full window measured 220-610ms where the same
	 * fill over already-faulted pages measured 0-10ms. Window backings
	 * are the surfaces that pay it; masks and gradient ramps are small
	 * and are rebuilt constantly, so they keep pixman's allocator. */
	unsigned long bytes = (unsigned long) width * 4u * height;

	if (bytes >= BITMAP_SHARED_MIN_BYTES) {
		void *p = mmap(nullptr, (size_t) bytes,
			       PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_ANONYMOUS, -1, 0);

		if (p != MAP_FAILED) {
			impl_->mapBuf = p;
			impl_->mapLen = bytes;
			impl_->img = pixman_image_create_bits(PIXMAN_x8r8g8b8,
							     (int) width,
							     (int) height,
							     (uint32_t *) p,
							     (int) width * 4);
		}
	}
	if (!impl_->img) {
		/* small surfaces, and the fallback when mmap is unavailable:
		 * pixman allocates (and owns) the buffer */
		impl_->img = pixman_image_create_bits(PIXMAN_x8r8g8b8,
						     (int) width, (int) height,
						     nullptr, 0);
	}
	if (!impl_->img) {
		std::fprintf(stderr, "ARGENTUM: pixman surface %ux%u failed\n",
			     width, height);
	}
}

BitmapImage::~BitmapImage()
{
	/* pixman_unref first: it does not own an externally provided buffer,
	 * and it must not be left pointing at memory we are about to unmap */
	if (impl_->img) {
		pixman_image_unref(impl_->img);
	}
	if (impl_->mapBuf) {
		munmap(impl_->mapBuf, (size_t) impl_->mapLen);
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

void
GraphicsContext::save()
{
	GraphicsContext::Impl::SavedFrame f;

	f.ox = impl_->ox;
	f.oy = impl_->oy;
	f.clipOn = impl_->clipOn;
	f.clipX = impl_->clipX;
	f.clipY = impl_->clipY;
	f.clipW = impl_->clipW;
	f.clipH = impl_->clipH;
	impl_->stack.push_back(f);
}

void
GraphicsContext::restore()
{
	if (impl_->stack.empty()) {
		return;
	}
	GraphicsContext::Impl::SavedFrame f = impl_->stack.back();

	impl_->stack.pop_back();
	impl_->ox = f.ox;
	impl_->oy = f.oy;
	impl_->clipOn = f.clipOn;
	impl_->clipX = f.clipX;
	impl_->clipY = f.clipY;
	impl_->clipW = f.clipW;
	impl_->clipH = f.clipH;
}

void
GraphicsContext::translate(int dxPx, int dyPx)
{
	impl_->ox += dxPx;
	impl_->oy += dyPx;
}

void
GraphicsContext::clipToRect(int xPx, int yPx, unsigned int wPx,
			    unsigned int hPx)
{
	/* the clip is stored in SURFACE space, fixed at set time (later
	 * translates move drawing, not the clip): convert local -> */
	int sx = xPx + impl_->ox;
	int sy = yPx + impl_->oy;
	int cx1 = sx + (int) wPx;
	int cy1 = sy + (int) hPx;

	if (!impl_->clipOn) {
		impl_->clipX = sx;
		impl_->clipY = sy;
		impl_->clipW = (int) wPx;
		impl_->clipH = (int) hPx;
		impl_->clipOn = true;
		return;
	}
	/* intersect with the current clip */
	int x0 = impl_->clipX > sx ? impl_->clipX : sx;
	int y0 = impl_->clipY > sy ? impl_->clipY : sy;
	int x1 = (impl_->clipX + impl_->clipW) < cx1 ?
		(impl_->clipX + impl_->clipW) : cx1;
	int y1 = (impl_->clipY + impl_->clipH) < cy1 ?
		(impl_->clipY + impl_->clipH) : cy1;

	impl_->clipX = x0;
	impl_->clipY = y0;
	impl_->clipW = x1 > x0 ? x1 - x0 : 0;
	impl_->clipH = y1 > y0 ? y1 - y0 : 0;
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

/* Build the a8 coverage mask of a rounded rect via a center fan over the
 * perimeter (arcs sampled at ARC_STEPS per quadrant), the same geometry
 * fillRoundedRect uses. The mask is `w` x `h` with the shape spanning
 * the full mask (origin at 0,0); pixman rasterizes the triangles with
 * AA coverage. Returns NULL on allocation failure. The caller owns the
 * mask. Radius 0 falls back to a full-opaque mask (callers route that
 * case to fillRect themselves). */
static pixman_image_t *
rounded_mask(unsigned int w, unsigned int h, unsigned int radius)
{
	pixman_image_t *mask;
	int rr = (int) radius;
	double cx, cy;
	struct { double px, py; } per[4 * (ARC_STEPS + 2)];
	int nv = 0;

	mask = pixman_image_create_bits(PIXMAN_a8, (int) w, (int) h,
					nullptr, 0);
	if (!mask) {
		return nullptr;
	}
	cx = (double) w / 2.0;
	cy = (double) h / 2.0;

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

	/* top-left corner: circle center (rr, rr); arc from 180deg
	 * (left edge) to 270deg (top edge), i.e. the corner round. */
	APPEND(rr, rr, PI, PI * 1.5);
	/* top-right: center (w-rr, rr); 270deg -> 360deg */
	APPEND((int) w - rr, rr, PI * 1.5, PI * 2.0);
	/* bottom-right: center (w-rr, h-rr); 0deg -> 90deg */
	APPEND((int) w - rr, (int) h - rr, 0, PI * 0.5);
	/* bottom-left: center (rr, h-rr); 90deg -> 180deg */
	APPEND(rr, (int) h - rr, PI * 0.5, PI);
#undef APPEND

	/* center fan — closed: nv triangles, each (center, per[i],
	 * per[(i+1) % nv]) so the straight left/right/top/bottom sides
	 * between the arc endpoints are covered too. */
	{
		pixman_triangle_t *tris = (pixman_triangle_t *)
			std::calloc((size_t) (nv > 0 ? nv : 0),
				    sizeof(pixman_triangle_t));
		int i;

		if (!tris) {
			pixman_image_unref(mask);
			return nullptr;
		}
		for (i = 0; i < nv; i++) {
			int j = (i + 1) % nv;

			tris[i].p1.x = pixman_double_to_fixed(cx);
			tris[i].p1.y = pixman_double_to_fixed(cy);
			tris[i].p2.x = pixman_double_to_fixed(per[i].px);
			tris[i].p2.y = pixman_double_to_fixed(per[i].py);
			tris[i].p3.x = pixman_double_to_fixed(per[j].px);
			tris[i].p3.y = pixman_double_to_fixed(per[j].py);
		}
		{
			pixman_color_t white =
				{ 0xffff, 0xffff, 0xffff, 0xffff };
			pixman_image_t *wsrc =
				pixman_image_create_solid_fill(&white);

			if (wsrc) {
				/* rasterize into the mask: triangles
				 * accumulate coverage (PIXMAN_OP_ADD keeps
				 * overlaps additive). */
				pixman_composite_triangles(PIXMAN_OP_ADD,
							   wsrc, mask,
							   PIXMAN_a8, 0, 0,
							   0, 0, nv, tris);
				pixman_image_unref(wsrc);
			}
		}
		std::free(tris);
	}
	return mask;
}

void
GraphicsContext::fillRect(int x, int y, unsigned int w, unsigned int h,
			  std::uint32_t rgb)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;
	int x0, y0, x1, y1;

	if (!b->img || w == 0 || h == 0) {
		return;
	}
	/* frame transform + clip, then the surface */
	frame_state fs = { impl_->ox, impl_->oy, impl_->clipOn,
			   impl_->clipX, impl_->clipY,
			   impl_->clipW, impl_->clipH };
	if (!map_frame(fs, x, y, (int) w, (int) h,
		       &x0, &y0, &x1, &y1)) {
		return;
	}
	if (!clip_rect((int) impl_->bitmap->width(),
		       (int) impl_->bitmap->height(),
		       x0, y0, x1 - x0, y1 - y0, &x0, &y0, &x1, &y1)) {
		return;
	}
	composite_solid(b->img, rgb, nullptr, 0, 0,
			x0, y0, x1 - x0, y1 - y0);
}

/* Clip a shape rect to the bitmap surface; returns false when fully
 * outside. On true, x0..y1 hold the clipped exclusive bounds. */
static bool
clip_rect(int bw, int bh, int x, int y, int w, int h,
	  int *x0, int *y0, int *x1, int *y1)
{
	*x0 = x < 0 ? 0 : x;
	*y0 = y < 0 ? 0 : y;
	*x1 = x + w;
	*y1 = y + h;
	if (*x0 >= bw || *y0 >= bh || *x1 <= 0 || *y1 <= 0) {
		return false;
	}
	if (*x1 > bw) {
		*x1 = bw;
	}
	if (*y1 > bh) {
		*y1 = bh;
	}
	return true;
}

/* ---- rounded-mask cache ---------------------------------------------
 *
 * Building the a8 coverage mask costs a pixman image PLUS a triangle-fan
 * rasterization with anti-aliasing, and it was rebuilt on every call: every
 * rounded rect, every gradient's interior, every menubar chip and menu row,
 * per draw. Measured 2026-09: a repaint of a heavy window (the widget zoo's
 * board) spent tens of ms per draw in the view-tree walk with the masks
 * being the non-text part of it.
 *
 * The mask is a pure function of (w, h, radius), so cache it. The cache
 * holds its OWN reference, so callers keep the existing contract (they unref
 * what they are handed) and the cached image survives; eviction drops the
 * cache's reference. Bounded by entry count and total mask bytes, because a
 * full-window rounded rect is a multi-MB mask.
 */
#define MASK_CACHE_ENTRIES 64
#define MASK_CACHE_BYTES (6u * 1024u * 1024u)

struct MaskEntry {
	unsigned int w, h, r;
	pixman_image_t *img;
};
static MaskEntry g_maskCache[MASK_CACHE_ENTRIES];
static int g_maskCount = 0;
static unsigned long g_maskBytes = 0;

static void
mask_cache_drop(int i)
{
	g_maskBytes -= (unsigned long) g_maskCache[i].w * g_maskCache[i].h;
	pixman_image_unref(g_maskCache[i].img);
	/* close the hole: order is least-recent-first after use */
	for (int j = i; j + 1 < g_maskCount; j++) {
		g_maskCache[j] = g_maskCache[j + 1];
	}
	g_maskCount--;
}

/* As rounded_mask(), but shared: the caller still owns one reference. */
static pixman_image_t *
rounded_mask_cached(unsigned int w, unsigned int h, unsigned int radius)
{
	unsigned long bytes = (unsigned long) w * h;
	int i;

	for (i = 0; i < g_maskCount; i++) {
		if (g_maskCache[i].w == w && g_maskCache[i].h == h &&
		    g_maskCache[i].r == radius) {
			pixman_image_t *img = g_maskCache[i].img;

			if (i > 0) {	/* promote: the hot shape stays */
				MaskEntry t = g_maskCache[i];

				for (int j = i; j > 0; j--) {
					g_maskCache[j] = g_maskCache[j - 1];
				}
				g_maskCache[0] = t;
			}
			pixman_image_ref(img);
			return img;
		}
	}
	pixman_image_t *img = rounded_mask(w, h, radius);

	if (!img) {
		return nullptr;
	}
	/* cache only what fits the byte budget; a bigger mask is returned
	 * uncached, which is exactly what the callers did before */
	if (bytes <= MASK_CACHE_BYTES) {
		while (g_maskCount > 0 &&
		       (g_maskCount >= MASK_CACHE_ENTRIES ||
			g_maskBytes + bytes > MASK_CACHE_BYTES)) {
			mask_cache_drop(g_maskCount - 1);
		}
		for (i = g_maskCount; i > 0; i--) {
			g_maskCache[i] = g_maskCache[i - 1];
		}
		g_maskCache[0].w = w;
		g_maskCache[0].h = h;
		g_maskCache[0].r = radius;
		g_maskCache[0].img = pixman_image_ref(img);
		g_maskCount++;
		g_maskBytes += bytes;
	}
	return img;
}

void
GraphicsContext::fillRoundedRect(int x, int y, unsigned int w,
				 unsigned int h, unsigned int radius,
				 std::uint32_t rgb)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;
	int x0, y0, x1, y1;

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
	frame_state fs = { impl_->ox, impl_->oy, impl_->clipOn,
			   impl_->clipX, impl_->clipY,
			   impl_->clipW, impl_->clipH };
	if (!map_frame(fs, x, y, (int) w, (int) h,
		       &x0, &y0, &x1, &y1)) {
		return;
	}
	if (!clip_rect((int) impl_->bitmap->width(),
		       (int) impl_->bitmap->height(),
		       x0, y0, x1 - x0, y1 - y0, &x0, &y0, &x1, &y1)) {
		return;
	}
	pixman_image_t *mask = rounded_mask_cached(w, h, radius);

	if (!mask) {
		return;
	}
	/* mask was built in LOCAL shape space (w x h at shape origin
	 * x,y); the shape's surface origin is x+ox,y+oy. Composite the
	 * clipped dest rect sampling the mask from there. */
	composite_solid(b->img, rgb, mask,
			x0 - (x + impl_->ox), y0 - (y + impl_->oy),
			x0, y0, x1 - x0, y1 - y0);
	pixman_image_unref(mask);
}

void
GraphicsContext::fillRoundedGradient(int x, int y, unsigned int w,
				     unsigned int h, unsigned int radius,
				     std::uint32_t rgb0, std::uint32_t rgb1,
				     bool vertical)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;
	int x0, y0, x1, y1;
	pixman_gradient_stop_t stops[2];
	pixman_point_fixed_t p1, p2;
	pixman_image_t *grad;
	pixman_image_t *mask;

	if (!b->img || w == 0 || h == 0) {
		return;
	}
	unsigned int half = (w < h ? w : h) / 2;
	if (radius > half) {
		radius = half;
	}
	frame_state fs = { impl_->ox, impl_->oy, impl_->clipOn,
			   impl_->clipX, impl_->clipY,
			   impl_->clipW, impl_->clipH };
	if (!map_frame(fs, x, y, (int) w, (int) h,
		       &x0, &y0, &x1, &y1)) {
		return;
	}
	if (!clip_rect((int) impl_->bitmap->width(),
		       (int) impl_->bitmap->height(),
		       x0, y0, x1 - x0, y1 - y0, &x0, &y0, &x1, &y1)) {
		return;
	}
	/* two-stop gradient clipped to the rounded mask; gradient is
	 * defined in SURFACE space (shape origin x+ox,y+oy) so it
	 * matches dest 1:1. */
	pixman_color_t c0 = pixcolor(rgb0);
	pixman_color_t c1 = pixcolor(rgb1);
	int sx = x + impl_->ox;	/* shape origin in surface space */
	int sy = y + impl_->oy;

	stops[0].x = 0;
	stops[0].color = c0;
	stops[1].x = pixman_fixed_1;
	stops[1].color = c1;
	if (vertical) {
		p1.x = pixman_int_to_fixed(sx);
		p1.y = pixman_int_to_fixed(sy);
		p2.x = pixman_int_to_fixed(sx);
		p2.y = pixman_int_to_fixed(sy + (int) h);
	} else {
		p1.x = pixman_int_to_fixed(sx);
		p1.y = pixman_int_to_fixed(sy);
		p2.x = pixman_int_to_fixed(sx + (int) w);
		p2.y = pixman_int_to_fixed(sy);
	}
	grad = pixman_image_create_linear_gradient(&p1, &p2, stops, 2);
	if (!grad) {
		return;
	}
	pixman_image_set_repeat(grad, PIXMAN_REPEAT_PAD);
	if (radius > 0) {
		mask = rounded_mask_cached(w, h, radius);
		if (!mask) {
			pixman_image_unref(grad);
			return;
		}
	} else {
		/* radius 0 = a plain rectangular band: same mapped +
		 * clipped composite, no AA mask (do NOT fall back to
		 * fillLinearGradient — that is the legacy surface-space
		 * primitive that ignores the frame translate/clip, so a
		 * translated view painted its "flat" zone at the wrong
		 * place and the outline showed through) */
		mask = nullptr;
	}
	/* gradient sampled in dest coordinates: src offset == dest offset */
	/* OVER (not SRC) through the AA mask: edge pixels where the mask
	 * is partial must blend with whatever is beneath (the ring/base),
	 * not be replaced by gradient*(mask) which darkens them to ~black */
	pixman_image_composite32(PIXMAN_OP_OVER, grad, mask, b->img,
				 x0, y0, x0 - sx, y0 - sy, x0, y0,
				 x1 - x0, y1 - y0);
	if (mask) {
		pixman_image_unref(mask);
	}
	pixman_image_unref(grad);
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

	/* centre in surface space (frame translate), then the disc
	 * bbox; intersect with the frame clip + surface. */
	cx += impl_->ox;
	cy += impl_->oy;
	pixman_point_fixed_t pc = { pixman_int_to_fixed(cx),
				    pixman_int_to_fixed(cy) };
	pixman_image_t *grad = pixman_image_create_radial_gradient(
		&pc, &pc, 0, pixman_int_to_fixed((int) radius), stops, 2);
	if (!grad) {
		return;
	}
	pixman_image_set_repeat(grad, PIXMAN_REPEAT_PAD);
	int r = (int) radius;
	int x0 = cx - r;
	int y0 = cy - r;
	int x1 = cx + r;
	int y1 = cy + r;

	if (impl_->clipOn) {
		int cxr = impl_->clipX + impl_->clipW;
		int cyb = impl_->clipY + impl_->clipH;

		if (x0 < impl_->clipX) {
			x0 = impl_->clipX;
		}
		if (y0 < impl_->clipY) {
			y0 = impl_->clipY;
		}
		if (x1 > cxr) {
			x1 = cxr;
		}
		if (y1 > cyb) {
			y1 = cyb;
		}
	}
	int bw = (int) impl_->bitmap->width();
	int bh = (int) impl_->bitmap->height();

	if (x0 < 0) {
		x0 = 0;
	}
	if (y0 < 0) {
		y0 = 0;
	}
	if (x1 > bw) {
		x1 = bw;
	}
	if (y1 > bh) {
		y1 = bh;
	}
	if (x1 <= x0 || y1 <= y0) {
		pixman_image_unref(grad);
		return;
	}
	/* sample the gradient at dest coords: src offset == dest offset,
	 * so pc must be in SURFACE space (done above) */
	pixman_image_composite32(PIXMAN_OP_SRC, grad, nullptr, b->img,
				 x0, y0, 0, 0, x0, y0, x1 - x0, y1 - y0);
	pixman_image_unref(grad);
}

void
GraphicsContext::drawLine(int x0, int y0, int x1, int y1, std::uint32_t rgb)
{
	BitmapImage::Impl *b = impl_->bitmap->impl_;

	if (!b->img) {
		return;
	}
	if (x0 == x1 && y0 == y1) {
		/* single pixel (fillRect applies the frame translate) */
		fillRect(x0, y0, 1, 1, rgb);
		return;
	}
	/* frame translate; long lines are triangle-fanned (below) and
	 * pixman clips them to the surface; the frame CLIP is honored
	 * only as a coarse bbox rejection (a line crossing the clip
	 * edge can leak up to one AA fringe — acceptable for the v1
	 * chrome, all lines interior). */
	x0 += impl_->ox;
	y0 += impl_->oy;
	x1 += impl_->ox;
	y1 += impl_->oy;
	if (impl_->clipOn) {
		int cxl = impl_->clipX;
		int cyl = impl_->clipY;
		int cxr = cxl + impl_->clipW;
		int cyb = cyl + impl_->clipH;
		int lx0 = x0 < x1 ? x0 : x1;
		int lx1 = x0 < x1 ? x1 : x0;
		int ly0 = y0 < y1 ? y0 : y1;
		int ly1 = y0 < y1 ? y1 : y0;

		if (lx1 < cxl || lx0 > cxr || ly1 < cyl || ly0 > cyb) {
			return;	/* fully outside the clip */
		}
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
GraphicsContext::drawText(const char *family, double sizePt,
			  int xPx, int yPx, const char *utf8,
			  std::uint32_t fg, bool bold)
{
	BitmapImage::Impl *b = impl_->bitmap ? impl_->bitmap->impl_ : nullptr;

	if (!b || !b->img || !family || !utf8 || sizePt <= 0) {
		return;
	}
	Application &app = Application::shared();
	unsigned int pixelSize = (unsigned int)
		((sizePt * app.pxPerPt()) + 0.5);

	TextRun *t = textRunPrepare(family, utf8, pixelSize, false, bold);
	if (!t) {
		return;
	}
	int boxW = textRunBoxW(t);
	int boxH = textRunBoxH(t);
	std::uint32_t fg2 = fg;

	/* frame state: run box in surface space + clipped bounds */
	frame_state fs = { impl_->ox, impl_->oy, impl_->clipOn,
			   impl_->clipX, impl_->clipY,
			   impl_->clipW, impl_->clipH };
	int runX = xPx + fs.ox;
	int runY = yPx + fs.oy;
	int x0, y0, x1, y1;

	if (!map_frame(fs, xPx, yPx, boxW, boxH, &x0, &y0, &x1, &y1)) {
		textRunFinish(t);
		return;
	}
	if (!clip_rect((int) impl_->bitmap->width(),
		       (int) impl_->bitmap->height(),
		       x0, y0, x1 - x0, y1 - y0, &x0, &y0, &x1, &y1)) {
		textRunFinish(t);
		return;
	}

	/* render glyph AA coverage into an A8 mask sized to the run box */
	unsigned char *cov = (unsigned char *)
		std::calloc((size_t) boxW * boxH, 1);
	if (!cov) {
		textRunFinish(t);
		return;
	}
	unsigned int rasterized = textRunComposeMask(t, cov);
	if (rasterized == 0) {
		std::free(cov);
		textRunFinish(t);
		return;
	}
	pixman_image_t *mask = pixman_image_create_bits(
		PIXMAN_a8, boxW, boxH, nullptr, 0);
	if (!mask) {
		std::free(cov);
		textRunFinish(t);
		return;
	}
	int stride = pixman_image_get_stride(mask);
	unsigned char *md = (unsigned char *) pixman_image_get_data(mask);

	for (int r = 0; r < boxH; r++) {
		std::memcpy(md + (size_t) r * stride, cov + (size_t) r * boxW,
			    (size_t) boxW);
	}
	std::free(cov);

	/* composite fg through the clipped mask region */
	pixman_color_t c = pixcolor(fg2);
	pixman_image_t *src = pixman_image_create_solid_fill(&c);

	if (src) {
		pixman_image_composite32(PIXMAN_OP_OVER, src, mask, b->img,
					 0, 0,
					 x0 - runX, y0 - runY,
					 x0, y0, x1 - x0, y1 - y0);
		pixman_image_unref(src);
	}
	pixman_image_unref(mask);
	textRunFinish(t);
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

/* Composite a source x8r8g8b8 image into the destination at 1:1 with
 * an (sx,sy) sample origin; dest region is (x,y,w,h) surface px. */
static void
composite_image(pixman_image_t *dest, pixman_image_t *src,
		int sx, int sy, int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0) {
		return;
	}
	pixman_image_composite32(PIXMAN_OP_OVER, src, nullptr, dest,
				 sx, sy, 0, 0, x, y, w, h);
}

void
GraphicsContext::drawImage(const BitmapImage &img, int xPx, int yPx,
			   unsigned int wPx, unsigned int hPx,
			   ImageContentMode mode)
{
	BitmapImage::Impl *src = img.impl_;
	BitmapImage::Impl *b = impl_->bitmap->impl_;

	if (!src->img || !b->img || wPx == 0 || hPx == 0) {
		return;
	}
	int iw = (int) src->width;
	int ih = (int) src->height;

	if (iw <= 0 || ih <= 0) {
		return;
	}
	/* layout of the source within the destination rect */
	int lx = xPx;
	int ly = yPx;
	int lw = (int) wPx;
	int lh = (int) hPx;

	switch (mode) {
	case ImageContentMode::Center:
		lw = iw;
		lh = ih;
		lx = xPx + ((int) wPx - iw) / 2;
		ly = yPx + ((int) hPx - ih) / 2;
		break;
	case ImageContentMode::ScaleToFit: {
		double s = wPx / (double) iw;

		if (hPx / (double) ih < s) {
			s = hPx / (double) ih;
		}
		lw = (int) (iw * s + 0.5);
		lh = (int) (ih * s + 0.5);
		lx = xPx + ((int) wPx - lw) / 2;
		ly = yPx + ((int) hPx - lh) / 2;
		break;
	}
	case ImageContentMode::Stretch:
	default:
		break;
	}
	/* map the layout through the frame translate + clip */
	frame_state fs = { impl_->ox, impl_->oy, impl_->clipOn,
			   impl_->clipX, impl_->clipY,
			   impl_->clipW, impl_->clipH };
	int x0, y0, x1, y1;

	if (!map_frame(fs, lx, ly, lw, lh, &x0, &y0, &x1, &y1)) {
		return;
	}
	if (!clip_rect((int) b->width, (int) b->height,
		       x0, y0, x1 - x0, y1 - y0, &x0, &y0, &x1, &y1)) {
		return;
	}
	int dw = x1 - x0;
	int dh = y1 - y0;

	if (lw == iw && lh == ih) {
		/* 1:1 — direct composite. The sample origin is the
		 * clipped rect's offset within the LAYOUT (which lives
		 * in translated space: surface rect Lx = lx+ox). */
		composite_image(b->img, src->img,
				x0 - (lx + fs.ox), y0 - (ly + fs.oy),
				x0, y0, dw, dh);
		return;
	}
	/* Scaled: resample the source into a dw x dh temp surface with
	 * manual bilinear sampling, then composite it 1:1. Image sizes
	 * here are small (view icons), so the per-pixel loop is fine
	 * and the mapping is explicit. Surface pixel (dx,dy) lies at
	 * layout offset ((dx - Lx), (dy - Ly)) with Lx = lx+ox; the
	 * image spans the layout rect, so the source coordinate is
	 * offset * iw/lw. */
	pixman_image_t *tmp = pixman_image_create_bits(
		PIXMAN_x8r8g8b8, dw, dh, nullptr, 0);

	if (!tmp) {
		return;
	}
	const std::uint32_t *sp =
		(const std::uint32_t *) pixman_image_get_data(src->img);
	int sstride = (int) (pixman_image_get_stride(src->img) /
			     (int) sizeof(std::uint32_t));
	std::uint32_t *tp = (std::uint32_t *) pixman_image_get_data(tmp);
	int tstride = (int) (pixman_image_get_stride(tmp) /
			     (int) sizeof(std::uint32_t));
	double sx = iw / (double) lw;
	double sy = ih / (double) lh;
	int Lx = lx + impl_->ox;
	int Ly = ly + impl_->oy;

	for (int j = 0; j < dh; j++) {
		double syf = ((y0 + j) - Ly) * sy;

		if (syf < 0) {
			syf = 0;
		}
		if (syf > ih - 1) {
			syf = ih - 1;
		}
		int sy0i = (int) syf;
		int sy1i = sy0i + 1 < ih ? sy0i + 1 : sy0i;
		double fy = syf - sy0i;

		for (int i = 0; i < dw; i++) {
			double sxf = ((x0 + i) - Lx) * sx;

			if (sxf < 0) {
				sxf = 0;
			}
			if (sxf > iw - 1) {
				sxf = iw - 1;
			}
			int sx0i = (int) sxf;
			int sx1i = sx0i + 1 < iw ? sx0i + 1 : sx0i;
			double fx = sxf - sx0i;
			std::uint32_t p00 = sp[sy0i * sstride + sx0i];
			std::uint32_t p10 = sp[sy0i * sstride + sx1i];
			std::uint32_t p01 = sp[sy1i * sstride + sx0i];
			std::uint32_t p11 = sp[sy1i * sstride + sx1i];
			std::uint32_t out = 0;

			for (int c = 0; c < 3; c++) {
				int sh = 16 - 8 * c;
				double v =
					((p00 >> sh) & 0xff) * (1 - fx) *
						(1 - fy) +
					((p10 >> sh) & 0xff) * fx *
						(1 - fy) +
					((p01 >> sh) & 0xff) * (1 - fx) *
						fy +
					((p11 >> sh) & 0xff) * fx * fy;

				out |= ((std::uint32_t) (v + 0.5) & 0xff)
					<< sh;
			}
			tp[j * tstride + i] = out;
		}
	}
	pixman_image_composite32(PIXMAN_OP_OVER, tmp, nullptr, b->img,
				 0, 0, 0, 0, x0, y0, dw, dh);
	pixman_image_unref(tmp);
}

} /* namespace argentum */
