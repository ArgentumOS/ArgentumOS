/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSJSONSerialization.h — JSON to Foundation objects and back (W2h, §14).
 *
 * THE RULES ARE APPLE'S AND THEY ARE THE CONTRACT: a top-level ARRAY or DICTIONARY (unless the
 * fragments option says otherwise), leaves from NSString/NSNumber/NSArray/NSDictionary/NSNull, keys
 * that are all NSString, and no NaN or infinity. `+isValidJSONObject:` answers exactly that question,
 * and it exists because **`+dataWithJSONObject:options:error:` THROWS on an invalid object** rather
 * than filling in the error - Apple's own caveat, and the reason a caller checks first.
 *
 * THE OPTION VALUES ARE APPLE'S, and unlike most constants in this library those ARE published:
 * mutableContainers 1<<0, mutableLeaves 1<<1, allowFragments 1<<2; prettyPrinted 1<<0, sortedKeys
 * 1<<1.
 *
 * `fragmentsAllowed` IS DECLARED AND CARRIES THE SAME BIT (user's decision, 2026-09-20), and the
 * distinction is the whole point of the sentence: it is the MODERN SPELLING of a behaviour this
 * library already implements, not a new behaviour. Apple renamed the flag; the parsing path below
 * has honoured it all along. So the only thing not published is the bit, which is exactly what
 * §11.6.1 D2 covers - and the deprecated `allowFragments` name is gone, because §11.5's second
 * exclusion says API Apple deprecates is out. A program that writes the modern name compiles and
 * gets the right answer; a program that writes the old one is told so by the compiler.
 *
 * THE REST ARE STILL REFUSED, and for the original reason: `json5Allowed`,
 * `topLevelDictionaryAssumed` and the writing-side `withoutEscapingSlashes` are NEW BEHAVIOUR,
 * not renames, and a value invented for them would be a difference a program could see.
 *
 * WHAT IS NOT, named: the two STREAM forms (`+writeJSONObject:toStream:options:error:` and
 * `+JSONObjectWithStream:options:error:`), which need NSStream - a class this library does not have.
 * The data forms are the whole surface a program without streams can use.
 */
#ifndef FOUNDATION_NSJSONSERIALIZATION_H
#define FOUNDATION_NSJSONSERIALIZATION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>

NS_ASSUME_NONNULL_BEGIN

typedef unsigned long NSJSONReadingOptions;
typedef unsigned long NSJSONWritingOptions;

enum {
	NSJSONReadingMutableContainers = (1UL << 0),
	NSJSONReadingMutableLeaves = (1UL << 1),
	NSJSONReadingFragmentsAllowed = (1UL << 2)
};

enum {
	NSJSONWritingPrettyPrinted = (1UL << 0),
	NSJSONWritingSortedKeys = (1UL << 1)
};

@interface NSJSONSerialization : NSObject

+ (nullable id)JSONObjectWithData:(NSData *)data
			  options:(NSJSONReadingOptions)options
			    error:(NSError * _Nullable * _Nullable)errorPtr;

/* THROWS on an object that cannot be represented, which is Apple's contract rather than an oversight:
 * a caller with an arbitrary object asks +isValidJSONObject: first. */
+ (nullable NSData *)dataWithJSONObject:(id)object
				options:(NSJSONWritingOptions)options
				  error:(NSError * _Nullable * _Nullable)errorPtr;

+ (BOOL)isValidJSONObject:(nullable id)object;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSJSONSERIALIZATION_H */
