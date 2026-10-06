/*
 * coregraphics_bitmapctx — the bitmap context's data, its release callback, and its snapshot.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THREE CLAIMS, EACH CHECKED WHERE IT CAN BE SEEN:
 *
 *   - `CGBitmapContextCreateWithData` DRAWS INTO THE CALLER'S BUFFER, which is checked by reading the caller's
 *     own array after a fill rather than by asking the context where its data is.
 *   - the RELEASE CALLBACK IS CALLED WHEN THE CONTEXT IS FREED, with `releaseInfo` and `data` in the order
 *     Apple's typedef gives — a swap of those two arguments would compile and be wrong, so both are checked.
 *     And it is called for a context that ALLOCATED its own data as well, which is the half of the promise a
 *     check on a caller-supplied buffer cannot see.
 *   - `CGBitmapContextCreateImage` IS A COPY: a snapshot taken and then drawn over must still show what was
 *     there when it was taken. Apple's copy-on-write note is an implementation permission; this is the
 *     observable half, and it is the half that is checked.
 */
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGColorSpace.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-BITMAPCTX %-60s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

#define W 4
#define H 4
#define STRIDE (W * 4)

/* This library's one bitmap chart is BGRA in memory. */
static unsigned char at(const unsigned char *buf, int row, int x, int c)
{
	return buf[(size_t)row * STRIDE + (size_t)x * 4 + (size_t)c];
}

struct released {
	int calls;
	void *info;
	void *data;
};

static void note_release(void *releaseInfo, void *data)
{
	struct released *r = releaseInfo;

	r->calls++;
	r->info = releaseInfo;
	r->data = data;
}

int main(void)
{
	unsigned char mine[STRIDE * H];
	struct released rel;
	CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
	CGContextRef c;

	memset(&rel, 0, sizeof rel);
	memset(mine, 0, sizeof mine);

	/* --- the caller's buffer, and the callback that says when it is done ----------------------- */
	c = CGBitmapContextCreateWithData(mine, W, H, 8, STRIDE, rgb,
					  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
					  note_release, &rel);
	check("a context over the CALLER's buffer is made", c != NULL);
	check("...and it draws INTO that buffer, which the caller can see",
	      CGBitmapContextGetData(c) == mine);
	CGContextSetRGBFillColor(c, 1, 0, 0, 1);
	CGContextFillRect(c, CGRectMake(0, 0, W, H));
	check("...so a red fill shows up in the caller's own array",
	      at(mine, 0, 0, 2) == 0xFF && at(mine, 0, 0, 3) == 0xFF);
	CGContextRelease(c);
	check("...and the callback fired exactly once, at release", rel.calls == 1);
	check("...with releaseInfo AND data, in Apple's order", rel.info == &rel && rel.data == mine);

	/* --- and the other half of the promise: data the CONTEXT allocated ------------------------- */
	{
		struct released rel2;

		memset(&rel2, 0, sizeof rel2);
		c = CGBitmapContextCreateWithData(NULL, W, H, 8, 0, rgb,
						  kCGImageAlphaPremultipliedFirst
						  | kCGBitmapByteOrder32Little, note_release, &rel2);
		check("a context that allocates its own data is made", c != NULL
		      && CGBitmapContextGetData(c) != NULL);
		CGContextRelease(c);
		check("...and its callback is told WHICH block the context had allocated",
		      rel2.calls == 1 && rel2.data != NULL && rel2.data != mine);
	}

	/* --- the snapshot is a copy ---------------------------------------------------------------- */
	{
		CGImageRef snap;
		unsigned char copy[STRIDE * H];

		memset(mine, 0, sizeof mine);
		c = CGBitmapContextCreate(mine, W, H, 8, STRIDE, rgb,
					  kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
		CGContextSetRGBFillColor(c, 1, 0, 0, 1);	/* red */
		CGContextFillRect(c, CGRectMake(0, 0, W, H));
		CGContextSetRGBFillColor(c, 0, 0, 1, 1);	/* blue, drawn AFTER the snapshot below */

		snap = CGBitmapContextCreateImage(c);
		check("a snapshot is made", snap != NULL);
		check("...and it is a picture of this surface",
		      snap != NULL && CGImageGetWidth(snap) == W && CGImageGetHeight(snap) == H
		      && CGImageGetBitsPerPixel(snap) == 32);
		if (snap != NULL) {
			CGContextFillRect(c, CGRectMake(0, 0, W, H));	/* now blue */
			/* READ THE SNAPSHOT THROUGH A CONTEXT OF ITS OWN: the image's bytes are copied in, and
			 * this is the only honest way to see what the image holds. */
			{
				CGContextRef probe = CGBitmapContextCreate(copy, W, H, 8, STRIDE, rgb,
									  kCGImageAlphaPremultipliedFirst
									  | kCGBitmapByteOrder32Little);

				CGContextDrawImage(probe, CGRectMake(0, 0, W, H), snap);
				check("...and drawing into the context AFTERWARDS does not change the snapshot",
				      at(copy, 0, 0, 2) == 0xFF && at(copy, 0, 0, 0) == 0x00);
				printf("CG-BITMAPCTX %-60s snapshot r=%u b=%u (context is blue)\\n", "...readout",
				       at(copy, 0, 0, 2), at(copy, 0, 0, 0));
				check("...while the context itself is blue now, so the two really did differ",
				      at(mine, 0, 0, 0) == 0xFF && at(mine, 0, 0, 2) == 0x00);
				CGContextRelease(probe);
			}
			CGImageRelease(snap);
		}
		CGContextRelease(c);
	}
	check("a snapshot of NULL, or of something that is not a bitmap context, is NULL",
	      CGBitmapContextCreateImage(NULL) == NULL);

	CGColorSpaceRelease(rgb);
	printf("CG-BITMAPCTX: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
