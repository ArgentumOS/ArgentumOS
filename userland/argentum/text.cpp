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

	/* FreeType + HarfBuzz (destroyed on finish; the face only when
	 * faceOwned — cached faces belong to the face cache) */
	FT_Face face = nullptr;
	bool faceOwned = false;
	hb_font_t *hbf = nullptr;
	hb_buffer_t *buf = nullptr;

	/* owned by the shaped-run cache (textRunFinish must not free it) */
	bool cached = false;

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

/* ---- face cache ------------------------------------------------
 *
 * Every draw used to re-run FcFontMatch (a fontconfig directory scan,
 * tens of ms) + FT_New_Face (opens + parses the file) for EVERY text
 * run — a window redraw with N strings paid N fontconfig matches.
 * Faces are process-lifetime: resolve (family, pixel size) once and
 * reuse. Sizes are pixel sizes at the session px/pt factor, so a
 * theme font at one pt size = one entry. */
struct FaceEntry {
	char family[64] = { 0 };
	unsigned int px = 0;
	FT_Face face = nullptr;
	FcPattern *match = nullptr;	/* keeps FC_FILE alive */
};

static FaceEntry g_faceCache[8];
static int g_faceCount = 0;

/* defined below (each with the cache it belongs to); a face that is freed
 * must drop everything that references it — cached glyphs (which copy their
 * bitmaps but are KEYED on the face pointer) and cached shaped runs (which
 * borrow the face through their hb_font) */
static void glyphCacheDropFace(FT_Face face);
static void runCacheDropFace(FT_Face face);

/* Look up (family, px); on miss resolve via fontconfig + FreeType and
 * cache it. Returns the face or null. *firstResolve is set when this
 * call created the entry (callers log the 'matched' line once). */
static FT_Face
textLookupFace(const char *family, unsigned int px, FT_Library lib,
	       FcPattern **matchRef, bool *firstResolve)
{
	int i;

	*firstResolve = false;
	for (i = 0; i < g_faceCount; i++) {
		if (g_faceCache[i].px == px &&
		    std::strcmp(g_faceCache[i].family, family) == 0) {
			if (matchRef) {
				*matchRef = g_faceCache[i].match;
			}
			return g_faceCache[i].face;
		}
	}
	/* resolve: fontconfig family match */
	FcPattern *pat = FcNameParse((const FcChar8 *) family);

	if (!pat) {
		return nullptr;
	}
	FcConfigSubstitute(nullptr, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult res = FcResultNoMatch;
	FcPattern *match = FcFontMatch(nullptr, pat, &res);

	FcPatternDestroy(pat);
	if (!match || res != FcResultMatch) {
		if (match) {
			FcPatternDestroy(match);
		}
		return nullptr;
	}
	FcChar8 *file = nullptr;
	int index = 0;

	if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch ||
	    FcPatternGetInteger(match, FC_INDEX, 0, &index) !=
		    FcResultMatch) {
		FcPatternDestroy(match);
		return nullptr;
	}
	FT_Face face = nullptr;

	if (FT_New_Face(lib, (char *) file, index, &face) || !face) {
		FcPatternDestroy(match);
		return nullptr;
	}
	/* store (evict slot 0 when full) */
	if (g_faceCount < (int) (sizeof(g_faceCache) /
				 sizeof(g_faceCache[0]))) {
		i = g_faceCount++;
	} else {
		i = 0;
		if (g_faceCache[i].face) {
			runCacheDropFace(g_faceCache[i].face);
			glyphCacheDropFace(g_faceCache[i].face);
			FT_Done_Face(g_faceCache[i].face);
		}
		if (g_faceCache[i].match) {
			FcPatternDestroy(g_faceCache[i].match);
		}
	}
	std::strncpy(g_faceCache[i].family, family,
		     sizeof(g_faceCache[i].family) - 1);
	g_faceCache[i].family[sizeof(g_faceCache[i].family) - 1] = 0;
	g_faceCache[i].px = px;
	g_faceCache[i].face = face;
	g_faceCache[i].match = match;
	*firstResolve = true;
	if (matchRef) {
		*matchRef = match;
	}
	return face;
}

/* Composite one glyph bitmap (FT_PIXEL_MODE_GRAY or _MONO) into an
 * RGB32 box at (x0,y0), blending fg over bg by per-pixel coverage. */
static void
compose_glyph_rgb(std::uint32_t *box, int boxW, int boxH,
		  const FT_Bitmap *bm, int x0, int y0,
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
		   const FT_Bitmap *bm, int x0, int y0)
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

/* ---- glyph raster cache -----------------------------------------
 *
 * Every draw re-ran FT_Load_Glyph(FT_LOAD_RENDER) for each glyph, i.e.
 * FreeType re-rasterized each character on every redraw. Faces are
 * pinned to one pixel size (the face cache key is family + px), so a
 * rendered glyph's bitmap and bitmap_left/top are constant per
 * (face, glyph) — cache them and blit on reuse. 512 entries at ~20px
 * is well under a megabyte. */
