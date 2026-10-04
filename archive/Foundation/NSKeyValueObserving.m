/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSKeyValueObserving.m — the observer registry (F13.9). MANUAL OWNERSHIP.
 *
 * THE REGISTRY IS TWO TABLES AND NO IVARS, because NSObject's category cannot grow an instance and
 * this library has no associated objects. That is also why Cocoa's `-observationInfo` exists as a
 * door at all, and this file implements it as one — over its own table, kept separate from the
 * registry so a caller's bookkeeping cannot collide with it.
 *
 * NOTHING IS RETAINED: neither the observed object nor the observer, which is Cocoa's rule. The
 * tables hold `__unsafe_unretained` references, so removing observations before either object dies
 * is the caller's job exactly as it is in Cocoa.
 *
 * THE CHANGE IN FLIGHT is the interesting part. `-willChangeValueForKey:` captures the OLD value and
 * leaves a pending record; `-didChangeValueForKey:` reads the NEW value, builds one dictionary per
 * observer from the option bits that observer asked for, and clears the pending. So a change is
 * reported once, with the two values that surround it, and only to observers that wanted it.
 */

#import <Foundation/NSKeyValueObserving.h>
#import <Foundation/NSKeyValueCoding.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSValue.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSException.h>
#import "FNObjectDeath.h"	/* §63.21: told when an object's address is going away */

NSString *const NSKeyValueChangeKindKey = @"NSKeyValueChangeKindKey";
NSString *const NSKeyValueChangeNewKey = @"NSKeyValueChangeNewKey";
NSString *const NSKeyValueChangeOldKey = @"NSKeyValueChangeOldKey";
NSString *const NSKeyValueChangeIndexesKey = @"NSKeyValueChangeIndexesKey";
NSString *const NSKeyValueChangeNotificationIsPriorKey = @"NSKeyValueChangeNotificationIsPriorKey";

/* ONE REGISTRATION: who is watching what, with which options and which context. */
@interface FNKVORegistration : NSObject
{
@public
	__unsafe_unretained NSObject *_observed;	/* NOT retained */
	__unsafe_unretained NSObject *_observer;	/* NOT retained */
	NSString *_keyPath;
	NSKeyValueObservingOptions _options;
	void *_context;
}
@end

@implementation FNKVORegistration
@end

/* ONE CHANGE between will and did. */
@interface FNKVOPending : NSObject
{
@public
	__unsafe_unretained NSObject *_observed;
	NSString *_key;
	__unsafe_unretained id _oldValue;
	BOOL _hasOld;
}
@end

@implementation FNKVOPending
@end

/* ONE `-observationInfo`. */
@interface FNKVOInfo : NSObject
{
@public
	__unsafe_unretained NSObject *_object;		/* NOT retained */
	void *_info;
}
@end

@implementation FNKVOInfo
@end

static NSMutableArray *fn_registrations = nil;
static NSMutableArray *fn_pendings = nil;
static NSMutableArray *fn_infos = nil;

/* §63.21: AN OBJECT'S DEATH TAKES ITS ROWS WITH IT — what this file could NOT do before the death seam
 * existed. THE PROBLEM IS STRUCTURAL, NOT AN OVERSIGHT: Apple's registry is per-object, so it dies with the
 * object for free; OURS IS A PROCESS-GLOBAL TABLE KEYED BY POINTER IDENTITY, so an entry whose object has
 * died is not merely a leak — a NEW object allocated at the recycled address INHERITS the dead object's
 * registrations, and `-removeObserver:forKeyPath:` then SUCCEEDS for an observer it never had. Measured
 * before this hook existed: the fresh object matched the stale entry and the removal did not raise.
 *
 * ALL THREE TABLES ARE KEYED THE SAME WAY, so all three are swept here: registrations (`_observed`), pending
 * records (`_observed` — a `-willChangeValueForKey:` whose object died before the matching `-did` would
 * otherwise sit in the table forever), and `-observationInfo` entries (`_object`).
 *
 * THE CONTRACT, from FNObjectDeath.h: this runs for EVERY dying object, mid-death, and uses only the
 * ADDRESS. It is bounded by the tables' own size, which is why the seam is installed LAZILY (below) and why
 * a program that never registers an observer never reaches this function at all. */
static void fn_object_died(NSObject *object)
{
	NSUInteger i;

	if (fn_registrations == nil) {
		return;
	}
	/* BACKWARDS, because a removal shifts the tail; these tables are small by nature. */
	for (i = [fn_registrations count]; i > 0; i--) {
		FNKVORegistration *held = [fn_registrations objectAtIndex:i - 1];

		if (held->_observed == object) {
			[fn_registrations removeObjectAtIndex:i - 1];
		}
	}
	for (i = [fn_pendings count]; i > 0; i--) {
		FNKVOPending *held = [fn_pendings objectAtIndex:i - 1];

		if (held->_observed == object) {
			[fn_pendings removeObjectAtIndex:i - 1];
		}
	}
	for (i = [fn_infos count]; i > 0; i--) {
		FNKVOInfo *held = [fn_infos objectAtIndex:i - 1];

		if (held->_object == object) {
			[fn_infos removeObjectAtIndex:i - 1];
		}
	}
}

