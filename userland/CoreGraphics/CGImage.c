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
#include <string.h>

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
	/* A MASK IS NOT A PICTURE: its samples ARE the mask, it has no color space, and it is not drawable
	 * on its own — it is for clipping with or for masking another image. The flag is what
	 * `CGImageIsMask` answers and what the drawing path refuses on. */
	int is_mask;
	/* A PICTURE PAINTED THROUGH SOMETHING: either a MASK image (retained) or MASKING COLORS (an owned
	 * array of 2N sample ranges, N being the image's component count). The two are exclusive — Apple's
	 * header says a masked picture may not be masked again — so `mask_color_count` is 0 unless the
	 * colors door was used. */
	CGImageRef mask;
	CGFloat *mask_colors;
	int mask_color_count;
	/* THE DECODE ARRAY, OWNED, or NULL: 2N sample ranges for the N components of the color space.
	 * Applied where the image is sampled, which is what makes storing it honest rather than a
	 * declaration — `CGImageGetDecode` hands back what the sampler actually uses. */
	CGFloat *decode;
	int decode_count;
};

int cg_image_is_drawable(CGImageRef image)
{
	return image != NULL && image->drawable;
}

/* WHAT A PICTURE MAY BE PAINTED THROUGH, for the drawing path. The mask is the image's own reference and the
 * path must not release it; the masking colours are the image's array and the count says how many components
 * they describe — zero meaning "this picture has neither". */
const CGFloat *CGImageGetDecode(CGImageRef image)
{
	return image == NULL ? NULL : image->decode;
}

CGImageRef cg_image_mask(CGImageRef image)
{
	return image == NULL ? NULL : image->mask;
}

const CGFloat *cg_image_masking_colors(CGImageRef image, int *count)
{
	if (image == NULL || image->mask_color_count <= 0) {
		if (count != NULL) {
			*count = 0;
		}
		return NULL;
	}
	if (count != NULL) {
		*count = image->mask_color_count;
	}
	return image->mask_colors;
}

/* THE LAYOUT, DERIVED ONCE AND USED BY BOTH CALLERS. It maps a chart's channels onto byte offsets,
 * and it is the ONLY place that knows the difference between the logical order Apple names and the
 * physical order the bytes are in.
 *
 * THE ONE RULE THAT LOOKS LIKE A SPECIAL CASE AND IS NOT: `kCGBitmapByteOrder32Little` REVERSES the
 * logical order in memory. An RGB space with `PremultipliedFirst` is logically A, R, G, B, and
 * little-endian storage is what makes it the B, G, R, A this library's contexts use everywhere. So
 * both "BGR" spellings fall out of the same reversal rather than needing their own case, and the
 * other three byte orders need no arithmetic at all.
 *
 * A GREY CHART HAS ONE CHANNEL AND THREE NAMES FOR IT: blue and green ARE red, which is why the
 * sampler can treat every chart as RGBA and let the map do the work. A `NoneSkip*` chart stores a
 * component that is never to be read, and a mask (`alphaOnly`) stores nothing but alpha. */
void cg_image_layout(CGColorSpaceRef space, CGImageAlphaInfo alpha, uint32_t order, int map[4],
		     int *stored, int *straight)
{
	int components = space == NULL ? 0 : (int)CGColorSpaceGetNumberOfComponents(space);
	int first;
	int alpha_slot;
	int i;

	map[0] = map[1] = map[2] = map[3] = -1;
	*stored = 0;
	*straight = 0;

	if (alpha == kCGImageAlphaOnly) {
		if (components != 1) {
			return;
		}
		map[3] = 0;
		*stored = 1;
		return;
	}
	/* The alpha and skip layouts put a component first; the rest put it last or not at all. */
	first = (alpha == kCGImageAlphaPremultipliedFirst || alpha == kCGImageAlphaFirst ||
		 alpha == kCGImageAlphaNoneSkipFirst);
	alpha_slot = (alpha == kCGImageAlphaPremultipliedLast || alpha == kCGImageAlphaLast ||
		      alpha == kCGImageAlphaPremultipliedFirst || alpha == kCGImageAlphaFirst ||
		      alpha == kCGImageAlphaNoneSkipLast || alpha == kCGImageAlphaNoneSkipFirst);
	*straight = (alpha == kCGImageAlphaLast || alpha == kCGImageAlphaFirst);
	*stored = components + alpha_slot;
	if (*stored < 1 || *stored > 4) {
		*stored = 0;
		return;
	}
	for (i = 0; i < components && i < 3; i++) {
		int logical = first ? i + 1 : i;
		int physical = (order == (uint32_t)kCGBitmapByteOrder32Big) ? logical
								      : (*stored - 1 - logical);

		map[2 - i] = physical;   /* i counts from RED; the map runs B, G, R */
	}
	if (components == 1) {
		map[0] = map[1] = map[2];
	}
	if (alpha_slot) {
		int logical = first ? 0 : *stored - 1;

		map[3] = (order == (uint32_t)kCGBitmapByteOrder32Big) ? logical : (*stored - 1 - logical);
	}
}

