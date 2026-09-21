/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPointerArray — an array of RAW POINTERS, ordered and indexable, with NSPointerFunctions deciding what a
 * pointer means. docs/design/foundation-plan.md §12.3 W13a.
 *
 * IT IS NOT AN NSArray OF POINTERS, and the difference is the reason it exists: an NSArray holds OBJECTS and
 * retains them; this holds addresses. That is what lets it keep things NSArray cannot — a `void *` to a C
 * struct, an integer that is not an object, a C string, or an object it deliberately does NOT retain (the
 * weak policy, which is a dangling pointer rather than a zeroed one — see NSPointerFunctions).
 *
 * THE ELEMENTS CAN BE NULL, AND THE COUNT STILL COUNTS THEM. A pointer array's `count` is the number of SLOTS,
 * so a slot holding NULL is a real slot; `-compact` is the operation that removes them, and the two are
 * separate on purpose: growing an array and filling it in later has to be expressible. This is also why the
 * `count` setter exists at all.
 *
 * APPLE'S TWO CONVENIENCE CONSTRUCTORS SELECT THE TWO COMMON CONFIGURATIONS, and they are worth reading as
 * the definition of each: `+strongObjectsPointerArray` is the OBJECT personality with strong memory (the
 * closest thing here to an NSArray), and `+weakObjectsPointerArray` is the OBJECT-POINTER personality with
 * weak memory — identity rather than equality, and no ownership.
 *
 * SERIALISATION CARRIES THE OBJECTS, NOT THE ADDRESSES (NSSecureCoding, and it is a real decision): a pointer
 * is meaningless in another process, so an archive holds the POINTEES in order and a reader rebuilds the
 * array through its own `+strongObjectsPointerArray`. That works for the object personalities and CANNOT work
 * for the others — an integer personality's "pointer" is a number and a struct's is bytes with no type — so
 * those archives are REFUSED by name rather than written as something a reader would misread.
 */

#ifndef FOUNDATION_NSPOINTERARRAY_H
#define FOUNDATION_NSPOINTERARRAY_H

#import <Foundation/NSObject.h>
#import <Foundation/NSPointerFunctions.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSCoding.h>

@class NSArray;
@class NSCoder;

NS_ASSUME_NONNULL_BEGIN

@interface NSPointerArray : NSObject <NSCopying, NSFastEnumeration, NSSecureCoding>
{
	void **_items;			/* the slots */
	NSUInteger _count;		/* how many slots are OCCUPIED, including the NULL ones */
	NSUInteger _capacity;		/* how many are allocated */
	NSPointerFunctions *_functions;	/* what a pointer means, and who owns it */
}

/* THE TWO WAYS TO CHOOSE THE FUNCTIONS: by options, or by handing over a configured object. */
- (instancetype)initWithOptions:(NSPointerFunctionsOptions)options;
- (instancetype)initWithPointerFunctions:(NSPointerFunctions *)functions;

+ (instancetype)strongObjectsPointerArray;
+ (instancetype)weakObjectsPointerArray;

/* The functions in use. It is the object the caller passed, or the one the options built. */
- (NSPointerFunctions *)pointerFunctions;

/* THE SLOTS. `count` counts them, NULL or not, and SETTING it grows with NULLs or shrinks (relinquishing
 * what it drops). */
- (NSUInteger)count;
- (void)setCount:(NSUInteger)count;

/* The elements, in order, as an NSArray — every pointer handed back as an object, so this is only meaningful
 * for the object personalities. */
- (NSArray *)allObjects;

- (nullable void *)pointerAtIndex:(NSUInteger)index;
- (void)addPointer:(nullable void *)pointer;
- (void)removePointerAtIndex:(NSUInteger)index;
- (void)insertPointer:(nullable void *)item atIndex:(NSUInteger)index;
- (void)replacePointerAtIndex:(NSUInteger)index withPointer:(nullable void *)item;

/* Removes the NULL slots, closing the gaps. */
- (void)compact;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPOINTERARRAY_H */
