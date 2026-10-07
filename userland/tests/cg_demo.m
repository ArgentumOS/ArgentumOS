/*
 * cg_demo — Core Graphics in one picture: THE SHADOWS, and the rest of the stack with them.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE SOURCE AND TWO SINKS, WHICH IS THE POINT OF THE FILE. In the guest it blits to
 * `/System/Devices/Display/fb0` (with `/dev/fb0` as a fallback, because the devfs symlink under /dev
 * is RELATIVE) and the screenshot is the evidence; on the host the same code path draws the same
 * surface and writes it out as a `.ppm`, because this library has no PNG ENCODER — only the decoder —
 * and a `.ppm` is what the test harness already produces and anyone can open. A demo that existed
 * twice would drift; a demo that draws ONCE cannot.
 *
 *     make demo-coregraphics           the host build, seconds, writes .build/cg-demo.ppm
 *     /System/Shared/tests/cg_demo 60  the guest build, on the real screen for a minute
 *
 * WHAT IS ON THE CARD, top to bottom:
 *
 *   1. THE SHADOW ROW, WHICH IS THE HEADLINE. Four identical panels, one shadow setting each:
 *      a HARD shadow (`CGContextSetShadow`, which is Apple's black at 1/3 alpha, offset 24 right and
 *      24 down), a BLURRED one (the same shadow with `blur` 18), a COLOURED one
 *      (`CGContextSetShadowWithColor` with a translucent blue) and one with shadowing OFF — which is
 *      a FULLY TRANSPARENT COLOUR and not a flag, Apple's own definition, and the reason that fourth
 *      panel demonstrates the door's off state rather than its absence. The offsets are in BASE space
 *      and land in device space through the CTM, which is why they are whole numbers here.
 *   2. TEXT through the string doors (`CGContextSelectFont` names the font AND the encoding,
 *      `CGContextShowTextAtPoint` maps each byte through the font's own character map), including
 *      digits, whose advance is the font's own.
 *   3. A LINEAR GRADIENT band with a RADIAL one over its right half.
 *   4. A dashed stroked ellipse, a Bezier stroke, three solid swatches, a fill CLIPPED to an ellipse,
 *      and a TRANSPARENCY LAYER holding two overlapping translucent squares blended with
 *      `kCGBlendModeMultiply`.
 *
 * THE MAP, AND WHY IT IS PRINTED. The demo prints `CG-DEMO-MAP <role> <x> <row>` for every place a
 * check should read, in SCREEN coordinates — the row already flipped, so nothing outside this file
 * has to know that a CG y is row (height - 1 - y). The checks then assert PIXEL FACTS at those points
 * (this one is darker than the card, that one is not, this one is blue-dominant) and never ask the
 * demo what it drew. A wrong coordinate in the map makes a check FAIL rather than pass, which is what
 * keeps the map from being testimony.
 *
 * EVERYTHING PRINTED IS PRINTED BEFORE THE BLIT, DELIBERATELY: the kernel's console owns the same
 * framebuffer, so a line printed after the picture would draw over it. Between the marker and the
 * sleep the screen holds the picture, which is the window a screendump needs — hence the hold.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGFont.h>
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

/* THE CARD, AND THE ONE PLACE THE LAYOUT LIVES. If a check and this table ever disagree, the check
 * fails — it does not follow the demo. */
#define CARD_W		900
#define CARD_H		520
#define PANEL_X0	40		/* the shadow row: four panels, 210 apart */
#define PANEL_DX	210
#define PANEL_Y		250
#define PANEL_W		130
#define PANEL_H		70
#define SHADOW_DX	24		/* right and DOWN on the screen, which is -y in CG space */
#define SHADOW_DY	(-24)
#define BLUR		18
#define STRIP_Y		150		/* the gradient band, bottom edge */
#define STRIP_H		50
#define SWATCH_Y	60
#define HEADER_H	70

/* WHERE A CHECK SHOULD READ. All are card-local CG units except the backdrop point, which is in
 * SURFACE coordinates because the whole point of it is that it is OUTSIDE the card. */
