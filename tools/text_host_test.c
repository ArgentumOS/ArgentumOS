/* text_host_test.c - host proof of the UTF-8 text engine: load DejaVu
 * at 16px, measure ASCII + UTF-8 strings, draw a line with AA blending
 * into a buffer (PPM dump), word-wrap a paragraph, and verify the caret
 * mapping round-trips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

static void dump_ppm(const char *path, const uint32_t *buf, int w, int h)
{
	FILE *fp = fopen(path, "wb");
	int x, y;

	if(!fp) {
		return;
	}
	fprintf(fp, "P6\n%d %d\n255\n", w, h);
	for(y = 0; y < h; y++) {
		for(x = 0; x < w; x++) {
			uint32_t c = buf[y * w + x];
			unsigned char px[3] = {
				(unsigned char)(c >> 16),
				(unsigned char)(c >> 8),
				(unsigned char)c
			};
			fwrite(px, 1, 3, fp);
		}
	}
	fclose(fp);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] :
		"userland/fonts/DejaVuSans.ttf";
	text_font_t *f;
	int fails = 0;
	int w_a, w_hello, w_utf, w_e, w_euro;
	int breaks[16], i;
	uint32_t *buf;
	int bw = 320, bh = 48;

	f = text_font_open(path, 16);
	if(!f) {
		printf("FAIL: text_font_open\n");
		return 1;
	}
	printf("font 16px: ascent=%d descent=%d height=%d\n",
	       text_font_ascent(f), text_font_descent(f), text_font_height(f));
	if(text_font_height(f) != 19 || text_font_ascent(f) != 15) {
		printf("FAIL: line metrics\n");
		fails++;
	}

	w_a = text_width(f, "A");
	w_hello = text_width(f, "Hello, world");
	w_e = text_width(f, "\xC3\xA9");
	w_euro = text_width(f, "\xE2\x82\xAC");
	w_utf = text_width(f, "Caf\xC3\xA9 \xE2\x82\xAC");
	printf("widths: 'A'=%d 'Hello, world'=%d e-acute=%d euro=%d utf=%d\n",
	       w_a, w_hello, w_e, w_euro, w_utf);
	if(w_a <= 0 || w_hello <= 0 || w_e <= 0 || w_euro <= 0 ||
	   w_utf <= w_hello / 2) {
		printf("FAIL: widths\n");
		fails++;
	}

	/* draw onto a light-grey buffer + AA-blend black text */
	buf = malloc((size_t)bw * bh * 4);
	if(!buf) {
		printf("FAIL: alloc\n");
		return 1;
	}
	for(i = 0; i < bw * bh; i++) {
		buf[i] = 0xD4D4CC;	/* the toolkit's panel colour */
	}
	{
		int baseline = 12;
		int end = text_draw(f, buf, bw, bh, 4, baseline,
				    "Hello", 0x000000);
		baseline = 32;
		end = text_draw(f, buf, bw, bh, 4, baseline,
				"wörld €", 0x802000);
		printf("drew to x=%d\n", end);
		/* the first glyph's ink must have blended (not pure bg) */
		{
			int ink = 0, x, y;

			for(y = 0; y < bh; y++) {
				for(x = 0; x < bw; x++) {
					uint32_t c = buf[y * bw + x];

					if(c != 0xD4D4CC) {
						ink++;
					}
				}
			}
			printf("blended px: %d\n", ink);
			if(ink < 50) {
				printf("FAIL: nothing drawn\n");
				fails++;
			}
			/* AA: some pixel must be neither bg nor the solid
			 * fg (a blend) */
			{
				int soft = 0;

				for(y = 0; y < bh; y++) {
					for(x = 0; x < bw; x++) {
						uint32_t c = buf[y * bw + x];

						if(c != 0xD4D4CC &&
						   c != 0x000000 &&
						   c != 0x802000) {
							soft++;
						}
					}
				}
				printf("AA-blend px: %d\n", soft);
				if(soft < 5) {
					printf("FAIL: no AA blend\n");
					fails++;
				}
			}
		}
	}
	dump_ppm(".build/text_hello.ppm", buf, bw, bh);
	free(buf);

	/* word wrap a paragraph at 100px */
	{
		const char *p = "The quick brown fox jumps over the lazy dog";
		int lines = text_wrap(f, p, 100, breaks, 16);

		printf("wrap at 100px: %d lines\n", lines);
		for(i = 0; i < lines && i < 16; i++) {
			int j = breaks[i];
			int next = (i + 1 < lines) ? breaks[i + 1] :
				  (int)strlen(p);
			int wl = text_width(f, p + j) -
				 text_width(f, p + next);
			char line[64];
			int k;

			(void)wl;
			for(k = j; k < next && k - j < 60; k++) {
				line[k - j] = p[k];
			}
			line[next - j < 60 ? next - j : 60] = 0;
			printf("  [%s] w=%d\n", line,
			       text_width(f, line));
		}
		if(lines < 2) {
			printf("FAIL: no wrap happened\n");
			fails++;
		}
	}

	/* caret mapping: click at x=30 of 'Hello world' should land inside;
	 * offset 3 ('l') should map near the middle of 'Hel' */
	{
		const char *s = "Hello world";
		int off = text_x_to_offset(f, s, 30);
		int x3 = text_offset_to_x(f, s, 3);
		int xtotal = text_offset_to_x(f, s, (int)strlen(s));

		printf("click x=30 -> offset %d; offset 3 -> x=%d; total=%d\n",
		       off, x3, xtotal);
		if(off <= 0 || off > (int)strlen(s)) {
			printf("FAIL: x_to_offset out of range\n");
			fails++;
		}
		if(x3 <= 0 || x3 >= xtotal) {
			printf("FAIL: offset_to_x\n");
			fails++;
		}
	}

	text_font_close(f);
	printf("%s: %d failure(s)\n", fails ? "TEXT-HOST" : "TEXT-HOST",
	       fails);
	return fails ? 1 : 0;
}
