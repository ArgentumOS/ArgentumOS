/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSArray / NSMutableArray — an ordered collection.
 * docs/design/foundation-plan.md, F3; subscripting and NSNotFound from the
 * public-API audit. The NSIndexSet methods came with the dependency queue.
 *
 * ORDERED AND ZERO-BASED, and one concrete class rather than a cluster (v1's
 * rule). Elements are RETAINED, not copied — Cocoa's rule for arrays, and the
 * opposite of its rule for dictionary keys, because an array's element identity
 * is the caller's business while a key is a lookup token.
 *
 * The slots are a C array of object pointers, so ARC does not manage them: the
 * implementation retains and releases each one itself (objc_retain/objc_release)
 * — the standard idiom for a collection's own storage, and why this file is not
 * an MRR file despite owning memory.
 */

#ifndef FOUNDATION_NSARRAY_H
#define FOUNDATION_NSARRAY_H

#import <Foundation/NSObject.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSOrderedCollectionDifference.h>

@class NSString;
@class NSIndexSet;
@class NSPredicate;

/* NULLABILITY (F6): NONNULL by default, and the two that can legitimately be nil
 * are -firstObject and -lastObject, because an EMPTY array has neither. */
NS_ASSUME_NONNULL_BEGIN
@class NSURL;

/* ===================================================================================================
 * NSARRAY IS A CLASS CLUSTER (2026-09-28; docs/design/foundation-clusters-plan.md §C.3).
 *
 * THE PRIMITIVE METHODS ARE THE CONTRACT, AND THERE ARE EXACTLY TWO:
 *
 *     - (NSUInteger)count
 *     - (ObjectType)objectAtIndex:(NSUInteger)index     (RAISES NSRangeException out of range)
 *
 * A SUBCLASS THAT OVERRIDES THOSE TWO GETS THE WHOLE FAMILY. Every other method here — -firstObject,
 * -indexOfObject:, -isEqualToArray:, -hash, -description, -subarrayWithRange:, the sorts, the filters,
 * -getObjects:range: and fast enumeration — is written OVER them rather than over this class's storage.
 * That is Apple's own sentence about primitive methods, and it is also mechanical need: a concrete class
 * with a different LAYOUT (an inline one, say) has none of the ivars below, so anything reaching into
 * `_items` would read whatever that class keeps there.
 *
 * THE FRONT IS PUBLIC AND ITS CONCRETE CLASSES ARE PRIVATE — in the implementation file and in no header.
 * Apple does not publish its names ("You don't, and can't, choose the actual class of the instance"), so
 * ours are a free choice: AGArrayEmpty (ONE shared instance, the empty case, and the answer to
 * `[[NSArray alloc] init]`), AGArrayOne (the object IS the storage), AGArraySmall (inline storage up to
 * eight elements), AGArrayItems (the general case, over these ivars), and AGArrayMutable (the mutable
 * family's). `-class` answers one of them, which is what a cluster means; `-classForCoder` answers
 * NSArray, so no private name can reach an archive.
 * =================================================================================================== */

@interface NSArray<__covariant ObjectType> : NSObject <NSCopying, NSFastEnumeration>
{
	id __unsafe_unretained *_items;	/* owned BY HAND: every slot is retained */
	unsigned long _count;
	unsigned long _capacity;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
}

+ (instancetype)array;
+ (instancetype)arrayWithObject:(ObjectType)object;
+ (instancetype)arrayWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
+ (instancetype)arrayWithArray:(NSArray<ObjectType> *)other;
+ (instancetype)arrayWithObjects:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (id)initWithObject:(id)object;
- (id)initWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
- (id)initWithArray:(NSArray<ObjectType> *)other;
- (id)initWithObjects:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (NSUInteger)count;
- (ObjectType)objectAtIndex:(NSUInteger)index;
/* Cocoa's subscript: `array[0]` lowers to this. */
- (ObjectType)objectAtIndexedSubscript:(NSUInteger)index;

- (nullable ObjectType)firstObject;
- (nullable ObjectType)lastObject;
/* NSNotFound for a missing element — NOT (NSUInteger)-1, which is what this
 * answered before the audit and which never equals NSNotFound. */
