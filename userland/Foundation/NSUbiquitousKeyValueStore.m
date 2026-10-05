/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUbiquitousKeyValueStore.m — the local half of the iCloud key/value store (§62.87). MANUAL OWNERSHIP.
 *
 * THE STORE IS A DICTIONARY, THE LIMITS ARE ENFORCED AT THE WRITE, AND A REFUSED WRITE TOUCHES NOTHING: Apple's
 * contract for a store call is one atomic transaction, so a write that would break the quota is rejected WHOLE
 * rather than partially applied and then reported.
 *
 * THE ONE PLACE A LOCAL ACTION POSTS THE "EXTERNAL" NOTICE IS A QUOTA VIOLATION, and that is Apple's own
 * behaviour rather than a shortcut: the violation has to reach the app somehow, and the notice with
 * `NSUbiquitousKeyValueStoreQuotaViolationChange` is how. Every other posting comes through the service seam in
 * `FNSUbiquitousStore.h`.
 */

#import <Foundation/NSUbiquitousKeyValueStore.h>
#import <Foundation/FNSUbiquitousStore.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSString.h>

NSString * const NSUbiquitousKeyValueStoreDidChangeExternallyNotification =
	@"NSUbiquitousKeyValueStoreDidChangeExternallyNotification";
NSString * const NSUbiquitousKeyValueStoreChangeReasonKey = @"NSUbiquitousKeyValueStoreChangeReasonKey";
NSString * const NSUbiquitousKeyValueStoreChangedKeysKey = @"NSUbiquitousKeyValueStoreChangedKeysKey";

/* APPLE'S DOCUMENTED LIMITS. The key length is a HARD ceiling and a longer key RAISES (Apple's behaviour); the
 * other two are the quota, and a write that breaks them is refused and reported. */
#define FN_UBIQUITOUS_MAX_KEYS		1024
#define FN_UBIQUITOUS_MAX_KEY_LENGTH	128
#define FN_UBIQUITOUS_MAX_BYTES		(1024 * 1024)

static id fn_ubiquitous_default_store = nil;

/* THE PRIVATE DOOR THE SERVICE SEAM USES. It is a METHOD rather than the seam reaching into the store's
 * storage, because reaching in from outside a class is what `-valueForKey:` would have been — a repurposed
 * public door for a private purpose, and a mistake this thread has already paid for once (§62.79). */
@interface NSUbiquitousKeyValueStore (FNPrivate)
- (void)fnApplyDeliveredValue:(id)value forKey:(NSString *)key;
@end

/* THE SIZE OF A VALUE, AS AN ESTIMATE AND SAID SO: strings by their UTF-16 length, data by its bytes, numbers
 * and dates as a fixed size, collections recursively. Apple's 1 MB is a real ceiling on real encoded bytes; this
 * is the same ceiling measured without encoding the value, and the header calls it an estimate. */
static NSUInteger fn_size_of_value(id value)
{
	NSUInteger total = 0;

	if ([value isKindOfClass:[NSData class]]) {
		return [value length];
	}
	if ([value isKindOfClass:[NSString class]]) {
		return [value length] * 2;
	}
	if ([value isKindOfClass:[NSArray class]]) {
		NSUInteger i, n = [value count];

		for (i = 0; i < n; i++) {
			total += fn_size_of_value([value objectAtIndex:i]);
		}
		return total;
	}
	if ([value isKindOfClass:[NSDictionary class]]) {
		NSArray *keys = [value allKeys];
		NSUInteger i, n = [keys count];

		for (i = 0; i < n; i++) {
			total += fn_size_of_value([keys objectAtIndex:i]);
			total += fn_size_of_value([value objectForKey:[keys objectAtIndex:i]]);
		}
		return total;
	}
	return 8;	/* numbers and dates */
}

static BOOL fn_is_property_list_value(id value)
{
	return [value isKindOfClass:[NSNumber class]] || [value isKindOfClass:[NSString class]] ||
	       [value isKindOfClass:[NSData class]] || [value isKindOfClass:[NSDate class]] ||
	       [value isKindOfClass:[NSArray class]] || [value isKindOfClass:[NSDictionary class]];
}

