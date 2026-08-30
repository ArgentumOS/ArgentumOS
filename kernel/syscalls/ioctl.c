/*
 * fnx/kernel/syscalls/ioctl.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/process.h>
#include <fnx/errno.h>
#include <fnx/fs.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

/* decode the asm-generic ioctl direction/size from the command number:
 * bits 30-31 = _IOC_WRITE(1)/_IOC_READ(2), bits 16-29 = encoded size.
 * Only applies to ioctls that carry direction bits; classic (unencoded)
 * ioctls are validated by their drivers. */
static int check_ioctl_arg(int cmd, addr_t arg)
{
	unsigned int dir = cmd & 0xC0000000;
	unsigned int sz;

	if(!dir) {
		return 0;
	}
	sz = (cmd >> 16) & 0x3FFF;
	if(!sz) {
		return 0;
	}
	/* _IOC_READ means the kernel writes to the user buffer, _IOC_WRITE
	 * means the kernel reads from it (same as VERIFY_* semantics). */
	if((dir == 0xC0000000 || dir == 0x80000000) &&
	   check_user_area(VERIFY_WRITE, (void *)arg, sz)) {
		return -EFAULT;
	}
	if((dir == 0xC0000000 || dir == 0x40000000) &&
	   check_user_area(VERIFY_READ, (void *)arg, sz)) {
		return -EFAULT;
	}
	return 0;
}

int sys_ioctl(unsigned int ufd, int cmd, addr_t arg)
{
	int errno;
	struct inode *i;

#ifdef __DEBUG__
	printk("(pid %d) sys_ioctl(%d, 0x%x, 0x%08x) -> ", current->pid, ufd, cmd, arg);
#endif /*__DEBUG__ */

	CHECK_UFD(ufd);
	i = fd_table[current->fd[ufd]].inode;

	/* Without this, a driver's CPL0 deref of 'arg' could touch any
	 * address the user chooses - including the supervisor-mapped
	 * kernel image at fnx_load_base (arbitrary kernel read/write) or
	 * an unmapped address (kernel panic). */
	if((errno = check_ioctl_arg(cmd, arg))) {
#ifdef __DEBUG__
		printk("%d\n", errno);
#endif /*__DEBUG__ */
		return errno;
	}

	if(i->fsop && i->fsop->ioctl) {
		errno = i->fsop->ioctl(i, &fd_table[current->fd[ufd]], cmd, arg);

#ifdef __DEBUG__
		printk("%d\n", errno);
#endif /*__DEBUG__ */

		return errno;
	}

#ifdef __DEBUG__
	printk("%d\n", -ENOTTY);
#endif /*__DEBUG__ */

	return -ENOTTY;
}
