/*
 * font_ttf.c - a minimal TrueType rasterizer (see font_ttf.h).
 *
 * Reads the sfnt table directory, the cmap (formats 4 + 12), head/
 * hhea/hmtx/maxp/loca/glyf. Glyph outlines are decoded to contours
 * (on/off-curve points), flattened (quadratics subdivided until flat),
 * scaled to the requested pixel size and filled with an even-odd
 * scanline rasterizer.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_ttf.h"

/* ---- big-endian readers ------------------------------------------- */

static unsigned short be16(const unsigned char *p)
{
	return (unsigned short)((p[0] << 8) | p[1]);
}

static unsigned int be32(const unsigned char *p)
{
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
	       ((unsigned int)p[2] << 8) | p[3];
}

static short bei16(const unsigned char *p)
{
	return (short)be16(p);
}

/* ---- the face ------------------------------------------------------ */

struct ttf_table {
	unsigned int off;
	unsigned int len;
};

struct ttf_face {
	const unsigned char *d;
	size_t len;
	unsigned int upem;
	int index_to_loc;	/* 0 = u16 loca, 1 = u32 */
	unsigned int num_glyphs;
	int ascender, descender, linegap;
	unsigned int n_hmetrics;
	struct ttf_table cmap, head, hhea, hmtx, maxp, loca, glyf;
	/* the cmap subtable chosen for Unicode lookup */
	unsigned int cmap_off, cmap_len;
	unsigned int cmap_format;
};

static struct ttf_table ttf_find_table(ttf_face_t *f, const char *tag)
{
	struct ttf_table t = { 0, 0 };
	unsigned int n, i;

	if(f->len < 12) {
		return t;
	}
	n = be16(f->d + 4);
	for(i = 0; i < n; i++) {
		unsigned int rec = 12 + i * 16;

		if(rec + 16 > f->len) {
			break;
		}
		if(memcmp(f->d + rec, tag, 4) == 0) {
			t.off = be32(f->d + rec + 8);
			t.len = be32(f->d + rec + 12);
			return t;
		}
	}
	return t;
}

ttf_face_t *ttf_open(const unsigned char *data, size_t len)
{
	ttf_face_t *f;
	unsigned int ncmap, i, best = 0;

	f = calloc(1, sizeof(*f));
	if(!f) {
		return NULL;
	}
	f->d = data;
	f->len = len;
	if(len < 12 || (unsigned int)be32(data) != 0x00010000) {
		free(f);
		return NULL;
	}

	f->head = ttf_find_table(f, "head");
	f->hhea = ttf_find_table(f, "hhea");
	f->hmtx = ttf_find_table(f, "hmtx");
	f->maxp = ttf_find_table(f, "maxp");
	f->loca = ttf_find_table(f, "loca");
	f->glyf = ttf_find_table(f, "glyf");
	f->cmap = ttf_find_table(f, "cmap");

	if(!f->head.len || !f->hhea.len || !f->hmtx.len || !f->maxp.len ||
	   !f->loca.len || !f->glyf.len || !f->cmap.len) {
		free(f);
		return NULL;
	}
	if(f->head.off + 54 > f->len || f->hhea.off + 36 > f->len ||
	   f->maxp.off + 6 > f->len) {
		free(f);
		return NULL;
	}
	f->upem = be16(f->d + f->head.off + 18);
	if(f->upem == 0) {
		f->upem = 2048;	/* broken fonts use 0 = 2048 */
	}
	f->index_to_loc = bei16(f->d + f->head.off + 50);
	f->num_glyphs = be16(f->d + f->maxp.off + 4);
	f->ascender = bei16(f->d + f->hhea.off + 4);
	f->descender = bei16(f->d + f->hhea.off + 6);
	f->linegap = bei16(f->d + f->hhea.off + 8);
	f->n_hmetrics = be16(f->d + f->hhea.off + 34);

	/* pick the cmap subtable: prefer Windows Unicode full (3,10),
	 * then Windows Unicode BMP (3,1), then Unicode (0,*); else the
	 * first subtable we can parse */
	ncmap = be16(f->d + f->cmap.off + 2);
	for(i = 0; i < ncmap; i++) {
		unsigned int rec = f->cmap.off + 4 + i * 8;
		unsigned int plat, enc, off, fmt;

		if(rec + 8 > f->len) {
			break;
		}
		plat = be16(f->d + rec);
		enc = be16(f->d + rec + 2);
		off = f->cmap.off + be32(f->d + rec + 4);
		if(off + 2 > f->len) {
			continue;
		}
		fmt = be16(f->d + off);
		if(fmt != 4 && fmt != 12) {
			continue;
		}
		if(!best) {
			best = off;
		}
		if(plat == 3 && enc == 10) {
			best = off;
			break;
		}
		if(plat == 3 && enc == 1) {
			best = off;
		}
		if(plat == 0 && enc <= 3 && fmt == 12) {
			best = off;
		}
	}
	if(!best) {
		free(f);
		return NULL;
	}
	f->cmap_off = best;
	f->cmap_format = be16(f->d + best);
	if(f->cmap_format == 12) {
		f->cmap_len = be32(f->d + best + 4);
	} else {
		f->cmap_len = be16(f->d + best + 2);
	}
	if(f->cmap_off + f->cmap_len > f->len) {
		free(f);
		return NULL;
	}
	return f;
}

