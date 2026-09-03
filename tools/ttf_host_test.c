/* ttf_host_test.c - host-side proof of the minimal TrueType rasterizer:
 * parse DejaVuSans, dump the metrics, glyph ids + advances for a few
 * chars, rasterize several glyphs at 16px and report ink coverage, and
 * write PGM dumps so the shapes can be eyeballed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font_ttf.h"

static unsigned char *load_file(const char *path, size_t *len)
{
	FILE *fp;
	long n;
	unsigned char *d;

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
	d = malloc((size_t)n);
	if(!d || fread(d, 1, (size_t)n, fp) != (size_t)n) {
		free(d);
		fclose(fp);
		return NULL;
	}
	fclose(fp);
	*len = (size_t)n;
	return d;
}

static void dump_pgm(const char *path, const unsigned char *bm,
		     int w, int h)
{
	FILE *fp = fopen(path, "wb");
	int x, y;

	if(!fp) {
		return;
	}
	fprintf(fp, "P1\n%d %d\n", w, h);
	for(y = 0; y < h; y++) {
		for(x = 0; x < w; x++) {
			fprintf(fp, "%c", bm[y * w + x] > 127 ? '1' : '0');
		}
		fputc('\n', fp);
	}
	fclose(fp);
}

/* print a small coverage ASCII-art of a glyph (for the log) */
static void dump_ascii(const unsigned char *bm, int w, int h)
{
	int x, y;

	for(y = 0; y < h; y++) {
		char line[128];
		int n = 0;

		for(x = 0; x < w && n < 120; x++) {
			int c = bm[y * w + x];

			line[n++] = c > 200 ? '#' : c > 100 ? '+' :
				   c > 20 ? '.' : ' ';
		}
		line[n] = 0;
		printf("  |%s|\n", line);
	}
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] :
		"userland/fonts/DejaVuSans.ttf";
	unsigned char *data;
	size_t len;
	ttf_face_t *f;
	int fails = 0;
	static const char *probe = "Agé 0";
	int i;

	data = load_file(path, &len);
	if(!data) {
		printf("FAIL: cannot load %s\n", path);
		return 1;
	}
	f = ttf_open(data, len);
	if(!f) {
		printf("FAIL: ttf_open failed\n");
		free(data);
		return 1;
	}
	printf("upem=%d asc=%d desc=%d gap=%d line16=%d\n",
	       ttf_units_per_em(f), ttf_ascender(f), ttf_descender(f),
	       ttf_linegap(f), ttf_lineheight_px(f, 16));
	if(ttf_units_per_em(f) != 2048) {
		printf("FAIL: upem\n");
		fails++;
	}

	/* glyph ids + advances */
	for(i = 0; probe[i]; i++) {
		unsigned int cp = (unsigned char)probe[i];
		int g = ttf_glyph_index(f, cp);
		int adv = ttf_advance_px(f, g, 16);

		printf("'%c' U+%04x glyph %d adv16=%d\n",
		       probe[i], cp, g, adv);
		if(g <= 0 && probe[i] != ' ') {
			printf("FAIL: no glyph for '%c'\n", probe[i]);
			fails++;
		}
	}

	/* rasterize: 'A' must have ink; 'o' must have a hole (a
	 * donut: the interior pixel at the middle is empty) */
	{
		static const unsigned int testcps[] = {
			'A', 'g', 'e', 'o', '0', 0xE9 /* e-acute */, 0
		};
		int t;

		for(t = 0; testcps[t]; t++) {
			unsigned int cp = testcps[t];
			int g = ttf_glyph_index(f, cp);
			unsigned char *bm = NULL;
			int w = 0, h = 0, x0 = 0, y0 = 0;
			int ink = 0, x, y;
			char name[64];

			if(ttf_render(f, g, 16, &bm, &w, &h, &x0, &y0)) {
				printf("FAIL: render U+%04x\n", cp);
				fails++;
				continue;
			}
			if(!bm) {
				printf("FAIL: render U+%04x empty\n", cp);
				fails++;
				continue;
			}
			int soft = 0;

			for(y = 0; y < h; y++) {
				for(x = 0; x < w; x++) {
					if(bm[y * w + x] > 127) {
						ink++;
					}
					if(bm[y * w + x] > 0 &&
					   bm[y * w + x] < 255) {
						soft++;
					}
				}
			}
			printf("U+%04x glyph %d: %dx%d at +%d,+%d solid %d "
			       "soft %d\n", cp, g, w, h, x0, y0, ink, soft);
			snprintf(name, sizeof(name),
				 ".build/ttf_U%04x.pgm", cp);
			dump_pgm(name, bm, w, h);
			if(cp == 'A' || cp == 'o' || cp == 0xE9) {
				printf("  shape (thresholded):\n");
				dump_ascii(bm, w, h);
			}
			if(ink < 8) {
				printf("FAIL: U+%04x too little ink\n", cp);
				fails++;
			}
			if(soft == 0) {
				printf("FAIL: U+%04x no anti-aliased pixels\n",
				       cp);
				fails++;
			}
			free(bm);
		}
		/* the 'o' counter (hole) check */
		{
			int g = ttf_glyph_index(f, 'o');
			unsigned char *bm = NULL;
			int w = 0, h = 0, x0 = 0, y0 = 0, cx, cy;

			ttf_render(f, g, 16, &bm, &w, &h, &x0, &y0);
			if(bm) {
				cx = w / 2;
				cy = h / 2;
				if(bm[cy * w + cx]) {
					printf("FAIL: 'o' counter filled "
					       "(cov %d)\n",
					       bm[cy * w + cx]);
					fails++;
				} else {
					printf("'o' counter: empty at "
					       "(%d,%d) OK\n", cx, cy);
				}
				free(bm);
			}
		}
	}

	ttf_close(f);
	free(data);
	printf("%s: %d failure(s)\n", fails ? "TTF-HOST" : "TTF-HOST", fails);
	return fails ? 1 : 0;
}
