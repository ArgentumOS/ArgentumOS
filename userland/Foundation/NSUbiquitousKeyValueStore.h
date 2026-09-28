/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSUbiquitousKeyValueStore — the iCloud key/value store (§62.87) with its change notification and the four
 * change reasons. This closes `Files and Data Persistence / iCloud key and value storage`.
 *
 * **THE LOCAL HALF IS REAL AND THE REMOTE HALF IS ABSENT, WHICH IS THE SHAPE §62.80 ALREADY ESTABLISHED.** The
 * store holds values, answers them through the typed doors, enforces the limits Apple documents, and reports
 * changes through the notification. What it does NOT have is iCloud: there is no service to synchronize with, so
 * `-synchronize` answers **NO** — a YES would claim a synchronization that cannot happen — and **IN v1 THE STORE
 * IS IN MEMORY ONLY**, because persistence in Apple's design belongs to the system service rather than to this
 * object.
 *
 * THE VALUES ARE PROPERTY-LIST TYPES AND NOTHING ELSE (Apple's rule, and the reason an app archives anything
 * richer into `NSData` first): `NSNumber`, `NSString`, `NSData`, `NSDate`, `NSArray`, `NSDictionary`. A write of
 * any other object is REFUSED BY NAME rather than being stored and failing later somewhere else.
 *
 * THE LIMITS ARE APPLE'S AND THEY ARE ENFORCED HERE: at most **1024 keys**, at most **1 MB of values**, and a
 * key of at most **128 characters** — a longer KEY RAISES (Apple's own behaviour) while a write that would break
 * the quota is REFUSED and reports itself through `NSUbiquitousKeyValueStoreQuotaViolationChange`. Two
 * consequences worth stating rather than discovering: the 1 MB total is enforced against an ESTIMATE (strings by
 * their UTF-16 length, data by its bytes, numbers and dates as fixed sizes, collections recursively), and a
 * refused write leaves the store EXACTLY as it was, because Apple's own contract is that a store call is one
 * atomic transaction.
 *
 * THE CHANGE NOTIFICATION IS ABOUT CHANGES FROM OUTSIDE, which is what its name says and what Apple's
 * documentation says in words: it is posted when a value changes because of incoming data, **not when this app
 * writes a value**. With no service to deliver such a change, the delivery path is an internal seam
 * (`FNSUbiquitousStore.h`, where `NSNull` is the removal spelling because a dictionary cannot hold a nil) — the
 * same device §62.85 used for the spell server's client half — so the notification
 * is reachable, testable, and waiting for the service that will produce it rather than being a declaration
 * nobody can drive.
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSData;
@class NSDate;
@class NSDictionary;
@class NSString;

/* APPLE PUBLISHES THE FOUR CASE NAMES AND THEIR MEANINGS; THE VALUES ARE OURS (§11.6.1 D2 — nobody publishes
 * enum values), and they are 0 through 3 in the order Apple's documentation lists them. */
typedef enum {
	NSUbiquitousKeyValueStoreServerChange = 0,		/* a value changed on the server */
	NSUbiquitousKeyValueStoreInitialSyncChange = 1,		/* the first load from the server */
	NSUbiquitousKeyValueStoreQuotaViolationChange = 2,	/* a write exceeded the limits */
	NSUbiquitousKeyValueStoreAccountChange = 3		/* the account changed underneath */
} NSUbiquitousKeyValueStoreChangeReasons;

@interface NSUbiquitousKeyValueStore : NSObject
{
@private
	id _values;		/* the store itself: key -> property-list value */
}

/* THE ONE STORE FOR THE PROCESS (Apple: "you should use the same object throughout your app"). A fresh process
 * gets an empty one; the store is NOT subclassable in spirit, which a language cannot enforce, so it is said
 * here instead. */
+ (NSUbiquitousKeyValueStore *)defaultStore;

/* Objects, by key. `setObject:forKey:` refuses a non-property-list value and a write that would break a limit;
 * `removeObjectForKey:` on an absent key is not an error. */
- (nullable id)objectForKey:(NSString *)aKey;
- (void)setObject:(nullable id)anObject forKey:(NSString *)aKey;
- (void)removeObjectForKey:(NSString *)aKey;

/* The typed doors. Apple's reading doors answer the VALUE, not the presence of a key: a key holding the wrong
 * type — or no key at all — answers nil or zero, so `-objectForKey:` is the door that distinguishes "absent"
 * from "false". */
- (nullable NSString *)stringForKey:(NSString *)aKey;
- (nullable NSArray *)arrayForKey:(NSString *)aKey;
- (nullable NSDictionary *)dictionaryForKey:(NSString *)aKey;
- (nullable NSData *)dataForKey:(NSString *)aKey;
- (BOOL)boolForKey:(NSString *)aKey;
- (double)doubleForKey:(NSString *)aKey;
- (long long)longLongForKey:(NSString *)aKey;

- (void)setString:(nullable NSString *)aString forKey:(NSString *)aKey;
- (void)setArray:(nullable NSArray *)anArray forKey:(NSString *)aKey;
- (void)setDictionary:(nullable NSDictionary *)aDictionary forKey:(NSString *)aKey;
- (void)setData:(nullable NSData *)aData forKey:(NSString *)aKey;
- (void)setBool:(BOOL)aBool forKey:(NSString *)aKey;
- (void)setDouble:(double)aDouble forKey:(NSString *)aKey;
- (void)setLongLong:(long long)aLongLong forKey:(NSString *)aKey;

/* EVERY PAIR THE STORE HOLDS, as property-list values. */
- (NSDictionary *)dictionaryRepresentation;

/* APPLE'S DOOR FOR "MAKE THE COPIES MATCH"; IT ANSWERS **NO** HERE, AND THAT IS THE HONEST ANSWER: there is no
 * service in this system to synchronize with, and answering YES would claim a synchronization that did not
 * happen. A caller can therefore tell the truth about this state instead of assuming it worked. */
- (BOOL)synchronize;

@end

/* --- THE CHANGE NOTICE AND ITS TWO KEYS ------------------------------------------------------------ */

/* Posted when a change ARRIVES FROM OUTSIDE (see the file's note) — never for this app's own writes. The object
 * is the store; the `userInfo` carries the two keys below. */
FOUNDATION_EXPORT NSString * const NSUbiquitousKeyValueStoreDidChangeExternallyNotification;
/* An `NSNumber` holding one of `NSUbiquitousKeyValueStoreChangeReasons`. */
FOUNDATION_EXPORT NSString * const NSUbiquitousKeyValueStoreChangeReasonKey;
/* An `NSArray` of `NSString`: the keys that changed. */
FOUNDATION_EXPORT NSString * const NSUbiquitousKeyValueStoreChangedKeysKey;

NS_ASSUME_NONNULL_END
