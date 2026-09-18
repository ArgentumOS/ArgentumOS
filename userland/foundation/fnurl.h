/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * fnurl.h — the private half of the URL family: one function, in its own header, for the same reason
 * fncalendar.h and fnpredicate.h are. RFC 3986 §5.2's resolution belongs to NSURLComponents (it is
 * the component-wise algorithm), and it is REACHED from NSURL's +URLWithString:relativeToURL: —
 * which is why F8 refused the two together: without the resolution, that door has nothing to do.
 */

#ifndef FOUNDATION_FNURL_H
#define FOUNDATION_FNURL_H

#import <foundation/NSObject.h>

@class NSURL;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* RESOLVE `reference` AGAINST `base`, both as URL text, per RFC 3986 §5.2. A reference that is
 * already absolute comes back as itself (normalised); one with no base to resolve against is
 * answered as far as it can be, which is the same algorithm with the base's fields absent. */
NSURL * _Nullable FNURLResolveRelative(NSString *reference, NSString * _Nullable base);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNURL_H */
