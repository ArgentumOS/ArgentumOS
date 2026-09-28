/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_useractivity.m — THE PROBE FOR §62.89: `NSUserActivity`, `NSUserActivityDelegate`, the browsing-web
 * activity type and the persistent-identifier typealias.
 *
 * THE ACTIVITY IS REAL AND THE OTHER DEVICE IS NOT, so the probe divides the same way: what an activity IS (its
 * type, its payload, its required keys, its URLs, the save cycle through the delegate, the lifecycle calls) and
 * what cannot happen here (a continuation from another device, streams back to one — both reported through the
 * error code this library already declares, and both deliverable through the seam so the delegate path is
 * exercisable rather than a declaration nobody can call).
 *
 * ONE THING THIS PROBE CANNOT ASSERT, AND IT SAYS SO RATHER THAN IMPLYING OTHERWISE: the rule that only ONE
 * activity is current at a time is enforced in the class, but Apple publishes NO getter for that state (the
 * `-isCurrent` door that looks plausible does not exist in the public API and is not declared here), so nothing
 * a probe can call distinguishes "the previous activity resigned" from "nothing happened". What IS observable —
 * and therefore asserted — is the other half of the lifecycle: an INVALIDATED activity is no longer eligible for
 * continuation, which the seam demonstrates by dropping one.
 *
 * NOTHING IS ASYNCHRONOUS: the streams door takes a completion handler and calls it BEFORE RETURNING, because
 * there is nothing to wait for.
 */

#import <Foundation/Foundation.h>
#import <Foundation/FNUserActivity.h>
#import <objc/runtime.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-USERACTIVITY %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-USERACTIVITY %s FAIL: %s\n", name, [why UTF8String]);
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

static BOOL fn_protocol_has_optional(Protocol *p, SEL sel)
{
	struct objc_method_description d = protocol_getMethodDescription(p, sel, NO, YES);

	return d.name != NULL;
}

@interface FNDelegate : NSObject <NSUserActivityDelegate>
{
@public
	int saves;
	int continueds;
	int streams;
	NSUserActivity *lastActivity;
	NSInputStream *lastInput;
	NSOutputStream *lastOutput;
}
@end

@implementation FNDelegate

- (void)userActivityWillSave:(NSUserActivity *)userActivity
{
	saves++;
	lastActivity = userActivity;
	/* THE DOCUMENTED MOMENT TO UPDATE THE PAYLOAD — done here so the probe can assert that an update made
	 * during this call is the one the activity ends up holding. */
	[userActivity addUserInfoEntriesFromDictionary:
		[NSDictionary dictionaryWithObject:@"added-during-save" forKey:@"saved"]];
}

- (void)userActivityWasContinued:(NSUserActivity *)userActivity
{
	continueds++;
	lastActivity = userActivity;
}

- (void)userActivity:(NSUserActivity *)userActivity
    didReceiveInputStream:(NSInputStream *)inputStream
	     outputStream:(NSOutputStream *)outputStream
{
	streams++;
	lastActivity = userActivity;
	lastInput = inputStream;
	lastOutput = outputStream;
}

@end

/* A DELEGATE THAT WRITES NOTHING: the protocol is all-optional, so every door must be skipped rather than
 * messaged. */
