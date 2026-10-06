/*
 * cgtext_demo — Core Graphics AND text, on the guest's screen.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS IS: a combined demonstration, run IN THE GUEST, of the whole Core Graphics stack this
 * tree has built — a bitmap context, fills, a path with curves, a linear gradient, a stroke — WITH
 * TEXT DRAWN BY THE ONE TEXT DOOR THAT EXISTS (`CGContextShowGlyphsAtPositions`, the seam Core Text
 * needs), laid out with REAL advances from `CGFontGetGlyphAdvances`. The surface is then put on the
 * framebuffer through `/dev/fb0`'s mmap, which is what makes it visible to a screendump.
 *
 * THE LAYOUT IS BY GLYPH NAME — a character map is still an owed door, and a font spells its digits
 * out ("zero", "one", ... proving it here would be a nice detail if anyone asked). Letters are their
 * own names, and a space is "space".
 *
 * THE DEVICE IS `/System/Devices/Display/fb0` — the path Xfb itself opens — with `/dev/fb0` as a
 * fallback, because the devfs symlink under /dev is RELATIVE and did not open on the first guest
 * run of this demo (the shell said "no /dev/fb0 to draw on", which is what sent me looking).
 *
 * THE MARKER IS PRINTED BEFORE THE BLIT, DELIBERATELY: printing writes to the same framebuffer (the
 * kernel's console owns it), so a marker printed after the blit would draw over the picture. Between
 * the marker and the sleep the screen holds the demonstration, which is the window a screendump
 * needs — hence the sleep, and hence this note.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGDataProvider.h>
#include <CoreGraphics/CGGradient.h>
#include <CoreGraphics/CGPath.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* fb0's mode ioctls and the struct they carry (include/fnx/fb.h), spelled out here because that
 * header pulls in kernel-only types — the same choice userland/tools/init.c makes and says so. */
#define IO_FB_GETMODE	4
struct fb_mode {
	unsigned int width;
	unsigned int height;
	unsigned int bpp;
	unsigned int pitch;
};

#define CARD_W 900
#define CARD_H 520
#define FONT_PATH "/System/Shared/Fonts/DejaVuSans.ttf"

static CGContextRef ctx;
static CGFontRef font;
static int units;

/* LAYOUT BY GLYPH NAME, with the advances the font actually reports: this is a caller doing the
 * arithmetic the ADVANCE DOORS would do for it, which is exactly why it is written out. */
static double draw_run(CGContextRef c, const char *s, double x, double y, double size)
{
	double pen = x;
	const char *p;

	for (p = s; *p != '\0'; p++) {
		CGGlyph g;
		CGPoint pos;
		int advance = 0;
		char name[8];
		NSString *key;

		if (*p == ' ') {
			key = @"space";
		} else if (*p >= '0' && *p <= '9') {
			static const char *digits[10] = { "zero", "one", "two", "three", "four",
							  "five", "six", "seven", "eight", "nine" };

			key = [NSString stringWithUTF8String:digits[*p - '0']];
		} else {
			name[0] = *p;
			name[1] = '\0';
			key = [NSString stringWithUTF8String:name];
		}
		g = CGFontGetGlyphWithGlyphName(font, key);
		if (g == 0 || g == (CGGlyph)kCGFontIndexInvalid) {
			pen += size * 0.4;
			continue;
		}
		pos = CGPointMake(pen, y);
		CGContextShowGlyphsAtPositions(c, &g, &pos, 1);
		if (CGFontGetGlyphAdvances(font, &g, 1, &advance)) {
			pen += (double)advance * size / (double)units;
		}
	}
	return pen;
}

static void draw_card(CGContextRef c, double x, double y, double w, double h)
{
	/* A CARD: a light panel, a coloured header, a framed gradient band, a curve, a stroke and the
	 * text — every part of the stack this tree has, in one picture. */
	CGGradientRef gradient;
	CGFloat stops[8];
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGMutablePathRef oval;

	CGContextSetRGBFillColor(c, 0.98, 0.98, 0.96, 1.0);
	CGContextFillRect(c, CGRectMake(x, y, w, h));

	CGContextSetRGBFillColor(c, 0.10, 0.22, 0.45, 1.0);
	CGContextFillRect(c, CGRectMake(x, y + h - 70.0, w, 70.0));

	stops[0] = 0.13; stops[1] = 0.42; stops[2] = 0.72; stops[3] = 1.0;
	stops[4] = 0.95; stops[5] = 0.72; stops[6] = 0.30; stops[7] = 1.0;
	gradient = CGGradientCreateWithColorComponents(space, stops, NULL, 2);
	if (gradient != NULL) {
		CGContextDrawLinearGradient(c, gradient, CGPointMake(x + 40.0, y + h - 150.0),
					    CGPointMake(x + w - 40.0, y + h - 150.0), 0);
		CGGradientRelease(gradient);
	}
	CGColorSpaceRelease(space);

	/* A CURVE: an ellipse outline, stroked. */
	oval = CGPathCreateMutable();
	CGPathAddEllipseInRect(oval, NULL, CGRectMake(x + 60.0, y + 70.0, 200.0, 140.0));
	CGContextAddPath(c, (CGPathRef)oval);
	CGContextSetRGBStrokeColor(c, 0.70, 0.15, 0.10, 1.0);
	CGContextSetLineWidth(c, 6.0);
	CGContextStrokePath(c);
	CGPathRelease((CGPathRef)oval);

	/* SOLID SWATCHES, to give the palette something to check. */
	CGContextSetRGBFillColor(c, 0.70, 0.15, 0.10, 1.0);
	CGContextFillRect(c, CGRectMake(x + 320.0, y + 70.0, 120.0, 60.0));
	CGContextSetRGBFillColor(c, 0.15, 0.55, 0.25, 1.0);
	CGContextFillRect(c, CGRectMake(x + 460.0, y + 70.0, 120.0, 60.0));
	CGContextSetRGBFillColor(c, 0.95, 0.72, 0.30, 1.0);
	CGContextFillRect(c, CGRectMake(x + 600.0, y + 70.0, 120.0, 60.0));

	/* THE TEXT: a title, a line, and the digits — the door this milestone added. */
	CGContextSetFont(c, font);
	CGContextSetRGBFillColor(c, 1.0, 1.0, 1.0, 1.0);
	CGContextSetFontSize(c, 44.0);
	(void)draw_run(c, "Core Graphics on FNX", x + 36.0, y + h - 48.0, 44.0);
	CGContextSetRGBFillColor(c, 0.12, 0.12, 0.14, 1.0);
	CGContextSetFontSize(c, 28.0);
	(void)draw_run(c, "rendered by FreeType, laid out by advance 0123", x + 36.0, y + 250.0, 28.0);
	CGContextSetFontSize(c, 20.0);
	(void)draw_run(c, "shapes, a curve, a gradient, a stroke, and glyphs", x + 36.0, y + 210.0, 20.0);
}