- (NSUInteger)indexOfObject:(ObjectType)object;
- (BOOL)containsObject:(ObjectType)object;

- (NSArray<ObjectType> *)arrayByAddingObject:(ObjectType)object;	/* a new array; self is untouched */
- (NSArray<ObjectType> *)arrayByAddingObjectsFromArray:(NSArray<ObjectType> *)other;
- (NSArray<ObjectType> *)subarrayWithRange:(NSRange)range;
- (void)getObjects:(ObjectType __unsafe_unretained _Nonnull * _Nonnull)buffer range:(NSRange)range;

- (NSUInteger)indexOfObject:(id)object inRange:(NSRange)range;
- (NSUInteger)indexOfObjectIdenticalTo:(ObjectType)object;
- (NSUInteger)indexOfObject:(id)object
		   inSortedRange:(NSRange)range
			   options:(NSBinarySearchingOptions)options
		   usingComparator:(NSComparator)comparator;

- (NSString *)componentsJoinedByString:(NSString *)separator;
- (NSArray<ObjectType> *)sortedArrayUsingSelector:(SEL)comparator;
- (NSArray<ObjectType> *)sortedArrayUsingComparator:(NSComparator)comparator;
/* THE OPTIONS FORM (§62.104). `NSSortStable` is honoured by the algorithm this library already sorts with
 * (NSObjCRuntime.h's note says which, and the probe MEASURES it); `NSSortConcurrent` is a hint nothing here
 * takes, so a caller who passes it gets a correct, sequential answer. */
- (NSArray<ObjectType> *)sortedArrayWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator;
/* THE DESCRIPTOR FORMS (F10). `sortDescriptors` is an ARRAY because a sort is a CHAIN: the first
 * descriptor decides, a tie falls to the second, and a tie that survives the whole chain keeps
 * the INPUT order — the sort is STABLE, which the probe measures directly. The C-function form
 * takes `NSInteger (*)(id, id, void *)` and passes `context` straight through. */
- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)sortDescriptors;
- (NSArray<ObjectType> *)sortedArrayUsingFunction:(NSInteger (*)(ObjectType, ObjectType, void *))comparator
			      context:(nullable void *)context;
- (void)enumerateObjectsUsingBlock:(void (^)(ObjectType object, NSUInteger index, BOOL *stop))block;

/* The NSIndexSet forms. -objectsAtIndexes: RAISES NSRangeException for an index
 * past the end — the caller asked for something that is not there — while
 * -indexesOfObjectsPassingTest: hands back the indexes that passed. */
- (NSArray<ObjectType> *)objectsAtIndexes:(NSIndexSet *)indexes;
- (NSIndexSet *)indexesOfObjectsPassingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;

/* THE PREDICATE FILTER (F11a): the elements the predicate answers YES for, in order. The
 * returned array is NEW and the receiver is untouched — Cocoa's rule everywhere here. */
- (NSArray *)filteredArrayUsingPredicate:(NSPredicate *)predicate;

- (NSEnumerator<ObjectType> *)objectEnumerator;
- (NSEnumerator<ObjectType> *)reverseObjectEnumerator;

- (BOOL)isEqualToArray:(NSArray<ObjectType> *)other;

@end

/* ---- THE DIFFERENCE DOORS (2026-09-26) --------------------------------------------------------------
 *
 * THE PLACEMENT IS THE HOUSE PATTERN RATHER THAN THE CLASS'S OWN INTERFACE, and the reason is mechanical: the
 * doors are IMPLEMENTED beside the differ, in NSOrderedCollectionDifference.m (as categories), and a method
 * declared in a class's own @interface but implemented in another translation unit makes clang warn
 * `-Wincomplete-implementation` in the class's own file. The plist conveniences (`+arrayWithContentsOfFile:`)
 * are declared the same way, for the same reason. A CALLER cannot tell the difference: `[array
 * differenceFromArray:other]` is the same call either way, and the umbrella header includes both.
 *
 * THE DIRECTION IS THE THING TO GET RIGHT, and NSOrderedCollectionDifference.h is its subject: `[A
 * differenceFromArray:B]` answers a difference that, APPLIED TO B, produces A. The receiver is the DESTINATION
 * and the argument is the SOURCE, so an INSERTION's `index` is in the receiver and a REMOVAL's is in the
 * argument. */