CGImageRef CGImageCreate(size_t width, size_t height, size_t bitsPerComponent, size_t bitsPerPixel,
			 size_t bytesPerRow, CGColorSpaceRef space, uint32_t bitmapInfo,
			 CGDataProviderRef provider, const CGFloat *decode, bool shouldInterpolate,
			 CGColorRenderingIntent intent)
{
	CGImageRef image;
	CGImageAlphaInfo alpha = (CGImageAlphaInfo)(bitmapInfo & 0x1f);
	uint32_t order = bitmapInfo & ~0x1fu;
	int channels[4];
	int stored;
	int straight;
	size_t size = 0;
	size_t needed;
	size_t row;

	/* `decode` IS STORED AND APPLIED — see CGImageGetDecode — so it is neither refused nor ignored.
	 * ITS VALUES ARE CHECKED WHERE N IS KNOWN, further down, because the length of the array follows
	 * from the color space and this function has not read the space yet. */
	if (width == 0 || height == 0 || provider == NULL) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate needs a size and a data provider\n");
		return NULL;
	}
	/* THE FORMAT MATRIX, SUCH AS IT IS. Anything this library can SAMPLE is accepted; anything it
	 * cannot is refused BY NAME, so the gap is legible rather than discovered as a wrong picture.
	 *
	 * WHAT IS IN: 8 bits per component, one to four channels, any of the eight alpha layouts and
	 * either byte order — grey, grey plus alpha, RGB with or without alpha, and the `NoneSkip*`
	 * charts whose fourth component exists only as padding. WHAT IS OUT, each for a stated reason:
	 * 16- and 32-bit components and the sub-byte depths (they would have to be SCALED, which
	 * discards the caller's data), CMYK (four COLOUR components, which the blit would have to
	 * convert rather than reorder), and any `bitmapInfo` whose alpha field is not one of the eight.
	 * `bitsPerPixel` must be what the channels actually need: the layout below computes that from
	 * the same chart the sampler will read, so the two cannot disagree. */
	if (bitsPerComponent != 8) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate supports 8 bits per component only; %lu would "
				"have to be scaled, which loses the caller's data\n",
			(unsigned long)bitsPerComponent);
		return NULL;
	}
	if (alpha < kCGImageAlphaNone || alpha > kCGImageAlphaOnly) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate: bitmapInfo names an alpha layout that is not "
				"one of the eight CGImageAlphaInfo values\n");
		return NULL;
	}
	if (space != NULL && CGColorSpaceGetNumberOfComponents(space) == 4) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate does not sample CMYK images yet: four COLOUR "
				"components need conversion, not reordering\n");
		return NULL;
	}
	cg_image_layout(space, alpha, order, channels, &stored, &straight);
	if (stored <= 0) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate: this colour space and alpha layout do not "
				"describe a chart the blit can read\n");
		return NULL;
	}
	if (bitsPerPixel != (size_t)stored * 8u) {
		fprintf(stderr, "CG-REFUSE: CGImageCreate was told %lu bits per pixel and this chart's "
				"channels need %d\n", (unsigned long)bitsPerPixel, stored * 8);
		return NULL;
	}
	if (bytesPerRow == 0) {
		bytesPerRow = width * (size_t)stored;
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
	/* THE DECODE ARRAY, VALIDATED NOW THAT THE SPACE IS KNOWN: 2N values for its N components, each a
	 * valid sample value for an 8-bit image. A caller whose array holds something else wrote something
	 * this door cannot express, so it is refused rather than clamped — the same rule the color space
	 * constructors and CGImageCreateWithMaskingColors apply to their ranges. */
	if (decode != NULL) {
		int ncomp = image->space == NULL ? 1 : CGColorSpaceGetNumberOfComponents(image->space);
		int i;

		if (ncomp < 1 || ncomp > 3) {
			fprintf(stderr, "CG-REFUSE: CGImageCreate decodes gray and RGB images\n");
			CGImageRelease(image);
			return NULL;
		}
		for (i = 0; i < ncomp * 2; i++) {
			if (!(decode[i] >= 0.0 && decode[i] <= 255.0)) {
				fprintf(stderr, "CG-REFUSE: CGImageCreate needs decode values in 0..255 for "
						"this 8-bit image, and value %d is %g\n", i, (double)decode[i]);
				CGImageRelease(image);
				return NULL;
			}
		}
		image->decode = malloc(sizeof(CGFloat) * (size_t)ncomp * 2u);
		if (image->decode == NULL) {
			CGImageRelease(image);
			return NULL;
		}
		memcpy(image->decode, decode, sizeof(CGFloat) * (size_t)ncomp * 2u);
		image->decode_count = ncomp;
	}
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
	CGImageRelease(image->mask);
	free(image->mask_colors);
	free(image->decode);
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

/* ------------------------------------------------------------------------- */
/* Masks, and the three ways an image is derived from another                  */
/* ------------------------------------------------------------------------- */

/* THE STRUCTURE COPY, SHARED BY ALL THREE DERIVATION DOORS: every field is carried over, the provider and
 * the color space are retained because the new image now holds them too, and the reference count starts at
 * one. THE BYTES ARE NEVER COPIED — every derived image refers to the original's, which is why each of
 * these retains what its bytes come from. */
static CGImageRef fn_copy_structure(CGImageRef image)
{
	CGImageRef copy;

	if (image == NULL) {
		return NULL;
	}
	copy = calloc(1, sizeof(struct CGImage));
	if (copy == NULL) {
		return NULL;
	}
	*copy = *image;
	copy->refcount = 1;
	CGColorSpaceRetain(copy->space);
	CGDataProviderRetain(copy->provider);
	/* THE DERIVED THINGS ARE COPIED RATHER THAN ALIASED: the mask is retained and the masking colors
	 * are a SECOND array, because the copy and the original are both going to free what they hold and
	 * one buffer cannot belong to two images. */
	CGImageRetain(copy->mask);
	if (copy->decode != NULL && copy->decode_count > 0) {
		copy->decode = malloc(sizeof(CGFloat) * (size_t)copy->decode_count * 2u);
		if (copy->decode == NULL) {
			CGImageRelease(copy);
			return NULL;
		}
		memcpy(copy->decode, image->decode, sizeof(CGFloat) * (size_t)copy->decode_count * 2u);
	}
	if (copy->mask_colors != NULL && copy->mask_color_count > 0) {
		copy->mask_colors = malloc(sizeof(CGFloat) * (size_t)copy->mask_color_count * 2u);
		if (copy->mask_colors == NULL) {
			CGImageRelease(copy);
			return NULL;
		}
		memcpy(copy->mask_colors, image->mask_colors,
		       sizeof(CGFloat) * (size_t)copy->mask_color_count * 2u);
	}
	return copy;
}

CGImageRef CGImageMaskCreate(size_t width, size_t height, size_t bitsPerComponent, size_t bitsPerPixel,
			     size_t bytesPerRow, CGDataProviderRef provider, const CGFloat *decode,
			     bool shouldInterpolate)
{
	CGImageRef image;

	/* A MASK HAS ONE COMPONENT — ITSELF — SO ITS DECODE ARRAY IS ONE PAIR, and it is applied to the mask's
	 * samples before the inversion: decode the sample, then treat it as an inverse alpha. */
	if (decode != NULL
	    && !(decode[0] >= 0.0 && decode[0] <= 255.0 && decode[1] >= 0.0 && decode[1] <= 255.0)) {
		fprintf(stderr, "CG-REFUSE: CGImageMaskCreate needs decode values in 0..255 for this "
				"8-bit mask\n");
		return NULL;
	}
	if (width == 0 || height == 0 || provider == NULL) {
		fprintf(stderr, "CG-REFUSE: CGImageMaskCreate needs a size and a data provider\n");
		return NULL;
	}
	/* THE SAME DEPTH RULE AS `CGImageCreate`, for the same reason: this library samples 8 bits per
	 * component, and a mask at another depth would have to be scaled. */
	if (bitsPerComponent != 8 || bitsPerPixel != 8) {
		fprintf(stderr, "CG-REFUSE: CGImageMaskCreate supports 8 bits per component and per pixel "
				"only; %lu/%lu would have to be scaled\n",
			(unsigned long)bitsPerComponent, (unsigned long)bitsPerPixel);
		return NULL;
	}
	if (bytesPerRow < width) {
		fprintf(stderr, "CG-REFUSE: CGImageMaskCreate needs a row at least as long as the mask is "
				"wide (%lu < %lu)\n", (unsigned long)bytesPerRow, (unsigned long)width);
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
	/* NO COLOR SPACE: a mask has no colors, which is what makes `CGImageGetColorSpace` answer NULL for it
	 * rather than a gray space invented to have something to say. */
	image->space = NULL;
	image->provider = CGDataProviderRetain(provider);
	image->alpha = kCGImageAlphaOnly;
	image->bitmap_info = (uint32_t)kCGImageAlphaOnly;
	image->should_interpolate = shouldInterpolate;
	image->intent = kCGRenderingIntentDefault;
	if (decode != NULL) {
		image->decode = malloc(sizeof(CGFloat) * 2u);
		if (image->decode == NULL) {
			CGImageRelease(image);
			return NULL;
		}
		image->decode[0] = decode[0];
		image->decode[1] = decode[1];
		image->decode_count = 1;
	}
	image->is_mask = 1;
	/* NOT DRAWABLE, AND THAT IS THE MASK'S WHOLE NATURE: it is for clipping with or for masking another
	 * image. `cg_image_is_drawable` is the one place that decides, so every drawing door agrees. */
	image->drawable = 0;
	return image;
}

bool CGImageIsMask(CGImageRef image)
{
	return image != NULL && image->is_mask;
}

/* ------------------------------------------------------------------------- */
/* Painting a picture through a mask                                           */
/* ------------------------------------------------------------------------- */

/* THE TWO DOORS SHARE ONE RULE THAT IS APPLE'S AND ONE THAT IS THIS LIBRARY'S: a picture may be masked ONCE
 * (the header says so), and the picture must not itself be a mask (a mask is not a picture to paint). Both
 * are refused by name, and the refusal names which one was broken. */
static int fn_masking_refused(CGImageRef image, const char *door)
{
	if (image == NULL) {
		return 1;
	}
	if (image->is_mask) {
		fprintf(stderr, "CG-REFUSE: %s takes a picture to paint, and a mask is not one — it is what "
				"you paint THROUGH\n", door);
		return 1;
	}
	if (image->mask != NULL || image->mask_colors != NULL) {
		fprintf(stderr, "CG-REFUSE: %s takes a picture that is not already masked, and this one is\n",
			door);
		return 1;
	}
	return 0;
}

CGImageRef CGImageCreateWithMask(CGImageRef image, CGImageRef mask)
{
	CGImageRef copy;

	if (fn_masking_refused(image, "CGImageCreateWithMask") || mask == NULL) {
		if (mask == NULL) {
			fprintf(stderr, "CG-REFUSE: CGImageCreateWithMask needs a mask to paint through\n");
		}
		return NULL;
	}
	if (!mask->is_mask) {
		/* AN IMAGE USED AS A MASK IS AN ALPHA MASK AND MUST BE A GRAY PICTURE WITH NOTHING ELSE IN IT:
		 * DeviceGray, no alpha channel, and not itself masked. Each of the three is Apple's rule and
		 * each is checked rather than trusted — a colored mask would have to be reduced to one number
		 * per pixel and the header does not say how, so REFUSING is the honest answer. */
		if (mask->space == NULL || CGColorSpaceGetModel(mask->space) != kCGColorSpaceModelMonochrome) {
			fprintf(stderr, "CG-REFUSE: CGImageCreateWithMask takes an image mask or a DeviceGray "
					"image, and this mask is neither\n");
			return NULL;
		}
		if (mask->alpha != kCGImageAlphaNone) {
			fprintf(stderr, "CG-REFUSE: CGImageCreateWithMask needs a mask image with no alpha "
					"channel: the mask's samples ARE the alpha\n");
			return NULL;
		}
		if (mask->mask != NULL || mask->mask_colors != NULL) {
			fprintf(stderr, "CG-REFUSE: CGImageCreateWithMask needs a mask image that is not itself "
					"masked\n");
			return NULL;
		}
	}
	if (mask->bits_per_pixel != 8) {
		/* THE SAMPLER BELOW READS ONE BYTE PER PIXEL, and to make that honest this door refuses a mask
		 * whose samples are not one byte. A deeper or sub-byte mask would have to be SCALED, which is a
		 * fact about the caller's data that this door will not invent. */
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithMask samples an 8-bit mask; this one has %lu bits "
				"per pixel\n", (unsigned long)mask->bits_per_pixel);
		return NULL;
	}
	copy = fn_copy_structure(image);
	if (copy == NULL) {
		return NULL;
	}
	copy->mask = CGImageRetain(mask);
	return copy;
}

CGImageRef CGImageCreateWithMaskingColors(CGImageRef image, const CGFloat *components)
{
	CGImageRef copy;
	int ncomp;

	if (fn_masking_refused(image, "CGImageCreateWithMaskingColors") || components == NULL) {
		if (components == NULL) {
			fprintf(stderr, "CG-REFUSE: CGImageCreateWithMaskingColors needs the 2N sample ranges to "
					"mask out\n");
		}
		return NULL;
	}
	if (image->space == NULL) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithMaskingColors needs a picture with a color space, "
				"because N is its component count\n");
		return NULL;
	}
	ncomp = CGColorSpaceGetNumberOfComponents(image->space);
	if (ncomp < 1 || ncomp > 3) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithMaskingColors supports gray and RGB pictures\n");
		return NULL;
	}
	/* EVERY VALUE MUST BE A VALID SAMPLE VALUE, and this library's samples are 8-bit: 0 to 255. A caller
	 * who wrote 300 for a range meant something this door cannot express, so it is refused rather than
	 * clamped — the same rule the colour-space constructors apply to their ranges. */
	{
		int i;

		for (i = 0; i < ncomp * 2; i++) {
			if (!(components[i] >= 0.0 && components[i] <= 255.0)) {
				fprintf(stderr, "CG-REFUSE: CGImageCreateWithMaskingColors needs sample values in "
						"0..255 for this 8-bit image, and value %d is %g\n", i,
					(double)components[i]);
				return NULL;
			}
		}
		if (ncomp * 2 > 1 && components[0] > components[1]) {
			fprintf(stderr, "CG-REFUSE: CGImageCreateWithMaskingColors needs min then max for each "
					"component; the first pair reads max then min\n");
			return NULL;
		}
	}
	copy = fn_copy_structure(image);
	if (copy == NULL) {
		return NULL;
	}
	copy->mask_colors = malloc(sizeof(CGFloat) * (size_t)ncomp * 2u);
	if (copy->mask_colors == NULL) {
		CGImageRelease(copy);
		return NULL;
	}
	memcpy(copy->mask_colors, components, sizeof(CGFloat) * (size_t)ncomp * 2u);
	copy->mask_color_count = ncomp;
	return copy;
}

