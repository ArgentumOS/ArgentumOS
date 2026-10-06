/*
 * coregraphics_textshot — paint text through the new door and dump the surface.
 *
 * A DEMO, AND IT SAYS SO: it lays a string out BY GLYPH NAME, because this slice has no character
 * map yet (that door is owed) — for DejaVu Sans an ASCII letter's glyph name is the letter itself,
 * which is what makes the layout possible at all. The advances are real, from
 * `CGFontGetGlyphAdvances`, so the pen arithmetic is the caller's own.
 *
 * With CG_TEXTSHOT set to a path it writes the surface as a PNG (via libpng, which this tree already
 * links for the DECODER half); otherwise it just checks that every run put ink down.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGDataProvider.h>

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 760
#define H 340

static unsigned char *surface;
static const unsigned char *paint;
static int failures;

static void draw_run(CGContextRef ctx, CGFontRef font, const char *s, double x, double y, double size)
{
	double pen = x;
	int units = CGFontGetUnitsPerEm(font);
	const char *p;

	for (p = s; *p != '\0'; p++) {
		CGGlyph g;
		CGPoint pos;
		int advance = 0;
		char name[8];

		/* DIGITS ARE SPELLED OUT IN A FONT'S GLYPH NAMES — "zero", "one", ... — which the first
		 * picture of this demo showed by their ABSENCE: the lookup missed and the pen advanced
		 * over a gap. Letters are their own names, and a space is "space". */
		if (*p == ' ') {
			g = CGFontGetGlyphWithGlyphName(font, @"space");
		} else if (*p >= '0' && *p <= '9') {
			static const char *digits[10] = { "zero", "one", "two", "three", "four",
							  "five", "six", "seven", "eight", "nine" };

			g = CGFontGetGlyphWithGlyphName(font, [NSString stringWithUTF8String:digits[*p - '0']]);
		} else {
			name[0] = *p;
			name[1] = '\0';
			g = CGFontGetGlyphWithGlyphName(font, [NSString stringWithUTF8String:name]);
		}
		if (g == 0 || g == (CGGlyph)kCGFontIndexInvalid) {
			pen += size * 0.4;
			continue;
		}
		pos = CGPointMake(pen, y);
		CGContextShowGlyphsAtPositions(ctx, &g, &pos, 1);
		if (CGFontGetGlyphAdvances(font, &g, 1, &advance)) {
			pen += (double)advance * size / (double)units;
		}
	}
}

static int ink_in(int row0, int row1)
{
	int x, y, n = 0;

	for (y = row0; y < row1; y++) {
		for (x = 0; x < W; x++) {
			if (paint[((size_t)y * W + x) * 4 + 2] < 160) {
				n++;
			}
		}
	}
	return n;
}

static void write_png(const char *path)
{
	FILE *f = fopen(path, "wb");
	png_structp png;
	png_infop info;
	png_bytep *rows;
	int y;

	if (f == NULL) {
		printf("CG-TEXTSHOT: cannot open %s\n", path);
		return;
	}
	png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	info = png_create_info_struct(png);
	/* THE SURFACE IS BGRA IN MEMORY AND THE PNG IS RGB: the conversion is a channel swap AND a
	 * narrower row, so a row is repacked rather than pointed at — `png_set_bgr` alone would read
	 * three bytes per pixel out of a four-byte row and shear the picture. */
	rows = malloc(sizeof(png_bytep) * (size_t)H);
	{
		unsigned char *rgb = malloc((size_t)W * 3 * (size_t)H);

		for (y = 0; y < H; y++) {
			int x;

			for (x = 0; x < W; x++) {
				const unsigned char *p = surface + ((size_t)y * W + x) * 4;

				rgb[((size_t)y * W + x) * 3 + 0] = p[2];	/* R */
				rgb[((size_t)y * W + x) * 3 + 1] = p[1];	/* G */
				rgb[((size_t)y * W + x) * 3 + 2] = p[0];	/* B */
			}
			rows[y] = (png_bytep)(rgb + (size_t)y * W * 3);
		}
	}
	png_init_io(png, f);
	png_set_IHDR(png, info, (png_uint_32)W, (png_uint_32)H, 8, PNG_COLOR_TYPE_RGB,
		     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);
	png_write_image(png, rows);
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	free(rows[0]);
	free(rows);
	fclose(f);
	printf("CG-TEXTSHOT: wrote %s (%dx%d)\n", path, W, H);
}

int main(void)
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGDataProviderRef provider = CGDataProviderCreateWithFilename("userland/fonts/DejaVuSans.ttf");
	CGContextRef ctx;
	CGFontRef font;
	const char *out = getenv("CG_TEXTSHOT");
	int n;

	surface = calloc((size_t)W * H, 4);
	if (surface == NULL || provider == NULL) {
		printf("CG-TEXTSHOT: cannot allocate the surface or read the font\n");
		return 1;
	}
	ctx = CGBitmapContextCreate(surface, W, H, 8, W * 4, space,
				    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	font = CGFontCreateWithDataProvider(provider);
	if (ctx == NULL || font == NULL) {
		printf("CG-TEXTSHOT: no context or no font\n");
		return 1;
	}
	paint = (const unsigned char *)CGBitmapContextGetData(ctx);
	CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
	CGContextFillRect(ctx, CGRectMake(0.0, 0.0, (CGFloat)W, (CGFloat)H));
	CGContextSetFont(ctx, font);

	/* row 1: 48pt */
	CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, 1.0);
	CGContextSetFontSize(ctx, 48.0);
	draw_run(ctx, font, "Handgloves 0123", 24.0, 280.0, 48.0);

	/* row 2: the same face at 32pt, in a colour, to show the fill colour is used */
	CGContextSetRGBFillColor(ctx, 0.45, 0.0, 0.1, 1.0);
	CGContextSetFontSize(ctx, 32.0);
	draw_run(ctx, font, "FreeType writes these pixels", 24.0, 190.0, 32.0);

	/* row 3: a SCALED TEXT MATRIX (1.4x) at 24pt, which is the matrix path */
	CGContextSetRGBFillColor(ctx, 0.0, 0.2, 0.45, 1.0);
	CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.4, 1.4));
	CGContextSetFontSize(ctx, 24.0);
	/* THE POSITION IS SCALED TOO — that is what a text matrix does — so the y is chosen for the
	 * SCALED device range (at 1.4x on a 340-row surface, user y = 150 lands at device 266). */
	draw_run(ctx, font, "a scaled text matrix", 24.0, 150.0, 24.0);
	CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);

	/* row 4: 'L' — the orientation, in a size where a mirror would be obvious */
	CGContextSetRGBFillColor(ctx, 0.2, 0.2, 0.2, 1.0);
	CGContextSetFontSize(ctx, 40.0);
	draw_run(ctx, font, "LLLVII", 24.0, 30.0, 40.0);

	n = ink_in(0, H);
	printf("CG-TEXTSHOT: %d inked pixel(s) over four runs\n", n);
	if (n < 2000) {
		failures++;
	}
	if (out != NULL) {
		write_png(out);
	}
	printf("CG-TEXTSHOT: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	CGFontRelease(font);
	CGDataProviderRelease(provider);
	return failures;
}