int main(int argc, char **argv)
{
	struct fb_mode mode;
	int hold = 10;
	unsigned char *fb;
	const unsigned char *pixels;
	CGDataProviderRef provider;
	int fd, y, ink = 0;
	size_t fbbytes;

	provider = CGDataProviderCreateWithFilename(FONT_PATH);
	if (provider == NULL) {
		printf("CGTEXT-DEMO: cannot read %s\n", FONT_PATH);
		return 1;
	}
	font = CGFontCreateWithDataProvider(provider);
	if (font == NULL) {
		printf("CGTEXT-DEMO: cannot open the font\n");
		return 1;
	}
	units = CGFontGetUnitsPerEm(font);

	fd = open("/System/Devices/Display/fb0", O_RDWR);
	if (fd < 0) {
		fd = open("/dev/fb0", O_RDWR);
	}
	if (fd < 0) {
		printf("CGTEXT-DEMO: no framebuffer: %m\n");
		return 1;
	}
	memset(&mode, 0, sizeof mode);
	if (ioctl(fd, IO_FB_GETMODE, &mode) < 0 || mode.width == 0 || mode.height == 0) {
		printf("CGTEXT-DEMO: GETMODE failed\n");
		return 1;
	}
	printf("CGTEXT-DEMO: framebuffer %ux%u %ubpp pitch %u\n", mode.width, mode.height, mode.bpp,
	       mode.pitch);
	if (mode.bpp != 32) {
		printf("CGTEXT-DEMO: the surface this library draws is 32bpp and this fb is %ubpp; "
		       "refusing rather than guessing a conversion\n", mode.bpp);
		return 1;
	}
	fbbytes = (size_t)mode.pitch * (size_t)mode.height;
	fb = mmap(NULL, fbbytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		printf("CGTEXT-DEMO: mmap of /dev/fb0 failed\n");
		return 1;
	}

	/* THE SURFACE IS THE WHOLE SCREEN, and the card is centred in it: one band of arithmetic, so the
	 * picture is deliberate at 640x480 and at 1080p alike. */
	ctx = CGBitmapContextCreate(NULL, mode.width, mode.height, 8, mode.width * 4,
				    CGColorSpaceCreateDeviceRGB(),
				    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	if (ctx == NULL) {
		printf("CGTEXT-DEMO: no bitmap context\n");
		return 1;
	}
	CGContextSetRGBFillColor(ctx, 0.86, 0.87, 0.90, 1.0);
	CGContextFillRect(ctx, CGRectMake(0.0, 0.0, (CGFloat)mode.width, (CGFloat)mode.height));
	draw_card(ctx, (double)(mode.width - CARD_W) / 2.0, (double)(mode.height - CARD_H) / 2.0,
		  (double)CARD_W, (double)CARD_H);

	pixels = (const unsigned char *)CGBitmapContextGetData(ctx);
	for (y = 0; y < (int)(mode.width * mode.height); y++) {
		if (pixels[(size_t)y * 4 + 2] < 200) {
			ink++;
		}
	}
	printf("CGTEXT-DEMO: %d inked pixel(s) of %u\n", ink, mode.width * mode.height);
	printf("CGTEXT-DEMO-READY\n");
	fflush(stdout);

	/* THE BLIT, AFTER THE MARKER: the console writes to this same framebuffer, so the picture has to
	 * land last and stay there while a screendump is taken. */
	/* THE HOLD IS THE CALLER'S: `cgtext_demo 60` keeps the picture up for a minute, which is what
	 * a MANUAL run needs (the console is quiet meanwhile, so the frame stays as painted). AND THE
	 * LINE IS PRINTED BEFORE THE BLIT, for the reason the marker is: printing writes to THIS
	 * framebuffer, so anything printed after the blit lands on top of the picture. */
	if (argc > 1) {
		hold = atoi(argv[1]);
	}
	printf("CGTEXT-DEMO: holding the picture for %d second(s)\n", hold);
	fflush(stdout);

	for (y = 0; y < (int)mode.height; y++) {
		memcpy(fb + (size_t)y * mode.pitch, pixels + (size_t)y * mode.width * 4,
		       (size_t)mode.width * 4);
	}
	msync(fb, fbbytes, MS_SYNC);
	sleep((unsigned)hold);
	munmap(fb, fbbytes);
	close(fd);
	CGContextRelease(ctx);
	CGFontRelease(font);
	CGDataProviderRelease(provider);
	return 0;
}
