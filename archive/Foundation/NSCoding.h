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
 * WHAT WAS NOT HERE, AND IS NOW (amended 2026-09-20, W11): `NSSecureCoding`. This header used to
 * refuse it in writing, and the refusal was honest while it stood: the protocol's whole content is a
 * promise about which CLASSES may be decoded, this library's unarchiver has no
 * `-decodeObjectOfClass:forKey:` doors to guard, and "adding the name without the enforcement would
 * be worse than not having it."
 *
 * THE DECISION WAS REOPENED BY W11 AND THE PROTOCOL SHIPS. What changed is the fact it was weighed
 * against: THREE classes Apple documents as `NSSecureCoding` now need it — `NSISO8601DateFormatter`
 * and `NSPersonNameComponents` in W11, `NSMeasurement` in W12 — so leaving it out stopped being a
 * tidy absence and became a wrong answer to `-conformsToProtocol:`, which is a thing a program can
 * ask on any day. A class that conforms on Apple's platform and does not conform here is a
 * DIFFERENCE (§11), and §11.6's gates do not cover it: nothing about Argentum's function prevents
 * the declaration.
 *
 * **THE ENFORCEMENT IS STILL MISSING AND IS NAMED RATHER THAN IMPLIED.** `+supportsSecureCoding`
 * answers, and the unarchiver does not yet ask. The gap is the coder's, not this protocol's, and it
 * is the work item §11.3 already carries for the secure-decoding doors
 * (`-decodeObjectOfClass:forKey:`, `-decodeTopLevelObjectOfClass:forKey:error:`). Until those land,
 * the conformance says what Apple's says and guards what ours can.
 */

#ifndef FOUNDATION_NSCODING_H
#define FOUNDATION_NSCODING_H

#import <Foundation/NSObject.h>

@class NSCoder;

NS_ASSUME_NONNULL_BEGIN

@protocol NSCoding

- (void)encodeWithCoder:(NSCoder *)coder;
- (nullable instancetype)initWithCoder:(NSCoder *)coder;

@end

/* The class-gating half. Apple's whole surface is the one class method: a decoder that is asked to
 * decode an object checks the CLASS's answer before it trusts the archive. */
@protocol NSSecureCoding <NSCoding>

+ (BOOL)supportsSecureCoding;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSCODING_H */
