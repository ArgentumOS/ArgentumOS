/*
 * fnx/include/fnx/timeb.h
 *
 * Copyright 2018, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_TIMEB_H
#define _FNX_TIMEB_H

struct timeb {
	unsigned int time;		/* in seconds since Epoch */
	unsigned short int millitm;	/* additional milliseconds */
	short int timezone;		/* minutes west of GMT */
	short int dstflag;		/* nonzero if DST is used */
};

#endif /* _FNX_TIMEB_H */
