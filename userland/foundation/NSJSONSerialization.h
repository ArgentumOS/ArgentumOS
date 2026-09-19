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
 * 1<<1. The two MODERN additions (`fragmentsAllowed`, `withoutEscapingSlashes`) are deliberately not
 * here: Apple does not publish their bit positions, and a value invented for them would be a
 * difference a program could see.
 *
 * WHAT IS NOT, named: the two STREAM forms (`+writeJSONObject:toStream:options:error:` and
 * `+JSONObjectWithStream:options:error:`), which need NSStream - a class this library does not have.
 * The data forms are the whole surface a program without streams can use.
 */
#ifndef FOUNDATION_NSJSONSERIALIZATION_H
#define FOUNDATION_NSJSONSERIALIZATION_H

#import <foundation/NSObject.h>
#import <foundation/NSData.h>
#import <foundation/NSError.h>

NS_ASSUME_NONNULL_BEGIN

typedef unsigned long NSJSONReadingOptions;
typedef unsigned long NSJSONWritingOptions;

enum {
	NSJSONReadingMutableContainers = (1UL << 0),
	NSJSONReadingMutableLeaves = (1UL << 1),
	NSJSONReadingAllowFragments = (1UL << 2)
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
