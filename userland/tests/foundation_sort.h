/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_sort — the two units' shared surface. docs/design/foundation-plan.md, F10.
 *
 * The support unit imports ONLY <Foundation/Foundation.h>, so this probe is also what proves
 * NSSortDescriptor reached the umbrella. And the split carries the family's claim: the OBJECTS
 * and one of the DESCRIPTORS are built on the other side, so a sort whose key was resolved by
 * name can only have gone through KVC.
 */

#ifndef FOUNDATION_SORT_H
#define FOUNDATION_SORT_H

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/* The fixtures. Each is nullable because the -init's are. The people are: ann(rank 3),
 * bob(1), ann(2), cid(4) — the NAME TIE between the two anns is what makes the chain and the
 * stability measurable, and the ranks order them unambiguously. */
NSArray * _Nullable foundation_sort_people(void);
NSArray * _Nullable foundation_sort_nil_value(void);
NSSortDescriptor * _Nullable foundation_sort_by_name(void);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_SORT_H */