#define P_BACKDROP_X	20
#define P_BACKDROP_Y	20
#define P_CARD_X	500
#define P_CARD_Y	120
/* THE PROBES SIT IN THE MIDDLE OF THE PANEL ROW'S HEIGHT AND TO THE RIGHT OF THE PANELS, and that
 * placement is arithmetic, not taste: the shadow of a panel is the panel's own rectangle translated
 * by (24, -24), so the band to the RIGHT of a panel is fully covered vertically only for `blur`
 * pixels inside the shadow's top and bottom edges. A probe nearer an edge would be measuring the
 * blur's falloff rather than the shadow, and `blur` here is 18 against a 70-unit-tall panel. */
#define P_SHADOWROW_Y	(PANEL_Y + 10)
#define P_SHADOW_INSET	(-6)		/* just inside the shadow's right edge */
/* PAST THE EDGE IS FOUR UNITS, AND THAT NUMBER WAS MEASURED OFF THE PICTURE RATHER THAN PICKED. Both
 * panels' hard shadows stop at the same place relative to their own shape; the blurred one's coverage
 * ramps out from there. Measured along the probe row in luma: the HARD panel is 249 (bare card) at
 * every distance, and the blurred one runs 210 at its edge, 228 at +4, 235 at +6, 245 at +10, 249 at
 * +16. So +10 is the far tail — it read 4 darker than the card and proved nothing — and +4 reads 21
 * darker. THE POINT IS ON A RAMP, which is why the check below asks for "clearly darker" rather than
 * for a value. */
#define P_PAST_EDGE	4
#define P_GRAD_LEFT_X	60
#define P_GRAD_RIGHT_X	840
#define P_RED_X		380

static CGContextRef ctx;
static int surface_h;

static int fb_fd = -1;

/* `x` and `y` are ABSOLUTE CG coordinates; the row is what a screendump uses. */
static void map_point(const char *role, double x, double y)
{
	printf("CG-DEMO-MAP %s %d %d\n", role, (int)(x + 0.5), surface_h - 1 - (int)(y + 0.5));
}

static void set_fill(double r, double g, double b, double a)
{
	CGContextSetRGBFillColor(ctx, (CGFloat)r, (CGFloat)g, (CGFloat)b, (CGFloat)a);
}

static void set_stroke(double r, double g, double b, double a)
{
	CGContextSetRGBStrokeColor(ctx, (CGFloat)r, (CGFloat)g, (CGFloat)b, (CGFloat)a);
}

static void text_at(double x, double y, const char *s, double size)
{
	CGContextSelectFont(ctx, "DejaVuSans", (CGFloat)size, kCGEncodingFontSpecific);
	CGContextShowTextAtPoint(ctx, (CGFloat)x, (CGFloat)y, s, (int)strlen(s));
}

/* THE COLOURED SHADOW'S COLOUR, through the object door rather than a grey level: a shadow's colour
 * is a `CGColorRef` and "may contain a non-opaque alpha value". */
static CGColorRef a_colour(double r, double g, double b, double a)
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	const CGFloat c[4] = { (CGFloat)r, (CGFloat)g, (CGFloat)b, (CGFloat)a };
	CGColorRef colour = CGColorCreate(space, c);

	CGColorSpaceRelease(space);
	return colour;
}

/* ONE PANEL: a dark shape under whatever shadow the caller has already set, plus its caption. THE
 * CAPTION IS DRAWN WITH A TRANSPARENT SHADOW COLOUR — a label is not a thing in the room, and this is
 * the off state being used for what it is for. */
static void panel(double x, double y, const char *caption)
{
	set_fill(0.16, 0.19, 0.26, 1.0);
	CGContextFillRect(ctx, CGRectMake((CGFloat)x, (CGFloat)y, (CGFloat)PANEL_W,
					  (CGFloat)PANEL_H));
	CGContextSetShadowWithColor(ctx, CGSizeMake(0.0, 0.0), 0.0, NULL);
	set_fill(0.35, 0.37, 0.42, 1.0);
	text_at(x, y - 26.0, caption, 15.0);
}