CGImageRef CGImageCreateCopy(CGImageRef image)
{
	return fn_copy_structure(image);
}

CGImageRef CGImageCreateCopyWithColorSpace(CGImageRef image, CGColorSpaceRef space)
{
	CGImageRef copy;

	if (image == NULL || space == NULL) {
		return NULL;
	}
	if (image->is_mask) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateCopyWithColorSpace takes a picture, and a mask has no "
				"color space to replace\n");
		return NULL;
	}
	/* THE COMPONENT COUNT IS THE CONTRACT: the bytes are the same bytes, so a space of a different shape
	 * would reinterpret them. Apple's own rule, and the reason this is a check rather than a warning. */
	if (image->space != NULL
	    && CGColorSpaceGetNumberOfComponents(image->space) != CGColorSpaceGetNumberOfComponents(space)) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateCopyWithColorSpace needs a space with the same "
				"number of components as the image's, or the bytes would be read as another "
				"shape\n");
		return NULL;
	}
	copy = fn_copy_structure(image);
	if (copy == NULL) {
		return NULL;
	}
	CGColorSpaceRelease(copy->space);
	copy->space = CGColorSpaceRetain(space);
	return copy;
}

/* THE PROVIDER'S RELEASE CALLBACK FOR A SUBRECTANGLE, and it is what makes "the new image retains a reference
 * to the original" true: the bytes the subrect's provider hands out belong to the ORIGINAL's provider, which
 * the original image owns, so the original must outlive them. The window carries it. */
