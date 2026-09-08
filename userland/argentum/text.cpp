/* argentum/text.cpp — Argentum's own text path (S0.4 + S2.2a).
 *
 * One UTF-8 run is drawn through the stack that fontconfig M0 + the
 * text_pipeline guest test proved end-to-end:
 *
 *   fontconfig family match -> FreeType face -> HarfBuzz shape ->
 *   FreeType glyph raster -> composite
 *
 * S2.2a splits the path into a shared core + two sinks so widgets can
 * draw text INSIDE the view-tree composite (per-view translate+clip
 * into the offscreen pixman surface) instead of only at window level:
 *
 *   TextRun (match -> shape -> run metrics)   shared
 *     textRunComposeRgb   -> opaque 0x00RRGGBB box (legacy
 *                            Window::drawText; S0.4 log parity)
 *     textRunComposeMask  -> A8 AA coverage (GraphicsContext::drawText
 *                            composites fg through it with pixman
 *                            OP_OVER, exactly like rounded masks)
 *
 * HarfBuzz decides glyph order + advances; FreeType renders each
 * glyph bitmap. No XRender/Xft client lib is involved.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <hb.h>
#include <hb-ft.h>

#include <fontconfig/fontconfig.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace argentum {

/* ---- shared run core ------------------------------------------- */

struct TextRun {
	/* fontconfig handles (destroyed on finish) */
	FcPattern *pat = nullptr;
	FcPattern *match = nullptr;

	/* FreeType + HarfBuzz (destroyed on finish) */
	FT_Face face = nullptr;
	hb_font_t *hbf = nullptr;
	hb_buffer_t *buf = nullptr;

	/* shaped glyphs (borrowed from buf) */
	unsigned int nglyphs = 0;
	hb_glyph_info_t *info = nullptr;
	hb_glyph_position_t *pos = nullptr;

	/* run geometry: total 26.6 advance + FreeType size metrics */
	long adv26 = 0;
	int ascPx = 0;		/* ascent, px above the baseline */
	int descPx = 0;		/* descent, px below the baseline */

	/* box (px) incl. the legacy padding; baseline sits at
	 * PADY + ascPx below the box top */
	int boxW = 0;
	int boxH = 0;
};

static const int PADX = 2;
static const int PADY = 1;

/* Composite one glyph bitmap (FT_PIXEL_MODE_GRAY or _MONO) into an
 * RGB32 box at (x0,y0), blending fg over bg by per-pixel coverage. */
static void
compose_glyph_rgb(std::uint32_t *box, int boxW, int boxH,
		  FT_Bitmap *bm, int x0, int y0,
		  std::uint32_t fg, std::uint32_t bg)
{
	int fr = (fg >> 16) & 0xff, fg2 = (fg >> 8) & 0xff, fb = fg & 0xff;
	int br = (bg >> 16) & 0xff, bg2 = (bg >> 8) & 0xff, bb = bg & 0xff;
	int rows = (int) bm->rows;
	int cols = (int) bm->width;

	for (int r = 0; r < rows; r++) {
		if (y0 + r < 0 || y0 + r >= boxH) {
			continue;
		}
		const unsigned char *row = bm->buffer + (size_t) r * bm->pitch;
		for (int c = 0; c < cols; c++) {
			if (x0 + c < 0 || x0 + c >= boxW) {
				continue;
			}
			int cov;
			if (bm->pixel_mode == FT_PIXEL_MODE_MONO) {
				cov = (row[c >> 3] & (0x80 >> (c & 7))) ? 255 : 0;
			} else {
				cov = row[c];	/* GRAY: 0..255 */
			}
			if (cov <= 0) {
				continue;
			}
			int inv = 255 - cov;
			int r2 = (fr * cov + br * inv) / 255;
			int g2 = (fg2 * cov + bg2 * inv) / 255;
			int b2 = (fb * cov + bb * inv) / 255;
			box[(y0 + r) * boxW + (x0 + c)] =
				(std::uint32_t) ((r2 << 16) | (g2 << 8) | b2);
		}
	}
}

/* Composite one glyph's coverage into an A8 mask at (x0,y0). */
static void
compose_glyph_mask(unsigned char *cov, int boxW, int boxH,
		   FT_Bitmap *bm, int x0, int y0)
{
	int rows = (int) bm->rows;
	int cols = (int) bm->width;

	for (int r = 0; r < rows; r++) {
		if (y0 + r < 0 || y0 + r >= boxH) {
			continue;
		}
		const unsigned char *row = bm->buffer + (size_t) r * bm->pitch;
		for (int c = 0; c < cols; c++) {
			if (x0 + c < 0 || x0 + c >= boxW) {
				continue;
			}
			int v;
			if (bm->pixel_mode == FT_PIXEL_MODE_MONO) {
				v = (row[c >> 3] & (0x80 >> (c & 7))) ? 255 : 0;
			} else {
				v = row[c];	/* GRAY: 0..255 */
			}
			if (v <= 0) {
				continue;
			}
			cov[(y0 + r) * boxW + (x0 + c)] = (unsigned char) v;
		}
	}
}

