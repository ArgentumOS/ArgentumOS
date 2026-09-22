/*
 * CGImage — the value, and the one format it accepts.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGDataProvider_internal.h>

#include <stdio.h>
#include <stdlib.h>

/* THE FORMAT THIS LIBRARY DRAWS: 8 bits per component, 32 bits per pixel, premultiplied alpha
 * FIRST, little-endian byte order — the same one `CGBitmapContextCreate` pins, which is the whole
 * point: an image drawn into that surface is a straight copy of its channels, with no swap and no
 * unpremultiply. `cg_image_is_drawable` is the single place that decides, so the refusal message
 * and the blit cannot come to different conclusions. */
struct CGImage {
	int refcount;
	size_t width;
	size_t height;
	size_t bits_per_component;
	size_t bits_per_pixel;
	size_t bytes_per_row;
	CGColorSpaceRef space;          /* RETAINED */
	CGDataProviderRef provider;     /* RETAINED: the bytes are the provider's */
	CGImageAlphaInfo alpha;
	uint32_t bitmap_info;
	bool should_interpolate;
	CGColorRenderingIntent intent;
	int drawable;
};

int cg_image_is_drawable(CGImageRef image)
{
	return image != NULL && image->drawable;
}

CGImageRef CGImageCreate(size_t width, size_t height, size_t bitsPerComponent, size_t bitsPerPixel,
			 size_t bytesPerRow, CGColorSpaceRef space, uint32_t bitmapInfo,
			 CGDataProviderRef provider, const CGFloat *decode, bool shouldInterpolate,
			 CGColorRenderingIntent intent)
{
	CGImageRef image;
	CGImageAlphaInfo alpha = (CGImageAlphaInfo)(bitmapInfo & 0x1f);
	uint32_t order = bitmapInfo & ~0x1fu;
	size_t size = 0;
	size_t needed;
	size_t row;

	/* `decode` MAPS INPUT RANGES ONTO OUTPUT ONES, and doing nothing with it would draw an image
	 * nobody asked for. Refused rather than ignored, like the colour-space constructors' ranges. */
	if (decode != NULL) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate does not implement the decode array yet, and "
				"ignoring it would draw an image the caller did not ask for\n");
		return NULL;
	}
	if (width == 0 || height == 0 || provider == NULL) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate needs a size and a data provider\n");
		return NULL;
	}
	if (bitsPerComponent != 8 || bitsPerPixel != 32 ||
	    alpha != kCGImageAlphaPremultipliedFirst ||
	    order != (uint32_t)kCGImageByteOrder32Little) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate supports only 8/32-bit "
				"kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little (the format "
				"the contexts use), not this chart\n");
		return NULL;
	}
	if (bytesPerRow == 0) {
		bytesPerRow = width * 4;
	}
	/* THE PROVIDER MUST HOLD WHAT THE CHART DESCRIBES: an image whose last row reads past its own
	 * data is a crash waiting for whoever draws it, and this is the one place that can see both. */
	cg_dataprovider_bytes(provider, &size);
	needed = bytesPerRow * height;
	if (size < needed) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate was given %lu bytes and its chart needs %lu\n",
			(unsigned long)size, (unsigned long)needed);
		return NULL;
	}
	image = calloc(1, sizeof(struct CGImage));
	if (image == NULL) {
		return NULL;
	}
	image->refcount = 1;
	image->width = width;
	image->height = height;
	image->bits_per_component = bitsPerComponent;
	image->bits_per_pixel = bitsPerPixel;
	image->bytes_per_row = bytesPerRow;
	image->space = CGColorSpaceRetain(space);
	image->provider = CGDataProviderRetain(provider);
	image->alpha = alpha;
	image->bitmap_info = bitmapInfo;
	image->should_interpolate = shouldInterpolate;
	image->intent = intent;
	image->drawable = 1;
	(void)row;
	return image;
}

CGImageRef CGImageRetain(CGImageRef image)
{
	if (image != NULL) {
		image->refcount++;
	}
	return image;
}

void CGImageRelease(CGImageRef image)
{
	if (image == NULL) {
		return;
	}
	if (--image->refcount > 0) {
		return;
	}
	/* THE IMAGE LETS GO OF WHAT IT RETAINED, in the opposite order from the one it took them. */
	CGDataProviderRelease(image->provider);
	CGColorSpaceRelease(image->space);
	free(image);
}

size_t CGImageGetWidth(CGImageRef image)
{
	return image == NULL ? 0 : image->width;
}

size_t CGImageGetHeight(CGImageRef image)
{
	return image == NULL ? 0 : image->height;
}

size_t CGImageGetBitsPerComponent(CGImageRef image)
{
	return image == NULL ? 0 : image->bits_per_component;
}

size_t CGImageGetBitsPerPixel(CGImageRef image)
{
	return image == NULL ? 0 : image->bits_per_pixel;
}

size_t CGImageGetBytesPerRow(CGImageRef image)
{
	return image == NULL ? 0 : image->bytes_per_row;
}

CGColorSpaceRef CGImageGetColorSpace(CGImageRef image)
{
	return image == NULL ? NULL : image->space;
}

CGImageAlphaInfo CGImageGetAlphaInfo(CGImageRef image)
{
	/* ZERO FOR A NULL IMAGE, WHICH IS `kCGImageAlphaNone`. The enum in CGBitmapContext.h lists the
	 * four values the CONTEXTS can be built with and not this one, and writing the value with the
	 * name here is honest where inventing a fifth spelling of the enum would not be. */
	return image == NULL ? (CGImageAlphaInfo)0 : image->alpha;
}

uint32_t CGImageGetBitmapInfo(CGImageRef image)
{
	return image == NULL ? 0 : image->bitmap_info;
}

CGDataProviderRef CGImageGetDataProvider(CGImageRef image)
{
	return image == NULL ? NULL : image->provider;
}

bool CGImageGetShouldInterpolate(CGImageRef image)
{
	return image == NULL ? false : image->should_interpolate;
}

CGColorRenderingIntent CGImageGetRenderingIntent(CGImageRef image)
{
	return image == NULL ? kCGRenderingIntentDefault : image->intent;
}
