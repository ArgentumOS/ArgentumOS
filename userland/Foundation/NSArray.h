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
/* FOR `NSCoding` AND THE `NSCoder` ITS TWO DOORS TAKE (§63.12): this collection conforms on Apple's platform,
 * so a class that declares the protocol here is one whose `-conformsToProtocol:` answers the same. */
#import <Foundation/NSCoding.h>

@class NSString;
@class NSIndexSet;
@class NSPredicate;

/* NULLABILITY (F6): NONNULL by default, and the two that can legitimately be nil
 * are -firstObject and -lastObject, because an EMPTY array has neither. */
NS_ASSUME_NONNULL_BEGIN
@class NSURL;
/* §63.45: the sort-hint property and its `hint:` parameter name NSData, so the type has to be visible — the
 * same reason NSURL is named here rather than imported. */
@class NSData;

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

@interface NSArray<__covariant ObjectType> : NSObject <NSCopying, NSFastEnumeration, NSCoding>
{
	id __unsafe_unretained *_items;	/* owned BY HAND: every slot is retained */
	unsigned long _count;
	unsigned long _capacity;
	unsigned long _mutations;	/* bumped by every mutation, for fast enumeration */
}

/* THE NSCoding DOORS (§63.12), the same pair the ordered sets and sets took in §63.10/§63.11: the members go
 * under the SHARED key (FNKeyedWire.h) that the archive's structural branch uses, the decoder reads it back
 * through `-initWithArray:` — so the class-choosing rule and the layout stay the INITIALIZER's — and the
 * ARCHIVER never calls either, because its structural branch recognises an array by KIND. They exist for a
 * caller who names them, and for `-conformsToProtocol:`. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;

+ (instancetype)array;
+ (instancetype)arrayWithObject:(ObjectType)object;
+ (instancetype)arrayWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
+ (instancetype)arrayWithArray:(NSArray<ObjectType> *)other;
+ (instancetype)arrayWithObjects:(ObjectType)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

- (id)initWithObject:(id)object;
- (id)initWithObjects:(const ObjectType _Nonnull * _Nullable)objects count:(NSUInteger)count;
- (id)initWithArray:(NSArray<ObjectType> *)other;
/* `copyItems` COPIES each member (`-copy` — this tree's NSCopying member, the zone API having been
 * removed; see NSObject.h), so the new array does not share them with `array`; NO means the members are
 * RETAINED like any other element. */
- (instancetype)initWithArray:(NSArray<ObjectType> *)array copyItems:(BOOL)flag;
- (id)initWithObjects:(ObjectType)firstObject, ... NS_REQUIRES_NIL_TERMINATION;

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
/* THE WHOLE ARRAY, no range: fills `objects`, which must hold at least -count elements. Deprecated by
 * Apple in favour of -getObjects:range:; kept because it is still the documented surface. */
- (void)getObjects:(ObjectType __unsafe_unretained _Nonnull * _Nonnull)objects;

- (NSUInteger)indexOfObject:(ObjectType)object inRange:(NSRange)range;
- (NSUInteger)indexOfObjectIdenticalTo:(ObjectType)object;
/* Identity within a range: the lowest index in `range` holding THE SAME object, NSNotFound if none. */
- (NSUInteger)indexOfObjectIdenticalTo:(ObjectType)object inRange:(NSRange)range;
- (NSUInteger)indexOfObject:(ObjectType)object
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

/* SEND A MESSAGE TO EVERY ELEMENT, in order, starting with the first. The one-argument form sends a
 * no-argument message; the other passes `argument` to each (nullable, as Apple declares it). */
- (void)makeObjectsPerformSelector:(SEL)aSelector;
- (void)makeObjectsPerformSelector:(SEL)aSelector withObject:(nullable id)argument;

/* The NSIndexSet forms. -objectsAtIndexes: RAISES NSRangeException for an index
 * past the end — the caller asked for something that is not there — while
 * -indexesOfObjectsPassingTest: hands back the indexes that passed. */
- (NSArray<ObjectType> *)objectsAtIndexes:(NSIndexSet *)indexes;
/* THE FIRST MATCH, and it STOPS at it (§63.8): the walk must not keep calling a caller's predicate after the
 * answer is known, because a predicate may have side effects and Apple's contract is the lowest matching
 * index rather than a survey of them. The indexes form below is the exhaustive one on purpose. */
- (NSUInteger)indexOfObjectPassingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;
- (NSIndexSet *)indexesOfObjectsPassingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;

/* THE PREDICATE FILTER (F11a): the elements the predicate answers YES for, in order. The
 * returned array is NEW and the receiver is untouched — Cocoa's rule everywhere here. */
- (NSArray *)filteredArrayUsingPredicate:(NSPredicate *)predicate;

/* --- §63.45: THE OPTIONS FORMS OF THE ENUMERATION AND TEST DOORS ------------------------------------
 *
 * THE OPTIONS ARE TWO AND ONLY ONE OF THEM DOES ANYTHING HERE: `NSEnumerationReverse` reverses the
 * DIRECTION of the walk, and `NSEnumerationConcurrent` is a hint this library does not take (the same
 * stance `-sortedArrayWithOptions:` records for `NSSortConcurrent`), so a caller who passes it gets a
 * correct SEQUENTIAL answer. The `-…WithOptions:passingTest:` forms STOP at the first match exactly as
 * their non-options twins do, and for the reason those document: a predicate may have side effects. */