static void draw_card(double x, double y)
{
	CGGradientRef gradient;
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGFloat stops[8];
	CGColorRef blue;
	CGMutablePathRef ellipse;
	CGMutablePathRef curve;
	CGPoint from, to;

	/* THE CARD ITSELF. The backdrop is the caller's, so a check can tell them apart. */
	set_fill(0.98, 0.98, 0.96, 1.0);
	CGContextFillRect(ctx, CGRectMake((CGFloat)x, (CGFloat)y, (CGFloat)CARD_W, (CGFloat)CARD_H));

	/* --- 1. the shadow row --------------------------------------------------------------------- */
	/* A HARD SHADOW: `CGContextSetShadow` IS black at 1/3 alpha, offset in base space. */
	CGContextSetShadow(ctx, CGSizeMake((CGFloat)SHADOW_DX, (CGFloat)SHADOW_DY), 0.0);
	panel(x + PANEL_X0, y + PANEL_Y, "hard, offset 24");
	map_point("hard-shadow", x + PANEL_X0 + PANEL_W + SHADOW_DX + P_SHADOW_INSET, y + P_SHADOWROW_Y);
	map_point("hard-canvas", x + PANEL_X0 + PANEL_W + SHADOW_DX + P_PAST_EDGE, y + P_SHADOWROW_Y);

	/* THE SAME SHADOW, BLURRED: the same offset, so the only difference is the radius — which makes
	 * the point that is bare CARD for the panel above a point IN SHADOW for this one. */
	CGContextSetShadow(ctx, CGSizeMake((CGFloat)SHADOW_DX, (CGFloat)SHADOW_DY), (CGFloat)BLUR);
	panel(x + PANEL_X0 + PANEL_DX, y + PANEL_Y, "blur 18");
	map_point("blur-near", x + PANEL_X0 + PANEL_DX + PANEL_W + SHADOW_DX + P_SHADOW_INSET,
		  y + P_SHADOWROW_Y);
	map_point("blur-far", x + PANEL_X0 + PANEL_DX + PANEL_W + SHADOW_DX + P_PAST_EDGE,
		  y + P_SHADOWROW_Y);

	/* A COLOURED SHADOW, which is the door that takes a `CGColorRef`. */
	blue = a_colour(0.10, 0.20, 0.85, 0.80);
	CGContextSetShadowWithColor(ctx, CGSizeMake((CGFloat)SHADOW_DX, (CGFloat)SHADOW_DY), 0.0, blue);
	panel(x + PANEL_X0 + 2 * PANEL_DX, y + PANEL_Y, "coloured");
	map_point("color-shadow", x + PANEL_X0 + 2 * PANEL_DX + PANEL_W + SHADOW_DX + P_SHADOW_INSET,
		  y + P_SHADOWROW_Y);
	CGColorRelease(blue);

	/* AND OFF, WHICH IS A FULLY TRANSPARENT COLOUR AND NOT A FLAG: the offset it would have had is
	 * still set, and the place its shadow would land must be bare card. */
	CGContextSetShadowWithColor(ctx, CGSizeMake((CGFloat)SHADOW_DX, (CGFloat)SHADOW_DY), 0.0, NULL);
	panel(x + PANEL_X0 + 3 * PANEL_DX, y + PANEL_Y, "shadow off");
	map_point("off-canvas", x + PANEL_X0 + 3 * PANEL_DX + PANEL_W + SHADOW_DX + P_SHADOW_INSET,
		  y + P_SHADOWROW_Y);

	/* --- 2. the text ------------------------------------------------------------------------- */
	/* THE HEADER BAND FIRST AND ITS TITLE ON TOP, which is the whole ordering: a band filled after
	 * its own text would cover it. */
	CGContextSetShadowWithColor(ctx, CGSizeMake(0.0, 0.0), 0.0, NULL);
	set_fill(0.10, 0.22, 0.45, 1.0);
	CGContextFillRect(ctx, CGRectMake((CGFloat)x, (CGFloat)(y + CARD_H - HEADER_H), (CGFloat)CARD_W,
					  (CGFloat)HEADER_H));
	map_point("header-tl", x + 6.0, y + CARD_H - 6.0);
	map_point("header-br", x + CARD_W - 6.0, y + CARD_H - HEADER_H + 6.0);

	/* THE STRING DOORS: `CGContextSelectFont` names the font AND the encoding, the pen advances by
	 * the font's OWN advances, and the digits below are the proof the character map exists. */
	set_fill(1.0, 1.0, 1.0, 1.0);
	text_at(x + 36.0, y + CARD_H - 46.0, "Core Graphics on FNX", 34.0);
	set_fill(0.12, 0.12, 0.14, 1.0);
	text_at(x + 36.0, y + CARD_H - HEADER_H - 30.0,
		"shadows, gradients, strokes, clips, layers, text 0123", 18.0);

	/* --- 3. the gradients -------------------------------------------------------------------- */
	/* CLIPPED TO THE BAND, WHICH IS NOT DECORATION: a linear gradient's colour is constant along the
	 * perpendicular, so between two horizontally separated endpoints it would otherwise fill the
	 * WHOLE height of the card at those x — its extent has to come from a clip, and taking it from
	 * the clip is also the demonstration of `CGContextClipToRect`. */
	stops[0] = 0.10; stops[1] = 0.25; stops[2] = 0.60; stops[3] = 1.0;
	stops[4] = 0.95; stops[5] = 0.55; stops[6] = 0.20; stops[7] = 1.0;
	gradient = CGGradientCreateWithColorComponents(space, stops, NULL, 2);
	if (gradient != NULL) {
		CGContextSaveGState(ctx);
		CGContextClipToRect(ctx, CGRectMake((CGFloat)(x + 40.0), (CGFloat)(y + STRIP_Y),
						    (CGFloat)(CARD_W - 80), (CGFloat)STRIP_H));
		from = CGPointMake((CGFloat)(x + 40.0), (CGFloat)(y + STRIP_Y + 25.0));
		to = CGPointMake((CGFloat)(x + CARD_W - 40.0), (CGFloat)(y + STRIP_Y + 25.0));
		CGContextDrawLinearGradient(ctx, gradient, from, to, 0);
		/* A RADIAL ONE over the right half, so both families are on the card. NO "draws after end
		 * location": that flag fills the rest of the clip with the last stop's colour. */
		from = CGPointMake((CGFloat)(x + 660.0), (CGFloat)(y + STRIP_Y + 25.0));
		CGContextDrawRadialGradient(ctx, gradient, from, 0.0, from, 90.0, 0);
		CGContextRestoreGState(ctx);
		CGGradientRelease(gradient);
	}
	CGColorSpaceRelease(space);
	map_point("grad-left", x + P_GRAD_LEFT_X, y + STRIP_Y + 25.0);
	map_point("grad-right", x + P_GRAD_RIGHT_X, y + STRIP_Y + 25.0);

	/* --- 4. strokes, swatches, a clip and a layer -------------------------------------------- */
	/* A DASHED stroke around an ellipse: the dash array is a graphics-state parameter. */
	set_stroke(0.70, 0.15, 0.10, 1.0);
	CGContextSetLineWidth(ctx, 5.0);
	CGContextSetLineDash(ctx, 0.0, (const CGFloat[]){ 12.0, 8.0 }, 2);
	CGContextAddEllipseInRect(ctx, CGRectMake((CGFloat)(x + 40.0), (CGFloat)(y + 40.0), 200.0,
						  100.0));
	CGContextStrokePath(ctx);
	CGContextSetLineDash(ctx, 0.0, NULL, 0);

	/* A BEZIER, stroked: a curve that is NOT an ellipse, so the control points are really control
	 * points and not a rounder rectangle. */
	curve = CGPathCreateMutable();
	CGPathMoveToPoint(curve, NULL, (CGFloat)(x + 40.0), (CGFloat)(y + 120.0));
	CGPathAddCurveToPoint(curve, NULL, (CGFloat)(x + 100.0), (CGFloat)(y + 190.0),
			      (CGFloat)(x + 200.0), (CGFloat)(y + 10.0), (CGFloat)(x + 260.0),
			      (CGFloat)(y + 90.0));
	CGContextAddPath(ctx, curve);
	set_stroke(0.10, 0.45, 0.35, 1.0);
	CGContextSetLineWidth(ctx, 4.0);
	CGContextStrokePath(ctx);
	CGPathRelease(curve);

	/* THE SWATCHES, which give a check three colours it can name. */
	set_fill(0.70, 0.15, 0.10, 1.0);
	CGContextFillRect(ctx, CGRectMake((CGFloat)(x + 340.0), (CGFloat)(y + SWATCH_Y), 110.0, 50.0));
	set_fill(0.15, 0.55, 0.25, 1.0);
	CGContextFillRect(ctx, CGRectMake((CGFloat)(x + 470.0), (CGFloat)(y + SWATCH_Y), 110.0, 50.0));
	set_fill(0.95, 0.72, 0.30, 1.0);
	CGContextFillRect(ctx, CGRectMake((CGFloat)(x + 600.0), (CGFloat)(y + SWATCH_Y), 110.0, 50.0));
	map_point("red-swatch", x + P_RED_X, y + SWATCH_Y + 25.0);

	/* A FILL CLIPPED TO AN ELLIPSE: the clip is a region, so the blue rectangle comes out round. THE
	 * SAVE/RESTORE IS WHAT MAKES IT A CLIP AND NOT A WOUND — the clip is graphics state. */
	CGContextSaveGState(ctx);
	ellipse = CGPathCreateMutable();
	CGPathAddEllipseInRect(ellipse, NULL, CGRectMake((CGFloat)(x + 720.0), (CGFloat)(y + 40.0),
							 120.0, 100.0));
	CGContextAddPath(ctx, ellipse);
	CGContextClip(ctx);
	set_fill(0.20, 0.35, 0.80, 1.0);
	CGContextFillRect(ctx, CGRectMake((CGFloat)(x + 640.0), (CGFloat)(y + 20.0), 280.0, 140.0));
	CGPathRelease(ellipse);
	CGContextRestoreGState(ctx);

	/* A TRANSPARENCY LAYER WITH A BLEND: two translucent squares inside one layer, multiplied, which
	 * is what a layer is FOR — the blend applies where they overlap, and nowhere else. */
	CGContextBeginTransparencyLayer(ctx, NULL);
	CGContextSetBlendMode(ctx, kCGBlendModeMultiply);
	set_fill(0.90, 0.25, 0.20, 0.75);
	CGContextFillRect(ctx, CGRectMake((CGFloat)(x + 300.0), (CGFloat)(y + 140.0), 90.0, 60.0));
	set_fill(0.20, 0.30, 0.85, 0.75);
	CGContextFillRect(ctx, CGRectMake((CGFloat)(x + 350.0), (CGFloat)(y + 170.0), 90.0, 60.0));
	CGContextSetBlendMode(ctx, kCGBlendModeNormal);
	CGContextEndTransparencyLayer(ctx);
}