static void fn_kvo_init(void)
{
	if (fn_registrations == nil) {
		fn_registrations = [[NSMutableArray alloc] init];
		fn_pendings = [[NSMutableArray alloc] init];
		fn_infos = [[NSMutableArray alloc] init];
		/* INSTALLED HERE, NOT AT `+load` TIME: the tables exist from this moment, so the hook is needed
		 * from this moment — and a program that never calls KVO never installs one. */
		fn_object_death_hook = fn_object_died;
	}
}

/* IDENTITY, NOT EQUALITY: two distinct but equal objects are two different things to observe. */
static FNKVORegistration *fn_registration_for(NSObject *observed, NSString *key)
{
	NSUInteger i;

	fn_kvo_init();
	for (i = 0; i < [fn_registrations count]; i++) {
		FNKVORegistration *held = [fn_registrations objectAtIndex:i];

		if (held->_observed == observed && [held->_keyPath isEqualToString:key]) {
			return held;
		}
	}
	return nil;
}

static NSUInteger fn_registration_count(NSObject *observed, NSString *key)
{
	NSUInteger i, count = 0;

	fn_kvo_init();
	for (i = 0; i < [fn_registrations count]; i++) {
		FNKVORegistration *held = [fn_registrations objectAtIndex:i];

		if (held->_observed == observed && [held->_keyPath isEqualToString:key]) {
			count++;
		}
	}
	return count;
}

static FNKVOPending *fn_pending_for(NSObject *observed, NSString *key)
{
	NSUInteger i;

	fn_kvo_init();
	for (i = 0; i < [fn_pendings count]; i++) {
		FNKVOPending *held = [fn_pendings objectAtIndex:i];

		if (held->_observed == observed && [held->_key isEqualToString:key]) {
			return held;
		}
	}
	return nil;
}

@implementation NSObject (NSKeyValueObserving)

- (void)addObserver:(NSObject *)observer
	 forKeyPath:(NSString *)keyPath
	    options:(NSKeyValueObservingOptions)options
	    context:(nullable void *)context
{
	FNKVORegistration *registration;

	if (observer == nil || keyPath == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"KVO: -addObserver:forKeyPath:options:context: needs both an observer "
				   "and a key path"];
	}
	fn_kvo_init();
	registration = [[FNKVORegistration alloc] init];
	registration->_observed = self;
	registration->_observer = observer;
	registration->_keyPath = [keyPath copy];
	registration->_options = options;
	registration->_context = context;
	[fn_registrations addObject:registration];

	if ((options & NSKeyValueObservingOptionInitial) != 0) {
		/* THE INITIAL NOTIFICATION carries the value as it stands, and arrives BEFORE this call
		 * returns — that is what makes it useful for a first paint. */
		NSMutableDictionary *change = [NSMutableDictionary dictionaryWithCapacity:2];

		[change setObject:[NSNumber numberWithUnsignedInteger:NSKeyValueChangeSetting]
			   forKey:NSKeyValueChangeKindKey];
		if ((options & NSKeyValueObservingOptionNew) != 0) {
			id now = [self valueForKey:keyPath];

			[change setObject:(now != nil ? now : (id)[NSNull null])
				   forKey:NSKeyValueChangeNewKey];
		}
		[observer observeValueForKeyPath:keyPath ofObject:self change:change context:context];
	}
}

- (void)removeObserver:(NSObject *)observer forKeyPath:(NSString *)keyPath
{
	NSUInteger i;

	/* THE TWO-ARGUMENT FORM IGNORES THE CONTEXT, which is the entire difference between it and the
	 * three-argument one: it removes whatever was registered for this observer and key path, however
	 * it was registered. Getting that wrong is how this file's first version RAISED on a removal
	 * Cocoa accepts — measured, after a marker showed the abort was one block earlier than the
	 * check that appeared to fail. */
	fn_kvo_init();
	for (i = 0; i < [fn_registrations count]; i++) {
		FNKVORegistration *held = [fn_registrations objectAtIndex:i];

		if (held->_observed == self && held->_observer == observer &&
		    [held->_keyPath isEqualToString:keyPath]) {
			[fn_registrations removeObjectAtIndex:i];
			return;
		}
	}
	/* COCOA'S RULE: removing something that was never registered is a programming error, not a
	 * silent success — and it is the failure mode that leaves a dangling observer behind. */
	[NSException raise:NSInvalidArgumentException
		    format:@"KVO: %@ is not registered as an observer of \"%@\"",
			   [observer class], keyPath];
}

- (void)removeObserver:(NSObject *)observer
	    forKeyPath:(NSString *)keyPath
	       context:(nullable void *)context
{
	NSUInteger i;

	fn_kvo_init();
	for (i = 0; i < [fn_registrations count]; i++) {
		FNKVORegistration *held = [fn_registrations objectAtIndex:i];

		if (held->_observed == self && held->_observer == observer &&
		    [held->_keyPath isEqualToString:keyPath] && held->_context == context) {
			[fn_registrations removeObjectAtIndex:i];
			return;
		}
	}
	/* COCOA'S RULE: removing something that was never registered is a programming error, not a
	 * silent success — and it is the failure mode that leaves a dangling observer behind. */
	[NSException raise:NSInvalidArgumentException
		    format:@"KVO: %@ is not registered as an observer of \"%@\"",
			   [observer class], keyPath];
}

