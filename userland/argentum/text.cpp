/* argentum/text.cpp — Argentum's own text path (S0.4).
 *
 * drawText() draws one UTF-8 run through the stack that fontconfig M0
 * + the text_pipeline guest test proved end-to-end:
 *
 *   fontconfig family match -> FreeType face -> HarfBuzz shape ->
 *   FreeType glyph raster -> core-protocol XPutImage of the run box.
 *
 * The run is composited into a small 32-bpp RGB box (fg glyph pixels
 * over the requested bg — 0x00RRGGBB words, the same layout fill()
 * blits) and put into the window at (x, y) with one core XPutImage.
 * No XRender/Xft client lib is involved; HarfBuzz decides the glyph
 * order + advances, FreeType renders each glyph bitmap.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <hb.h>
#include <hb-ft.h>

#include <fontconfig/fontconfig.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace argentum {

/* Composite one glyph bitmap (FT_PIXEL_MODE_GRAY or _MONO) into the
 * run box at (x0,y0), blending fg over bg by the per-pixel coverage. */
static void
compose_glyph(std::uint32_t *box, int boxW, int boxH,
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

void
Window::drawText(const char *family, int x, int y, const char *utf8,
		 unsigned int pixelSize, std::uint32_t fg,
		 std::uint32_t bg)
{
	Application &app = Application::shared();

	if (!app.impl_->ftInited || !app.impl_->ft) {
		fprintf(stderr, "ARGENTUM-TEXT: text stack not inited\n");
		return;
	}
	if (!impl_->dpy || !impl_->xwin || !family || !utf8 ||
	    pixelSize == 0) {
		return;
	}

	/* 1) fontconfig: match the family from the system.fonts domain */
	FcPattern *pat = FcNameParse((const FcChar8 *) family);
	if (!pat) {
		fprintf(stderr, "ARGENTUM-TEXT: FcNameParse(%s) failed\n",
			family);
		return;
	}
	FcConfigSubstitute(nullptr, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult res = FcResultNoMatch;
	FcPattern *match = FcFontMatch(nullptr, pat, &res);
	if (!match || res != FcResultMatch) {
		fprintf(stderr, "ARGENTUM-TEXT: FcFontMatch(%s) failed\n",
			family);
		FcPatternDestroy(pat);
		return;
	}
	FcChar8 *file = nullptr;
	int index = 0;
	if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch ||
	    FcPatternGetInteger(match, FC_INDEX, 0, &index) !=
		    FcResultMatch) {
		fprintf(stderr, "ARGENTUM-TEXT: no FC_FILE/FC_INDEX\n");
		FcPatternDestroy(match);
		FcPatternDestroy(pat);
		return;
	}
	fprintf(stderr, "ARGENTUM-TEXT: matched '%s' index %d\n",
		(char *) file, index);

	/* 2) FreeType face from the matched file at the requested size */
	FT_Face face = nullptr;
	if (FT_New_Face(app.impl_->ft, (char *) file, index, &face) ||
	    !face) {
		fprintf(stderr, "ARGENTUM-TEXT: FT_New_Face failed\n");
		FcPatternDestroy(match);
		FcPatternDestroy(pat);
		return;
	}
	FT_Set_Pixel_Sizes(face, 0, pixelSize);

	/* 3) HarfBuzz shapes the run (glyph order + 26.6 advances) */
	hb_font_t *hbf = hb_ft_font_create_referenced(face);
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, (const char *) utf8, -1, 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_shape(hbf, buf, nullptr, 0);

	unsigned int nglyphs = hb_buffer_get_length(buf);
	hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, nullptr);
	hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf,
								 nullptr);
	fprintf(stderr, "ARGENTUM-TEXT: shaped '%s' -> %u glyphs\n",
		utf8, nglyphs);

	/* run box metrics: total advance + face ascent/descent (26.6) */
	long adv26 = 0;
	unsigned int i;
	for (i = 0; i < nglyphs; i++) {
		adv26 += pos[i].x_advance;
	}
	FT_Size_Metrics *sm = &face->size->metrics;
	int ascPx = (sm->ascender + 32) >> 6;
	int descPx = (0 - sm->descender + 32) >> 6;	/* below baseline */
	const int PADX = 2;
	const int PADY = 1;
	int boxW = (int) ((adv26 + 32) >> 6) + 2 * PADX;
	int boxH = ascPx + descPx + 2 * PADY;
	if (boxW < 1 || boxH < 1) {
		fprintf(stderr, "ARGENTUM-TEXT: empty run box\n");
		hb_buffer_destroy(buf);
		hb_font_destroy(hbf);
		FT_Done_Face(face);
		FcPatternDestroy(match);
		FcPatternDestroy(pat);
		return;
	}

	std::uint32_t *box = (std::uint32_t *)
		std::calloc((size_t) boxW * boxH, sizeof(std::uint32_t));
	if (!box) {
		hb_buffer_destroy(buf);
		hb_font_destroy(hbf);
		FT_Done_Face(face);
		FcPatternDestroy(match);
		FcPatternDestroy(pat);
		return;
	}
	for (int k = 0; k < boxW * boxH; k++) {
		box[k] = bg & 0xffffff;
	}

	/* 4) rasterize each shaped glyph at its pen position */
	long pen26 = 0;
	unsigned int rasterized = 0;
	for (i = 0; i < nglyphs; i++) {
		int penPx = (int) (pen26 >> 6);	/* truncation is fine */
		pen26 += pos[i].x_advance;
		if (FT_Load_Glyph(face, info[i].codepoint, FT_LOAD_RENDER)) {
			continue;
		}
		FT_GlyphSlot g = face->glyph;
		if (!g->bitmap.buffer) {
			continue;
		}
		/* box coords: baseline sits ascPx+PADY below the top */
		int gx = PADX + penPx + g->bitmap_left;
		int gy = PADY + ascPx - g->bitmap_top;
		compose_glyph(box, boxW, boxH, &g->bitmap, gx, gy, fg, bg);
		if (rasterized == 0) {
			fprintf(stderr,
				"ARGENTUM-TEXT: rasterized glyph %u -> "
				"%ux%u bitmap\n",
				(unsigned int) info[i].codepoint,
				g->bitmap.width, g->bitmap.rows);
		}
		rasterized++;
	}
	fprintf(stderr, "ARGENTUM-TEXT: blitted %u glyph(s) box %dx%d "
		"at %d,%d\n", rasterized, boxW, boxH, x, y);

	/* 5) core-protocol blit of the run box (mirrors Window::fill) */
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

	hb_buffer_destroy(buf);
	hb_font_destroy(hbf);
	FT_Done_Face(face);
	FcPatternDestroy(match);
	FcPatternDestroy(pat);
}

} /* namespace argentum */
