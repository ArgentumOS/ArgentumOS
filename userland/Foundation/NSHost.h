/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSHost — a host as a NAME and a set of ADDRESSES (2026-09-26, plan §62.63). APPLE-DEPRECATED, AND THEREFORE
 * IN SCOPE: §62.24's decision is that API Apple deprecated is a PORTING TARGET rather than an exclusion, so the
 * class ships with its `deprecated` ledger row flipped like any other.
 *
 * WHAT IT IS, IN APPLE'S WORDS: "provides methods to access the network name and address information for a
 * host" — and one sentence of Apple's shapes the whole implementation, because it decides what the class is a
 * VIEW of:
 *
 *   "These methods use the available network administration services to discover this information but do NOT
 *    CONTACT THE HOST ITSELF; this may mean the information is incomplete."
 *
 * SO THIS IS THE LOCAL RESOLVER'S ANSWER, NOT A NETWORK PROTOCOL: the system's own name service answers, through
 * musl's resolver in this tree (getaddrinfo(3)/getnameinfo(3), which read the FSH hosts domain — plan §M5). A
 * host that is not in the name service simply has no NSHost, which is the same answer Apple's classes give.
 *
 * THREE FACTS APPLE PUBLISHES THAT A CALLER DEPENDS ON, and each is a property of this implementation rather
 * than a detail:
 *
 *   * A HOST MAY HAVE SEVERAL NAMES AND SEVERAL ADDRESSES — "sales" and "sales.anycorp.com" are one host, and
 *     a host may be reachable at more than one address. `-name` and `-address` therefore answer ONE of them
 *     ("chosen arbitrarily if multiple", Apple says) while `-names` and `-addresses` answer ALL;
 *   * DO NOT USE -alloc/-init: the three CLASS METHODS are the doors, because a host only exists as an answer
 *     from the name service. `-init` is not declared here for exactly that reason.
 *   * THE METHODS ARE THREAD-SAFE, which is why the two arrays a host hands out are IMMUTABLE: a caller that
 *     could mutate one would be mutating another thread's answer.
 *
 * AND THE CACHE DOORS SHIP ANSWERING AS APPLE DOCUMENTS THEM NOW — Apple's own annotation on
 * `+setHostCacheEnabled:`, `+isHostCacheEnabled` and `+flushHostCache` is "Caching no longer supported", so
 * caching is NOT implemented, `+isHostCacheEnabled` answers NO, and the other two do nothing. That is a
 * documented answer rather than a stub, and it is the same shape §62.63's sibling records for
 * `NSDateComponentsFormatter`'s `formattingContext` ("Not yet supported" in Apple's own abstract).
 *
 * WHAT IS OURS (§11.6.1 D2): the IVARS. Apple documents its layout (`addresses`, `names`, `reserved`) as
 * instance variables of the class; this library spells its own with a leading underscore and has no `reserved`
 * slot to keep, and neither is reachable by a caller.
 */

#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray, NSString;

@interface NSHost : NSObject
{
@protected
	NSArray *_names;		/* NSString, immutable: the methods are thread-safe and these are shared */
	NSArray *_addresses;		/* NSString, in the resolver's own textual form */
}

/* APPLE'S THREE DOORS, and the only way to make one: a host IS an answer from the name service, so there is no
 * `-init` to declare. Each answers nil when the name service has nothing, which is a legitimate answer and not
 * a failure (Apple: the information may be incomplete). */
+ (nullable NSHost *)currentHost;
+ (nullable NSHost *)hostWithAddress:(NSString *)address;
+ (nullable NSHost *)hostWithName:(NSString *)name;

/* ONE AND ALL. The singular doors answer "arbitrarily chosen" members of the plural ones — arbitrary because
 * the name service has no preference to report, not because this class randomises.
 *
 * THE TWO SINGULAR ONES ARE NULLABLE, AND THAT IS A STATED CHOICE RATHER THAN APPLE'S: a host built from a name
 * the service has no ADDRESS for (or the reverse) is the case Apple's own "information may be incomplete"
 * describes, and there is nothing truthful for `-address` to answer there. The alternative — an empty string —
 * would invent an address. (§11.6.1 D2: Apple publishes the method and no nil rule.) */
@property (nullable, readonly) NSString *address;
@property (readonly) NSArray *addresses;
@property (nullable, readonly) NSString *name;
@property (readonly) NSArray *names;
/* "The name used by default when publishing the receiver via NSNetServices" — the first of the names here, and
 * nil when the host has none. */
@property (nullable, readonly) NSString *localizedName;

/* TWO HOSTS ARE ONE HOST WHEN THEY SHARE AN ADDRESS — the only identity question that means anything when the
 * names are aliases and the addresses are what a connection is actually made to. */
- (BOOL)isEqualToHost:(NSHost *)aHost;

/* THE CACHE DOORS, ANSWERING AS APPLE DOCUMENTS THAT THERE IS NO CACHE (above). */
+ (BOOL)isHostCacheEnabled;
+ (void)setHostCacheEnabled:(BOOL)flag;
+ (void)flushHostCache;

@end

NS_ASSUME_NONNULL_END
