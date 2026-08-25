/*
 * fnx/kernel/syscalls/getrandom.c
 *
 * FNX: getrandom(318) - fill a user buffer with kernel randomness.
 *
 * The kernel has a single LCG (kstat.random_seed, also used by
 * /dev/urandom); GRND_RANDOM (/dev/random semantics) is treated the
 * same. GRND_NONBLOCK is accepted but never blocks (the LCG is always
 * ready). GRND_INSECURE (4) is accepted too.
 */

#include <fnx/syscalls.h>
#include <fnx/kernel.h>
#include <fnx/process.h>
#include <fnx/errno.h>

#define GRND_NONBLOCK	0x0001
#define GRND_RANDOM	0x0002
#define GRND_INSECURE	0x0004

int sys_getrandom(char *buf, __size_t count, unsigned int flags)
{
	int errno, n;

	if(flags & ~(GRND_NONBLOCK | GRND_RANDOM | GRND_INSECURE)) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, buf, count))) {
		return errno;
	}

	for(n = 0; n < count; n++) {
		kstat.random_seed = kstat.random_seed * 1103515245 + 12345;
		buf[n] = (char)(unsigned int)(kstat.random_seed / 65536) % 256;
	}
	return count;
}
