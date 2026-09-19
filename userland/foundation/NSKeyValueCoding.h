/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSKeyValueCoding — reaching a property by NAME. docs/design/foundation-plan.md, F9.
 *
 * KVC IS A SET OF NAMING RULES, and that is the whole of it: given the key
 * `@"title"`, look for `-getTitle`, `-title`, `-isTitle`, then the ivars `_title`,
 * `_isTitle`, `title`, `isTitle`, and for writing `-setTitle:` then `_title` and
 * `title`. Nothing here needs a database, a registry or a stack, which is why the
 * family ships whole — and why what is NOT here is refused by name rather than
 * half-built (the list at the foot of this comment).
 *
 * THE ONE THING TO KNOW ABOUT THE SHAPE: Cocoa declares NSKeyValueCoding as an
 * INFORMAL protocol — NSObject answers its methods through a category, and the
 * protocol exists for adopters. That is what this is: `conformsToProtocol:` is NOT
 * the test for "does this object do KVC"; `respondsToSelector:` is, which is what
 * the probe asserts.
 *
 * A KEY PATH is the same lookup one dot at a time, and an `@`-led segment is an
 * OPERATOR over the collection the path has reached: `@count`, `@sum`, `@avg`,
 * `@max`, `@min`, `@unionOfObjects`, `@distinctUnionOfObjects`. Those are FOLDS,
 * so they are rules; the ones that would return a SET are refused, because there
 * is no NSSet here.
 *
 * REFUSED BY NAME, each needing something this library does not ship:
 *   - KEY-VALUE OBSERVING (the NSKeyValueObserving family): -addObserver:...,
 *     -willChangeValueForKey:, -didChangeValueForKey:, -observeValueForKeyPath:... —
 *     KVO is a REGISTRY of observers with a dependency graph. It is a service, not
 *     a naming rule, and the two are different sizes of thing;
 *   - the MUTABLE PROXIES: -mutableArrayValueForKey:, -mutableSetValueForKey:,
 *     -mutableOrderedSetValueForKey: and their ...KeyPath: forms return a live proxy
 *     CLASS, which is its own family;
 *   - the SET-returning operators: @unionOfArrays, @unionOfSets,
 *     @distinctUnionOfArrays, @distinctUnionOfSets — no NSSet ships;
 *   - the legacy dictionary-era API -takeValue:forKey: / -takeValuesFromDictionary:,
 *     which Cocoa removed as well.
 *
 * THE FAILURE DEFAULTS ARE THE HONEST ONES: an undefined key RAISES rather than
 * answering nil, and setting nil through a SCALAR key raises too. Both are the
 * methods a subclass overrides, and both are asserted by the probe.
 */

#ifndef FOUNDATION_NSKEYVALUECODING_H
#define FOUNDATION_NSKEYVALUECODING_H

#import <foundation/NSObject.h>
/* The three are IMPORTED, not forward-declared: a category cannot be declared on
 * an undefined class, and this header defines categories on all three.
 * NSError need only be named, so it stays a forward declaration. */
#import <foundation/NSString.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>

@class NSError;

NS_ASSUME_NONNULL_BEGIN

/*
 * The protocol, declared for ADOPTERS (see the header note). Every method is
 * implemented for NSObject by the category below.
 */
@protocol NSKeyValueCoding

- (nullable id)valueForKey:(NSString *)key;
- (void)setValue:(nullable id)value forKey:(NSString *)key;
- (nullable id)valueForKeyPath:(NSString *)keyPath;
- (void)setValue:(nullable id)value forKeyPath:(NSString *)keyPath;

/* The hooks. The -ForUndefinedKey: pair and -setNilValueForKey: RAISE as
 * implemented; -validateValue:forKey:error: answers YES unless the class has a
 * -validate<Key>:error: of its own. All four are overridable, and that is the
 * point of declaring them. */
- (nullable id)valueForUndefinedKey:(NSString *)key;
- (void)setValue:(nullable id)value forUndefinedKey:(NSString *)key;
- (void)setNilValueForKey:(NSString *)key;
- (BOOL)validateValue:(id _Nullable * _Nullable)ioValue
	       forKey:(NSString *)key
		error:(NSError * _Nullable * _Nullable)outError;

@end

/*
 * The defaults, on NSObject — and this is the informal-protocol shape, not a
 * conformance: `@interface NSObject <NSKeyValueCoding>` would need the protocol's
 * definition here, and this header needs NSObject's, so a category is what both
 * Cocoa and this library use.
 */
@interface NSObject (NSKeyValueCoding)

- (nullable id)valueForKey:(NSString *)key;
- (void)setValue:(nullable id)value forKey:(NSString *)key;
- (nullable id)valueForKeyPath:(NSString *)keyPath;
- (void)setValue:(nullable id)value forKeyPath:(NSString *)keyPath;

- (nullable id)valueForUndefinedKey:(NSString *)key;
- (void)setValue:(nullable id)value forUndefinedKey:(NSString *)key;
- (void)setNilValueForKey:(NSString *)key;
- (BOOL)validateValue:(id _Nullable * _Nullable)ioValue
	       forKey:(NSString *)key
		error:(NSError * _Nullable * _Nullable)outError;

@end

/*
 * AN ARRAY MAPS. `[array valueForKey:@"title"]` answers an array of the elements'
 * `-valueForKey:@"title"`, which is what makes `valueForKeyPath:@"@sum.price"`
 * mean anything: the operator folds what the path produced.
 */
@interface NSArray (NSKeyValueCoding)
- (nullable id)valueForKey:(NSString *)key;
@end

/*
 * A DICTIONARY LOOKS UP — unless the key is `@`-led, in which case it FOLDS over
 * its VALUES, exactly as Cocoa does. So `[dict valueForKey:@"@count"]` is the
 * number of values, not a missing key. It is the FOLD (through -valueForKeyPath:)
 * rather than the array's map: mapping an operator means asking each value for the
 * operator, which is not a question about the dictionary at all.
 */
@interface NSDictionary (NSKeyValueCoding)
- (nullable id)valueForKey:(NSString *)key;
@end


/*
 * THE COLLECTION OPERATORS (W2f), and these are the one family in W2 whose VALUES ARE NOT OURS:
 * each constant is the OPERATOR STRING a program also types into `-valueForKeyPath:`
 * ("@avg", "@count", …), so Apple publishes them as SYNTAX, and a value that did not match
 * would break the documented usage rather than merely differ from an unpublished number.
 * `@count` is the one this library's KVC is measured to implement, and the probe exercises it
 * THROUGH the API as well as asserting the string.
 */
typedef NSString *NSKeyValueOperator;

extern NSString *const NSAverageKeyValueOperator;
extern NSString *const NSCountKeyValueOperator;
extern NSString *const NSDistinctUnionOfArraysKeyValueOperator;
extern NSString *const NSDistinctUnionOfObjectsKeyValueOperator;
extern NSString *const NSDistinctUnionOfSetsKeyValueOperator;
extern NSString *const NSMaximumKeyValueOperator;
extern NSString *const NSMinimumKeyValueOperator;
extern NSString *const NSSumKeyValueOperator;
extern NSString *const NSUnionOfArraysKeyValueOperator;
extern NSString *const NSUnionOfObjectsKeyValueOperator;
extern NSString *const NSUnionOfSetsKeyValueOperator;

/* THE KVC ERROR CODE (W2f). Apple's number names a KVC validation failure; this library's own
 * `-validateValue:forKey:error:` is where such an error would be built, and the number is stated
 * once, here, so the two cannot disagree. */
extern NSInteger NSKeyValueValidationError;
NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSKEYVALUECODING_H */
