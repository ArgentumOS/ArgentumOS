/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSSet / NSMutableSet — an unordered collection with no duplicates. docs/design/foundation-plan.md
 * §10 (the un-refusal program), F13.8.
 *
 * THIS CLASS IS A GAP §10 COULD NOT EXPLAIN AWAY, and it is the first one that family adds. Every
 * other entry in the plan's refusal table was refused for a REASON — a table, a service, an
 * evaluator. A set was never refused: it was simply missing, and the plan's own KVC section says so
 * in passing ("the set-returning operators need an NSSet that does not exist here"). So this is what
 * the un-refusal program looks like when the boundary is not the point.
 *
 * THE RULE IS THE ONE EVERY COLLECTION HERE FOLLOWS: a member's place is decided by ITS OWN -hash
 * and -isEqual:, so a set of strings dedupes BY VALUE — two distinct NSString objects with the same
 * characters are ONE member — and -member: finds an object by value rather than by pointer. That is
 * Cocoa's contract, and it is what makes a set different from an array.
 *
 * TWO DESIGN FACTS WORTH STATING RATHER THAN DISCOVERING:
 *   * THE MEMBERS ARE RETAINED, NOT COPIED — Cocoa's rule. That is also why the storage is NOT a
 *     dictionary, tempting as that looks: NSDictionary COPIES its keys (F3's audited rule), so a
 *     set built on one would copy its members and would refuse a member that cannot be copied. The
 *     members live in an array and lookup is LINEAR, which is honest at this scale and leaves a
 *     real hash table as a later optimisation rather than a quiet change of contract;
 *   * EVERY MUTATION REPLACES the member array rather than editing it, so a loop that is running
 *     over a set cannot be handed storage a later mutation frees. The same rule NSDictionary's
 *     fast enumeration follows, for the same reason.
 *
 * WHAT IS NOT HERE, named: NSCountedSet (its counts are its own structure), and NSOrderedSet (a
 * different collection with its own ordering rules) — each its own step.
 */

#ifndef FOUNDATION_NSSET_H
#define FOUNDATION_NSSET_H

#import <Foundation/NSObject.h>
#import <Foundation/NSFastEnumeration.h>

@class NSArray<ObjectType>, NSEnumerator<ObjectType>;
@class NSPredicate;

NS_ASSUME_NONNULL_BEGIN

@interface NSSet<__covariant ObjectType> : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
{
	NSArray *_members;		/* the members, once each; REPLACED by every mutation */
	unsigned long _mutations;	/* the for-in consistency token */
}

+ (instancetype)set;
+ (instancetype)setWithObject:(ObjectType)object;
/* THE NIL-TERMINATED VARIADIC FORM. It is Apple's, and this tree did not declare it until 2026-09-30: the
 * selector ledger carried the row as `open`, which is how an absence that has a NAME stays visible rather
 * than becoming an assumption. `NSArray` has had its counterpart all along, so the two families' factories
 * now read the same way. */
+ (instancetype)setWithObjects:(ObjectType)firstObject, ... NS_REQUIRES_NIL_TERMINATION;
+ (instancetype)setWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
+ (instancetype)setWithArray:(NSArray<ObjectType> *)array;
+ (instancetype)setWithSet:(NSSet<ObjectType> *)set;

- (instancetype)initWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
- (instancetype)initWithArray:(NSArray<ObjectType> *)array;
- (instancetype)initWithSet:(NSSet<ObjectType> *)set;

- (NSUInteger)count;
/* BY VALUE: the member that is -isEqual: to `object`, or nil. This is the door that separates a set
 * from an array, and the probe measures exactly that (a distinguished object finds its twin). */
- (nullable ObjectType)member:(ObjectType)object;
- (BOOL)containsObject:(ObjectType)object;
- (nullable ObjectType)anyObject;
- (NSArray<ObjectType> *)allObjects;
- (NSEnumerator<ObjectType> *)objectEnumerator;
- (void)enumerateObjectsUsingBlock:(void (^)(ObjectType object, BOOL *stop))block;

- (BOOL)isEqualToSet:(NSSet<ObjectType> *)other;
- (BOOL)isSubsetOfSet:(NSSet<ObjectType> *)other;
- (BOOL)intersectsSet:(NSSet<ObjectType> *)other;

/* The three "adding" forms answer a NEW set (the receiver is immutable), which is why no addObject:
 * exists here at all. */
- (instancetype)setByAddingObject:(ObjectType)object;
- (instancetype)setByAddingObjectsFromSet:(NSSet<ObjectType> *)other;
- (instancetype)setByAddingObjectsFromArray:(NSArray<ObjectType> *)other;

- (NSArray *)sortedArrayUsingDescriptors:(NSArray *)descriptors;
- (instancetype)filteredSetUsingPredicate:(NSPredicate *)predicate;

- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

@interface NSMutableSet<ObjectType> : NSSet<ObjectType>

+ (instancetype)setWithCapacity:(NSUInteger)capacity;
- (instancetype)initWithCapacity:(NSUInteger)capacity;

- (void)addObject:(ObjectType)object;
- (void)removeObject:(ObjectType)object;
- (void)removeAllObjects;
- (void)addObjectsFromArray:(NSArray<ObjectType> *)array;
- (void)unionSet:(NSSet<ObjectType> *)other;
- (void)minusSet:(NSSet<ObjectType> *)other;
- (void)intersectSet:(NSSet<ObjectType> *)other;
- (void)setSet:(NSSet<ObjectType> *)other;
- (void)filterUsingPredicate:(NSPredicate *)predicate;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSET_H */
