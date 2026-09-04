/*
 * fnx/include/fnx/video.h
 *
 * Copyright 2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_VIDEO_H
#define _FNX_VIDEO_H

#define FB_MMIO_VA		0xFFFFBE2000000000UL	/* pml4[385]: the LFB map */

int video_map_framebuffer(unsigned int phys, unsigned int memsize);

void video_gop_geometry(unsigned int width, unsigned int height,
			unsigned int bpp, unsigned int pitch);
int video_gop_set_mode(unsigned int width, unsigned int height,
		       unsigned int bpp);

void video_init(void);

#endif /* _FNX_VIDEO_H */
