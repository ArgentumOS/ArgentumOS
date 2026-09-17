/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_collection — F3's acceptance probe (docs/design/foundation-plan.md).
 *
 * Two translation units again; the support unit builds a NESTED structure (a
 * dictionary holding an array), which is the cheapest way to check that a
 * collection is held by reference like any other object.
 */

#ifndef FOUNDATION_COLLECTION_H
#define FOUNDATION_COLLECTION_H

#import <foundation/Foundation.h>

NSDictionary *foundation_collection_nested(void);

#endif /* FOUNDATION_COLLECTION_H */
