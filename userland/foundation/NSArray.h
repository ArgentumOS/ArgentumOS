/*
 * NSArray / NSMutableArray — an ordered collection.
 * docs/design/foundation-plan.md, F3.
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

@interface NSArray : NSObject <NSCopying, NSFastEnumeration>
{
	id __unsafe_unretained *_items;	/* owned BY HAND: every slot is retained */
	unsigned long _count;
	unsigned long _capacity;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
}

+ (NSArray *)array;
+ (NSArray *)arrayWithObject:(id)object;
+ (NSArray *)arrayWithObjects:(const id *)objects count:(unsigned long)count;

- (id)initWithObject:(id)object;
- (id)initWithObjects:(const id *)objects count:(unsigned long)count;

- (unsigned long)count;
- (id)objectAtIndex:(unsigned long)index;
- (id)firstObject;
- (id)lastObject;
- (unsigned long)indexOfObject:(id)object;
- (BOOL)containsObject:(id)object;

- (NSArray *)arrayByAddingObject:(id)object;	/* a new array; self is untouched */
- (BOOL)isEqualToArray:(NSArray *)other;

@end

@interface NSMutableArray : NSArray

+ (NSMutableArray *)array;
+ (NSMutableArray *)arrayWithCapacity:(unsigned long)capacity;
- (id)initWithCapacity:(unsigned long)capacity;

- (void)addObject:(id)object;
- (void)insertObject:(id)object atIndex:(unsigned long)index;
- (void)removeObjectAtIndex:(unsigned long)index;
- (void)removeAllObjects;

@end

#endif /* FOUNDATION_NSARRAY_H */
