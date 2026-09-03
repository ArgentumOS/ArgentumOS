/*
 * text.h - the FNX text engine: a glyph source bound to one TTF face +
 * size, an 8-bit AA rasterizer cache, and UTF-8 layout primitives.
 *
 * This is the layout half of the text stack (the rasterizer half is
 * font_ttf.c). It gives the widget toolkit what a Pango gives desktop
 * toolkits minus shaping/bidi (deferred per docs): measure, draw,
 * wrap, align and caret<->index mapping for UTF-8 text.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef FNX_TEXT_H
#define FNX_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "font_ttf.h"

typedef struct text_font text_font_t;

struct glyph_bm {
	unsigned char *px;	/* w*h 8-bit coverage */
	int w, h;
	int dx;			/* bitmap left relative to the pen */
	int dy;			/* bitmap top relative to the baseline */
	int adv;		/* advance past the pen */
	int loaded;
};

struct text_font {
	ttf_face_t *face;
	unsigned char *data;	/* the font file image (owned) */
	size_t len;
	int size;		/* em size in pixels */
	int ascent, descent, height;	/* px line metrics */
	struct glyph_bm *cache;
	int nglyphs;
};

/* Load a TTF/OTF from a file, ready to draw at 'size' pixels. */
text_font_t *text_font_open(const char *path, int size);
/* Bind an already-parsed face + its data (caller owns both). */
text_font_t *text_font_bind(ttf_face_t *face, unsigned char *data,
			    size_t len, int size);
void text_font_close(text_font_t *f);

/* line metrics in pixels */
int text_font_height(text_font_t *f);
int text_font_ascent(text_font_t *f);
int text_font_descent(text_font_t *f);

/* UTF-8: decode one code point; returns the byte length (1..4), 0 at
 * the NUL terminator, -1 on an invalid sequence. */
int utf8_decode(const char *s, unsigned int *cp);
int utf8_encode(unsigned int cp, char out[4]);

/* total advance width of a UTF-8 string in pixels */
int text_width(text_font_t *f, const char *s);

/* draw a UTF-8 line with the pen at (x, baseline) into a 32-bpp
 * 0x00RRGGBB buffer; AA coverage is alpha-blended over whatever is
 * there. Returns the advance (end x). */
int text_draw(text_font_t *f, uint32_t *buf, int bw, int bh,
	      int x, int baseline, const char *s, uint32_t color);

/* Word-wrap s to at most 'maxw' pixels. Fills 'breaks' with the byte
 * offsets where each line starts (breaks[0] = 0); returns the line
 * count. s must stay alive for the caller to slice lines. */
int text_wrap(text_font_t *f, const char *s, int maxw, int *breaks,
	      int maxlines);

/* caret mapping for a single line of s (no wrap): given a pixel x
 * relative to the line's left edge, return the byte offset of the
 * glyph under it (click-to-position); given a byte offset, return the
 * x of that glyph's left edge (cursor position). */
int text_x_to_offset(text_font_t *f, const char *s, int x);
int text_offset_to_x(text_font_t *f, const char *s, int off);

#endif /* FNX_TEXT_H */
