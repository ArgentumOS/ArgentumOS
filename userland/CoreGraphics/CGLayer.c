/*
 * CGLayer — the offscreen surface, and the round trip that makes it worth having.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE SURFACE IS A BITMAP CONTEXT, built by `CGBitmapContextCreate` with the one chart this library's contexts
 * accept. That is not a shortcut: a layer IS a bitmap context with a size attached, and reusing the existing
 * constructor means the layer's surface is created, validated and released by the code that already does that.
 *
 * DRAWING THE LAYER OUT GOES THROUGH `CGContextDrawImage`, AND THAT IS THE POINT: a layer is an image of its own
 * surface, so making that image and drawing it inherits the whole tested path — the CTM, the clip, the alpha,
 * the scaling the header promises — rather than a second blit that would have to be kept in step with the first.
 * The provider that lends those bytes HOLDS A REFERENCE TO THE LAYER, so the surface cannot be freed while the
 * image is being drawn from it.
 */
#include <CoreGraphics/CGLayer.h>
#include <CoreGraphics/CGImage.h>
#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGDataProvider.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGTypeID_internal.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct CGLayer {
	int refcount;
	CGContextRef context;	/* the layer's surface: OWNED by the layer, handed out borrowed */
	CGSize size;		/* WHAT THE CALLER ASKED FOR, which is what GetSize answers */
};

/* THE PROVIDER'S RELEASE CALLBACK: the layer is what keeps the bytes alive, so the image that reads them must
 * hold it. This is the same shape CGImageCreateWithImageInRect uses to keep its original. */
static void fn_release_layer(void *info, const void *data, size_t size)
{
	(void)data;
	(void)size;
	CGLayerRelease((CGLayerRef)info);
}

CGLayerRef CGLayerCreateWithContext(CGContextRef context, CGSize size, NSDictionary *auxiliaryInfo)
{
	CGLayerRef layer;
	CGColorSpaceRef space;

	/* "The parameter `auxiliaryInfo' should be NULL; it is reserved for future expansion" — so it is accepted
	 * and ignored rather than inspected, and the cast keeps the unused parameter honest. */
	(void)auxiliaryInfo;
	if (context == NULL) {
		fprintf(stderr, "CG-REFUSE: CGLayerCreateWithContext needs the context whose space and units the "
				"layer is defined against\n");
		return NULL;
	}
	if (!(size.width > 0.0) || !(size.height > 0.0)) {
		fprintf(stderr, "CG-REFUSE: CGLayerCreateWithContext needs a non-empty size; a layer with no "
				"area has nothing to draw into\n");
		return NULL;
	}
	layer = calloc(1, sizeof(struct CGLayer));
	if (layer == NULL) {
		return NULL;
	}
	layer->size = size;
	layer->refcount = 1;
	/* THE SIZE IS ROUNDED UP: a surface is made of pixels, and rounding down would lose a row or column of
	 * what the caller asked for. The SIZE ITSELF is kept as asked, which is what GetSize answers. */
	space = CGColorSpaceCreateDeviceRGB();
	if (space == NULL) {
		free(layer);
		return NULL;
	}
	layer->context = CGBitmapContextCreate(NULL, (size_t)ceil((double)size.width),
					       (size_t)ceil((double)size.height), 8, 0, space,
					       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	CGColorSpaceRelease(space);
	if (layer->context == NULL) {
		free(layer);
		return NULL;
	}
	return layer;
}

CGContextRef CGLayerGetContext(CGLayerRef layer)
{
	return layer == NULL ? NULL : layer->context;
}

CGSize CGLayerGetSize(CGLayerRef layer)
{
	return layer == NULL ? CGSizeMake(0, 0) : layer->size;
}

CGLayerRef CGLayerRetain(CGLayerRef layer)
{
	if (layer != NULL) {
		layer->refcount++;
	}
	return layer;
}

void CGLayerRelease(CGLayerRef layer)
{
	if (layer == NULL) {
		return;		/* the header says so: this does not crash where CFRelease does */
	}
	if (--layer->refcount > 0) {
		return;
	}
	CGContextRelease(layer->context);
	free(layer);
}

void CGContextDrawLayerInRect(CGContextRef context, CGRect rect, CGLayerRef layer)
{
	CGDataProviderRef provider;
	CGColorSpaceRef space;
	CGImageRef image;
	void *data;
	size_t w, h, stride;

	if (context == NULL || layer == NULL || layer->context == NULL) {
		return;
	}
	data = CGBitmapContextGetData(layer->context);
	w = CGBitmapContextGetWidth(layer->context);
	h = CGBitmapContextGetHeight(layer->context);
	stride = CGBitmapContextGetBytesPerRow(layer->context);
	if (data == NULL || w == 0 || h == 0 || stride == 0) {
		return;
	}
	/* THE IMAGE IS THE LAYER'S OWN SURFACE, and the provider holds the layer so the surface outlives the
	 * draw. The chart is the one that surface was built with, which is also the one CGImageCreate reads. */
	provider = CGDataProviderCreateWithData((void *)CGLayerRetain(layer), data, stride * h,
						fn_release_layer);
	space = CGColorSpaceCreateDeviceRGB();
	if (provider == NULL || space == NULL) {
		CGDataProviderRelease(provider);
		CGColorSpaceRelease(space);
		return;
	}
	image = CGImageCreate(w, h, 8, 32, stride, space,
			      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, provider, NULL,
			      false, kCGRenderingIntentDefault);
	CGColorSpaceRelease(space);
	CGDataProviderRelease(provider);
	if (image == NULL) {
		return;
	}
	CGContextDrawImage(context, rect, image);
	CGImageRelease(image);
}

void CGContextDrawLayerAtPoint(CGContextRef context, CGPoint point, CGLayerRef layer)
{
	/* APPLE'S OWN SENTENCE, IMPLEMENTED AS WRITTEN: "equivalent to calling CGContextDrawLayerInRect with a
	 * rectangle having origin at `point' and size equal to the size of `layer'". Calling it is the only way
	 * the equivalence cannot drift. */
	if (layer == NULL) {
		return;
	}
	CGContextDrawLayerInRect(context, CGRectMake(point.x, point.y, layer->size.width,
						     layer->size.height), layer);
}