int main(int argc, char **argv)
{
	struct fb_mode mode;
	int hold = 10;
	int fb_ok = 0;
	unsigned char *fb = NULL;
	const unsigned char *pixels;
	const char *out = "cg-demo.ppm";
	int width, height, y, ink = 0;
	size_t fbbytes = 0;

	/* THE FONT REGISTRY, NOT A PATH. In the guest the directories are the ones this tree stages
	 * /System/Shared/Fonts into. ON THE HOST THEY ARE THE REPOSITORY'S OWN COPY, through the
	 * `FN_FONT_PATH` override the text probe also uses — the host has no font tree this library
	 * searches, and the override is the door the library documents for exactly that. */
	if (CGFontCreateWithFontName(@"DejaVuSans") == NULL) {
		setenv("FN_FONT_PATH", "userland/fonts", 1);
		if (CGFontCreateWithFontName(@"DejaVuSans") == NULL) {
			printf("CG-DEMO: no DejaVuSans in the font directories, and none in "
			       "userland/fonts either\n");
			return 1;
		}
	}

	fb_fd = open("/System/Devices/Display/fb0", O_RDWR);
	if (fb_fd < 0) {
		fb_fd = open("/dev/fb0", O_RDWR);
	}
	if (fb_fd >= 0) {
		memset(&mode, 0, sizeof mode);
		if (ioctl(fb_fd, IO_FB_GETMODE, &mode) < 0 || mode.width == 0 || mode.height == 0) {
			printf("CG-DEMO: GETMODE failed\n");
			return 1;
		}
		printf("CG-DEMO: framebuffer %ux%u %ubpp pitch %u\n", mode.width, mode.height, mode.bpp,
		       mode.pitch);
		if (mode.bpp != 32) {
			printf("CG-DEMO: the surface this library draws is 32bpp and this fb is %ubpp; "
			       "refusing rather than guessing a conversion\n", mode.bpp);
			return 1;
		}
		fbbytes = (size_t)mode.pitch * (size_t)mode.height;
		fb = mmap(NULL, fbbytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
		if (fb == MAP_FAILED) {
			printf("CG-DEMO: mmap of the framebuffer failed\n");
			return 1;
		}
		fb_ok = 1;
		width = (int)mode.width;
		height = (int)mode.height;
	} else {
		/* NO FRAMEBUFFER: THE HOST BUILD, and the picture goes to a file. A FIXED SIZE, so two host
		 * runs produce comparable pictures; the guest's size is whatever its screen is. */
		width = 1280;
		height = 800;
		printf("CG-DEMO: surface %dx%d (no framebuffer: this is the host build)\n", width, height);
	}
	if (argc > 1) {
		hold = atoi(argv[1]);
	}
	if (argc > 2) {
		out = argv[2];
	}
	surface_h = height;

	ctx = CGBitmapContextCreate(NULL, (size_t)width, (size_t)height, 8, (size_t)width * 4,
				    CGColorSpaceCreateDeviceRGB(),
				    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	if (ctx == NULL) {
		printf("CG-DEMO: no bitmap context\n");
		return 1;
	}

	/* THE BACKDROP, then the card centred on it: one band of arithmetic, so the picture is deliberate
	 * at 640x480 and at 1080p alike. */
	set_fill(0.86, 0.87, 0.90, 1.0);
	CGContextFillRect(ctx, CGRectMake(0.0, 0.0, (CGFloat)width, (CGFloat)height));
	draw_card((double)(width - CARD_W) / 2.0, (double)(height - CARD_H) / 2.0);

	/* THE MAP AND THE COUNTS GO OUT BEFORE THE BLIT — see the header. */
	{
		double x0 = (double)(width - CARD_W) / 2.0;
		double y0 = (double)(height - CARD_H) / 2.0;

		printf("CG-DEMO: card origin %d,%d size %dx%d\n", (int)x0, (int)y0, CARD_W, CARD_H);
		map_point("backdrop", P_BACKDROP_X, P_BACKDROP_Y);
		map_point("card", x0 + P_CARD_X, y0 + P_CARD_Y);
	}

	pixels = (const unsigned char *)CGBitmapContextGetData(ctx);
	for (y = 0; y < width * height; y++) {
		const unsigned char *p = pixels + (size_t)y * 4;

		if ((int)(p[0] + p[1] + p[2]) / 3 < 200) {
			ink++;
		}
	}
	printf("CG-DEMO: %d inked pixel(s) of %d\n", ink, width * height);
	printf("CG-DEMO-READY\n");
	fflush(stdout);

	if (fb_ok) {
		printf("CG-DEMO: holding the picture for %d second(s)\n", hold);
		fflush(stdout);
		for (y = 0; y < height; y++) {
			memcpy(fb + (size_t)y * mode.pitch, pixels + (size_t)y * width * 4,
			       (size_t)width * 4);
		}
		msync(fb, fbbytes, MS_SYNC);
		sleep((unsigned)hold);
		munmap(fb, fbbytes);
		close(fb_fd);
	} else {
		/* THE PPM IS WRITTEN FROM THE SAME SURFACE THE GUEST BLITS, so the two pictures are one.
		 * The rows go out top-first, which is the order the surface already has: the context's row 0
		 * is the TOP of the picture (CG y = height - 1), which is why `map_point` flips. */
		FILE *f = fopen(out, "wb");

		if (f == NULL) {
			printf("CG-DEMO: cannot write %s: %m\n", out);
			return 1;
		}
		fprintf(f, "P6\n%d %d\n255\n", width, height);
		for (y = 0; y < height; y++) {
			int x;

			for (x = 0; x < width; x++) {
				const unsigned char *p = pixels + ((size_t)y * (size_t)width + (size_t)x) * 4;
				unsigned char rgb[3];

				/* THE SURFACE IS PREMULTIPLIED-FIRST 32LITTLE, which is B,G,R,A in memory. The
				 * whole surface is opaque — the backdrop covers it — so straight RGB is the
				 * picture, and a PPM has no alpha to carry anyway. */
				rgb[0] = p[2];
				rgb[1] = p[1];
				rgb[2] = p[0];
				fwrite(rgb, 1, sizeof rgb, f);
			}
		}
		fclose(f);
		printf("CG-DEMO: wrote %s\n", out);
	}

	CGContextRelease(ctx);
	return 0;
}
