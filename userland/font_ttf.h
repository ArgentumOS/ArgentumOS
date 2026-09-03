/*
 * font_ttf.h - a minimal TrueType rasterizer for the FNX widget toolkit.
 *
 * Purpose: scalable text. The GUI's v1 glyph source is the fixed 8x16
 * bitmap; this module renders TrueType outlines at any pixel size so
 * the toolkit can do proportional, sized, Unicode text (docs
 * gui-e-toolkit.md §6: the renderer takes a "glyph source" interface,
 * and the font-format + rasterizer item is the path to full Unicode).
 *
 * Scope (honest limits): cmap formats 4 (BMP) and 12 (full), simple
 * glyph outlines + composite glyphs, hmtx advances, 1-bit scanline
 * rasterization with optional 4x supersampling. No hinting, no
 * kerning, no OpenType shaping - those are out of scope for the first
 * GUI (single-locale Latin/UTF-8 text, per docs/utf8-only.md).
 *
 * All sizes are in pixels of the em square: a "size" of 16 renders a
 * 16px em (the same visual scale as the 8x16 bitmap font's cap area).
 * Coordinates are in the outline's design units unless stated.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef FNX_FONT_TTF_H
#define FNX_FONT_TTF_H

#include <stddef.h>

typedef struct ttf_face ttf_face_t;

/* Parse a TrueType/OpenType face from a memory image (not copied - the
 * caller must keep data alive until ttf_close). Returns NULL on error. */
ttf_face_t *ttf_open(const unsigned char *data, size_t len);
void ttf_close(ttf_face_t *f);

/* font-wide metrics (design units unless _px) */
int ttf_units_per_em(ttf_face_t *f);
int ttf_ascender(ttf_face_t *f);
int ttf_descender(ttf_face_t *f);	/* negative */
int ttf_linegap(ttf_face_t *f);
int ttf_lineheight_px(ttf_face_t *f, int size);	/* asc - desc + gap */

/* map a Unicode code point to a glyph id (0 = .notdef) */
int ttf_glyph_index(ttf_face_t *f, unsigned int codepoint);

/* horizontal advance of a glyph in pixels at 'size' (rounded) */
int ttf_advance_px(ttf_face_t *f, int glyph, int size);

/* render a glyph at 'size' pixels into a fresh 1-bit bitmap.
 * Returns 0 on success and sets:
 *   *out = malloc'd bitmap, w*h bytes, 1 = ink
 *   *w,*h = bitmap size (includes side bearings)
 *   *x0,*y0 = glyph origin offset: the glyph's pen position is at
 *   (x0, y0) top-left of the bitmap, i.e. advance past the bitmap by
 *   (w - (x0 + advance)) ... callers normally draw at pen + (x0, y0).
 * Returns -1 if the glyph is empty (nothing drawn; *out = NULL). */
int ttf_render(ttf_face_t *f, int glyph, int size,
	       unsigned char **out, int *w, int *h, int *x0, int *y0);

#endif /* FNX_FONT_TTF_H */