void ttf_close(ttf_face_t *f)
{
	if(f) {
		free(f);
	}
}

int ttf_units_per_em(ttf_face_t *f)
{
	return (int)f->upem;
}

int ttf_ascender(ttf_face_t *f)
{
	return f->ascender;
}

int ttf_descender(ttf_face_t *f)
{
	return f->descender;
}

int ttf_linegap(ttf_face_t *f)
{
	return f->linegap;
}

int ttf_lineheight_px(ttf_face_t *f, int size)
{
	long v = (long)(f->ascender - f->descender + f->linegap) * size;

	return (int)((v + f->upem / 2) / (long)f->upem);
}

/* ---- cmap ---------------------------------------------------------- */

int ttf_glyph_index(ttf_face_t *f, unsigned int cp)
{
	const unsigned char *p = f->d + f->cmap_off;

	if(f->cmap_format == 12) {
		unsigned int n = be32(p + 12), i;

		for(i = 0; i < n; i++) {
			unsigned int g = 16 + i * 12;
			unsigned int sc, ec, sg;

			if(f->cmap_off + g + 12 > f->len) {
				break;
			}
			sc = be32(p + g);
			ec = be32(p + g + 4);
			sg = be32(p + g + 8);
			if(cp >= sc && cp <= ec) {
				return (int)(sg + (cp - sc));
			}
		}
		return 0;
	}
	/* format 4 */
	{
		unsigned int segx2 = be16(p + 6);
		unsigned int seg = segx2 / 2;
		unsigned int i, end_off = 14;
		unsigned int start_off, delta_off, ro_off;

		start_off = end_off + segx2 + 2;
		delta_off = start_off + segx2;
		ro_off = delta_off + segx2;
		for(i = 0; i < seg; i++) {
			unsigned int sc = be16(p + start_off + i * 2);
			unsigned int ec = be16(p + end_off + i * 2);

			if(cp > 0xFFFF) {
				continue;
			}
			if(cp < sc || cp > ec) {
				continue;
			}
			{
				short delta = (short)be16(p + delta_off +
							    i * 2);
				unsigned int ro = be16(p + ro_off + i * 2);

				if(ro == 0) {
					return (int)((cp + delta) & 0xFFFF);
				}
				{
					/* idRangeOffset is measured from
					 * the idRangeOffset word itself */
					unsigned int addr = ro_off + i * 2 +
							   ro +
							   2 * (cp - sc);

					if(f->cmap_off + addr + 2 > f->len) {
						return 0;
					}
					{
						unsigned int g =
							be16(p + addr);

						if(g == 0) {
							return 0;
						}
						return (int)((g + delta) &
							    0xFFFF);
					}
				}
			}
		}
	}
	return 0;
}

