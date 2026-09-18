/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_kvc — the two units' shared surface. docs/design/foundation-plan.md, F9.
 *
 * The support unit imports ONLY <foundation/Foundation.h>, so this probe is also
 * what proves NSKeyValueCoding reached the umbrella — and that KVC reaches an
 * object defined in ANOTHER translation unit, which is the whole point of the
 * family: the lookup goes through the RUNTIME, not through a compile-time list.
 */

#ifndef FOUNDATION_KVC_H
#define FOUNDATION_KVC_H

#import <foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/* The fixtures. Each is nullable because the -init's are (the house's rule: an
 * initialiser can fail, so a factory that hands one back inherits that). */
NSArray * _Nullable foundation_kvc_items(void);
id _Nullable foundation_kvc_accessor_object(void);
id _Nullable foundation_kvc_ivar_object(void);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_KVC_H */
