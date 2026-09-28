/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_distributednotification — §62.80's acceptance: NSDistributedNotificationCenter, and with it
 * `App Support / Cross-Process Notifications`.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THE CLASS SHIPPED AND THE DECISION THAT KEPT IT OUT WAS REVERSED, the same reversal §62.23 made for
 * NSItemProvider: "the door is absent rather than stubbed" is the right rule for a DOOR and the wrong one for a
 * CLASS whose LOCAL half is real. What is real here is everything a process can do on its own - observers with a
 * SUSPENSION BEHAVIOUR each, a suspended center that drops, holds or coalesces what arrives, and the resume that
 * flushes it - and the probe drives all four behaviours rather than asserting that they exist.
 *
 * THE BUS IS THE BOUNDARY, AND TWO DOORS STATE IT BY NAME: +notificationCenterForType: answers this process's
 * center for NSLocalNotificationCenterType and REFUSES any other type, and
 * NSDistributedNotificationPostToAllSessions is refused for the same ground. Both are asserted as refusals.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-DISTRIBUTEDNOTIFICATION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DISTRIBUTEDNOTIFICATION %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* AN OBSERVER THAT RECORDS WHAT IT WAS HANDED, in order, because the ENGINE's promises are about order and count:
 * held notifications come back in arrival order and coalescing is about how many. */
@interface ObservationLog : NSObject
{
	NSMutableArray *_names;
	NSString *_lastUserInfoValue;
}
- (void)note:(NSNotification *)notification;
- (NSUInteger)count;
- (nullable NSString *)nameAt:(NSUInteger)index;
- (nullable NSString *)lastUserInfoValue;
@end

@implementation ObservationLog

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_names = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)note:(NSNotification *)notification
{
	[_names addObject:[notification name]];
	if ([notification userInfo] != nil) {
		_lastUserInfoValue = [[notification userInfo] objectForKey:@"why"];	/* ARC keeps it */
	}
}

- (NSUInteger)count { return [_names count]; }
- (nullable NSString *)nameAt:(NSUInteger)index
{
	return index < [_names count] ? [_names objectAtIndex:index] : nil;
}
- (nullable NSString *)lastUserInfoValue { return _lastUserInfoValue; }

@end