/* ---- advances ------------------------------------------------------ */

int ttf_advance_px(ttf_face_t *f, int glyph, int size)
{
	unsigned int adv;
	long v;

	if(glyph < 0 || (unsigned int)glyph >= f->num_glyphs) {
		return 0;
	}
	if(f->n_hmetrics == 0) {
		return 0;
	}
	if((unsigned int)glyph < f->n_hmetrics) {
		unsigned int off = f->hmtx.off + glyph * 4;

		if(off + 2 > f->len) {
			return 0;
		}
		adv = be16(f->d + off);
	} else {
		unsigned int off = f->hmtx.off +
				   (f->n_hmetrics - 1) * 4;

		if(off + 2 > f->len) {
			return 0;
		}
		adv = be16(f->d + off);
	}
	v = (long)adv * size;
	return (int)((v + f->upem / 2) / (long)f->upem);
}

/* ---- glyph outline decoding ---------------------------------------- */

#define MAX_GLYPH_PTS	2048
#define MAX_SEGS	8192

typedef struct {
	long x, y;	/* design units */
} ttf_pt;

typedef struct {
	long x0, y0, x1, y1;	/* design units, y-up */
} ttf_seg;

struct glyph_outline {
	ttf_pt *pts;		/* on/off points, per contour, contiguous */
	unsigned char *on;
	int *contours;		/* end index per contour */
	int npts, ncontours;
};

/* recursively flatten one quadratic bezier P0-C-P1 into segs */
static void flat_quad(long p0x, long p0y, long cx, long cy,
		      long p1x, long p1y, long tol,
		      ttf_seg *segs, int *nsegs, int cap)
{
	long mx = (p0x + p1x) / 2;
	long my = (p0y + p1y) / 2;
	long dx = cx - mx;
	long dy = cy - my;

	if(dx < 0) {
		dx = -dx;
	}
	if(dy < 0) {
		dy = -dy;
	}
	if((dx + dy) <= tol || *nsegs >= cap - 1) {
		if(*nsegs < cap) {
			segs[*nsegs].x0 = p0x;
			segs[*nsegs].y0 = p0y;
			segs[*nsegs].x1 = p1x;
			segs[*nsegs].y1 = p1y;
			(*nsegs)++;
		}
		return;
	}
	/* de Casteljau at t=1/2 */
	{
		long ax = (p0x + cx) / 2, ay = (p0y + cy) / 2;
		long bx = (cx + p1x) / 2, by = (cy + p1y) / 2;
		long qx = (ax + bx) / 2, qy = (ay + by) / 2;

		flat_quad(p0x, p0y, ax, ay, qx, qy, tol, segs, nsegs, cap);
		flat_quad(qx, qy, bx, by, p1x, p1y, tol, segs, nsegs, cap);
	}
}

