/*
 * CGImageJPEG — a JPEG becomes a CGImage.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE DECODER IS LIBJPEG-TURBO (3.2.0, IJG + Modified BSD-3), VENDORED BY THIS TREE FOR EXACTLY THIS
 * and built by tools/libjpeg-build.sh. Unlike PNG — which rode the X stack and needed a link flag
 * and nothing else — this one really is a new dependency, so it also has a submodule and a §B entry
 * in docs/design/self-hosting-packages.md.
 *
 * LIBJPEG'S ERROR HANDLING IS THE PART THAT IS EASY TO GET WRONG, and getting it wrong is hostile:
 * the default `error_exit` handler calls EXIT(3). A decoder handed a truncated or corrupt file must
 * not take the process with it, so this file installs its own handler over `jpeg_std_error`'s and
 * escapes through a jmp_buf — the recipe libjpeg's own documentation prescribes, done here so that
 * "not a JPEG" is a NULL return like every other refusal in this library rather than a dead caller.
 * `output_message` is deliberately LEFT ALONE: it prints the reason to stderr, which is what makes a
 * refusal diagnosable.
 *
 * AND THE CHANNEL ORDER IS THE LIBRARY'S, NOT LIBJPEG'S. `JCS_RGB` makes the decoder produce three
 * bytes per pixel, but this library's format is B, G, R, A — so the samples are swizzled as they are
 * copied, and alpha is filled with 255 because A JPEG HAS NO ALPHA CHANNEL. That is not a shortcut:
 * an opaque image in a premultiplied format is exactly the pixels themselves, so the "premultiply"
 * this library's format promises is satisfied by writing 255 in the last byte.
 */
#include <CoreGraphics/CGDataProvider_internal.h>
#include <CoreGraphics/CGImage.h>

/* STDIO COMES BEFORE JPEGLIB, AND THE ORDER IS LOAD-BEARING. libjpeg's header declares
 * `jpeg_stdio_src` and `jpeg_stdio_dest` with `FILE *` arguments, so a translation unit that has not
 * included <stdio.h> first gets "unknown type name 'FILE'" FROM THE HEADER ITSELF — which is exactly
 * how this file failed on its first build, on the host's jpeglib.h, at the declaration of a function
 * this seam never calls. libjpeg's own documentation says to include stdio first; the compiler said it
 * again. */
#include <stdio.h>
#include <jpeglib.h>

#include <setjmp.h>
#include <stdlib.h>

/* THE ESCAPE HATCH. libjpeg types its error manager and hands it a jmp_buf of the caller's
 * choosing through this pattern, so the struct is a standard one plus the buffer. */
typedef struct cg_jpeg_error {
	struct jpeg_error_mgr pub;
	jmp_buf escape;
} cg_jpeg_error;

static void cg_jpeg_fail(j_common_ptr cinfo)
{
	cg_jpeg_error *error = (cg_jpeg_error *)cinfo->err;

	longjmp(error->escape, 1);
}

CGImageRef CGImageCreateWithJPEGDataProvider(CGDataProviderRef provider, const CGFloat *decode,
					     bool shouldInterpolate,
					     CGColorRenderingIntent intent)
{
	struct jpeg_decompress_struct cinfo;
	cg_jpeg_error error;
	unsigned char *rgb = NULL;
	unsigned char *pixels = NULL;
	const void *bytes;
	size_t length = 0;
	size_t row_stride;
	size_t count;
	size_t i;
	unsigned int width;
	unsigned int height;
	CGDataProviderRef output;
	CGImageRef result;

	if (decode != NULL) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithJPEGDataProvider does not implement the "
				"decode array yet, and ignoring it would draw an image the caller did not "
				"ask for\n");
		return NULL;
	}
	if (provider == NULL) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithJPEGDataProvider needs a data provider\n");
		return NULL;
	}
	bytes = cg_dataprovider_bytes(provider, &length);
	if (bytes == NULL || length == 0) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithJPEGDataProvider was given an empty "
				"provider\n");
		return NULL;
	}

	cinfo.err = jpeg_std_error(&error.pub);
	error.pub.error_exit = cg_jpeg_fail;
	/* EVERY LIBJPEG CALL AFTER THIS POINT CAN JUMP BACK HERE, which is why the objects it may have
	 * allocated are cleaned up on the way out rather than left to a caller who cannot see them. */
	if (setjmp(error.escape) != 0) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithJPEGDataProvider: not a JPEG this "
				"libjpeg can read\n");
		jpeg_destroy_decompress(&cinfo);
		free(rgb);
		free(pixels);
		return NULL;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, (unsigned char *)bytes, (unsigned long)length);
	jpeg_read_header(&cinfo, TRUE);
	/* THREE CHANNELS OUT WHATEVER WENT IN: a greyscale or CMYK JPEG is expanded to RGB here, so
	 * there is one output layout and the swizzle below never has to ask what it is looking at. */
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);
	width = cinfo.output_width;
	height = cinfo.output_height;
	if (width == 0 || height == 0) {
		fprintf(stderr, "CG-REFUSE: CGImageCreateWithJPEGDataProvider decoded a zero-sized "
				"image\n");
		jpeg_destroy_decompress(&cinfo);
		return NULL;
	}
	row_stride = (size_t)width * 3u;
	rgb = malloc(row_stride * (size_t)height);
	pixels = malloc((size_t)width * 4u * (size_t)height);
	if (rgb == NULL || pixels == NULL) {
		jpeg_destroy_decompress(&cinfo);
		free(rgb);
		free(pixels);
		return NULL;
	}
	while (cinfo.output_scanline < cinfo.output_height) {
		JSAMPROW row = rgb + (size_t)cinfo.output_scanline * row_stride;

		jpeg_read_scanlines(&cinfo, &row, 1);
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	/* THE SWIZZLE, WHICH IS THE WHOLE OF THE CONVERSION: RGB becomes B, G, R, 255. */
	count = (size_t)width * (size_t)height;
	for (i = 0; i < count; i++) {
		pixels[i * 4u + 0u] = rgb[i * 3u + 2u];
		pixels[i * 4u + 1u] = rgb[i * 3u + 1u];
		pixels[i * 4u + 2u] = rgb[i * 3u + 0u];
		pixels[i * 4u + 3u] = 255u;
	}
	free(rgb);

	output = CGDataProviderCreateWithData(NULL, pixels, count * 4u, NULL);
	if (output == NULL) {
		/* NO RELEASE CALLBACK WAS GIVEN, so the buffer is still this function's to free. */
		free(pixels);
		return NULL;
	}
	result = CGImageCreate((size_t)width, (size_t)height, 8, 32, (size_t)width * 4u,
			       CGColorSpaceCreateDeviceRGB(),
			       kCGImageAlphaPremultipliedFirst | kCGImageByteOrder32Little, output,
			       NULL, shouldInterpolate, intent);
	CGDataProviderRelease(output);
	return result;
}