static NSUInteger fn_total_bytes(NSDictionary *values)
{
	NSArray *keys = [values allKeys];
	NSUInteger i, n = [keys count], total = 0;

	for (i = 0; i < n; i++) {
		total += fn_size_of_value([values objectForKey:[keys objectAtIndex:i]]);
	}
	return total;
}

static void fn_post_change(NSUbiquitousKeyValueStore *store, NSArray *changedKeys,
			   NSUbiquitousKeyValueStoreChangeReasons reason)
{
	NSDictionary *userInfo =
		[NSDictionary dictionaryWithObjectsAndKeys:
			[NSNumber numberWithInteger:(NSInteger)reason], NSUbiquitousKeyValueStoreChangeReasonKey,
			changedKeys, NSUbiquitousKeyValueStoreChangedKeysKey,
			nil];

	[[NSNotificationCenter defaultCenter] postNotificationName:
		NSUbiquitousKeyValueStoreDidChangeExternallyNotification
							    object:store
							  userInfo:userInfo];
}

@implementation NSUbiquitousKeyValueStore

+ (NSUbiquitousKeyValueStore *)defaultStore
{
	if (fn_ubiquitous_default_store == nil) {
		fn_ubiquitous_default_store = [[NSUbiquitousKeyValueStore alloc] init];
	}
	return fn_ubiquitous_default_store;
}

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_values = [[NSMutableDictionary alloc] init];
	}
	return self;
}

- (void)dealloc
{
	[(id)_values release];
	[super dealloc];
}

/* --- the object doors, where every rule actually lives ------------------------------------------------ */

- (nullable id)objectForKey:(NSString *)aKey
{
	if (aKey == nil) {
		return nil;
	}
	return [(NSDictionary *)_values objectForKey:aKey];
}

- (void)setObject:(nullable id)anObject forKey:(NSString *)aKey
{
	NSUInteger keyLength, projected, held;
	id previous;

	if (aKey == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"a ubiquitous store key cannot be nil"];
	}
	keyLength = [aKey length];
	if (keyLength > FN_UBIQUITOUS_MAX_KEY_LENGTH) {
		/* A LONGER KEY RAISES, as Apple's own store does: silently truncating a key would store the value
		 * under a name the caller never chose. */
		[NSException raise:NSInvalidArgumentException
			    format:@"a ubiquitous store key is limited to %d characters, and this one is %lu",
				   FN_UBIQUITOUS_MAX_KEY_LENGTH, (unsigned long)keyLength];
	}
	if (anObject == nil) {
		[self removeObjectForKey:aKey];		/* nil is not a value here: it is the absence of one */
		return;
	}
	if (!fn_is_property_list_value(anObject)) {
		[NSException raise:NSInvalidArgumentException
			    format:@"%@ is not a property-list value — archive it into NSData first",
				   [anObject class]];
	}
	previous = [(NSDictionary *)_values objectForKey:aKey];
	held = fn_total_bytes((NSDictionary *)_values) -
	       (previous != nil ? fn_size_of_value(previous) : 0);
	projected = held + fn_size_of_value(anObject);
	if ((previous == nil && [(NSDictionary *)_values count] >= FN_UBIQUITOUS_MAX_KEYS) ||
	    projected > FN_UBIQUITOUS_MAX_BYTES) {
		/* REFUSED WHOLE (nothing is stored) and reported through the change notice, which is how Apple's own
		 * store reports a quota violation. */
		fn_post_change(self, [NSArray arrayWithObject:aKey],
			       NSUbiquitousKeyValueStoreQuotaViolationChange);
		return;
	}
	[(NSMutableDictionary *)_values setObject:anObject forKey:aKey];
}

- (void)removeObjectForKey:(NSString *)aKey
{
	if (aKey != nil) {
		[(NSMutableDictionary *)_values removeObjectForKey:aKey];
	}
}

/* --- the typed doors --------------------------------------------------------------------------------- */

- (nullable NSString *)stringForKey:(NSString *)aKey
{
	id v = [self objectForKey:aKey];

	return [v isKindOfClass:[NSString class]] ? v : nil;
}

