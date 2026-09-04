/*
 * fnx/include/fnx/fb.h
 *
 * Copyright 2021, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_FB_H
#define _FNX_FB_H

#include <fnx/fs.h>

#define FB_MAJOR	29	/* major number */
#define FB_MINOR	0	/* minor number */

/* fb0 ioctls (the fnx convention: geometry queries return the value;
 * the mode ioctls take/return a struct fb_mode through arg) */
#define IO_FB_XRES	2	/* legacy: returns video.fb_width */
#define IO_FB_YRES	3	/* legacy: returns video.fb_height */
#define IO_FB_GETMODE	4	/* arg: struct fb_mode * (filled in) */
#define IO_FB_SETMODE	5	/* arg: struct fb_mode * (request; 0 on success) */

struct fb_mode {
	unsigned int width;
	unsigned int height;
	unsigned int bpp;
	unsigned int pitch;	/* bytes per scanline */
};

int fb_open(struct inode *, struct fd *);
int fb_close(struct inode *, struct fd *);
int fb_read(struct inode *, struct fd *, char *, __size_t);
int fb_write(struct inode *, struct fd *, const char *, __size_t);
int fb_mmap(struct inode *, struct vma *);
int fb_ioctl(struct inode *, struct fd *, int, addr_t);
__loff_t fb_llseek(struct inode *, __loff_t);

void fb_init(void);

#endif /* _FNX_FB_H */
