/*
 * fiwix/kernel/syscalls/set_thread_area.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64 (M6 userland): set_thread_area() (Linux #243). musl/glibc i386
 * call this during libc init to install the thread-control block as the
 * %gs base (errno, thread pointer, etc.). Fiwix64 is single-threaded, so
 * the TLS always occupies the fixed GDT slot 12 (selector 0x63).
 */

#include <fiwix/fs.h>
#include <fiwix/errno.h>

/* linux/asm-i386/processor.h: struct user_desc */
struct user_desc {
	unsigned int entry_number;
	unsigned int base_addr;
	unsigned int limit;
	unsigned int seg_32bit:1;
	unsigned int contents:2;
	unsigned int read_exec_only:1;
	unsigned int limit_in_pages:1;
	unsigned int seg_not_present:1;
	unsigned int useable:1;
};

int sys_set_thread_area(struct user_desc *u_info)
{
	unsigned int base;
	int errno;

#ifdef __x86_64__
	if((errno = check_user_area(VERIFY_WRITE, u_info, sizeof(struct user_desc)))) {
		return errno;
	}

	base = u_info->base_addr;
	{
		extern void gdt64_set_tls_base(unsigned long);

		gdt64_set_tls_base((unsigned long)base);
	}
	current->tls_base = (unsigned long)base;

	/* single-threaded: always the fixed slot; musl computes the selector
	 * as (entry_number << 3) | 3 == 0x63 */
	u_info->entry_number = 12;

	return 0;
#else
	return -ENOSYS;
#endif /* __x86_64__ */
}
