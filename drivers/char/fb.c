/*
 * fnx/drivers/char/fb.c
 *
 * Copyright 2021-2022, Jordi Sanfeliu. All rights reserved.
 * Portions Copyright 2024, Greg Haerr.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/asm.h>
#include <fnx/kernel.h>
#include <fnx/fb.h>
#include <fnx/devices.h>
#include <fnx/fs.h>
#include <fnx/errno.h>
#include <fnx/stdio.h>
#include <fnx/string.h>
#include <fnx/fb.h>
#include <fnx/pci.h>
#include <fnx/mm.h>
#include <fnx/mman.h>
#include <fnx/fcntl.h>
#include <fnx/bios.h>
#include <fnx/fs_devfs.h>
#include <fnx/stat.h>
#include <fnx/video.h>


static struct fs_operations fb_driver_fsop = {
	0,
	0,

	fb_open,
	fb_close,
	fb_read,
	fb_write,
	fb_ioctl,
	fb_llseek,
	NULL,			/* readdir */
	NULL,			/* readdir64 */
	fb_mmap,
	NULL,			/* select */

	NULL,			/* readlink */
	NULL,			/* followlink */
	NULL,			/* bmap */
	NULL,			/* lockup */
	NULL,			/* rmdir */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* mknod */
	NULL,			/* truncate */
	NULL,			/* create */
	NULL,			/* rename */

	NULL,			/* read_block */
	NULL,			/* write_block */

	NULL,			/* read_inode */
	NULL,			/* write_inode */
	NULL,			/* ialloc */
	NULL,			/* ifree */
	NULL,			/* statfs */
	NULL,			/* read_superblock */
	NULL,			/* remount_fs */
	NULL,			/* write_superblock */
	NULL			/* release_superblock */
};

static struct device fb_device = {
	"fb",
	FB_MAJOR,
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	NULL,
	NULL,
	&fb_driver_fsop,
	NULL,
	NULL,
	NULL
};

int fb_open(struct inode *i, struct fd *f)
{
	return 0;
}

int fb_close(struct inode *i, struct fd *f)
{
	return 0;
}

int fb_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	addr_t addr;

	if(f->offset >= video.memsize) {
		return 0;
	}

	addr = (addr_t)video.address + f->offset;
	count = MIN(count, video.memsize - f->offset);
	memcpy_b(buffer, (void *)addr, count);
	f->offset += count;
	return count;
}

int fb_write(struct inode *i, struct fd *f, const char *buffer, __size_t count)
{
	addr_t addr;

	if(f->offset >= video.memsize) {
		return -ENOSPC;
	}

	addr = (addr_t)video.address + f->offset;
	count = MIN(count, video.memsize - f->offset);
	memcpy_b((void *)addr, buffer, count);
	f->offset += count;
	return count;
}

int fb_mmap(struct inode *i, struct vma *vma)
{
	/* 64-bit widths: vma->start/end are full user addresses
	 * (e.g. 0x400000000000); a 32-bit loop variable truncates to 0
	 * and runs until the address wraps - an unbounded map loop that
	 * stomps the process's low mappings. */
	addr_t fbaddr, addr;

	/* a mapping longer than the framebuffer would walk fbaddr past
	 * the device window and eventually wrap the physical address.
	 * Compare against the page-rounded size: the kernel rounds a map
	 * length up to a page, so non-page-multiple framebuffers (e.g.
	 * 800x600x32 = 1,920,000 B) would otherwise always be rejected */
	if(vma->end - vma->start > ((video.memsize + 4095) & ~4095)) {
		return -EINVAL;
	}

	fbaddr = (addr_t)video.fb_phys;
	for (addr = vma->start; addr < vma->end; addr += 4096) {
		/* map framebuffer physaddr into user space without page allocations */
		map_page_flags(current, addr, fbaddr, PROT_READ|PROT_WRITE, PAGE_NOALLOC);
		fbaddr += 4096;
	}
	return 0;
}

int fb_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	struct fb_mode mode;
	int errno;

	switch (cmd) {
		case IO_FB_XRES:
			return video.fb_width;
		case IO_FB_YRES:
			return video.fb_height;
		case IO_FB_GETMODE:
			if((errno = check_user_area(VERIFY_WRITE, (void *)arg,
						    sizeof(struct fb_mode)))) {
				return errno;
			}
			mode.width = video.fb_width;
			mode.height = video.fb_height;
			mode.bpp = video.fb_bpp;
			mode.pitch = video.fb_pitch;
			if((errno = copy_to_user((void *)arg, &mode,
						 sizeof(struct fb_mode)))) {
				return errno;
			}
			return 0;
		case IO_FB_SETMODE:
			if((errno = check_user_area(VERIFY_READ, (void *)arg,
						    sizeof(struct fb_mode)))) {
				return errno;
			}
			if((errno = copy_from_user(&mode, (void *)arg,
						   sizeof(struct fb_mode)))) {
				return errno;
			}
			return video_gop_set_mode(mode.width, mode.height,
						  mode.bpp);
		default:
			return -EINVAL;
	}
}

__loff_t fb_llseek(struct inode *i, __loff_t offset)
{
	return offset;
}

void fb_init(void)
{
	unsigned int limit, from;

	SET_MINOR(fb_device.minors, FB_MINOR);
	limit = (addr_t)video.address + video.memsize - 1;

	/*
	 * Frame buffer memory must be marked as reserved because its memory
	 * range (e.g: 0xFD000000-0xFDFFFFFF) might conflict with the physical
	 * memory below 1GB (e.g: 0x3D000000-0x3DFFFFFF + PAGE_OFFSET).
	 */
	from = (addr_t)video.address - PAGE_OFFSET;
	bios_map_reserve(from, from + video.memsize);

	printk("fb0       0x%08x-0x%08x\ttype=%s %x.%x resolution=%dx%dx%d size=%dMB\n",
		video.address,
		limit,
		video.signature,
		video.fb_version >> 8,
		video.fb_version & 0xFF,
		video.fb_width,
		video.fb_height,
		video.fb_bpp,
		video.memsize / 1024 / 1024
	);
#ifdef CONFIG_PCI
	if(video.pci_dev) {
		pci_show_desc(video.pci_dev);
	}
#endif /* CONFIG_PCI */
	if(register_device(CHR_DEV, &fb_device)) {
		printk("ERROR: %s(): unable to register fb device.\n", __FUNCTION__);
		return;
	}
	devfs_make_node("Display/fb0", MKDEV(FB_MAJOR, FB_MINOR),
			S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
			S_IROTH | S_IWOTH);
}
