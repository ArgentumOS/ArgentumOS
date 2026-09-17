/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSArray / NSMutableArray — an ordered collection.
 * docs/design/foundation-plan.md, F3; subscripting and NSNotFound from the
 * public-API audit.
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

@class NSString;

@interface NSArray : NSObject <NSCopying, NSFastEnumeration>
{
	id __unsafe_unretained *_items;	/* owned BY HAND: every slot is retained */
	unsigned long _count;
	unsigned long _capacity;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
}

+ (instancetype)array;
+ (instancetype)arrayWithObject:(id)object;
+ (instancetype)arrayWithObjects:(const id *)objects count:(NSUInteger)count;
+ (instancetype)arrayWithArray:(NSArray *)other;
+ (instancetype)arrayWithObjects:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (id)initWithObject:(id)object;
- (id)initWithObjects:(const id *)objects count:(NSUInteger)count;
- (id)initWithArray:(NSArray *)other;
- (id)initWithObjects:(id)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
/* Cocoa's subscript: `array[0]` lowers to this. */
- (id)objectAtIndexedSubscript:(NSUInteger)index;

- (id)firstObject;
- (id)lastObject;
/* NSNotFound for a missing element — NOT (NSUInteger)-1, which is what this
 * answered before the audit and which never equals NSNotFound. */
- (NSUInteger)indexOfObject:(id)object;
- (BOOL)containsObject:(id)object;

- (NSArray *)arrayByAddingObject:(id)object;	/* a new array; self is untouched */
- (NSArray *)arrayByAddingObjectsFromArray:(NSArray *)other;
- (NSArray *)subarrayWithRange:(NSRange)range;
- (void)getObjects:(id __unsafe_unretained *)buffer range:(NSRange)range;

- (NSUInteger)indexOfObject:(id)object inRange:(NSRange)range;
- (NSUInteger)indexOfObjectIdenticalTo:(id)object;

- (NSString *)componentsJoinedByString:(NSString *)separator;
- (NSArray *)sortedArrayUsingSelector:(SEL)comparator;

- (BOOL)isEqualToArray:(NSArray *)other;

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
- (void)removeObject:(id)object inRange:(NSRange)range;
- (void)removeObjectsInRange:(NSRange)range;
- (void)setArray:(NSArray *)other;
- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second;
- (void)replaceObjectsInRange:(NSRange)range withObjectsFromArray:(NSArray *)other;
- (void)sortUsingSelector:(SEL)comparator;

/* `array[i] = x`: replaces, and APPENDS when i == count (Cocoa's rule). */
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index;

@end

#endif /* FOUNDATION_NSARRAY_H */
