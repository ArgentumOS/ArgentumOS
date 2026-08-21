/*
 * fiwix/include/fiwix/gop.h
 *
 * Fiwix64: framebuffer info captured from the UEFI Graphics Output
 * Protocol by the EFI stub and handed to the real kernel (which runs only
 * after ExitBootServices, so it can no longer query the firmware itself).
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FIWIX_GOP_H
#define _FIWIX_GOP_H

struct fiwix_gop_fb {
	unsigned long phys_base;	/* framebuffer physical base (identity-mapped) */
	unsigned long size;		/* total framebuffer size in bytes */
	unsigned int width;		/* horizontal resolution in pixels */
	unsigned int height;		/* vertical resolution in pixels */
	unsigned int pixels_per_scanline; /* stride in pixels (>= width) */
	unsigned int pixel_format;	/* EFI_GRAPHICS_PIXEL_FORMAT */
};

extern struct fiwix_gop_fb fiwix_gop_fb;

#endif /* _FIWIX_GOP_H */
