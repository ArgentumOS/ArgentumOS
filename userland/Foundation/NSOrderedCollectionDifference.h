/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSOrderedCollectionDifference, NSOrderedCollectionChange and NSCollectionChangeType — W13's last row
 * (docs/design/foundation-plan.md §12.6's "diff algorithm" dependency, paid here).
 *
 * NO LIGHTWEIGHT GENERICS, THE TREE'S OWN RULE: this library's collection classes are declared `NSArray *` and
 * `NSOrderedSet *` rather than `NSArray<Foo *> *`, because annotating the containers is a unit of its own and a
 * parameterised spelling would not compile against the classes as they exist. The doors below follow the same
 * rule, so `-object` and the two block parameters are `id`.
 *
 * THE SHAPE IS APPLE'S AND THE DIRECTION IS THE THING TO GET RIGHT. `[A differenceFromArray:B]` answers a
 * difference that, APPLIED TO B, produces A — the receiver is the DESTINATION and the argument is the SOURCE.
 * That is not a reading of the name; it is what two of Apple's pages state in worked examples, and it is the
 * same direction as Swift's `CollectionDifference`, whose `difference(from:)` is called on the new collection
 * with the old one as the argument (the example below is Apple's own, from -arrayByApplyingDifference:):
 *
 *     NSArray *original = @[@"1", @"2"];
 *     NSArray *modified = @[@"1", @"2", @"3"];
 *     NSOrderedCollectionDifference *diff = [modified differenceFromArray:original];
 *     NSArray *updated = [original arrayByApplyingDifference:diff];   // == modified
 *
 * SO THE TWO INDEX SPACES ARE DIFFERENT ONES, and that is the whole reason this type is not just two index
 * sets: an INSERTION's `index` is in the destination (the receiver of -differenceFromArray:), while a
 * REMOVAL's `index` is in the source (the argument) — which is the array -arrayByApplyingDifference: is
 * called on. Apple's -associatedIndex page states the move pairing the same way: "the object @"Red" moves from
 * index 8 to index 3" is a removal{index 8, associatedIndex 3} plus an insertion{index 3, associatedIndex 8}.
 *
 * AND ONE OF APPLE'S EXAMPLES IS INTERNALLY INCONSISTENT, RECORDED HERE BECAUSE A READER WILL MEET IT: the
 * -differenceFromArray:withOptions: page's InferMoves example calls `[original differenceFromArray:modified]`
 * and then reports indexes as though `original` were the source — the opposite of the two examples above. The
 * DIRECTION above is the one two pages agree on; the numbers on that page are not used as a specification
 * anywhere in this library. (The plan records the same class of doc defect elsewhere.)
 *
 * WHAT IS OURS AND WHY (§11.6.1 D2 — Apple publishes the case names and no values):
 *
 *   - `NSCollectionChangeType`'s NUMBERS. Apple publishes `insert` and `remove` and no values.
 *   - the ORDER of `-insertions`, `-removals` and fast enumeration. Apple documents the members and no order;
 *     this library answers each list ASCENDING BY INDEX, which is the order an applier needs, and enumerates
 *     the changes as insertions-then-removals.
 *   - the diff ALGORITHM. Apple documents the CONTRACT ("applying the difference to the source produces the
 *     receiver") and no algorithm: the page that shows a diff for `@[A,B,C]` -> `@[C,B]` says a legitimate
 *     answer may remove index 0 and move index 1 to index 1, which is exactly the freedom a non-unique edit
 *     script has. This library uses Myers' greedy O(ND) shortest edit script, which is A minimal one where
 *     several exist; its one NAMED LIMIT is in the .m (the trace's memory, and the coarse script past it).
 *   - which changes carry `-object`. Apple's `OmitInsertedObjects`/`OmitRemovedObjects` options suppress
 *     exactly that, and the object of a suppressed change is nil.
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSFastEnumeration.h>
#import <Foundation/NSIndexSet.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray, NSString;

/* "The type of change" - Apple's two cases in the order Apple's page lists them; the NUMBERS are ours
 * (§11.6.1 D2). */
typedef enum {
	NSCollectionChangeInsert = 0,
	NSCollectionChangeRemove = 1
} NSCollectionChangeType;

/* HOW A DIFFERENCE WAS CALCULATED. Apple files this type under BOTH `NSArray` and `NSOrderedSet` (each page
 * carries its own "Difference Calculation Options" section), so it is declared HERE, in the header both of
 * them import, rather than in one of them — which is where it lived until this class's own header existed.
 *
 *   - `InferMoves` asks the differ to work out what MOVED rather than reporting a removal plus an insertion,
 *     which it does by giving the two halves of a move each other's index in `-associatedIndex`.
 *   - the other two suppress a SIDE's objects: `OmitInsertedObjects` leaves `-object` nil on every insertion
 *     and `OmitRemovedObjects` does the same for removals. Apple states this on
 *     `-differenceFromArray:withOptions:`: the options "choose to omit insertion or removal references to the
 *     change objects within the difference object".
 *
 * THE NAMES ARE APPLE'S AND THE VALUES ARE OURS (§11.6.1 D2, see NSFileManager.h). */
typedef enum {
	NSOrderedCollectionDifferenceCalculationInferMoves = 1 << 0,
	NSOrderedCollectionDifferenceCalculationOmitInsertedObjects = 1 << 1,
	NSOrderedCollectionDifferenceCalculationOmitRemovedObjects = 1 << 2
} NSOrderedCollectionDifferenceCalculationOptions;

/* ONE HALF OF A CHANGE: an object, a type, an index, and — when it is half of a MOVE — the index of the other
 * half. Apple: "an object that represents an indexed change to an ordered collection and references the object
 * to be inserted or removed". */
@interface NSOrderedCollectionChange : NSObject
{
@protected
	id _object;			/* nil when the change was built from indexes alone, or suppressed by an option */
	NSCollectionChangeType _changeType;
	NSUInteger _index;
	NSUInteger _associatedIndex;
}

+ (instancetype)changeWithObject:(nullable id)anObject
			    type:(NSCollectionChangeType)type
			   index:(NSUInteger)index;
+ (instancetype)changeWithObject:(nullable id)anObject
			    type:(NSCollectionChangeType)type
			   index:(NSUInteger)index
		 associatedIndex:(NSUInteger)associatedIndex;

- (instancetype)initWithObject:(nullable id)anObject
			  type:(NSCollectionChangeType)type
			 index:(NSUInteger)index;
- (instancetype)initWithObject:(nullable id)anObject
			  type:(NSCollectionChangeType)type
			 index:(NSUInteger)index
	       associatedIndex:(NSUInteger)associatedIndex;

/* "An object the change inserts or removes." NULLABLE, and nil has two ways to arise: a change built from an
 * index set with no parallel object array, and one whose objects an option suppressed. */
@property (nullable, readonly, strong) id object;
/* "The type of change." */
@property (readonly) NSCollectionChangeType changeType;
/* "The index location of the change" - IN THE CHANGE'S OWN COLLECTION: the destination for an insertion and
 * the source for a removal (see the header's opening comment). */
@property (readonly) NSUInteger index;
/* "When this property is set to a value other than NSNotFound, the receiver is one half of a move, and this
 * value is the index of the change's counterpart of the opposite type." */
@property (readonly) NSUInteger associatedIndex;

@end

/* A DIFFERENCE BETWEEN TWO ORDERED COLLECTIONS, as a list of changes that are also two index sets' worth of
 * answers. It conforms to NSFastEnumeration: Apple publishes NO `-enumerateChanges…` door, so walking the
 * changes is a `for (NSOrderedCollectionChange *c in difference)` and nothing else. */
/* PARAMETERIZED (2026-09-28) because Apple's own `NSArray.h` declares `<ObjectType>` on it —
 * `- (NSOrderedCollectionDifference<ObjectType> *)differenceFromArray:…` — which is how the compiler
 * found the gap: our `NSArray.h` was refused for applying type arguments to a non-parameterized class
 * while Apple writes exactly that there. ITS VARIANCE IS NOT STATED, because Apple's own declaration of
 * THIS class has not been read yet; invariant is the conservative reading, and the class is on
 * `docs/TODO-before-release.md` §2 for its row in the clause's list and for the variance. */
@interface NSOrderedCollectionDifference<ObjectType> : NSObject <NSFastEnumeration>
{
@protected
	NSArray *_insertions;		/* owned; NSOrderedCollectionChange *, ascending by index */
	NSArray *_removals;		/* owned; likewise */
	NSArray *_changes;		/* owned; insertions, then removals */
}

/* THE TWO WAYS TO BUILD ONE. The member array is checked: every element must be an NSOrderedCollectionChange,
 * and every MOVE association must be REFLEXIVE — a change whose associatedIndex is not NSNotFound must have a
 * counterpart of the opposite type pointing back at it. Apple states the second rule as an exception on
 * `-changeWithObject:type:index:associatedIndex:`: "initializing a NSOrderedCollectionDifference with broken
 * associations (or associations that aren't reflexive) will generate an exception". */
- (instancetype)initWithChanges:(NSArray *)changes;

/* The index-set form. THE OBJECT ARRAYS PAIR WITH THE INDEXES BY ASCENDING ORDER — index set {1,4} with
 * objects @[a,b] means "insert a at 1 and b at 4" — and a count that does not match raises rather than
 * silently dropping one side. Passing nil for an object array is allowed and leaves every `-object` nil, which
 * is what the suppression options would have produced anyway. */
- (instancetype)initWithInsertIndexes:(NSIndexSet *)inserts
		      insertedObjects:(nullable NSArray *)insertedObjects
			removeIndexes:(NSIndexSet *)removes
			removedObjects:(nullable NSArray *)removedObjects;

/* THE SAME THING PLUS CHANGES THAT BELONG TO NEITHER SIDE, which is what a move PAIR is: an insertion and a
 * removal that already point at each other. They are counted in `-insertions`/`-removals` by their own type. */
- (instancetype)initWithInsertIndexes:(NSIndexSet *)inserts
		      insertedObjects:(nullable NSArray *)insertedObjects
			removeIndexes:(NSIndexSet *)removes
			removedObjects:(nullable NSArray *)removedObjects
		     additionalChanges:(NSArray *)changes;

/* "A Boolean value that indicates if the difference has changes." */
@property (readonly) BOOL hasChanges;
/* "A collection of insertion change objects" / "of removal change objects" — each ASCENDING BY INDEX. */
@property (readonly, copy) NSArray *insertions;
@property (readonly, copy) NSArray *removals;

/* "A copy of the receiver with all removals changed to insertions (and vice versa)" — and each half of a move
 * keeps its pairing by SWAPPING the two indexes, so applying a difference and then its inverse returns the
 * original collection (Apple's own example on that page). */
- (NSOrderedCollectionDifference *)inverseDifference;

/* "Create a new ordered collection difference by mapping over this difference's members, processing the change
 * objects with the block provided." The result is checked exactly as a member array is, so a block that breaks
 * an association raises rather than producing a difference that cannot be applied. */
- (NSOrderedCollectionDifference *)differenceByTransformingChangesWithBlock:
	(NSOrderedCollectionChange * (^)(NSOrderedCollectionChange *change))block;

@end

NS_ASSUME_NONNULL_END
