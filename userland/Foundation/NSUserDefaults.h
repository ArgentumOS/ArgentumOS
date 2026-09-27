/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUserDefaults — THE SETTINGS STORE. docs/design/foundation-plan.md W5, §41.
 *
 * A DEFAULTS OBJECT IS A SEARCH LIST OF DOMAINS, and everything below follows from that one sentence: a
 * lookup walks the list in order and answers with the first domain holding the key, and a write goes to the
 * APP DOMAIN, which is the only one the object owns.
 *
 * THE STORE IS ONE PROPERTY LIST PER DOMAIN IN THE FSH'S `Configuration/` SCOPES (user, 2026-09-20). Apple
 * writes `<bundle-id>.plist` under `~/Library/Preferences`; the equivalent here is
 *
 *     <root>/Users/<user>/Configuration/<domain>.plist     the USER scope — where a WRITE lands
 *     <root>/Shared/Configuration/<domain>.plist           the SHARED scope — shipped defaults
 *     <root>/System/Configuration/<domain>.plist           the SYSTEM scope — an administrator's value
 *
 * where `<root>` is `getenv("FNX_CONFIG_ROOT")` or empty, and `<user>` is `getpwuid(getuid())->pw_name`,
 * then `getenv("USER")`, then `"root"`. The directories are CREATED on demand, and the files are XML
 * property lists written atomically through `NSPropertyListSerialization` — the tree's own plist core,
 * which is not libconfig. `FNX_CONFIG_ROOT` exists for the same reason libconfig has it: a probe points the
 * whole store at a scratch directory with `setenv(3)` and touches nothing in the image.
 *
 * THE SCOPE AXIS IS OURS, NOT APPLE'S, AND IT IS WHAT `-objectIsForcedForKey:` MEANS. Apple models one file
 * per domain and answers "did an administrator provide this?" from a managed-device flag. Here one domain
 * name resolves across up to three files, merged with `SYSTEM > USER > SHARED` precedence — the order
 * config-design.md already gives this OS, where the system's value is authoritative and the user's overrides
 * the shipped default — so **`-objectIsForcedForKey:inDomain:` answers YES exactly when the SYSTEM scope's
 * file for that domain supplies the key.** No extra mechanism, and the answer is true by construction.
 *
 * THE SEARCH LIST, in order, and it is DOCUMENTED RATHER THAN ASSUMED:
 *
 *   1. `NSArgumentDomain` — the `-NAME VALUE` pairs this process was launched with, VOLATILE.
 *   2. every other volatile domain the caller set with `-setVolatileDomain:forName:`, in the order set.
 *   3. the APP DOMAIN — `NSGlobalDomain` FOR NOW, and that is a named consequence of W18 being unbuilt:
 *      with no `NSBundle` there is no bundle identifier, so this class cannot yet tell one app's settings
 *      from every app's. `-initWithSuiteName:` and `-addSuiteNamed:` are how a caller separates them today,
 *      and the app domain becomes the bundle's when NSBundle lands.
 *   4. every suite added with `-addSuiteNamed:`, in the order added.
 *   5. `NSRegistrationDomain` — the fallback, VOLATILE, and the LAST word: it exists to stop a call
 *      answering nil for a setting the app is entitled to assume.
 *
 * THE VALUE MODEL IS THE PROPERTY LIST, EXACTLY: NSString, NSNumber, NSDate, NSData, NSArray and
 * NSDictionary. There is no encoding layer and no lossy conversion, because the store IS a property list —
 * a value a caller sets is a value the next process reads, which is the only fidelity promise this class
 * makes.
 *
 * DEVIATIONS, ALL NAMED, because §11's bar counts them:
 *
 *   * `-synchronize` is DEPRECATED by Apple (§11.5) and therefore ABSENT — and here it would be a lie as
 *     well as struck: every write reaches the disk before it returns, and the periodic background flush
 *     Apple's version waits for does not exist. A caller that wants the write is already holding it.
 *     `-persistentDomainNames`, `-initWithUser:` and `+resetStandardUserDefaults` are absent for the same
 *     §11.5 reason; the three iCloud notifications are struck with their family.
 *   * `-URLForKey:` READS THE STRING FORM and `-setURL:forKey:` WRITES it. Apple also archives a URL as
 *     NSData and decodes that on read; that path needs the keyed unarchiver's class allowlist, which is
 *     not decided, so a data-encoded URL answers nil here rather than being half-decoded.
 *   * THE SIZE LIMIT IS OURS. Apple documents that `NSUserDefaultsSizeLimitExceededNotification` is posted
 *     when the database passes "the allowed maximum" and publishes no number; ours is
 *     `NSUserDefaultsMaximumDomainSize`, and exceeding it NOTIFIES WITHOUT DISCARDING — a caller's value is
 *     saved and the warning is the notification, because silently dropping settings is the one failure mode
 *     this class must not have.
 *   * `-initWithSuiteName:` with nil, and `-init`, both give the standard configuration (Apple's `-init` is
 *     documented as "the app's current settings", which is exactly the app domain).
 *
 * THREAD SAFETY IS CLAIMED HERE, as Apple claims it: every door below is taken under a lock, and two
 * threads may share one defaults object.
 */

