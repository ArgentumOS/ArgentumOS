/*
 * fnx/drivers/video/video.c
 *
 * Copyright 2021-2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/config.h>
#include <fnx/vgacon.h>
#include <fnx/fb.h>
#include <fnx/fbcon.h>
#include <fnx/bga.h>
#include <fnx/console.h>
#include <fnx/stdio.h>
#include <fnx/string.h>

void video_init(void)
{
	memset_b(vcbuf, 0, (video.columns * video.lines * SCREENS_LOG * 2 * sizeof(short int)));

	if(video.flags & VPF_VGA) {
		vgacon_init();
		return;
	}

#ifdef CONFIG_PCI
#ifdef CONFIG_BGA
	if(video.flags & VPF_VESAFB) {
		bga_init();
	}
#endif /* CONFIG_BGA */
#endif /* CONFIG_PCI */

	if(video.flags & VPF_VESAFB) {
		fb_init();
		fbcon_init();
	}
}