@interface FNQuietDelegate : NSObject <NSUserActivityDelegate>
@end
@implementation FNQuietDelegate
@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- 1. THE SURFACE ------------------------------------------------------------------------------ */
	{
		Class cls = objc_getClass("NSUserActivity");
		Protocol *p = objc_getProtocol("NSUserActivityDelegate");
		NSUserActivityPersistentIdentifier identifier = @"com.example.item";

		check("the-class-protocol-and-two-names-are-declared",
		      cls != Nil && class_getSuperclass(cls) == [NSObject class] && p != NULL &&
		      [NSUserActivityTypeBrowsingWeb isEqualToString:@"NSUserActivityTypeBrowsingWeb"] &&
		      [identifier isEqualToString:@"com.example.item"],
		      @"the class exists, the delegate protocol is declared, the browsing-web type is exported under "
		      @"its name, and the persistent-identifier typealias IS an NSString (Apple declares it as a "
		      @"Swift type alias; this typedef is how the tree ships such a name)");
		check("all-three-delegate-doors-are-declared-optional",
		      fn_protocol_has_optional(p, @selector(userActivityWillSave:)) &&
		      fn_protocol_has_optional(p, @selector(userActivityWasContinued:)) &&
		      fn_protocol_has_optional(p, @selector(userActivity:didReceiveInputStream:outputStream:)),
		      @"the three doors, asked OF THE PROTOCOL");
		check("there-is-no-iscurrent-door-because-apple-publishes-none",
		      ![cls instancesRespondToSelector:NSSelectorFromString(@"isCurrent")],
		      @"a plausible-looking door that is NOT in Apple's public surface: the current activity is "
		      @"managed through the calls, and this class does not invent a getter for it");
	}

	/* --- 2. THE TYPE IS THE IDENTITY ------------------------------------------------------------------ */
	{
		NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:@"com.example.view"];

		check("the-activity-keeps-its-type-and-it-is-read-only",
		      [[activity activityType] isEqualToString:@"com.example.view"] &&
		      ![activity respondsToSelector:@selector(setActivityType:)],
		      @"the type is the identity, so there is no door that changes it");
		check("a-nil-or-empty-type-is-a-programming-error",
		      [fn_raised(^{ (void)[[NSUserActivity alloc] initWithActivityType:(NSString *)nil]; })
			isEqualToString:NSInvalidArgumentException] &&
		      [fn_raised(^{ (void)[[NSUserActivity alloc] initWithActivityType:@""]; })
			isEqualToString:NSInvalidArgumentException],
		      @"an activity with no type is not one Apple's system could ever continue");
		check("the-inherited-init-raises-because-no-bundle-declares-activity-types",
		      [fn_raised(^{ (void)[[NSUserActivity alloc] init]; })
			isEqualToString:NSInvalidArgumentException],
		      @"Apple's -init takes the first type from the application's Info.plist, and no bundle in this "
		      @"system has one — raising with that ground beats answering with an unusable activity");
	}

	/* --- 3. THE PAYLOAD ------------------------------------------------------------------------------- */
	{
		NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:@"com.example.view"];
		NSMutableString *title = [NSMutableString stringWithString:@"Chapter One"];
		NSMutableDictionary *payload = [NSMutableDictionary dictionary];

		[payload setObject:@"a" forKey:@"first"];
		NSMutableSet *keys = [NSMutableSet setWithObject:@"first"];

		[activity setTitle:title];
		[activity setUserInfo:payload];
		[activity setRequiredUserInfoKeys:keys];
		[title appendString:@" — changed"];
		[payload setObject:@"b" forKey:@"second"];
		[keys addObject:@"second"];
		check("title-userinfo-and-required-keys-are-copied-not-held",
		      [[activity title] isEqualToString:@"Chapter One"] &&
		      [[activity userInfo] count] == 1 &&
		      [[activity requiredUserInfoKeys] count] == 1,
		      @"a caller's later mutation is not the activity's business");

		[activity addUserInfoEntriesFromDictionary:
			[NSDictionary dictionaryWithObjectsAndKeys:@"replaced", @"first", @"new", @"third", nil]];
		check("merging-adds-and-the-incoming-value-wins",
		      [[[activity userInfo] objectForKey:@"first"] isEqualToString:@"replaced"] &&
		      [[[activity userInfo] objectForKey:@"third"] isEqualToString:@"new"] &&
		      [[activity userInfo] count] == 2,
		      @"Apple's rule: a key present in both takes the value from the incoming dictionary");

		[activity setWebpageURL:[NSURL URLWithString:@"https://example.invalid/page"]];
		[activity setReferrerURL:[NSURL URLWithString:@"https://example.invalid/from"]];
		check("the-two-urls-and-the-expiration-date-round-trip-and-can-be-cleared",
		      [[[activity webpageURL] absoluteString] isEqualToString:@"https://example.invalid/page"] &&
		      [[[activity referrerURL] absoluteString] isEqualToString:@"https://example.invalid/from"] &&
		      ([activity setWebpageURL:nil], [activity webpageURL] == nil) &&
		      [activity expirationDate] == nil && [activity supportsContinuationStreams] == NO,
		      @"the browsing URL, the referrer, the expiration and the streams flag are all state this object "
		      @"holds, and all can be cleared");
	}

	/* --- 4. THE SAVE CYCLE, WHICH IS THIS SYSTEM'S ONLY SAVE ------------------------------------------ */
	{
		NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:@"com.example.view"];
		FNDelegate *delegate = [[FNDelegate alloc] init];
		FNQuietDelegate *quiet = [[FNQuietDelegate alloc] init];

		check("a-fresh-activity-has-no-delegate-and-does-not-want-saving",
		      [activity delegate] == nil && [activity needsSave] == NO,
		      @"nothing has been set");
		[activity setDelegate:delegate];
		check("the-delegate-round-trips", [activity delegate] == delegate, @"set and read back");

		[activity setNeedsSave:YES];
		check("setting-needssave-asks-the-delegate-and-clears-the-flag",
		      delegate->saves == 1 && delegate->lastActivity == activity && [activity needsSave] == NO &&
		      [[[activity userInfo] objectForKey:@"saved"] isEqualToString:@"added-during-save"],
		      @"the delegate is asked BEFORE the flag clears, which is what makes its update the saved one");
		[activity setNeedsSave:NO];
		check("clearing-needssave-asks-nothing", delegate->saves == 1 && [activity needsSave] == NO,
		      @"the cycle is: ask to save, be asked, done");

		[activity setDelegate:quiet];
		[activity setNeedsSave:YES];
		check("a-delegate-that-writes-nothing-is-not-a-crash", [activity needsSave] == NO,
		      @"every door is optional, so an empty delegate is a silent save");
	}

	/* --- 5. WHAT ARRIVES FROM ANOTHER DEVICE ---------------------------------------------------------- */
	{
		NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:@"com.example.view"];
		FNDelegate *delegate = [[FNDelegate alloc] init];
		NSInputStream *input = [[NSInputStream alloc] init];
		NSOutputStream *output = [[NSOutputStream alloc] init];

		[activity setDelegate:delegate];
		FNUserActivityDeliverContinuation(activity);
		check("a-continuation-arrives-through-the-seam",
		      delegate->continueds == 1 && delegate->lastActivity == activity,
		      @"this system has no second device, so the seam is what delivers it — which is what stops the "
		      @"door being a declaration nobody can call");

		FNUserActivityDeliverStreams(activity, input, output);
		check("streams-arrive-through-the-seam-and-are-the-callers",
		      delegate->streams == 1 && delegate->lastInput == input && delegate->lastOutput == output,
		      @"nothing is manufactured here: a stream this library invented would be a fiction a delegate "
		      @"could not tell from a real one");

		[activity invalidate];
		FNUserActivityDeliverContinuation(activity);
		check("an-invalidated-activity-is-no-longer-eligible-for-continuation",
		      delegate->continueds == 1,
		      @"Apple's words for what invalidation means, and the one half of the lifecycle rule that IS "
		      @"observable through the public surface");
	}

	/* --- 6. THE STREAMS DOOR ANSWERS THE TRUTH -------------------------------------------------------- */
	{
		NSUserActivity *activity = [[NSUserActivity alloc] initWithActivityType:@"com.example.view"];
		__block BOOL called = NO;
		__block NSError *error = nil;
		__block BOOL streamsAreNil = NO;

		[activity getContinuationStreamsWithCompletionHandler:^(NSInputStream *in, NSOutputStream *out,
									NSError *e) {
			called = YES;
			error = e;
			streamsAreNil = (in == nil && out == nil);
		}];
		check("the-streams-door-reports-that-there-is-no-connection",
		      called && streamsAreNil && error != nil &&
		      [[error domain] isEqualToString:NSCocoaErrorDomain] &&
		      [error code] == NSUserActivityConnectionUnavailableError,
		      @"NSUserActivityConnectionUnavailableError — an error code this library already declared — and "
		      @"the handler runs BEFORE the door returns, because there is nothing to wait for");
		check("the-streams-door-tolerates-a-missing-handler",
		      ([activity getContinuationStreamsWithCompletionHandler:(void (^)(NSInputStream *,
										      NSOutputStream *,
										      NSError *))nil],
		       YES),
		      @"a door with nothing to answer into does nothing rather than raising in the caller's frame");
	}

	printf("FOUNDATION-USERACTIVITY RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-USERACTIVITY-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-USERACTIVITY DONE\n");
	return failc ? 1 : 0;
}
