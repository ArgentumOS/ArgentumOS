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

#import <foundation/NSObject.h>

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