static void contour_to_segs(const ttf_pt *p, const unsigned char *on,
			    int n, long tol, ttf_seg *segs, int *nsegs,
			    int cap)
{
	/* The contour is a closed ring. Find the first on-curve point to
	 * anchor the walk; a run of off-curve points between two on
	 * points forms one quadratic per off point with implied on
	 * points at the midpoints between consecutive offs. */
	int i, start = -1;
	long px, py;		/* the last on-curve point */

	for(i = 0; i < n; i++) {
		if(on[i]) {
			start = i;
			break;
		}
	}
	if(start < 0) {
		/* all off-curve: implied on-points at each midpoint; the
		 * ring of quadratics mid(i-1) -> off[i] -> mid(i) */
		for(i = 0; i < n; i++) {
			long mx0 = (p[(i + n - 1) % n].x + p[i].x) / 2;
			long my0 = (p[(i + n - 1) % n].y + p[i].y) / 2;
			long mx1 = (p[i].x + p[(i + 1) % n].x) / 2;
			long my1 = (p[i].y + p[(i + 1) % n].y) / 2;

			flat_quad(mx0, my0, p[i].x, p[i].y, mx1, my1,
				  tol, segs, nsegs, cap);
		}
		return;
	}
	/* walk the ring from 'start' around to start+n; pending offs
	 * between two on-curve points */
	px = p[start].x;
	py = p[start].y;
	{
		int off[8];	/* pending offs between ons (rarely > 2) */
		int noff = 0;

		for(i = 1; i <= n; i++) {
			int idx = (start + i) % n;
			int last = (i == n);

			if(!last && !on[idx]) {
				if(noff < 8) {
					off[noff] = idx;
				}
				noff++;
				continue;
			}
			/* an on-curve point (or the wrap to the start):
			 * emit the curve(s) from (px,py) to this point */
			{
				long nx, ny;

				if(last) {
					nx = p[start].x;
					ny = p[start].y;
				} else {
					nx = p[idx].x;
					ny = p[idx].y;
				}
				if(noff == 0) {
					if(*nsegs < cap) {
						segs[*nsegs].x0 = px;
						segs[*nsegs].y0 = py;
						segs[*nsegs].x1 = nx;
						segs[*nsegs].y1 = ny;
						(*nsegs)++;
					}
				} else if(noff == 1) {
					flat_quad(px, py,
						  p[off[0]].x, p[off[0]].y,
						  nx, ny, tol,
						  segs, nsegs, cap);
				} else {
					/* k offs -> implied ons between */
					int j;

					flat_quad(px, py,
						  p[off[0]].x, p[off[0]].y,
						  (p[off[0]].x + p[off[1]].x) / 2,
						  (p[off[0]].y + p[off[1]].y) / 2,
						  tol, segs, nsegs, cap);
					for(j = 1; j + 1 < noff; j++) {
						flat_quad(
						  (p[off[j - 1]].x + p[off[j]].x) / 2,
						  (p[off[j - 1]].y + p[off[j]].y) / 2,
						  p[off[j]].x, p[off[j]].y,
						  (p[off[j]].x + p[off[j + 1]].x) / 2,
						  (p[off[j]].y + p[off[j + 1]].y) / 2,
						  tol, segs, nsegs, cap);
					}
					flat_quad(
					  (p[off[noff - 2]].x + p[off[noff - 1]].x) / 2,
					  (p[off[noff - 2]].y + p[off[noff - 1]].y) / 2,
					  p[off[noff - 1]].x, p[off[noff - 1]].y,
					  nx, ny, tol, segs, nsegs, cap);
				}
				px = nx;
				py = ny;
				noff = 0;
			}
		}
	}
}

/* decode one glyph's outline(s) into a flattened segment list in
 * design units. Handles simple glyphs and composites (recursively).
 * Returns the number of segments (>= 0) or -1 on error. */
