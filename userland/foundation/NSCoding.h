/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSCoding — the protocol a class adopts to be archivable. F13.12,
 * docs/design/foundation-plan.md §10.
 *
 * THE CONTRACT IS TWO METHODS AND IT IS SYMMETRIC: `-encodeWithCoder:` writes the object's state
 * into the coder it is handed, and `-initWithCoder:` reads the same keys back. A class that
 * implements both can go into an archive and come out of one; a class that implements one is a class
 * that can be written and never read, which is why the pair is the protocol rather than either half.
 *
 * WHAT IS NOT HERE, named: `NSSecureCoding`. Its whole content is a promise about which CLASSES may
 * be decoded — a security boundary for archives that arrive from elsewhere — and this library's
 * unarchiver has no `-decodeObjectOfClass:forKey:` doors for it to guard. Adding the name without
 * the enforcement would be worse than not having it.
 */

#ifndef FOUNDATION_NSCODING_H
#define FOUNDATION_NSCODING_H

#import <foundation/NSObject.h>

@class NSCoder;

NS_ASSUME_NONNULL_BEGIN

@protocol NSCoding

- (void)encodeWithCoder:(NSCoder *)coder;
- (nullable instancetype)initWithCoder:(NSCoder *)coder;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCODING_H */