/* Pen advance callback shared by the two sinks. */
template <typename Sink>
static unsigned int
render_glyphs(TextRun *t, Sink sink)
{
	unsigned int rasterized = 0;
	long pen26 = 0;
	unsigned int i;

	for (i = 0; i < t->nglyphs; i++) {
		int penPx = (int) (pen26 >> 6);	/* truncation is fine */
		pen26 += t->pos[i].x_advance;
		if (FT_Load_Glyph(t->face, t->info[i].codepoint,
				  FT_LOAD_RENDER)) {
			continue;
		}
		FT_GlyphSlot g = t->face->glyph;
		if (!g->bitmap.buffer) {
			continue;
		}
		/* box coords: baseline sits ascPx+PADY below the top */
		int gx = PADX + penPx + g->bitmap_left;
		int gy = PADY + t->ascPx - g->bitmap_top;
		sink(t, i, g, gx, gy);
		rasterized++;
	}
	return rasterized;
}

TextRun *
textRunPrepare(const char *family, const char *utf8, unsigned int pixelSize)
{
	Application &app = Application::shared();

	if (!app.textStackReady()) {
		fprintf(stderr, "ARGENTUM-TEXT: text stack not inited\n");
		return nullptr;
	}
	if (!family || !utf8 || pixelSize == 0) {
		return nullptr;
	}

	TextRun *t = new (std::nothrow) TextRun();
	if (!t) {
		return nullptr;
	}

	/* 1) fontconfig: match the family */
	t->pat = FcNameParse((const FcChar8 *) family);
	if (!t->pat) {
		fprintf(stderr, "ARGENTUM-TEXT: FcNameParse(%s) failed\n",
			family);
		textRunFinish(t);
		return nullptr;
	}
	FcConfigSubstitute(nullptr, t->pat, FcMatchPattern);
	FcDefaultSubstitute(t->pat);
	FcResult res = FcResultNoMatch;
	t->match = FcFontMatch(nullptr, t->pat, &res);
	if (!t->match || res != FcResultMatch) {
		fprintf(stderr, "ARGENTUM-TEXT: FcFontMatch(%s) failed\n",
			family);
		textRunFinish(t);
		return nullptr;
	}
	FcChar8 *file = nullptr;
	int index = 0;
	if (FcPatternGetString(t->match, FC_FILE, 0, &file) != FcResultMatch ||
	    FcPatternGetInteger(t->match, FC_INDEX, 0, &index) !=
		    FcResultMatch) {
		fprintf(stderr, "ARGENTUM-TEXT: no FC_FILE/FC_INDEX\n");
		textRunFinish(t);
		return nullptr;
	}
	fprintf(stderr, "ARGENTUM-TEXT: matched '%s' index %d\n",
		(char *) file, index);

	/* 2) FreeType face from the matched file at the requested size */
	if (FT_New_Face((FT_Library) app.freeTypeHandle(), (char *) file,
			 index, &t->face) ||
	    !t->face) {
		fprintf(stderr, "ARGENTUM-TEXT: FT_New_Face failed\n");
		textRunFinish(t);
		return nullptr;
	}
	FT_Set_Pixel_Sizes(t->face, 0, pixelSize);

	/* 3) HarfBuzz shapes the run (glyph order + 26.6 advances) */
	t->hbf = hb_ft_font_create_referenced(t->face);
	t->buf = hb_buffer_create();
	hb_buffer_add_utf8(t->buf, (const char *) utf8, -1, 0, -1);
	hb_buffer_guess_segment_properties(t->buf);
	hb_shape(t->hbf, t->buf, nullptr, 0);

	t->nglyphs = hb_buffer_get_length(t->buf);
	t->info = hb_buffer_get_glyph_infos(t->buf, nullptr);
	t->pos = hb_buffer_get_glyph_positions(t->buf, nullptr);
	fprintf(stderr, "ARGENTUM-TEXT: shaped '%s' -> %u glyphs\n",
		utf8, t->nglyphs);

	/* run box metrics: total advance + face ascent/descent (26.6) */
	long adv26 = 0;
	unsigned int i;

	for (i = 0; i < t->nglyphs; i++) {
		adv26 += t->pos[i].x_advance;
	}
	FT_Size_Metrics *sm = &t->face->size->metrics;
	t->adv26 = adv26;
	t->ascPx = (sm->ascender + 32) >> 6;
	t->descPx = (0 - sm->descender + 32) >> 6;	/* below baseline */
	t->boxW = (int) ((adv26 + 32) >> 6) + 2 * PADX;
	t->boxH = t->ascPx + t->descPx + 2 * PADY;
	if (t->boxW < 1 || t->boxH < 1) {
		fprintf(stderr, "ARGENTUM-TEXT: empty run box\n");
		textRunFinish(t);
		return nullptr;
	}
	return t;
}

