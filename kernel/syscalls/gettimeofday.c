/*
 * fnx/kernel/syscalls/gettimeofday.c
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/fs.h>
#include <fnx/process.h>
#include <fnx/time.h>
#include <fnx/timer.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_gettimeofday()\n", current->pid);
#endif /*__DEBUG__ */

	if(tv) {
		if((errno = check_user_area(VERIFY_WRITE, tv, sizeof(struct timeval)))) {
			return errno;
		}
		tv->tv_sec = CURRENT_TIME;
		tv->tv_usec = ((CURRENT_TICKS % HZ) * 1000000) / HZ;
		tv->tv_usec += gettimeoffset();
	}
	if(tz) {
		if((errno = check_user_area(VERIFY_WRITE, tz, sizeof(struct timezone)))) {
			return errno;
		}
		tz->tz_minuteswest = kstat.tz_minuteswest;
		tz->tz_dsttime = kstat.tz_dsttime;
	}
	return 0;
}