- (nullable NSArray *)arrayForKey:(NSString *)aKey
{
	id v = [self objectForKey:aKey];

	return [v isKindOfClass:[NSArray class]] ? v : nil;
}

- (nullable NSDictionary *)dictionaryForKey:(NSString *)aKey
{
	id v = [self objectForKey:aKey];

	return [v isKindOfClass:[NSDictionary class]] ? v : nil;
}

- (nullable NSData *)dataForKey:(NSString *)aKey
{
	id v = [self objectForKey:aKey];

	return [v isKindOfClass:[NSData class]] ? v : nil;
}

- (BOOL)boolForKey:(NSString *)aKey
{
	id v = [self objectForKey:aKey];

	return [v isKindOfClass:[NSNumber class]] ? [v boolValue] : NO;
}

- (double)doubleForKey:(NSString *)aKey
{
	id v = [self objectForKey:aKey];

	return [v isKindOfClass:[NSNumber class]] ? [v doubleValue] : 0.0;
}

- (long long)longLongForKey:(NSString *)aKey
{
	id v = [self objectForKey:aKey];

	return [v isKindOfClass:[NSNumber class]] ? [v longLongValue] : 0;
}

- (void)setString:(nullable NSString *)aString forKey:(NSString *)aKey
{
	[self setObject:aString forKey:aKey];
}

- (void)setArray:(nullable NSArray *)anArray forKey:(NSString *)aKey
{
	[self setObject:anArray forKey:aKey];
}

- (void)setDictionary:(nullable NSDictionary *)aDictionary forKey:(NSString *)aKey
{
	[self setObject:aDictionary forKey:aKey];
}

- (void)setData:(nullable NSData *)aData forKey:(NSString *)aKey
{
	[self setObject:aData forKey:aKey];
}

- (void)setBool:(BOOL)aBool forKey:(NSString *)aKey
{
	[self setObject:[NSNumber numberWithBool:aBool] forKey:aKey];
}

- (void)setDouble:(double)aDouble forKey:(NSString *)aKey
{
	[self setObject:[NSNumber numberWithDouble:aDouble] forKey:aKey];
}

- (void)setLongLong:(long long)aLongLong forKey:(NSString *)aKey
{
	[self setObject:[NSNumber numberWithLongLong:aLongLong] forKey:aKey];
}

- (NSDictionary *)dictionaryRepresentation
{
	return [NSDictionary dictionaryWithDictionary:(NSDictionary *)_values];
}

- (void)fnApplyDeliveredValue:(id)value forKey:(NSString *)key
{
	[(NSMutableDictionary *)_values setObject:value forKey:key];
}

- (BOOL)synchronize
{
	/* NO, AND THAT IS A FACT RATHER THAN A FAILURE: there is no iCloud service in this system, so there is
	 * nothing to make the local copy agree with, and YES would claim a synchronization that never happened. */
	return NO;
}

@end

void FNSUbiquitousStoreDeliverExternalChange(NSUbiquitousKeyValueStore *store, NSDictionary *values,
					     NSArray *changedKeys,
					     NSUbiquitousKeyValueStoreChangeReasons reason)
{
	NSArray *keys = [values allKeys];
	NSUInteger i, n = [keys count];

	for (i = 0; i < n; i++) {
		NSString *key = [keys objectAtIndex:i];
		id value = [values objectForKey:key];

		/* THE SERVICE IS THE AUTHORITY ON WHAT EXISTS REMOTELY, so a delivered value is applied as given —
		 * its TYPE is still checked (a store of non-property-list values is not a store), but the QUOTA is
		 * not, because that limit exists to bound what THIS process writes. */
		if (value == nil || value == [NSNull null]) {
			/* NSNull IS THE REMOVAL SPELLING: a dictionary cannot carry a nil, and NSNull is not a
			 * property-list type this store accepts, so it is free to mean "gone". */
			[store removeObjectForKey:key];
		} else if (fn_is_property_list_value(value)) {
			[store fnApplyDeliveredValue:value forKey:key];
		}
	}
	fn_post_change(store, changedKeys, reason);
}
