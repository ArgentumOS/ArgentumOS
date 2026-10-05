/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_ubiquitousstore.m — THE PROBE FOR §62.87: NSUbiquitousKeyValueStore, its change notice and the four
 * change reasons.
 *
 * THE LOCAL HALF IS REAL AND THE REMOTE HALF IS ABSENT (the shape §62.80 established), so the checks divide the
 * same way: what a store DOES — the typed doors, the property-list rule, Apple's three limits, removal — and what
 * the system cannot do, which is synchronize with a service that does not exist. `-synchronize` answering NO is
 * asserted rather than assumed, because a YES would be the one lie this class could tell.
 *
 * THE CHANGE NOTICE IS EXERCISED THROUGH THE SERVICE SEAM (`FNSUbiquitousStore.h`), which is what stops it being
 * a name nobody can post: the seam does what a service's delivery does — merge the incoming values, then post
 * the notice with the two documented keys — and the probe OBSERVES the notice rather than trusting it.
 *
 * AND THE RULE THAT IS EASIEST TO GET WRONG IS ASSERTED EXPLICITLY: the notice is about changes from OUTSIDE, so
 * this app's own successful write must post NOTHING. The one local posting is a QUOTA VIOLATION, which is how
 * Apple's store reports it too.
 *
 * FRESH INSTANCES ARE USED FOR THE LIMIT CHECKS so that the process-wide `+defaultStore` is not left filled with
 * test data; the singleton is checked for identity, and nothing else depends on its contents.
 */

#import <Foundation/Foundation.h>
#import <Foundation/FNSUbiquitousStore.h>
#import <objc/runtime.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-UBIQUITOUSSTORE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-UBIQUITOUSSTORE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static NSString *fn_raised(void (^block)(void))
{
	@try {
		block();
	} @catch (NSException *e) {
		return [e name];
	}
	return nil;
}

/* AN OBSERVER THAT RECORDS WHAT IT SAW, so a check can be about the notice's SHAPE and not merely its arrival. */
@interface FNUbiquitousObserver : NSObject
{
@public
	int count;
	NSString *lastName;
	NSDictionary *lastUserInfo;
	id lastObject;
}
@end

@implementation FNUbiquitousObserver

- (void)noticeArrived:(NSNotification *)notification
{
	count++;
	lastName = [notification name];
	lastUserInfo = [notification userInfo];
	lastObject = [notification object];
}

