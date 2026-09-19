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

#import <foundation/NSObject.h>
#import <foundation/NSFastEnumeration.h>
#import <foundation/NSEnumerator.h>

@class NSString;
@class NSIndexSet;
@class NSPredicate;

/* NULLABILITY (F6): NONNULL by default, and the two that can legitimately be nil
 * are -firstObject and -lastObject, because an EMPTY array has neither. */
NS_ASSUME_NONNULL_BEGIN
@class NSURL;

@interface NSArray : NSObject <NSCopying, NSFastEnumeration>
{
	id __unsafe_unretained *_items;	/* owned BY HAND: every slot is retained */
	unsigned long _count;
	unsigned long _capacity;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
}

+ (instancetype)array;
+ (instancetype)arrayWithObject:(id)object;
+ (instancetype)arrayWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count;
+ (instancetype)arrayWithArray:(NSArray *)other;
+ (instancetype)arrayWithObjects:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (id)initWithObject:(id)object;
- (id)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count;
- (id)initWithArray:(NSArray *)other;
- (id)initWithObjects:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
/* Cocoa's subscript: `array[0]` lowers to this. */
- (id)objectAtIndexedSubscript:(NSUInteger)index;

- (nullable id)firstObject;
- (nullable id)lastObject;
/* NSNotFound for a missing element — NOT (NSUInteger)-1, which is what this
 * answered before the audit and which never equals NSNotFound. */
- (NSUInteger)indexOfObject:(id)object;
- (BOOL)containsObject:(id)object;

- (NSArray *)arrayByAddingObject:(id)object;	/* a new array; self is untouched */
- (NSArray *)arrayByAddingObjectsFromArray:(NSArray *)other;
- (NSArray *)subarrayWithRange:(NSRange)range;
- (void)getObjects:(id __unsafe_unretained _Nonnull * _Nonnull)buffer range:(NSRange)range;

- (NSUInteger)indexOfObject:(id)object inRange:(NSRange)range;
- (NSUInteger)indexOfObjectIdenticalTo:(id)object;
- (NSUInteger)indexOfObject:(id)object
		   inSortedRange:(NSRange)range
			   options:(NSBinarySearchingOptions)options
		   usingComparator:(NSComparator)comparator;

- (NSString *)componentsJoinedByString:(NSString *)separator;
- (NSArray *)sortedArrayUsingSelector:(SEL)comparator;
- (NSArray *)sortedArrayUsingComparator:(NSComparator)comparator;
/* THE DESCRIPTOR FORMS (F10). `sortDescriptors` is an ARRAY because a sort is a CHAIN: the first
 * descriptor decides, a tie falls to the second, and a tie that survives the whole chain keeps
 * the INPUT order — the sort is STABLE, which the probe measures directly. The C-function form
 * takes `NSInteger (*)(id, id, void *)` and passes `context` straight through. */
- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)sortDescriptors;
- (NSArray *)sortedArrayUsingFunction:(NSInteger (*)(id, id, void *))comparator
			      context:(nullable void *)context;
- (void)enumerateObjectsUsingBlock:(void (^)(id object, NSUInteger index, BOOL *stop))block;

/* The NSIndexSet forms. -objectsAtIndexes: RAISES NSRangeException for an index
 * past the end — the caller asked for something that is not there — while
 * -indexesOfObjectsPassingTest: hands back the indexes that passed. */
- (NSArray *)objectsAtIndexes:(NSIndexSet *)indexes;
- (NSIndexSet *)indexesOfObjectsPassingTest:(BOOL (^)(id object, NSUInteger index, BOOL *stop))predicate;

/* THE PREDICATE FILTER (F11a): the elements the predicate answers YES for, in order. The
 * returned array is NEW and the receiver is untouched — Cocoa's rule everywhere here. */
- (NSArray *)filteredArrayUsingPredicate:(NSPredicate *)predicate;

- (NSEnumerator *)objectEnumerator;
- (NSEnumerator *)reverseObjectEnumerator;

- (BOOL)isEqualToArray:(NSArray *)other;

/* THE PLIST FILE AND URL FORMS (D7's kind (D), the other half). Apple reads a property list whose
 * ROOT IS AN ARRAY, answers nil when the root is something else, and writes with
 * NSPropertyListSerialization — so these are delegation to that pair plus a root-class check, not
 * a second parser. The URL forms go through NSData's, which refuse a non-file scheme with a
 * registered error (§11.6.1 D9). */
+ (nullable NSArray *)arrayWithContentsOfFile:(NSString *)path;
+ (nullable NSArray *)arrayWithContentsOfURL:(NSURL *)url;
- (nullable id)initWithContentsOfFile:(NSString *)path;
- (nullable id)initWithContentsOfURL:(NSURL *)url;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)useAuxiliaryFile;

@end

@interface NSMutableArray : NSArray <NSMutableCopying>

+ (instancetype)array;
+ (instancetype)arrayWithCapacity:(NSUInteger)capacity;
- (id)initWithCapacity:(NSUInteger)capacity;

- (void)addObject:(id)object;
- (void)insertObject:(id)object atIndex:(NSUInteger)index;
- (void)removeObjectAtIndex:(NSUInteger)index;
- (void)removeAllObjects;
- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(id)object;
- (void)addObjectsFromArray:(NSArray *)other;
- (void)removeLastObject;
- (void)removeObject:(id)object;
- (void)removeObjectIdenticalTo:(id)object;
- (void)removeObjectIdenticalTo:(id)object inRange:(NSRange)range;
- (void)removeObject:(id)object inRange:(NSRange)range;
- (void)removeObjectsInRange:(NSRange)range;
- (void)setArray:(NSArray *)other;
- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second;
- (void)replaceObjectsInRange:(NSRange)range withObjectsFromArray:(NSArray *)other;
- (void)replaceObjectsInRange:(NSRange)range
	 withObjectsFromArray:(NSArray *)other
			  range:(NSRange)otherRange;
- (void)sortUsingComparator:(NSComparator)comparator;
- (void)sortUsingSelector:(SEL)comparator;
- (void)sortUsingDescriptors:(NSArray *)sortDescriptors;
- (void)sortUsingFunction:(NSInteger (*)(id, id, void *))comparator context:(nullable void *)context;
/* Keeps only what the predicate answers YES for. In place, because that is what MUTABLE means. */
- (void)filterUsingPredicate:(NSPredicate *)predicate;

/* The NSIndexSet forms. The counts of objects and indexes must AGREE, and the
 * mismatches raise NSInvalidArgumentException because the message is the only
 * thing that makes the bug diagnosable. */
- (void)insertObjects:(NSArray *)objects atIndexes:(NSIndexSet *)indexes;
- (void)removeObjectsAtIndexes:(NSIndexSet *)indexes;
- (void)replaceObjectsAtIndexes:(NSIndexSet *)indexes withObjects:(NSArray *)objects;

/* `array[i] = x`: replaces, and APPENDS when i == count (Cocoa's rule). */
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index;

NS_ASSUME_NONNULL_END



@end

#endif /* FOUNDATION_NSARRAY_H */
