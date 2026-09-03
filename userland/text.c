/*
 * text.c - the FNX text engine (see text.h).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

/* ---- UTF-8 ---------------------------------------------------------- */

int utf8_decode(const char *s, unsigned int *cp)
{
	const unsigned char *p = (const unsigned char *)s;
	unsigned int c = p[0];

	if(c < 0x80) {
		*cp = c;
		return c ? 1 : 0;
	}
	if((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
		*cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
		return 2;
	}
	if((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
	   (p[2] & 0xC0) == 0x80) {
		*cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) |
		      (p[2] & 0x3F);
		return 3;
	}
	if((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
	   (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
		*cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
		      ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
		return 4;
	}
	return -1;
}

int utf8_encode(unsigned int cp, char out[4])
{
	if(cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if(cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if(cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

/* ---- the font ------------------------------------------------------- */

static void cache_glyph(text_font_t *f, int gid)
{
	struct glyph_bm *g;
	unsigned char *bm;
	int w = 0, h = 0, x0 = 0, y0 = 0;

	if(gid < 0 || gid >= f->nglyphs || f->cache[gid].loaded) {
		return;
	}
	g = &f->cache[gid];
	if(ttf_render(f->face, gid, f->size, &bm, &w, &h, &x0, &y0)) {
		/* empty or .notdef: still mark loaded so we do not retry
		 * every draw */
		g->loaded = 1;
		return;
	}
	g->px = bm;
	g->w = w;
	g->h = h;
	g->dx = x0;
	/* flipped raster space: y=0 is the em top, the baseline sits at
	 * flipped-y == size; the bitmap spans [y0, y0+h) */
	g->dy = y0 - f->size;
	g->adv = ttf_advance_px(f->face, gid, f->size);
	g->loaded = 1;
}

/* return the glyph bitmap for a code point (lazily rasterized), or
 * NULL for .notdef/missing */
static struct glyph_bm *font_glyph(text_font_t *f, unsigned int cp)
{
	int gid = ttf_glyph_index(f->face, cp);

	if(gid <= 0) {
		gid = ttf_glyph_index(f->face, 0xFFFD);	/* replacement */
		if(gid <= 0) {
			gid = 0;
		}
	}
	cache_glyph(f, gid);
	return f->cache[gid].loaded ? &f->cache[gid] : NULL;
}

static text_font_t *font_new(ttf_face_t *face, unsigned char *data,
			     size_t len, int size)
{
	text_font_t *f;
	long v;

	f = calloc(1, sizeof(*f));
	if(!f) {
		return NULL;
	}
	f->face = face;
	f->data = data;
	f->len = len;
	f->size = size;
	f->nglyphs = face ? ttf_num_glyphs(face) : 0;
	f->cache = calloc((size_t)f->nglyphs, sizeof(struct glyph_bm));
	if(!f->cache) {
		free(f);
		return NULL;
	}
	v = (long)ttf_ascender(face) * size;
	f->ascent = (int)((v + ttf_units_per_em(face) / 2) /
			  ttf_units_per_em(face));
	v = (long)(-ttf_descender(face)) * size;
	f->descent = (int)((v + ttf_units_per_em(face) / 2) /
			   ttf_units_per_em(face));
	f->height = ttf_lineheight_px(face, size);
	if(f->height < f->ascent + f->descent) {
		f->height = f->ascent + f->descent;
	}
	return f;
}

text_font_t *text_font_open(const char *path, int size)
{
	FILE *fp;
	long n;
	unsigned char *data;
	ttf_face_t *face;
	text_font_t *f;

	fp = fopen(path, "rb");
	if(!fp) {
		return NULL;
	}
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if(n <= 0) {
		fclose(fp);
		return NULL;
	}
	data = malloc((size_t)n);
	if(!data || fread(data, 1, (size_t)n, fp) != (size_t)n) {
		free(data);
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	face = ttf_open(data, (size_t)n);
	if(!face) {
		free(data);
		return NULL;
	}
	f = font_new(face, data, (size_t)n, size);
	if(!f) {
		ttf_close(face);
		free(data);
		return NULL;
	}
	return f;
}

text_font_t *text_font_bind(ttf_face_t *face, unsigned char *data,
			    size_t len, int size)
{
	return font_new(face, data, len, size);
}

void text_font_close(text_font_t *f)
{
	int i;

	if(!f) {
		return;
	}
	for(i = 0; i < f->nglyphs; i++) {
		if(f->cache[i].px) {
			free(f->cache[i].px);
		}
	}
	free(f->cache);
	if(f->face) {
		ttf_close(f->face);
	}
	if(f->data) {
		free(f->data);
	}
	free(f);
}

int text_font_height(text_font_t *f)
{
	return f->height;
}

int text_font_ascent(text_font_t *f)
{
	return f->ascent;
}

int text_font_descent(text_font_t *f)
{
	return f->descent;
}

/* ---- measure / draw ------------------------------------------------ */

static int string_advance(text_font_t *f, const char *s, int stop_off)
{
	int x = 0;
	int off = 0;

	while(s[off]) {
		unsigned int cp;
		int n = utf8_decode(s + off, &cp);
		struct glyph_bm *g;

		if(n <= 0) {
			break;
		}
		if(stop_off >= 0 && off >= stop_off) {
			break;
		}
		g = font_glyph(f, cp);
		if(g) {
			x += g->adv;
		}
		off += n;
	}
	return x;
}

int text_width(text_font_t *f, const char *s)
{
	return string_advance(f, s, -1);
}

int text_draw_clip(text_font_t *f, uint32_t *buf, int bw, int bh,
		    int cx, int cy, int cw, int ch, int x, int baseline,
		    const char *s, uint32_t color)
{
	int pen = x;
	int x0 = cx < 0 ? 0 : cx;
	int y0 = cy < 0 ? 0 : cy;
	int x1 = cx + cw > bw ? bw : cx + cw;
	int y1 = cy + ch > bh ? bh : cy + ch;
	int off = 0;
	uint32_t r = (color >> 16) & 0xFF;
	uint32_t g = (color >> 8) & 0xFF;
	uint32_t b = color & 0xFF;

	while(s[off]) {
		unsigned int cp;
		int n = utf8_decode(s + off, &cp);
		struct glyph_bm *gl;
		int gx, gy, px, py;

		if(n <= 0) {
			break;
		}
		gl = font_glyph(f, cp);
		if(gl && gl->px) {
			gx = pen + gl->dx;
			gy = baseline + gl->dy;
			for(py = 0; py < gl->h; py++) {
				int yy = gy + py;

				if(yy < y0 || yy >= y1) {
					continue;
				}
				for(px = 0; px < gl->w; px++) {
					int xx = gx + px;
					unsigned int cov;
					uint32_t *d;

					if(xx < x0 || xx >= x1) {
						continue;
					}
					cov = gl->px[(size_t)py * gl->w +
						     px];
					if(!cov) {
						continue;
					}
					d = &buf[(size_t)yy * bw + xx];
					if(cov == 255) {
						*d = color;
					} else {
						uint32_t dr = (*d >> 16) & 0xFF;
						uint32_t dg = (*d >> 8) & 0xFF;
						uint32_t db = *d & 0xFF;

						dr = (r * cov + dr *
						      (255 - cov)) / 255;
						dg = (g * cov + dg *
						      (255 - cov)) / 255;
						db = (b * cov + db *
						      (255 - cov)) / 255;
						*d = (dr << 16) | (dg << 8) |
						     db;
					}
				}
			}
			pen += gl->adv;
		} else {
			/* .notdef: advance a space-width so the text
			 * stays positioned */
			pen += f->size / 2;
		}
		off += n;
	}
	return pen;
}

int text_draw(text_font_t *f, uint32_t *buf, int bw, int bh,
	      int x, int baseline, const char *s, uint32_t color)
{
	return text_draw_clip(f, buf, bw, bh, 0, 0, bw, bh,
			      x, baseline, s, color);
}

/* ---- wrapping / caret ---------------------------------------------- */

int text_wrap(text_font_t *f, const char *s, int maxw, int *breaks,
	      int maxlines)
{
	int lines = 0;
	int line_start = 0;
	int last_space = -1;	/* byte offset after the last space */
	int last_space_x = 0;
	int x = 0;
	int off = 0;

	breaks[0] = 0;
	while(s[off]) {
		unsigned int cp;
		int n = utf8_decode(s + off, &cp);
		struct glyph_bm *g;

		if(n <= 0) {
			break;
		}
		g = font_glyph(f, cp);
		if(s[off] == ' ' || s[off] == '\t') {
			last_space = off + n;
			last_space_x = x;
		}
		if(s[off] == '\n') {
			/* hard break */
			if(lines + 1 >= maxlines) {
				break;
			}
			lines++;
			line_start = off + n;
			x = 0;
			last_space = -1;
			off += n;
			breaks[lines] = line_start;
			continue;
		}
		if(g) {
			x += g->adv;
		} else {
			x += f->size / 2;
		}
		if(x > maxw && line_start < off) {
			/* wrap at the last space, else hard-break */
			if(last_space > line_start) {
				if(lines + 1 >= maxlines) {
					break;
				}
				lines++;
				line_start = last_space;
				x -= last_space_x;
				last_space = -1;
				last_space_x = 0;
			} else {
				if(lines + 1 >= maxlines) {
					break;
				}
				lines++;
				line_start = off;
				x = 0;
				last_space = -1;
				last_space_x = 0;
			}
			breaks[lines] = line_start;
		}
		off += n;
	}
	return lines + 1;
}

int text_offset_to_x(text_font_t *f, const char *s, int off)
{
	int x = 0;
	int i = 0;

	while(s[i] && i < off) {
		unsigned int cp;
		int n = utf8_decode(s + i, &cp);
		struct glyph_bm *g;

		if(n <= 0) {
			break;
		}
		g = font_glyph(f, cp);
		if(g) {
			x += g->adv;
		} else {
			x += f->size / 2;
		}
		i += n;
	}
	return x;
}

int text_x_to_offset(text_font_t *f, const char *s, int x)
{
	int pen = 0;
	int off = 0;

	while(s[off]) {
		unsigned int cp;
		int n = utf8_decode(s + off, &cp);
		struct glyph_bm *g;
		int half;

		if(n <= 0) {
			break;
		}
		g = font_glyph(f, cp);
		half = g ? g->adv / 2 : f->size / 4;
		if(x < pen + half) {
			return off;
		}
		pen += g ? g->adv : f->size / 2;
		off += n;
	}
	return off;
}