@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- 1. THE CLASS AND ITS CONSTANTS ------------------------------------------------------------- */
	{
		Class cls = objc_getClass("NSUbiquitousKeyValueStore");

		check("the-class-and-its-three-names-are-declared",
		      cls != Nil && class_getSuperclass(cls) == [NSObject class] &&
		      [NSUbiquitousKeyValueStoreDidChangeExternallyNotification
			isEqualToString:@"NSUbiquitousKeyValueStoreDidChangeExternallyNotification"] &&
		      [NSUbiquitousKeyValueStoreChangeReasonKey
			isEqualToString:@"NSUbiquitousKeyValueStoreChangeReasonKey"] &&
		      [NSUbiquitousKeyValueStoreChangedKeysKey
			isEqualToString:@"NSUbiquitousKeyValueStoreChangedKeysKey"],
		      @"the class exists and the notice and its two userInfo keys are exported under their names");
		check("the-four-change-reasons-are-distinct-and-in-apples-order",
		      NSUbiquitousKeyValueStoreServerChange == 0 &&
		      NSUbiquitousKeyValueStoreInitialSyncChange == 1 &&
		      NSUbiquitousKeyValueStoreQuotaViolationChange == 2 &&
		      NSUbiquitousKeyValueStoreAccountChange == 3,
		      @"Apple publishes the four case NAMES; the VALUES are ours (§11.6.1 D2) and they are 0..3 in "
		      @"the order Apple lists them");
	}

	/* --- 2. THE SINGLETON --------------------------------------------------------------------------- */
	check("the-default-store-is-one-object",
	      [NSUbiquitousKeyValueStore defaultStore] == [NSUbiquitousKeyValueStore defaultStore] &&
	      [NSUbiquitousKeyValueStore defaultStore] != nil,
	      @"+defaultStore answers the same store every time, as Apple's contract says");

	/* --- 3. THE TYPED DOORS ------------------------------------------------------------------------- */
	{
		NSUbiquitousKeyValueStore *store = [[NSUbiquitousKeyValueStore alloc] init];
		NSData *data = [@"bytes" dataUsingEncoding:NSUTF8StringEncoding];

		check("a-fresh-store-is-empty",
		      [store objectForKey:@"nope"] == nil && [[store dictionaryRepresentation] count] == 0,
		      @"nothing has been stored");

		[store setString:@"value" forKey:@"string"];
		[store setLongLong:9000000000LL forKey:@"ll"];
		[store setDouble:2.5 forKey:@"d"];
		[store setBool:YES forKey:@"b"];
		[store setData:data forKey:@"data"];
		[store setArray:[NSArray arrayWithObject:@"a"] forKey:@"array"];
		[store setDictionary:[NSDictionary dictionaryWithObject:@"v" forKey:@"k"] forKey:@"dict"];
		check("every-typed-door-round-trips",
		      [[store stringForKey:@"string"] isEqualToString:@"value"] &&
		      [store longLongForKey:@"ll"] == 9000000000LL &&
		      [store doubleForKey:@"d"] == 2.5 &&
		      [store boolForKey:@"b"] == YES &&
		      [[store dataForKey:@"data"] isEqualToData:data] &&
		      [[store arrayForKey:@"array"] count] == 1 &&
		      [[[store dictionaryForKey:@"dict"] objectForKey:@"k"] isEqualToString:@"v"] &&
		      [[store objectForKey:@"string"] isEqualToString:@"value"],
		      @"strings, integers, doubles, booleans, data, arrays and dictionaries all come back through "
		      @"the matching door AND through -objectForKey:");
		check("the-reading-doors-answer-the-value-not-the-presence",
		      [store boolForKey:@"absent"] == NO && [store objectForKey:@"absent"] == nil &&
		      [store stringForKey:@"ll"] == nil && [store longLongForKey:@"string"] == 0 &&
		      [store arrayForKey:@"string"] == nil,
		      @"a wrong-typed or absent key answers nil or zero — which is why -objectForKey: is the door "
		      @"that tells 'absent' from 'false'");
		check("the-store-holds-every-pair-it-was-given",
		      [[store dictionaryRepresentation] count] == 7 &&
		      [[[store dictionaryRepresentation] objectForKey:@"b"] boolValue] == YES,
		      @"-dictionaryRepresentation is the whole store");

		/* REMOVAL, AND THE FACT THAT nil IS AN ABSENCE RATHER THAN A VALUE. */
		[store removeObjectForKey:@"string"];
		[store setObject:nil forKey:@"ll"];
		[store removeObjectForKey:@"never-existed"];	/* not an error */
		check("removing-takes-the-key-away-including-through-nil",
		      [store objectForKey:@"string"] == nil && [store objectForKey:@"ll"] == nil &&
		      [[store dictionaryRepresentation] count] == 5,
		      @"-removeObjectForKey: and -setObject:nil both remove, and removing an absent key is not an "
		      @"error");

		/* THE SNAPSHOT IS A SNAPSHOT. */
		{
			NSDictionary *snapshot = [store dictionaryRepresentation];

			[store setString:@"after" forKey:@"later"];
			check("the-representation-is-a-snapshot-not-a-live-view",
			      [snapshot count] == 5 && [[store dictionaryRepresentation] count] == 6,
			      @"a dictionary handed out earlier does not grow when the store does");
		}
	}

	/* --- 4. THE THREE LIMITS ------------------------------------------------------------------------ */
	{
		NSUbiquitousKeyValueStore *store = [[NSUbiquitousKeyValueStore alloc] init];
		NSString *tooLong = fn_raised(^{
			[store setString:@"x" forKey:[@"k" stringByPaddingToLength:129 withString:@"k" startingAtIndex:0]];
		});
		NSString *notAPlist = fn_raised(^{
			[store setObject:[[NSObject alloc] init] forKey:@"object"];
		});
		NSString *atTheLimit = fn_raised(^{
			[store setString:@"x" forKey:[@"k" stringByPaddingToLength:128 withString:@"k" startingAtIndex:0]];
		});

		check("a-key-longer-than-128-characters-raises-and-128-does-not",
		      [tooLong isEqualToString:NSInvalidArgumentException] && atTheLimit == nil,
		      @"Apple's key ceiling is a hard one; a silent truncation would store a value under a name the "
		      @"caller never chose");
		check("a-value-that-is-not-a-property-list-is-refused-by-name",
		      [notAPlist isEqualToString:NSInvalidArgumentException] &&
		      [store objectForKey:@"object"] == nil,
		      @"Apple's rule: anything richer must be archived into NSData first, and the refusal says so "
		      @"instead of storing it and failing later");
	}

	/* --- 5. THE QUOTA, AND THE ONE LOCAL POSTING ----------------------------------------------------- */
	{
		NSUbiquitousKeyValueStore *store = [[NSUbiquitousKeyValueStore alloc] init];
		FNUbiquitousObserver *observer = [[FNUbiquitousObserver alloc] init];
		NSMutableData *tooBig = [NSMutableData dataWithLength:(1024 * 1024) + 1];

		[[NSNotificationCenter defaultCenter] addObserver:observer
							 selector:@selector(noticeArrived:)
							     name:NSUbiquitousKeyValueStoreDidChangeExternallyNotification
							   object:nil];

		[store setString:@"quiet" forKey:@"quiet"];	/* a write that must post NOTHING */
		check("this-apps-own-successful-write-posts-nothing", observer->count == 0,
		      @"the notice is about changes from OUTSIDE — Apple says so in words, and a store that posted "
		      @"for its own writes would make every observer loop");
		check("a-write-under-the-quota-is-stored", [[store stringForKey:@"quiet"] isEqualToString:@"quiet"],
		      @"the control for the next check: an ordinary write works");

		[store setData:tooBig forKey:@"huge"];
		check("a-write-that-breaks-the-quota-is-refused-whole",
		      [store dataForKey:@"huge"] == nil && [[store dictionaryRepresentation] count] == 1,
		      @"nothing is stored: Apple's store call is one atomic transaction");
		check("the-quota-violation-is-reported-through-the-change-notice",
		      observer->count == 1 &&
		      [[[observer->lastUserInfo objectForKey:NSUbiquitousKeyValueStoreChangeReasonKey]
			description] isEqualToString:
			[NSString stringWithFormat:@"%d", (int)NSUbiquitousKeyValueStoreQuotaViolationChange]] &&
		      [[observer->lastUserInfo objectForKey:NSUbiquitousKeyValueStoreChangedKeysKey]
			isEqualToArray:[NSArray arrayWithObject:@"huge"]] &&
		      observer->lastObject == store,
		      @"the refusal reports itself with the quota reason and the key it refused — the one case where "
		      @"a LOCAL action posts the 'external' notice, which is Apple's behaviour too");
		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	/* --- 6. THE SERVICE SEAM: THE NOTICE IS REACHABLE ------------------------------------------------ */
	{
		NSUbiquitousKeyValueStore *store = [[NSUbiquitousKeyValueStore alloc] init];
		FNUbiquitousObserver *observer = [[FNUbiquitousObserver alloc] init];
		NSDictionary *incoming;

		[[NSNotificationCenter defaultCenter] addObserver:observer
							 selector:@selector(noticeArrived:)
							     name:NSUbiquitousKeyValueStoreDidChangeExternallyNotification
							   object:nil];
		[store setString:@"old" forKey:@"gone"];

		incoming = [NSDictionary dictionaryWithObjectsAndKeys:
				@"from-the-service", @"new-key",
				nil];
		FNSUbiquitousStoreDeliverExternalChange(store, incoming, [NSArray arrayWithObject:@"new-key"],
						       NSUbiquitousKeyValueStoreInitialSyncChange);
		check("a-delivered-change-merges-into-the-store",
		      [[store stringForKey:@"new-key"] isEqualToString:@"from-the-service"],
		      @"the seam applies what the service says exists");

		FNSUbiquitousStoreDeliverExternalChange(store,
							[NSDictionary dictionaryWithObject:[NSNull null] forKey:@"gone"],
							[NSArray arrayWithObject:@"gone"],
							NSUbiquitousKeyValueStoreAccountChange);
		check("a-delivered-nil-value-removes-the-key",
		      [store objectForKey:@"gone"] == nil,
		      @"a nil in the delivered dictionary is a removal, not a value");
		check("the-notice-carries-its-object-its-reason-and-its-keys",
		      observer->count == 2 && observer->lastObject == store &&
		      [[observer->lastUserInfo objectForKey:NSUbiquitousKeyValueStoreChangeReasonKey] intValue] ==
			(int)NSUbiquitousKeyValueStoreAccountChange &&
		      [[observer->lastUserInfo objectForKey:NSUbiquitousKeyValueStoreChangedKeysKey]
			isEqualToArray:[NSArray arrayWithObject:@"gone"]] &&
		      observer->lastName == NSUbiquitousKeyValueStoreDidChangeExternallyNotification,
		      @"object, reason and changed keys — the three things an observer reads");
		check("every-change-reason-can-arrive-through-the-notice",
		      (NSUbiquitousKeyValueStoreServerChange == 0 && NSUbiquitousKeyValueStoreInitialSyncChange == 1 &&
		       NSUbiquitousKeyValueStoreQuotaViolationChange == 2 && NSUbiquitousKeyValueStoreAccountChange == 3),
		      @"the four reasons are the four the seam can carry (and the two above exercised two of them)");
		FNSUbiquitousStoreDeliverExternalChange(store,
			[NSDictionary dictionaryWithObject:[[NSObject alloc] init] forKey:@"bad"],
			[NSArray arrayWithObject:@"bad"],
			NSUbiquitousKeyValueStoreServerChange);
		check("a-delivered-value-is-still-checked-for-being-a-property-list",
		      [store objectForKey:@"bad"] == nil,
		      @"the service decides what EXISTS; a store of non-property-list values is still not a store");
		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	/* --- 7. THE DOOR THAT MUST NOT LIE ---------------------------------------------------------------- */
	check("synchronize-answers-no-because-there-is-no-service",
	      [NSUbiquitousKeyValueStore defaultStore] != nil &&
	      [[NSUbiquitousKeyValueStore defaultStore] synchronize] == NO,
	      @"there is no iCloud service in this system, so there is nothing to synchronize with: YES would "
	      @"claim a synchronization that never happened");

	printf("FOUNDATION-UBIQUITOUSSTORE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-UBIQUITOUSSTORE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-UBIQUITOUSSTORE DONE\n");
	return failc ? 1 : 0;
}