struct GlyphEntry {
	FT_Face face = nullptr;
	unsigned int glyph = 0;
	FT_Bitmap bm = {};		/* buffer owned by the entry */
	int left = 0;
	int top = 0;
};

static GlyphEntry g_glyphCache[512];
static int g_glyphCount = 0;

/* Drop every cached glyph belonging to a face that is going away. The
 * cache key is the FT_Face POINTER: FreeType frees the face and the
 * allocator can hand the same address to a later FT_New_Face, at which
 * point the stale entries would be served for a DIFFERENT face — the
 * wrong glyph, silently, for a font that never rasterized one. (The
 * bitmap buffers are copies the entries own, so only correctness is at
 * stake, not memory safety.) */
static void
glyphCacheDropFace(FT_Face face)
{
	int i;

	for (i = 0; i < g_glyphCount; i++) {
		if (g_glyphCache[i].face == face) {
			delete[] g_glyphCache[i].bm.buffer;
			g_glyphCache[i] = g_glyphCache[g_glyphCount - 1];
			g_glyphCount--;
			i--;
		}
	}
}

/* Rasterize (or fetch) a glyph. Returns a stable FT_Bitmap (the entry
 * owns the buffer) or null on failure, with the glyph's bitmap offset
 * in the FreeType metric convention stored in left and top. */
static const FT_Bitmap *
glyphBitmap(FT_Face face, unsigned int glyph, int *left, int *top)
{
	int i;

	for (i = 0; i < g_glyphCount; i++) {
		if (g_glyphCache[i].face == face &&
		    g_glyphCache[i].glyph == glyph) {
			*left = g_glyphCache[i].left;
			*top = g_glyphCache[i].top;
			return &g_glyphCache[i].bm;
		}
	}
	if (FT_Load_Glyph(face, glyph, FT_LOAD_RENDER)) {
		return nullptr;
	}
	FT_GlyphSlot s = face->glyph;

	if (!s->bitmap.buffer) {
		return nullptr;
	}
	if (g_glyphCount == (int) (sizeof(g_glyphCache) /
				  sizeof(g_glyphCache[0]))) {
		/* full: drop everything (glyph sets are small) */
		for (i = 0; i < g_glyphCount; i++) {
			delete[] g_glyphCache[i].bm.buffer;
		}
		g_glyphCount = 0;
	}
	GlyphEntry &e = g_glyphCache[g_glyphCount++];
	FT_Bitmap &bm = e.bm;

	e.face = face;
	e.glyph = glyph;
	e.left = s->bitmap_left;
	e.top = s->bitmap_top;
	bm.rows = s->bitmap.rows;
	bm.width = s->bitmap.width;
	bm.pitch = s->bitmap.pitch;
	bm.pixel_mode = s->bitmap.pixel_mode;
	if (s->bitmap.pitch > 0 && s->bitmap.rows > 0) {
		size_t bytes = (size_t) s->bitmap.rows * s->bitmap.pitch;

		bm.buffer = new unsigned char[bytes];
		std::memcpy(bm.buffer, s->bitmap.buffer, bytes);
	} else {
		bm.buffer = nullptr;
	}
	*left = e.left;
	*top = e.top;
	return &bm;
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
		int left = 0;
		int top = 0;
		const FT_Bitmap *bm = glyphBitmap(t->face,
						 t->info[i].codepoint,
						 &left, &top);

		if (!bm) {
			continue;
		}
		/* box coords: baseline sits ascPx+PADY below the top */
		int gx = PADX + penPx + left;
		int gy = PADY + t->ascPx - top;
		sink(t, bm, gx, gy);
		rasterized++;
	}
	return rasterized;
}

/* ---- shaped-run cache -------------------------------------------
 *
 * Measured: a shaping call costs about one 10ms timer tick (this kernel's
 * clock granularity) for even a one-glyph run, and "every redraw reshapes
 * every string": the menubar alone re-shaped its three titles twice per
 * repaint (metrics + draw), which is ~30ms of every bar repaint, and a
 * menu row re-shaped its label on every hover repaint. A run's shaped
 * glyphs are a function of (family, pixel size, text) alone — the face
 * cache pins the face to one pixel size — so shape once and hand the run
 * back. Bounded ring; eviction frees the oldest run for real. Runs whose
 * text does not fit the key are never cached. */
#define RUN_CACHE 64
struct RunCacheEntry {
	char key[128];
	TextRun *run;
};

static RunCacheEntry g_runCache[RUN_CACHE];
static int g_runCount = 0;	/* entries in use */
static int g_runNext = 0;	/* ring cursor once full */

static void textRunFree(TextRun *t);