#ifndef FOUNDATION_NSUSERDEFAULTS_H
#define FOUNDATION_NSUSERDEFAULTS_H

#import <Foundation/NSObject.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#include <stdint.h>

@class NSData;
@class NSDate;
@class NSLock;
@class NSMutableArray;
@class NSMutableDictionary;
@class NSNumber;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* The domain NAMES. Apple publishes the semantics and not the spellings, so these are the conventional
 * spellings — a caller comparing against the constant is unaffected either way, and one that passes the
 * literal gets what it expects (§11's D2: a value nobody publishes is ours). */
extern NSString *const NSArgumentDomain;	/* volatile: the -NAME VALUE launch arguments */
extern NSString *const NSGlobalDomain;		/* persistent: system-wide settings, and the app domain for now */
extern NSString *const NSRegistrationDomain;	/* volatile: the caller's registered fallbacks */

/* Posted on the default notification centre, object = the defaults object, after this process changes a
 * setting. Posted for a volatile change too: the notification is about the OBJECT's contents, not the
 * file's. */
extern NSString *const NSUserDefaultsDidChangeNotification;

/* Posted when the app domain's USER file passes `NSUserDefaultsMaximumDomainSize` BYTES. The write that
 * crossed the line still happened — see the header's deviations. */
extern NSString *const NSUserDefaultsSizeLimitExceededNotification;

/* The per-domain maximum, in bytes of the serialized USER file. Ours, because Apple publishes no number. */
extern const NSUInteger NSUserDefaultsMaximumDomainSize;

/* --- THE LEGACY LOCALIZATION KEYS, AND THE UBIQUITY NOTIFICATIONS (§62.43) ---------------------------
 *
 * TWENTY-SIX KEYS AND THREE NOTIFICATION NAMES, AND THEY ARE HERE BECAUSE §62.24 RETIRED THE DEPRECATION
 * GROUND: these are the keys an application written before `NSLocale` read out of the user's defaults
 * (`[defaults objectForKey:NSMonthNameArray]`), Apple dropped them when localization moved into `NSLocale`, and
 * a ported application's SOURCE still names every one of them.
 *
 * WHAT THEY ARE IS A NAME AND NOTHING ELSE — which is also what Apple's own are: each one's value is the key
 * string a defaults store is asked about, so `objectForKey:NSMonthNameArray` answers whatever the store holds
 * under that spelling and NOTHING IN THIS LIBRARY PUTS ANYTHING THERE. That is the honest shape of a legacy
 * key: the vocabulary compiles, and the data behind it was always the user's.
 *
 * AND THE THREE NOTIFICATIONS ARE NAMED WITH THEIR ABSENCE STATED: `NSUbiquitousUserDefaults…` are the iCloud
 * defaults store's notifications, THIS SYSTEM HAS NO UBIQUITOUS STORE, and nothing here posts them. A caller
 * that registers an observer for one compiles and is never told — which is what "no iCloud" means, said where
 * the name is rather than left to be discovered at runtime.
 */
extern NSString *const NSAMPMDesignation;
extern NSString *const NSCurrencySymbol;
extern NSString *const NSDateFormatString;
extern NSString *const NSDateTimeOrdering;
extern NSString *const NSDecimalDigits;
extern NSString *const NSDecimalSeparator;
extern NSString *const NSEarlierTimeDesignations;
extern NSString *const NSHourNameDesignations;
extern NSString *const NSInternationalCurrencyString;
extern NSString *const NSLaterTimeDesignations;
extern NSString *const NSMonthNameArray;
extern NSString *const NSNegativeCurrencyFormatString;
extern NSString *const NSNextDayDesignations;
extern NSString *const NSNextNextDayDesignations;
extern NSString *const NSPositiveCurrencyFormatString;
extern NSString *const NSPriorDayDesignations;
extern NSString *const NSShortDateFormatString;
extern NSString *const NSShortMonthNameArray;
extern NSString *const NSShortTimeDateFormatString;
extern NSString *const NSShortWeekDayNameArray;
extern NSString *const NSThisDayDesignations;
extern NSString *const NSThousandsSeparator;
extern NSString *const NSTimeDateFormatString;
extern NSString *const NSTimeFormatString;
extern NSString *const NSWeekDayNameArray;
extern NSString *const NSYearMonthWeekDesignations;
extern NSString *const NSUbiquitousUserDefaultsCompletedInitialSyncNotification;
extern NSString *const NSUbiquitousUserDefaultsDidChangeAccountsNotification;
extern NSString *const NSUbiquitousUserDefaultsNoCloudAccountNotification;


@interface NSUserDefaults : NSObject
{
	NSString *_appDomain;			/* the persistent domain writes go to, NSGlobalDomain by default */
	NSMutableDictionary *_persistent;	/* domain name -> the MERGED view (system>user>shared) */
	NSMutableDictionary *_userFiles;	/* domain name -> the USER file's own contents: the write target */
	NSMutableDictionary *_volatileDomains;	/* domain name -> NSMutableDictionary */
	NSMutableArray *_volatileNames;		/* volatile domain names, in the order they were created */
	NSMutableDictionary *_registration;	/* the NSRegistrationDomain dictionary, directly */
	NSMutableDictionary *_argument;		/* the NSArgumentDomain dictionary, directly */
	NSMutableArray *_suites;		/* suite names added, in order */
	NSLock *_lock;
}

/* The shared object for this process. Built once, on first use. */
+ (NSUserDefaults *)standardUserDefaults;

/* A defaults object over the app domain. The argument and registration domains start EMPTY for every new
 * object — the arguments are read from THIS process's argv, and registrations belong to the caller that
 * made them. */
- (instancetype)init;

/* The app domain becomes `suiteName` instead of NSGlobalDomain. NOTHING HAPPENS when `suiteName` is nil,
 * which is Apple's contract: "If you pass nil, the method has no effect" — so the result is `-init`. */
- (nullable instancetype)initWithSuiteName:(nullable NSString *)suitename;

/* Merge `registrationDictionary` into NSRegistrationDomain. Nothing is written to disk and nothing can be
 * removed this way: the registration domain is the fallback, so it may be added to or replaced, never
 * emptied, which is Apple's documented rule. */
- (void)registerDefaults:(NSDictionary *)registrationDictionary;

/* --- reading, which walks the search list --- */

- (nullable id)objectForKey:(NSString *)defaultName;
- (nullable NSString *)stringForKey:(NSString *)defaultName;
- (nullable NSArray *)arrayForKey:(NSString *)defaultName;
- (nullable NSArray *)stringArrayForKey:(NSString *)defaultName;
- (nullable NSDictionary *)dictionaryForKey:(NSString *)defaultName;
- (nullable NSData *)dataForKey:(NSString *)defaultName;
- (nullable NSURL *)URLForKey:(NSString *)defaultName;

/* THE COERCING FOUR. `-boolForKey:` answers NO and the others answer zero when the key is absent, and each
 * coerces what it finds rather than refusing it — Apple's documented examples are the rule: a NUMBER that
 * is non-zero is true, and the STRINGS "true", "yes" and "1" are true, compared case-insensitively. */
- (BOOL)boolForKey:(NSString *)defaultName;
- (NSInteger)integerForKey:(NSString *)defaultName;
- (float)floatForKey:(NSString *)defaultName;
- (double)doubleForKey:(NSString *)defaultName;

/* The union of every domain in the search list, lowest precedence first, so the answer is what a lookup
 * would give. Volatile and persistent alike. */
- (NSDictionary *)dictionaryRepresentation;

/* --- writing, which lands in the USER scope of the app domain --- */

- (void)setObject:(nullable id)value forKey:(NSString *)defaultName;
- (void)setBool:(BOOL)value forKey:(NSString *)defaultName;
- (void)setInteger:(NSInteger)value forKey:(NSString *)defaultName;
- (void)setFloat:(float)value forKey:(NSString *)defaultName;
- (void)setDouble:(double)value forKey:(NSString *)defaultName;
- (void)setURL:(nullable NSURL *)url forKey:(NSString *)defaultName;
- (void)removeObjectForKey:(NSString *)defaultName;

/* --- the search list, as a caller may edit it --- */

- (void)addSuiteNamed:(NSString *)suiteName;
- (void)removeSuiteNamed:(NSString *)suiteName;

/* Volatile domain names, in creation order, always including NSArgumentDomain and NSRegistrationDomain
 * unless a caller removed them by name. */
- (NSArray *)volatileDomainNames;

/* --- whole domains --- */

/* The merged view of one persistent domain, or nil when no scope has a file for it (Apple returns nil for
 * "contains no keys" and for a volatile domain, which is what this is). */
- (nullable NSDictionary *)persistentDomainForName:(NSString *)domainName;

/* REPLACES the domain: `domain` is written to the USER scope, and every key the USER file held that
 * `domain` does not is REMOVED from it. Keys a SYSTEM file supplies SURVIVE, which is not an oversight —
 * that file is an administrator's value, and it is what `-objectIsForcedForKey:inDomain:` reports. */
- (void)setPersistentDomain:(NSDictionary *)domain forName:(NSString *)domainName;

/* Removes the USER scope's file for the domain. A SYSTEM or SHARED file for the same name is untouched. */
- (void)removePersistentDomainForName:(NSString *)domainName;

- (NSDictionary *)volatileDomainForName:(NSString *)domainName;
- (void)setVolatileDomain:(NSDictionary *)domain forName:(NSString *)domainName;
- (void)removeVolatileDomainForName:(NSString *)domainName;

/* --- managed keys --- */

/* YES when the SYSTEM scope's file for `domain` — the app domain, in the one-argument form — supplies
 * `key`. See the header's note: this is what "an administrator provided the value" means here. */
- (BOOL)objectIsForcedForKey:(NSString *)key;
- (BOOL)objectIsForcedForKey:(NSString *)key inDomain:(NSString *)domain;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUSERDEFAULTS_H */