static int glyph_decode(ttf_face_t *f, unsigned int glyph, long tol,
			ttf_seg *segs, int *nsegs, int cap, int depth)
{
	unsigned int lo, hi, off;
	const unsigned char *g;
	int ncont, i;

	if(depth > 4 || glyph >= f->num_glyphs) {
		return -1;
	}
	if(f->index_to_loc == 0) {
		unsigned int a = f->loca.off + glyph * 2;

		if(a + 4 > f->len) {
			return -1;
		}
		lo = (unsigned int)be16(f->d + a) * 2;
		hi = (unsigned int)be16(f->d + a + 2) * 2;
	} else {
		unsigned int a = f->loca.off + glyph * 4;

		if(a + 8 > f->len) {
			return -1;
		}
		lo = be32(f->d + a);
		hi = be32(f->d + a + 4);
	}
	if(lo >= hi) {
		return 0;	/* empty glyph */
	}
	off = f->glyf.off + lo;
	if(off + 10 > f->len || off + (hi - lo) > f->len) {
		return -1;
	}
	g = f->d + off;
	ncont = bei16(g);
	if(ncont < 0) {
		/* composite */
		unsigned int pos = 10;

		while(1) {
			unsigned int cpos = off + pos;
			unsigned int flags, gid;
			long dx = 0, dy = 0;
			double sx = 1, sy = 1;
			int more;

			if(cpos + 4 > f->len) {
				return -1;
			}
			flags = be16(f->d + cpos);
			gid = be16(f->d + cpos + 2);
			pos += 4;
			more = flags & 0x0020;
			if(flags & 0x0001) {	/* ARG_1_AND_2_ARE_WORDS */
				if(cpos + 8 > f->len) {
					return -1;
				}
				if(flags & 0x0002) {
					dx = (short)be16(f->d + cpos + 4);
					dy = (short)be16(f->d + cpos + 6);
				}
				pos += 4;
			} else {
				if(cpos + 6 > f->len) {
					return -1;
				}
				if(flags & 0x0002) {
					dx = (signed char)f->d[cpos + 4];
					dy = (signed char)f->d[cpos + 5];
				}
				pos += 2;
			}
			/* transforms (F2Dot14, 16384 = 1.0) */
			if(flags & 0x0008) {	/* WE_HAVE_A_SCALE */
				short s = (short)be16(f->d + cpos + pos);

				sx = sy = s / 16384.0;
				pos += 2;
			} else if(flags & 0x0040) {	/* X_AND_Y_SCALE */
				sx = (short)be16(f->d + cpos + pos) / 16384.0;
				sy = (short)be16(f->d + cpos + pos + 2) /
				     16384.0;
				pos += 4;
			} else if(flags & 0x0080) {	/* TWO_BY_TWO */
				/* a,b / c,d - ignore rotation for now
				 * (rare in UI faces); skip the words */
				pos += 8;
			}
			if(!(flags & 0x0002)) {
				/* point matching: unsupported; bail */
				return -1;
			}
			{
				int before = *nsegs;
				int n = glyph_decode(f, gid, tol, segs,
						     nsegs, cap, depth + 1);

				if(n < 0) {
					return -1;
				}
				/* offset the decoded segments */
				for(i = before; i < *nsegs; i++) {
					segs[i].x0 = (long)(segs[i].x0 *
							   sx) + dx;
					segs[i].y0 = (long)(segs[i].y0 *
							   sy) + dy;
					segs[i].x1 = (long)(segs[i].x1 *
							   sx) + dx;
					segs[i].y1 = (long)(segs[i].y1 *
							   sy) + dy;
				}
			}
			if(!more) {
				break;
			}
		}
		return 0;
	}
	/* simple glyph */
	{
		unsigned int pos = 10;
		unsigned int epc, last_pt;
		unsigned short *endp;
		unsigned char *flags;
		ttf_pt pts[MAX_GLYPH_PTS];
		unsigned char onb[MAX_GLYPH_PTS];
		unsigned int k, ci;

		if(ncont == 0) {
			return 0;
		}
		if((unsigned int)ncont > MAX_GLYPH_PTS) {
			return -1;
		}
		epc = (unsigned int)ncont * 2;
		if(off + pos + epc > f->len) {
			return -1;
		}
		endp = malloc(epc);
		if(!endp) {
			return -1;
		}
		for(i = 0; i < ncont; i++) {
			endp[i] = be16(f->d + off + pos + i * 2);
		}
		pos += epc;
		if(pos + 2 > f->len) {
			free(endp);
			return -1;
		}
		pos += 2;	/* instructionLength */
		pos += be16(f->d + off + pos - 2);	/* instructions */
		last_pt = endp[ncont - 1];
		if(last_pt + 1 > MAX_GLYPH_PTS) {
			free(endp);
			return -1;
		}
		flags = malloc(last_pt + 1);
		if(!flags) {
			free(endp);
			return -1;
		}
		for(k = 0; k <= last_pt; ) {
			unsigned char fl;

			if(off + pos >= f->len) {
				free(flags);
				free(endp);
				return -1;
			}
			fl = f->d[off + pos++];
			flags[k++] = fl;
			if(fl & 0x08) {	/* repeat */
				int r;

				if(off + pos >= f->len) {
					free(flags);
					free(endp);
					return -1;
				}
				r = f->d[off + pos++];
				while(r-- > 0 && k <= last_pt) {
					flags[k++] = fl;
				}
			}
		}
		/* x coordinates */
		{
			long x = 0;

			for(k = 0; k <= last_pt; k++) {
				unsigned char fl = flags[k];

				if(fl & 0x02) {	/* X_SHORT */
					if(off + pos >= f->len) {
						free(flags);
						free(endp);
						return -1;
					}
					x += (fl & 0x10) ?
					     f->d[off + pos++] :
					     -f->d[off + pos++];
				} else if(!(fl & 0x10)) {
					if(off + pos + 2 > f->len) {
						free(flags);
						free(endp);
						return -1;
					}
					x += (short)be16(f->d + off + pos);
					pos += 2;
				}
				pts[k].x = x;
			}
		}
		/* y coordinates */
		{
			long y = 0;

			for(k = 0; k <= last_pt; k++) {
				unsigned char fl = flags[k];

				if(fl & 0x04) {	/* Y_SHORT */
					if(off + pos >= f->len) {
						free(flags);
						free(endp);
						return -1;
					}
					y += (fl & 0x20) ?
					     f->d[off + pos++] :
					     -f->d[off + pos++];
				} else if(!(fl & 0x20)) {
					if(off + pos + 2 > f->len) {
						free(flags);
						free(endp);
						return -1;
					}
					y += (short)be16(f->d + off + pos);
					pos += 2;
				}
				pts[k].y = y;
			}
		}
		for(k = 0; k <= last_pt; k++) {
			onb[k] = (flags[k] & 0x01) ? 1 : 0;
		}
		/* per contour */
		{
			unsigned int start = 0;

			for(ci = 0; ci < (unsigned int)ncont; ci++) {
				unsigned int n = endp[ci] - start + 1;

				contour_to_segs(pts + start, onb + start,
						(int)n, tol, segs, nsegs,
						cap);
				start = endp[ci] + 1;
			}
		}
		free(flags);
		free(endp);
	}
	return 0;
}