@interface NSArray<ObjectType> (NSOrderedCollectionDifferenceAdditions)

- (NSOrderedCollectionDifference<ObjectType> *)differenceFromArray:(NSArray<ObjectType> *)other;
- (NSOrderedCollectionDifference *)differenceFromArray:(NSArray *)other
					  withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options;
/* THE EQUIVALENCE-TEST FORM. Apple: "don't use the option inferMoves when providing a block for the equivalence
 * test. The changes returned in the difference object don't include valid values for associatedIndex" — so a
 * move option here is IGNORED and every associated index stays NSNotFound, which is what that page describes. */
- (NSOrderedCollectionDifference *)differenceFromArray:(NSArray *)other
					  withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options
				  usingEquivalenceTest:(BOOL (^)(id obj1, id obj2))block;
/* "Creates a new array by applying a difference object to an existing array." The RECEIVER IS THE SOURCE, so
 * `[b arrayByApplyingDifference:[a differenceFromArray:b]]` answers `a`. */
- (NSArray<ObjectType> *)arrayByApplyingDifference:(NSOrderedCollectionDifference<ObjectType> *)difference;

@end

@interface NSMutableArray<ObjectType> : NSArray<ObjectType> <NSMutableCopying>

+ (instancetype)array;
+ (instancetype)arrayWithCapacity:(NSUInteger)capacity;
- (id)initWithCapacity:(NSUInteger)capacity;

- (void)addObject:(ObjectType)object;
- (void)insertObject:(ObjectType)object atIndex:(NSUInteger)index;
- (void)removeObjectAtIndex:(NSUInteger)index;
- (void)removeAllObjects;
- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(ObjectType)object;
- (void)addObjectsFromArray:(NSArray<ObjectType> *)other;
- (void)removeLastObject;
- (void)removeObject:(ObjectType)object;
- (void)removeObjectIdenticalTo:(ObjectType)object;
- (void)removeObjectIdenticalTo:(id)object inRange:(NSRange)range;
- (void)removeObject:(id)object inRange:(NSRange)range;
- (void)removeObjectsInRange:(NSRange)range;
- (void)setArray:(NSArray<ObjectType> *)other;
- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second;
- (void)replaceObjectsInRange:(NSRange)range withObjectsFromArray:(NSArray<ObjectType> *)other;
- (void)replaceObjectsInRange:(NSRange)range
	 withObjectsFromArray:(NSArray *)other
			  range:(NSRange)otherRange;
- (void)sortUsingComparator:(NSComparator)comparator;
- (void)sortWithOptions:(NSSortOptions)options usingComparator:(NSComparator)comparator;
- (void)sortUsingSelector:(SEL)comparator;
- (void)sortUsingDescriptors:(NSArray *)sortDescriptors;
- (void)sortUsingFunction:(NSInteger (*)(ObjectType, ObjectType, void *))comparator context:(nullable void *)context;
/* Keeps only what the predicate answers YES for. In place, because that is what MUTABLE means. */
- (void)filterUsingPredicate:(NSPredicate *)predicate;

/* The NSIndexSet forms. The counts of objects and indexes must AGREE, and the
 * mismatches raise NSInvalidArgumentException because the message is the only
 * thing that makes the bug diagnosable. */
- (void)insertObjects:(NSArray<ObjectType> *)objects atIndexes:(NSIndexSet *)indexes;
- (void)removeObjectsAtIndexes:(NSIndexSet *)indexes;
- (void)replaceObjectsAtIndexes:(NSIndexSet *)indexes withObjects:(NSArray<ObjectType> *)objects;

/* `array[i] = x`: replaces, and APPENDS when i == count (Cocoa's rule). */
- (void)setObject:(ObjectType)object atIndexedSubscript:(NSUInteger)index;

NS_ASSUME_NONNULL_END



@end

#endif /* FOUNDATION_NSARRAY_H */
