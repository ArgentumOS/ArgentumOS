/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnit — a unit of measure, as a VALUE. docs/design/foundation-plan.md §12.3 W12 (first slice).
 *
 * THE WHOLE CLASS IS A SYMBOL AND AN IDENTITY, and the second half is the interesting one: a unit is
 * equal to another when it is the SAME unit, not when it has the same symbol. Two units symbolised "m"
 * (a metre and a minute, say) are different units, and a measurement's unit is what its conversion
 * arithmetic is defined against — so `-isEqual:` compares identity and the symbol is presentation.
 * That is Apple's shape; a symbol-comparing equality would make two unrelated units interchangeable.
 *
 * NOTHING HERE CONVERTS: a plain NSUnit has no converter at all. `NSDimension` (the next class up) is
 * what adds one, which is why `NSUnit` is the parent of the dimensional families rather than the thing
 * measurements are made of. Apple's own pages say the same in their inheritance section: NSDimension
 * inherits from NSUnit and the NSUnit* families inherit from NSDimension.
 */

#ifndef FOUNDATION_NSUNIT_H
#define FOUNDATION_NSUNIT_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

@class NSString;

NS_ASSUME_NONNULL_BEGIN

@interface NSUnit : NSObject <NSCopying, NSSecureCoding>
{
	NSString *_symbol;
}

/* The symbolic representation of the unit — "bytes", "B", "kB". PRESENTATION: see the note above about
 * what equality means. */
- (NSString *)symbol;

/* The designated initializer. Everything below this class builds on it. */
- (instancetype)initWithSymbol:(NSString *)symbol;

- (id)copy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUNIT_H */