/* ---- rasterization -------------------------------------------------- */

/* scanline even-odd fill of the segment list, scaled to 'size' px.
 * Design y is up; screen y is down. */
static unsigned char *rasterize(ttf_face_t *f, ttf_seg *segs, int nsegs,
				int size, int *out_w, int *out_h,
				int *out_x0, int *out_y0)
{
	double s = (double)size / (double)f->upem;
	double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
	int i, w, h, x0, y0;
	double *xs;
	int *dirs;
	unsigned char *bm;
	int py;

	if(nsegs <= 0) {
		*out_w = *out_h = *out_x0 = *out_y0 = 0;
		return NULL;
	}
	/* bounds in the y-flipped pixel space */
	for(i = 0; i < nsegs; i++) {
		double ax = segs[i].x0 * s, bx = segs[i].x1 * s;
		double ay = (double)size - segs[i].y0 * s;
		double by = (double)size - segs[i].y1 * s;

		if(ax < minx) {
			minx = ax;
		}
		if(bx < minx) {
			minx = bx;
		}
		if(ax > maxx) {
			maxx = ax;
		}
		if(bx > maxx) {
			maxx = bx;
		}
		if(ay < miny) {
			miny = ay;
		}
		if(by < miny) {
			miny = by;
		}
		if(ay > maxy) {
			maxy = ay;
		}
		if(by > maxy) {
			maxy = by;
		}
	}
	/* clip to sane bounds + a 1px pad */
	if(minx < 0) {
		minx = 0;
	}
	if(miny < 0) {
		miny = 0;
	}
	{
		double em = (double)size * 1.4;

		if(maxx > em) {
			maxx = em;
		}
		if(maxy > em) {
			maxy = em;
		}
	}
	x0 = (int)minx;
	y0 = (int)miny;
	w = (int)(maxx - minx) + 2;
	h = (int)(maxy - miny) + 2;
	if(w > 4096) {
		w = 4096;
	}
	if(h > 4096) {
		h = 4096;
	}
	if(w <= 0 || h <= 0) {
		*out_w = *out_h = *out_x0 = *out_y0 = 0;
		return NULL;
	}
	bm = calloc((size_t)w * (size_t)h, 1);
	if(!bm) {
		return NULL;
	}
	/* per scanline: collect crossings */
	xs = malloc(sizeof(double) * (nsegs + 1));
	dirs = malloc(sizeof(int) * (nsegs + 1));
	if(!xs || !dirs) {
		free(xs);
		free(dirs);
		free(bm);
		return NULL;
	}
	for(py = 0; py < h; py++) {
		double cy = (double)(y0 + py) + 0.5;	/* row centre */
		int n = 0;
		int ix;

		for(i = 0; i < nsegs; i++) {
			double xa = segs[i].x0 * s;
			double xb = segs[i].x1 * s;
			double ya = (double)size - segs[i].y0 * s;
			double yb = (double)size - segs[i].y1 * s;

			if((ya <= cy && yb > cy) || (yb <= cy && ya > cy)) {
				double t = (cy - ya) / (yb - ya);
				double x = xa + (xb - xa) * t;

				if(n < nsegs) {
					xs[n] = x;
					dirs[n] = (yb > ya) ? 1 : -1;
					n++;
				}
			}
		}
		/* insertion sort by x */
		for(i = 1; i < n; i++) {
			double v = xs[i];
			int d = dirs[i];
			int j = i - 1;

			while(j >= 0 && xs[j] > v) {
				xs[j + 1] = xs[j];
				dirs[j + 1] = dirs[j];
				j--;
			}
			xs[j + 1] = v;
			dirs[j + 1] = d;
		}
		/* even-odd: fill between pairs */
		for(ix = 0; ix + 1 < n; ix += 2) {
			int xa = (int)xs[ix] - x0;
			int xb = (int)xs[ix + 1] - x0 - 1;

			if(xa < 0) {
				xa = 0;
			}
			if(xb >= w) {
				xb = w - 1;
			}
			for(; xa <= xb; xa++) {
				bm[(size_t)py * w + xa] = 1;
			}
		}
	}
	free(xs);
	free(dirs);
	*out_w = w;
	*out_h = h;
	*out_x0 = x0;
	*out_y0 = y0;
	return bm;
}

int ttf_render(ttf_face_t *f, int glyph, int size,
	       unsigned char **out, int *w, int *h, int *x0, int *y0)
{
	ttf_seg *segs;
	int nsegs = 0, cap = MAX_SEGS;
	long tol;
	unsigned char *bm;

	*out = NULL;
	*w = *h = *x0 = *y0 = 0;
	if(!f || glyph < 0 || size <= 0) {
		return -1;
	}
	segs = malloc(sizeof(ttf_seg) * cap);
	if(!segs) {
		return -1;
	}
	tol = (long)f->upem / (size * 2);
	if(tol < 1) {
		tol = 1;
	}
	if(glyph_decode(f, (unsigned int)glyph, tol, segs, &nsegs, cap, 0) < 0) {
		free(segs);
		return -1;
	}
	bm = rasterize(f, segs, nsegs, size, w, h, x0, y0);
	free(segs);
	if(!bm) {
		return (*w == 0) ? -1 : -1;
	}
	*out = bm;
	return 0;
}