- (void)willChangeValueForKey:(NSString *)key
{
	FNKVOPending *pending;
	BOOL wantsOld = NO;
	BOOL wantsPrior = NO;
	NSUInteger i;
	id current = nil;

	if (key == nil) {
		return;
	}
	fn_kvo_init();
	if (fn_registration_count(self, key) == 0) {
		/* NOBODY IS WATCHING: no pending record, no reads, nothing to undo in -didChange. */
		return;
	}
	for (i = 0; i < [fn_registrations count]; i++) {
		FNKVORegistration *held = [fn_registrations objectAtIndex:i];

		if (held->_observed != self || ![held->_keyPath isEqualToString:key]) {
			continue;
		}
		if ((held->_options & NSKeyValueObservingOptionOld) != 0) {
			wantsOld = YES;
		}
		if ((held->_options & NSKeyValueObservingOptionPrior) != 0) {
			wantsPrior = YES;
		}
	}
	if (wantsOld || wantsPrior) {
		current = [self valueForKey:key];
	}
	pending = [[FNKVOPending alloc] init];
	pending->_observed = self;
	pending->_key = [key copy];
	pending->_oldValue = current;
	pending->_hasOld = (wantsOld || wantsPrior);
	[fn_pendings addObject:pending];

	if (wantsPrior) {
		for (i = 0; i < [fn_registrations count]; i++) {
			FNKVORegistration *held = [fn_registrations objectAtIndex:i];
			NSMutableDictionary *change;

			if (held->_observed != self || ![held->_keyPath isEqualToString:key] ||
			    (held->_options & NSKeyValueObservingOptionPrior) == 0) {
				continue;
			}
			change = [NSMutableDictionary dictionaryWithCapacity:3];
			[change setObject:[NSNumber numberWithUnsignedInteger:NSKeyValueChangeSetting]
				   forKey:NSKeyValueChangeKindKey];
			[change setObject:[NSNumber numberWithBool:YES]
				   forKey:NSKeyValueChangeNotificationIsPriorKey];
			if ((held->_options & NSKeyValueObservingOptionOld) != 0) {
				[change setObject:(current != nil ? current : (id)[NSNull null])
					   forKey:NSKeyValueChangeOldKey];
			}
			[held->_observer observeValueForKeyPath:key
						       ofObject:self
							 change:change
							context:held->_context];
		}
	}
}

- (void)didChangeValueForKey:(NSString *)key
{
	FNKVOPending *pending;
	NSUInteger i;

	if (key == nil) {
		return;
	}
	fn_kvo_init();
	pending = fn_pending_for(self, key);
	if (pending == nil) {
		return;			/* nothing was in flight (or nobody is watching) */
	}
	for (i = 0; i < [fn_registrations count]; i++) {
		FNKVORegistration *held = [fn_registrations objectAtIndex:i];
		NSMutableDictionary *change;

		if (held->_observed != self || ![held->_keyPath isEqualToString:key]) {
			continue;
		}
		change = [NSMutableDictionary dictionaryWithCapacity:3];
		[change setObject:[NSNumber numberWithUnsignedInteger:NSKeyValueChangeSetting]
			   forKey:NSKeyValueChangeKindKey];
		if ((held->_options & NSKeyValueObservingOptionNew) != 0) {
			/* THE NEW VALUE IS READ HERE, after the write — which is why this half exists. */
			id now = [self valueForKey:key];

			[change setObject:(now != nil ? now : (id)[NSNull null])
				   forKey:NSKeyValueChangeNewKey];
		}
		if ((held->_options & NSKeyValueObservingOptionOld) != 0 && pending->_hasOld) {
			[change setObject:(pending->_oldValue != nil ? pending->_oldValue
								     : (id)[NSNull null])
				   forKey:NSKeyValueChangeOldKey];
		}
		[held->_observer observeValueForKeyPath:key
					       ofObject:self
						 change:change
						context:held->_context];
	}
	[fn_pendings removeObjectIdenticalTo:pending];
}

- (nullable void *)observationInfo
{
	NSUInteger i;

	fn_kvo_init();
	for (i = 0; i < [fn_infos count]; i++) {
		FNKVOInfo *held = [fn_infos objectAtIndex:i];

		if (held->_object == self) {
			return held->_info;
		}
	}
	return NULL;
}

- (void)setObservationInfo:(nullable void *)info
{
	NSUInteger i;

	fn_kvo_init();
	for (i = 0; i < [fn_infos count]; i++) {
		FNKVOInfo *held = [fn_infos objectAtIndex:i];

		if (held->_object == self) {
			held->_info = info;
			return;
		}
	}
	{
		FNKVOInfo *fresh = [[FNKVOInfo alloc] init];

		fresh->_object = self;
		fresh->_info = info;
		[fn_infos addObject:fresh];
	}
}

@end
