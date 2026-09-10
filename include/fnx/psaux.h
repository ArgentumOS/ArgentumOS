/*
 * fnx/include/fnx/psaux.h
 *
 * Copyright 2024, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#ifdef CONFIG_PSAUX

#ifndef _FNX_PSAUX_H
#define _FNX_PSAUX_H

#include <fnx/fs.h>
#include <fnx/charq.h>
#include <fnx/sigcontext.h>

#define PSAUX_IRQ	12

void irq_psaux(int num, struct sigcontext *);
void psaux_init(void);

#endif /* _FNX_PSAUX_H */

#endif /* CONFIG_PSAUX */