static void fn_release_subrect_window(void *info, const void *data, size_t size)
{
	(void)data;
	(void)size;
	CGImageRelease((CGImageRef)info);
}

CGImageRef CGImageCreateWithImageInRect(CGImageRef image, CGRect rect)
{
	CGImageRef copy;
	CGRect bounds;
	CGRect clipped;
	CGRect whole;
	size_t x, y;
	size_t w, h;
	const void *bytes;
	size_t size = 0;
	CGDataProviderRef provider;

	if (image == NULL) {
		return NULL;
	}
	if (image->bits_per_pixel % 8 != 0) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithImageInRect needs a BYTE-ALIGNED pixel; this "
				"image's pixels are %lu bits, so a subrectangle's rows would not start on a byte\n",
			(unsigned long)image->bits_per_pixel);
		return NULL;
	}
	/* APPLE'S THREE STEPS, IN APPLE'S ORDER: integral bounds first, then the intersection with the image's
	 * own rectangle, then the pixels. THE INTEGRAL STEP COMES FIRST because it can grow the rectangle, and
	 * the intersection is what makes the result lie inside the image. */
	bounds = CGRectMake(0, 0, (CGFloat)image->width, (CGFloat)image->height);
	clipped = CGRectIntegral(rect);
	whole = CGRectIntersection(clipped, bounds);
	if (CGRectIsNull(whole) || CGRectIsEmpty(whole)) {
		return NULL;
	}
	x = (size_t)whole.origin.x;
	y = (size_t)whole.origin.y;
	w = (size_t)whole.size.width;
	h = (size_t)whole.size.height;
	if (w == 0 || h == 0 || x + w > image->width || y + h > image->height) {
		return NULL;
	}
	bytes = cg_dataprovider_bytes(image->provider, &size);
	if (bytes == NULL) {
		return NULL;
	}
	{
		size_t offset = y * image->bytes_per_row + x * (image->bits_per_pixel / 8);
		size_t need = (h - 1) * image->bytes_per_row + w * (image->bits_per_pixel / 8);

		if (offset + need > size) {
			return NULL;
		}
		/* THE WINDOW RETAINS THE ORIGINAL and hands out the subrect's first byte: the same buffer, read
		 * from a later offset, with the SAME stride — a subrectangle's rows still skip the pixels outside
		 * it, because the data underneath still has them. */
		provider = CGDataProviderCreateWithData((void *)CGImageRetain(image),
						       (const char *)bytes + offset, need,
						       fn_release_subrect_window);
	}
	if (provider == NULL) {
		CGImageRelease(image);
		return NULL;
	}
	copy = fn_copy_structure(image);
	if (copy == NULL) {
		CGDataProviderRelease(provider);
		return NULL;
	}
	CGDataProviderRelease(copy->provider);
	copy->provider = provider;
	copy->width = w;
	copy->height = h;
	return copy;
}
