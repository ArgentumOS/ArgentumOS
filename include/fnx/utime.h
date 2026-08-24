/*
 * fnx/include/fnx/utime.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_UTIME_H
#define _FNX_UTIME_H

#include <fnx/types.h>

struct utimbuf {
	__time_t actime;	/* access time */
	__time_t modtime;	/* modification time */
};

#endif /* _FNX_UTIME_H */