- (void)enumerateObjectsWithOptions:(NSEnumerationOptions)opts
			 usingBlock:(void (^)(ObjectType object, NSUInteger index, BOOL *stop))block;
- (void)enumerateObjectsAtIndexes:(NSIndexSet *)indexes
			  options:(NSEnumerationOptions)opts
		       usingBlock:(void (^)(ObjectType object, NSUInteger index, BOOL *stop))block;
- (NSUInteger)indexOfObjectWithOptions:(NSEnumerationOptions)opts
			   passingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;
- (NSUInteger)indexOfObjectAtIndexes:(NSIndexSet *)indexes
			     options:(NSEnumerationOptions)opts
			 passingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;
- (NSIndexSet *)indexesOfObjectsWithOptions:(NSEnumerationOptions)opts
				passingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;
- (NSIndexSet *)indexesOfObjectsAtIndexes:(NSIndexSet *)indexes
				  options:(NSEnumerationOptions)opts
			      passingTest:(BOOL (^)(ObjectType object, NSUInteger index, BOOL *stop))predicate;

/* --- §63.45: THE DESCRIPTION, PATHNAME, SHUFFLE AND HINT DOORS --------------------------------------
 *
 * `-descriptionWithLocale:` and its `indent:` form are documented as "a string that represents the contents
 * of the array, formatted as a property list" — the MULTI-LINE layout, which is the whole of what they add
 * to `-description`; the element rendering is SHARED with it, so the two cannot disagree about a member.
 *
 * `-sortedArrayHint` returns a value whose FORMAT Apple does not publish ("a 'hint' that speeds the
 * sorting"), so the format is OURS and is stated at the door: the receiver's count, which is what makes a
 * stale hint DETECTABLE, and nothing that could make an answer wrong if it is reused on another array. */
- (NSString *)descriptionWithLocale:(nullable id)locale;
- (NSString *)descriptionWithLocale:(nullable id)locale indent:(NSUInteger)level;
- (NSArray<NSString *> *)pathsMatchingExtensions:(NSArray<NSString *> *)filterTypes;
/* ⚠ `-shuffledArray` IS DECLARED `NSArray<id> *`, WHICH IS APPLE'S OWN SPELLING AND NOT THIS FILE'S
 * HABIT — the parameterization instrument (§11.0) caught the difference: Apple's declaration for this door
 * names NO type parameter, so writing `NSArray<ObjectType> *` here was a signature that did not match the
 * one it claims to implement. It is the only door in this family where Apple writes the bare wildcard. */
- (NSArray<id> *)shuffledArray;
@property (readonly, copy) NSData *sortedArrayHint;
- (NSArray<ObjectType> *)sortedArrayUsingFunction:(NSInteger (*)(ObjectType, ObjectType, void *))comparator
			      context:(nullable void *)context
				 hint:(nullable NSData *)hint;

- (NSEnumerator<ObjectType> *)objectEnumerator;
- (NSEnumerator<ObjectType> *)reverseObjectEnumerator;

- (BOOL)isEqualToArray:(NSArray<ObjectType> *)other;
/* THE FIRST SHARED MEMBER: the first element of the receiver that is -isEqual: to an element of `other`,
 * or nil when the two arrays have none in common. */
- (nullable ObjectType)firstObjectCommonWithArray:(NSArray<ObjectType> *)other;

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
- (NSOrderedCollectionDifference *)differenceFromArray:(NSArray<ObjectType> *)other
					  withOptions:(NSOrderedCollectionDifferenceCalculationOptions)options;
/* THE EQUIVALENCE-TEST FORM. Apple: "don't use the option inferMoves when providing a block for the equivalence
 * test. The changes returned in the difference object don't include valid values for associatedIndex" — so a
 * move option here is IGNORED and every associated index stays NSNotFound, which is what that page describes. */
- (NSOrderedCollectionDifference *)differenceFromArray:(NSArray<ObjectType> *)other
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
/* APPLE DECLARES THIS ON THE MUTABLE CLASS TOO (§63.12), so it is redeclared here rather than left to
 * inheritance — the ledger's shipped test is a declaration in the owner's own block — and its implementation
 * lives in this class's own `@implementation`, because `--unimplemented` counts an implementation in the class
 * or a SUBCLASS, so the front's body does not satisfy this. */
- (nullable instancetype)initWithCoder:(NSCoder *)coder;

- (void)addObject:(ObjectType)object;
- (void)insertObject:(ObjectType)object atIndex:(NSUInteger)index;
- (void)removeObjectAtIndex:(NSUInteger)index;
- (void)removeAllObjects;
- (void)replaceObjectAtIndex:(NSUInteger)index withObject:(ObjectType)object;
- (void)addObjectsFromArray:(NSArray<ObjectType> *)other;
- (void)removeLastObject;
- (void)removeObject:(ObjectType)object;
- (void)removeObjectIdenticalTo:(ObjectType)object;
- (void)removeObjectIdenticalTo:(ObjectType)object inRange:(NSRange)range;
- (void)removeObject:(ObjectType)object inRange:(NSRange)range;
- (void)removeObjectsInRange:(NSRange)range;
- (void)setArray:(NSArray<ObjectType> *)other;
- (void)exchangeObjectAtIndex:(NSUInteger)first withObjectAtIndex:(NSUInteger)second;
- (void)replaceObjectsInRange:(NSRange)range withObjectsFromArray:(NSArray<ObjectType> *)other;
- (void)replaceObjectsInRange:(NSRange)range
	 withObjectsFromArray:(NSArray<ObjectType> *)other
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