int main(void)
{
	NSDistributedNotificationCenter *center = [NSDistributedNotificationCenter defaultCenter];

	check("the-default-center-is-this-classes-own-and-the-local-type-answers-it",
	      center != nil && center == [NSDistributedNotificationCenter defaultCenter] &&
	      [NSDistributedNotificationCenter notificationCenterForType:NSLocalNotificationCenterType] == center,
	      [NSString stringWithFormat:@"center=%p local=%p", (void *)center,
		(void *)[NSDistributedNotificationCenter notificationCenterForType:NSLocalNotificationCenterType]]);

	/* THE BUS IS ABSENT, AND THE TYPE DOOR SAYS SO. */
	{
		BOOL refused = NO;

		@try {
			(void)[NSDistributedNotificationCenter notificationCenterForType:@"NSDistributedNotificationCenterType"];
		} @catch (NSException *e) {
			refused = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("an-unknown-center-type-is-refused-by-name",
		      refused,
		      [NSString stringWithFormat:@"refused=%d", (int)refused]);
	}

	/* A POST REACHES A LOCAL OBSERVER, with its user info, and the name and object FILTERS are honoured. */
	{
		ObservationLog *log = [[ObservationLog alloc] init];
		ObservationLog *byName = [[ObservationLog alloc] init];
		ObservationLog *byObject = [[ObservationLog alloc] init];
		NSString *mine = @"sender-a";
		NSString *other = @"sender-b";

		[center addObserver:log selector:@selector(note:) name:@"Chosen" object:nil];
		[center addObserver:byName selector:@selector(note:) name:@"SomethingElse" object:nil];
		[center addObserver:byObject selector:@selector(note:) name:@"Chosen" object:other];
		[center postNotificationName:@"Chosen"
				      object:mine
				    userInfo:[NSDictionary dictionaryWithObject:@"because" forKey:@"why"]
			   deliverImmediately:NO];
		check("a-post-reaches-a-local-observer-with-its-user-info",
		      [log count] == 1 && [[log nameAt:0] isEqualToString:@"Chosen"] &&
		      [[log lastUserInfoValue] isEqualToString:@"because"],
		      [NSString stringWithFormat:@"count=%lu name=%@ why=%@", (unsigned long)[log count],
			[log nameAt:0], [log lastUserInfoValue]]);
		check("the-name-and-object-filters-are-honoured",
		      [byName count] == 0 && [byObject count] == 0,
		      [NSString stringWithFormat:@"by-name=%lu by-object=%lu", (unsigned long)[byName count],
			(unsigned long)[byObject count]]);
	}

	/* THE FOUR BEHAVIOURS, DRIVEN RATHER THAN ASSERTED: a suspended center drops for a DROP observer, holds for a
	 * HOLD one and coalesces for a COALESCE one, while DELIVER_IMMEDIATELY still receives - and the resume flushes
	 * what was held, in arrival order. */
	{
		ObservationLog *drop = [[ObservationLog alloc] init];
		ObservationLog *hold = [[ObservationLog alloc] init];
		ObservationLog *coalesce = [[ObservationLog alloc] init];
		ObservationLog *immediate = [[ObservationLog alloc] init];
		NSString *sender = @"sender";

		[center addObserver:drop
			   selector:@selector(note:)
			       name:@"Quiet"
			     object:nil
		 suspensionBehavior:NSNotificationSuspensionBehaviorDrop];
		[center addObserver:hold
			   selector:@selector(note:)
			       name:@"Quiet"
			     object:nil
		 suspensionBehavior:NSNotificationSuspensionBehaviorHold];
		[center addObserver:coalesce
			   selector:@selector(note:)
			       name:@"Quiet"
			     object:nil
		 suspensionBehavior:NSNotificationSuspensionBehaviorCoalesce];
		[center addObserver:immediate
			   selector:@selector(note:)
			       name:@"Quiet"
			     object:nil
		 suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];

		[center setSuspended:YES];
		[center postNotificationName:@"Quiet" object:sender userInfo:nil deliverImmediately:NO];
		[center postNotificationName:@"Quiet" object:sender userInfo:nil deliverImmediately:NO];
		[center postNotificationName:@"Quiet" object:sender userInfo:nil deliverImmediately:NO];
		check("a-suspended-center-drops-for-a-drop-observer",
		      [drop count] == 0,
		      [NSString stringWithFormat:@"count=%lu", (unsigned long)[drop count]]);
		check("deliver-immediately-is-honoured-while-suspended",
		      [immediate count] == 3,
		      [NSString stringWithFormat:@"count=%lu (want 3)", (unsigned long)[immediate count]]);
		check("a-suspended-center-coalesces-one-per-name-for-a-coalescing-observer",
		      [coalesce count] == 0,	/* nothing yet: it is held */
		      [NSString stringWithFormat:@"count=%lu before the resume", (unsigned long)[coalesce count]]);

		[center setSuspended:NO];
		check("the-resume-flushes-what-was-held-in-arrival-order",
		      [hold count] == 3 && [[hold nameAt:0] isEqualToString:@"Quiet"] &&
		      [[hold nameAt:2] isEqualToString:@"Quiet"],
		      [NSString stringWithFormat:@"held=%lu", (unsigned long)[hold count]]);
		check("the-resume-delivers-a-coalesced-notification-once",
		      [coalesce count] == 1,
		      [NSString stringWithFormat:@"coalesced=%lu (want 1)", (unsigned long)[coalesce count]]);

		[center removeObserver:hold name:nil object:nil];
		[center removeObserver:coalesce name:nil object:nil];
		[center removeObserver:drop name:nil object:nil];
		[center removeObserver:immediate name:nil object:nil];
	}

	/* A POSTER'S IMMEDIATE REQUEST IS HONOURED TOO, and the CROSS-SESSION option is refused by name. */
	{
		ObservationLog *log = [[ObservationLog alloc] init];
		BOOL refused = NO;

		[center addObserver:log
			   selector:@selector(note:)
			       name:@"Loud"
			     object:nil
		 suspensionBehavior:NSNotificationSuspensionBehaviorHold];
		[center setSuspended:YES];
		[center postNotificationName:@"Loud"
				      object:@"sender"
				    userInfo:nil
			   deliverImmediately:YES];
		check("a-posters-immediate-request-is-honoured-while-suspended",
		      [log count] == 1,
		      [NSString stringWithFormat:@"count=%lu", (unsigned long)[log count]]);

		@try {
			[center postNotificationName:@"Loud"
					      object:@"sender"
					    userInfo:nil
					     options:NSDistributedNotificationPostToAllSessions];
		} @catch (NSException *e) {
			refused = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("the-cross-session-option-is-refused-by-name",
		      refused,
		      [NSString stringWithFormat:@"refused=%d", (int)refused]);
		[center removeObserver:log name:nil object:nil];
		[center setSuspended:NO];
	}

	/* AN OBSERVER REMOVED WHILE THE CENTER IS SUSPENDED RECEIVES NOTHING FROM THE RESUME: the flush asks the
	 * registry whether the observer is still there. */
	{
		ObservationLog *gone = [[ObservationLog alloc] init];

		[center addObserver:gone
			   selector:@selector(note:)
			       name:@"Later"
			     object:nil
		 suspensionBehavior:NSNotificationSuspensionBehaviorHold];
		[center setSuspended:YES];
		[center postNotificationName:@"Later" object:@"sender" userInfo:nil deliverImmediately:NO];
		[center removeObserver:gone name:nil object:nil];
		[center setSuspended:NO];
		check("an-observer-removed-while-suspended-gets-nothing-from-the-resume",
		      [gone count] == 0,
		      [NSString stringWithFormat:@"count=%lu", (unsigned long)[gone count]]);
	}

	printf("FOUNDATION-DISTRIBUTEDNOTIFICATION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DISTRIBUTEDNOTIFICATION DONE\n");
	return failc == 0 ? 0 : 1;
}