/* a run that borrows a face must go when the face does */
static void
runCacheDropFace(FT_Face face)
{
	for (int i = 0; i < g_runCount; i++) {
		if (g_runCache[i].run && g_runCache[i].run->face == face) {
			textRunFree(g_runCache[i].run);
			g_runCache[i].run = nullptr;
		}
	}
}

TextRun *
textRunPrepare(const char *family, const char *utf8, unsigned int pixelSize,
	       bool quiet)
{
	Application &app = Application::shared();

	if (!app.textStackReady()) {
		fprintf(stderr, "ARGENTUM-TEXT: text stack not inited\n");
		return nullptr;
	}
	if (!family || !utf8 || pixelSize == 0) {
		return nullptr;
	}

	/* the shaped-run cache: the same string at the same size shapes the
	 * same glyphs for the life of the process */
	char ckey[128];

	if (std::snprintf(ckey, sizeof(ckey), "%s|%u|%s", family, pixelSize,
			  utf8) < (int) sizeof(ckey)) {
		for (int i = 0; i < g_runCount; i++) {
			if (g_runCache[i].run &&
			    std::strcmp(g_runCache[i].key, ckey) == 0) {
				return g_runCache[i].run;
			}
		}
	}

	TextRun *t = new (std::nothrow) TextRun();
	if (!t) {
		return nullptr;
	}

	/* 1) fontconfig match + FreeType face come from the process
	 * face cache (resolve once per family+pixel size; the cached
	 * face is borrowed — finish() must not destroy it) */
	bool first = false;
	FcPattern *matchRef = nullptr;
	FT_Face face = textLookupFace(
		family, pixelSize, (FT_Library) app.freeTypeHandle(),
		&matchRef, &first);

	if (!face) {
		fprintf(stderr, "ARGENTUM-TEXT: face lookup(%s) failed\n",
			family);
		textRunFinish(t);
		return nullptr;
	}
	if (!quiet && first && matchRef) {
		FcChar8 *file = nullptr;
		int index = 0;

		if (FcPatternGetString(matchRef, FC_FILE, 0, &file) ==
			    FcResultMatch) {
			fprintf(stderr,
				"ARGENTUM-TEXT: matched '%s' index %d\n",
				(char *) file, index);
		}
	}
	t->face = face;		/* borrowed from the cache */
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
	if (!quiet) {
		/* log a run's shaping once per (family|px|text) — the
		 * console is serial-bound, and every redraw reshapes
		 * every string; printing each time was ~0.2s of pure
		 * log I/O per full-window draw */
		static char s_logged[8][160];
		static int s_loggedN = 0;
		char key[160];
		int li;

		std::snprintf(key, sizeof(key), "%s|%u|%s", family,
			      pixelSize, utf8);
		for (li = 0; li < s_loggedN; li++) {
			if (std::strcmp(s_logged[li], key) == 0) {
				break;
			}
		}
		if (li == s_loggedN && s_loggedN < 8) {
			std::strncpy(s_logged[s_loggedN], key,
				     sizeof(s_logged[0]) - 1);
			s_logged[s_loggedN]
				[sizeof(s_logged[0]) - 1] = 0;
			s_loggedN++;
			fprintf(stderr,
				"ARGENTUM-TEXT: shaped '%s' -> %u glyphs\n",
				utf8, t->nglyphs);
		}
	}

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
	if (std::strcmp(ckey, "") != 0 && strlen(ckey) < sizeof(ckey)) {
		/* keep it: metrics and draws repeat the same strings */
		int slot;

		if (g_runCount < RUN_CACHE) {
			slot = g_runCount++;
		} else {
			slot = g_runNext;
			g_runNext = (g_runNext + 1) % RUN_CACHE;
			if (g_runCache[slot].run) {
				textRunFree(g_runCache[slot].run);
			}
		}
		std::strncpy(g_runCache[slot].key, ckey,
			     sizeof(g_runCache[0].key) - 1);
		g_runCache[slot].key[sizeof(g_runCache[0].key) - 1] = 0;
		g_runCache[slot].run = t;
		t->cached = true;
	}
	return t;
}

void
textRunFinish(TextRun *t)
{
	if (!t || t->cached) {
		return;		/* the shaped-run cache owns it */
	}
	textRunFree(t);
}

static void
textRunFree(TextRun *t)
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
	if (t->face && t->faceOwned) {
		runCacheDropFace(t->face);
		glyphCacheDropFace(t->face);
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

long
textRunAdvance26(const TextRun *t)
{
	return t->adv26;
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
	unsigned int n = render_glyphs(t, [&ctx](TextRun *, const FT_Bitmap *bm,
						 int gx, int gy) {
		compose_glyph_rgb(ctx.box, ctx.boxW, ctx.boxH, bm,
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
	unsigned int n = render_glyphs(t, [cov, t](TextRun *,
						   const FT_Bitmap *bm,
						   int gx, int gy) {
		compose_glyph_mask(cov, t->boxW, t->boxH, bm, gx, gy);
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