void
textRunFinish(TextRun *t)
{
	if (!t) {
		return;
	}
	if (t->buf) {
		hb_buffer_destroy(t->buf);
	}
	if (t->hbf) {
		hb_font_destroy(t->hbf);
	}
	if (t->face) {
		FT_Done_Face(t->face);
	}
	if (t->match) {
		FcPatternDestroy(t->match);
	}
	if (t->pat) {
		FcPatternDestroy(t->pat);
	}
	delete t;
}

unsigned int
textRunGlyphCount(const TextRun *t)
{
	return t->nglyphs;
}

int
textRunBoxW(const TextRun *t)
{
	return t->boxW;
}

int
textRunBoxH(const TextRun *t)
{
	return t->boxH;
}

int
textRunAscent(const TextRun *t)
{
	return t->ascPx;
}

/* ---- sinks ------------------------------------------------------ */

/* RGB sink: legacy window-box rendering (fg over opaque bg). */
struct RgbSinkCtx {
	std::uint32_t *box;
	int boxW;
	int boxH;
	std::uint32_t fg;
	std::uint32_t bg;
};

unsigned int
textRunComposeRgb(TextRun *t, std::uint32_t *box, std::uint32_t fg,
		  std::uint32_t bg)
{
	RgbSinkCtx ctx = { box, t->boxW, t->boxH, fg, bg };

	for (int k = 0; k < t->boxW * t->boxH; k++) {
		box[k] = bg & 0xffffff;
	}
	unsigned int n = render_glyphs(t, [&ctx](TextRun *t, unsigned int i,
						 FT_GlyphSlot g, int gx,
						 int gy) {
		(void) t;
		(void) i;
		compose_glyph_rgb(ctx.box, ctx.boxW, ctx.boxH, &g->bitmap,
				  gx, gy, ctx.fg, ctx.bg);
	});
	return n;
}

/* A8 sink: AA coverage mask for GC compositing. */
unsigned int
textRunComposeMask(TextRun *t, unsigned char *cov)
{
	for (int k = 0; k < t->boxW * t->boxH; k++) {
		cov[k] = 0;
	}
	unsigned int n = render_glyphs(t, [cov, t](TextRun *, unsigned int i,
						   FT_GlyphSlot g, int gx,
						   int gy) {
		(void) i;
		compose_glyph_mask(cov, t->boxW, t->boxH, &g->bitmap, gx, gy);
	});
	return n;
}

/* ---- public metrics (pt) ---------------------------------------- */

TextMetrics
textMetrics(const char *family, double sizePt, const char *utf8)
{
	Application &app = Application::shared();
	TextMetrics m = { 0, 0, 0 };

	if (!family || !utf8 || sizePt <= 0) {
		return m;
	}
	unsigned int pixelSize = (unsigned int)
		((sizePt * app.pxPerPt()) + 0.5);
	TextRun *t = textRunPrepare(family, utf8, pixelSize);
	if (!t) {
		return m;
	}
	m.widthPt = ((double) t->adv26 / 64.0) / app.pxPerPt();
	m.ascentPt = (double) t->ascPx / app.pxPerPt();
	m.descentPt = (double) t->descPx / app.pxPerPt();
	textRunFinish(t);
	return m;
}

/* ---- legacy window-level draw (S0.4; parity path) --------------- */

void
Window::drawText(const char *family, int x, int y, const char *utf8,
		 unsigned int pixelSize, std::uint32_t fg,
		 std::uint32_t bg)
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	TextRun *t = textRunPrepare(family, utf8, pixelSize);
	if (!t) {
		return;
	}

	int boxW = t->boxW;
	int boxH = t->boxH;
	std::uint32_t *box = (std::uint32_t *)
		std::calloc((size_t) boxW * boxH, sizeof(std::uint32_t));
	if (!box) {
		textRunFinish(t);
		return;
	}
	unsigned int rasterized = textRunComposeRgb(t, box, fg, bg);

	/* 4) core-protocol blit of the run box (mirrors Window::fill) */
	Display *dpy = impl_->dpy;
	::Window xwin = impl_->xwin;
	int screen = DefaultScreen(dpy);
	Visual *vis = DefaultVisual(dpy, screen);
	unsigned int depth = (unsigned int) DefaultDepth(dpy, screen);
	XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0,
				   (char *) box, (unsigned int) boxW,
				   (unsigned int) boxH, 32, boxW * 4);
	if (img) {
		GC gc = XCreateGC(dpy, xwin, 0, nullptr);
		if (gc) {
			fprintf(stderr, "ARGENTUM-TEXT: blitted %u glyph(s) "
				"box %dx%d at %d,%d\n", rasterized, boxW,
				boxH, x, y);
			XPutImage(dpy, xwin, gc, img, 0, 0, x, y,
				  (unsigned int) boxW,
				  (unsigned int) boxH);
			XFreeGC(dpy, gc);
		}
		XDestroyImage(img);	/* frees box */
	} else {
		std::free(box);
	}
	XSync(dpy, False);

	textRunFinish(t);
}

} /* namespace argentum */
