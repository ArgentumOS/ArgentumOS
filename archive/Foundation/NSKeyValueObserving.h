/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSKeyValueObserving — the registry of observers. docs/design/foundation-plan.md §10 (the
 * un-refusal program), F13.9.
 *
 * THIS IS THE DOOR THE KVC HEADER CLOSED BY NAME: "-willChangeValueForKey:, -didChangeValueForKey:,
 * -observeValueForKeyPath:… — KVO is a REGISTRY of observers with a dependency graph. It is a
 * service, not a rule." That reasoning was never a boundary in §10's sense and is now a gap being
 * filled: a registry is storage, and storage is the easy half.
 *
 * WHAT IS FAITHFUL IN v1, and it is the part the API is actually used through:
 *   * the three option bits that matter — New, Old and Initial — plus Prior;
 *   * the CONTEXT pointer, which comes back to the observer untouched (that is how a real observer
 *     tells its own registrations apart);
 *   * the manual `-willChangeValueForKey:`/-didChangeValueForKey:` PAIR, which is the documented way
 *     to notify for a setter the runtime cannot see;
 *   * AUTOMATIC notification for writes made through `-setValue:forKey:`/`-setValue:forKeyPath:`,
 *     which is where this library's writers go.
 *
 * WHAT IS NOT, named: a setter called DIRECTLY (`[object setFoo:]`) does not notify, because that
 * needs per-class interception (isa-swizzling) that v1 does not do — the manual pair is the answer
 * for those. A DOTTED key path is registered as the literal string it was given rather than
 * decomposed into its segments. And nothing is retained: neither the observed object nor the
 * observer, which is Cocoa's own rule and means the caller must remove observations before either
 * one dies.
 */

#ifndef FOUNDATION_NSKEYVALUEOBSERVING_H
#define FOUNDATION_NSKEYVALUEOBSERVING_H

#import <Foundation/NSObject.h>
/* ⚠ `NSArray` IS IMPORTED, NOT FORWARD-DECLARED, AND THAT IS A LANGUAGE RULE RATHER THAN A PREFERENCE (§63.54):
 * `@class NSArray;` is enough to NAME the class but NOT to DEFINE A CATEGORY on it — clang answers "cannot
 * define category for undefined class". The category at the bottom of this file is Apple's arrangement (its own
 * `NSKeyValueObserving.h` carries `NSArray(NSKeyValueObserverRegistration)`), so the class it extends has to be
 * in scope, and the import is cycle-free: `NSArray.h` never imports this header. */
#import <Foundation/NSArray.h>

@class NSDictionary;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* A plain typedef and an anonymous enum, which is the shape the rest of this library uses for an
 * option set: NS_OPTIONS is not available here. */
typedef NSUInteger NSKeyValueObservingOptions;
enum {
	NSKeyValueObservingOptionNew = 0x01,
	NSKeyValueObservingOptionOld = 0x02,
	NSKeyValueObservingOptionInitial = 0x04,
	NSKeyValueObservingOptionPrior = 0x08
};

typedef NSUInteger NSKeyValueChange;
enum {
	NSKeyValueChangeSetting = 1,
	NSKeyValueChangeInsertion = 2,
	NSKeyValueChangeRemoval = 3,
	NSKeyValueChangeReplacement = 4
};

/* Cocoa's names, and their VALUES ARE THE NAMES — the same rule the rest of this library's keys
 * follow, because an observer compares against the constant. */
extern NSString *const NSKeyValueChangeKindKey;
extern NSString *const NSKeyValueChangeNewKey;
extern NSString *const NSKeyValueChangeOldKey;
extern NSString *const NSKeyValueChangeIndexesKey;
extern NSString *const NSKeyValueChangeNotificationIsPriorKey;

/* THE KEY TYPE, and the set-mutation KIND (W2f). The kind's VALUES are this tree's (Apple
 * publishes the case names and not their numbers — the same situation as NSAlignmentOptions,
 * §14.2): 1 through 4, in the order the operations read. Nothing here consumes the enum yet —
 * the KVO set-mutation method that takes one is a separate row. */
typedef NSString *NSKeyValueChangeKey;

typedef NSUInteger NSKeyValueSetMutationKind;

#define NSKeyValueUnionSetMutation	1
#define NSKeyValueMinusSetMutation	2
#define NSKeyValueIntersectSetMutation	3
#define NSKeyValueSetSetMutation	4

@interface NSObject (NSKeyValueObserving)

- (void)addObserver:(NSObject *)observer
	 forKeyPath:(NSString *)keyPath
	    options:(NSKeyValueObservingOptions)options
	    context:(nullable void *)context;
- (void)removeObserver:(NSObject *)observer forKeyPath:(NSString *)keyPath;
- (void)removeObserver:(NSObject *)observer
	    forKeyPath:(NSString *)keyPath
	       context:(nullable void *)context;

/* The manual pair. Between them, the change is reported exactly once. */
- (void)willChangeValueForKey:(NSString *)key;
- (void)didChangeValueForKey:(NSString *)key;

/* Cocoa's own doors for hanging an observer's OWN bookkeeping off an object. This library keeps the
 * registry in a table rather than in the object, so these are a faithful place for a caller to keep
 * something of its own — and are NOT what the registry uses internally. */
- (nullable void *)observationInfo;
- (void)setObservationInfo:(nullable void *)info;

@end

/* ---- §63.45: THE ARRAY-WIDE REGISTRATION DOORS -----------------------------------------------------
 *
 * THEY BELONG HERE, IN THE KVO HEADER, WHICH IS ALSO APPLE'S OWN ARRANGEMENT: the SDK declares them in
 * NSKeyValueObserving.h as a category on NSArray, because they are KVO's surface that happens to be spelled
 * on a collection — not NSArray's business.
 *
 * APPLE'S NOTE SAYS WHAT THEY ARE FOR, AND IT IS SPEED: invoking them "is potentially much faster than
 * repeatedly invoking NSObject(NSKeyValueObserverRegistration) methods". A PERFORMANCE CLAIM, NOT A
 * DIFFERENT SEMANTICS — so this library's implementation IS the walk over the index set, and says so at the
 * body rather than implying a batch registration happens somewhere. The context-bearing removal is the one
 * Apple's header RECOMMENDS ("use -removeObserver:fromObjectsAtIndexes:forKeyPath:context: instead of
 * -removeObserver:fromObjectsAtIndexes:forKeyPath: whenever possible"), for the same reason its NSObject
 * twin is preferred: a context is what makes an observation uniquely removable. */
/* ⚠ `NSArray` IS FORWARD-DECLARED HERE BY THIS LINE, AND ITS ABSENCE WAS A LATENT BREAK (found 2026-10-01,
 * §63.54): the category below is declared in THIS header, which does not import NSArray.h, so
 * `@interface NSArray (…)` needs at least a forward declaration or clang answers "cannot find interface
 * declaration for 'NSArray'". IT SURVIVED EVERY INCREMENTAL BUILD because the object that includes this header
 * was never recompiled — the same stale-object effect that hid §63.54's own break, and the reason this unit's
 * acceptance is a FULL rebuild. */
@class NSIndexSet;
/* §63.54: NSArray.h is imported above; a forward declaration cannot carry a category. */

@interface NSArray (NSKeyValueObserverRegistration)

- (void)addObserver:(NSObject *)observer
 toObjectsAtIndexes:(NSIndexSet *)indexes
	 forKeyPath:(NSString *)keyPath
	    options:(NSKeyValueObservingOptions)options
	    context:(nullable void *)context;
- (void)removeObserver:(NSObject *)observer
 fromObjectsAtIndexes:(NSIndexSet *)indexes
	    forKeyPath:(NSString *)keyPath
	       context:(nullable void *)context;
- (void)removeObserver:(NSObject *)observer
 fromObjectsAtIndexes:(NSIndexSet *)indexes
	    forKeyPath:(NSString *)keyPath;

@end

@interface NSObject (NSKeyValueObservingObserver)

/* What an OBSERVER implements. It is declared here rather than in NSObject.h because it is the other
 * half of the contract: an observing object implements it, an observed one never calls it directly. */
- (void)observeValueForKeyPath:(nullable NSString *)keyPath
		      ofObject:(nullable id)object
			change:(nullable NSDictionary *)change
		       context:(nullable void *)context;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSKEYVALUEOBSERVING_H */
