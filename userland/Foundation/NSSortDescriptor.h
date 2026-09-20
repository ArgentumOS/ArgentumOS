/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSortDescriptor — how to order a collection, as a VALUE.
 * docs/design/foundation-plan.md, F10.
 *
 * A DESCRIPTOR IS A KEY PLUS A DIRECTION PLUS A WAY TO COMPARE, and the key is why this family
 * follows KVC (F9): `-valueForKey:` is what makes `@"title"` mean anything when the objects
 * being sorted have no relation to this file. Nothing here knows what a "title" is.
 *
 * THE THREE COMPARISON KINDS ARE THE THREE THIS LIBRARY CAN ALREADY EXPRESS, which is what
 * makes the family small rather than half-built:
 *   - the default, `-compare:` on the two keys' values;
 *   - `-selector:`, a comparison the values answer themselves;
 *   - `-comparator:`, a caller's block.
 * The constructors are separate so that "how" is never silently assumed.
 *
 * A CHAIN IS LEXICOGRAPHIC AND THE SORT IS STABLE. `-sortedArrayUsingDescriptors:` takes an
 * ARRAY of them: the first decides, a tie falls to the second, and a tie that survives the
 * whole chain keeps the INPUT order. The second half of that sentence is a promise the probe
 * measures directly, because an unstable sort passes every single-descriptor test ever
 * written — and it is the reason the implementation sorts by merging rather than by a
 * comparison sort in place.
 *
 * REFUSED BY NAME, and neither is a gap in sorting:
 *   - `-allowEvaluation` / `-isEvaluationAllowed`: a sandbox for UNTRUSTED archives, which is
 *     a security policy rather than an ordering rule;
 *   - `-initWithCoder:` / `-encodeWithCoder:`: they belong to an NSCoding family this library
 *     does not ship. A descriptor here is built, not resurrected.
 */

#ifndef FOUNDATION_NSSORTDESCRIPTOR_H
#define FOUNDATION_NSSORTDESCRIPTOR_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>

@class NSString;
@class NSArray;

/* NSComparator lives with the collections (it is what an array's comparator sorts take). */

NS_ASSUME_NONNULL_BEGIN

@interface NSSortDescriptor : NSObject <NSCopying>

/*
 * The key is nullable because `nil` is a real answer in Cocoa: a descriptor with no key
 * compares the OBJECTS THEMSELVES rather than a value taken from them. That is the one case
 * where "which key" has an answer, and it is not a missing argument.
 */
+ (instancetype)sortDescriptorWithKey:(nullable NSString *)key ascending:(BOOL)ascending;
+ (instancetype)sortDescriptorWithKey:(nullable NSString *)key
			    ascending:(BOOL)ascending
			     selector:(nullable SEL)selector;
+ (instancetype)sortDescriptorWithKey:(nullable NSString *)key
			    ascending:(BOOL)ascending
			   comparator:(NSComparator)comparator;

- (instancetype)initWithKey:(nullable NSString *)key ascending:(BOOL)ascending;
- (instancetype)initWithKey:(nullable NSString *)key
		  ascending:(BOOL)ascending
		   selector:(nullable SEL)selector;
- (instancetype)initWithKey:(nullable NSString *)key
		  ascending:(BOOL)ascending
		 comparator:(NSComparator)comparator;

- (nullable NSString *)key;
- (BOOL)ascending;
- (nullable SEL)selector;
- (nullable NSComparator)comparator;

/* The same descriptor with `ascending` flipped — the key and the comparison are untouched. */
- (NSSortDescriptor *)reversedSortDescriptor;

/*
 * THE ORDERING RULE ITSELF, in one place: two objects in, one NSComparisonResult out. The key
 * (if any) is resolved through KVC on BOTH, then the comparison runs, then the direction is
 * applied — so a chain built on this cannot disagree with a single descriptor about direction.
 */
- (NSComparisonResult)compareObject:(id)object1 toObject:(id)object2;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSORTDESCRIPTOR_H */
