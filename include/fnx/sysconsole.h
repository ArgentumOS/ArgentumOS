/*
 * fnx/include/fnx/sysconsole.h
 *
 * Copyright 2024, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_SYSCONSOLE_H
#define _FNX_SYSCONSOLE_H

#include <fnx/config.h>
#include <fnx/types.h>
#include <fnx/tty.h>

struct sysconsole {
	__dev_t dev;
	struct tty *tty;
};

extern struct sysconsole sysconsole_table[NR_SYSCONSOLES];

int add_sysconsoledev(__dev_t);
void register_console(struct tty *);
void sysconsole_init(void);

#endif /* _FNX_SYSCONSOLE_H */
