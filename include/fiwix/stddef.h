/*
 * fiwix/include/fiwix/stddef.h
 *
 * Copyright 2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _INCLUDE_STDDEF_H
#define _INCLUDE_STDDEF_H

#include <fiwix/types.h>

#define offsetof(st, m)	((addr_t)&(((st *)0)->m))

#endif /* _INCLUDE_STDDEF_H */
