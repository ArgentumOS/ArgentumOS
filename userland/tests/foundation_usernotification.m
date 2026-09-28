/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_usernotification — §62.68's acceptance: NSUserNotification + NSUserNotificationAction +
 * NSUserNotificationCenter + NSUserNotificationCenterDelegate.
 *
 * ONE unit, importing only <Foundation/Foundation.h>, plus the INTERNAL seam FNUserNotification.h for the one
 * door a user's click would reach (an activation, which this system has no service to send — see the header).
 *
 * TWO OF APPLE'S OWN SENTENCES ARE THE CHECKS HERE, because they are the whole content of the class:
 *
 *   * "The `isPresented` property ... will ALWAYS be set to true if a notification is delivered using this
 *     method" — so -deliverNotification: presents even when the delegate would refuse, which is what makes the
 *     next sentence honest rather than a contradiction (a REFUSAL only affects a notification that comes due on
 *     the scheduled path, and that rule is ours and marked so);
 *   * "Bulk setting new scheduled notifications UNSCHEDULES existing notifications" — so the setter replaces the
 *     queue, and the probe checks that the REPLACED one never arrives.
 *
 * WHAT A DELIVERY IS IN THIS SYSTEM IS STATED RATHER THAN FAKED: there is no presentation service, so "presented"
 * is a record the centre keeps and the delegate's answer is what fills it in; the probe asserts the RECORD, and
 * says so.
 */

#import <Foundation/Foundation.h>
#import <Foundation/FNUserNotification.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-USERNOTIFICATION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-USERNOTIFICATION %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* ONE TICK OF THE LOOP: the centre arms a timer on this thread's run loop, and a tick is what carries it out.
 * It WAITS rather than polling one pass, because every timer here has a floor under its date (the same hazard
 * §62.66 measured). */
static void fn_tick(void)
{
	[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
}

/* THE DELEGATE THAT RECORDS WHAT IT WAS TOLD, and the two things it can refuse: the presentation of a scheduled
 * notification (our rule) and nothing else. `_refuse` is what the probe flips. */
@interface DeliveryProbe : NSObject <NSUserNotificationCenterDelegate>
{
	NSInteger _delivered;
	NSInteger _activated;
	BOOL _refuse;
	NSUserNotificationActivationType _seen;
}
- (void)setRefuse:(BOOL)refuse;
- (NSInteger)delivered;
- (NSInteger)activated;
- (NSUserNotificationActivationType)seen;
@end

@implementation DeliveryProbe

- (void)setRefuse:(BOOL)refuse { _refuse = refuse; }
- (NSInteger)delivered { return _delivered; }
- (NSInteger)activated { return _activated; }
- (NSUserNotificationActivationType)seen { return _seen; }

- (void)userNotificationCenter:(NSUserNotificationCenter *)center
	    didDeliverNotification:(NSUserNotification *)notification
{
	_delivered++;
}

- (void)userNotificationCenter:(NSUserNotificationCenter *)center
	  didActivateNotification:(NSUserNotification *)notification
{
	_activated++;
	_seen = [notification activationType];
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center
	shouldPresentNotification:(NSUserNotification *)notification
{
	return !_refuse;
}

@end

/* A DELEGATE THAT IMPLEMENTS NONE OF THE THREE DOORS, because "a delegate that does not implement the door means
 * present" is a rule that needs a delegate proving it. */
@interface SilentDelegate : NSObject <NSUserNotificationCenterDelegate>
@end

@implementation SilentDelegate
@end

static NSAttributedString *fn_text(NSString *text)
{
	return [[NSAttributedString alloc] initWithString:text];
}

int main(void)
{
	NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
	DeliveryProbe *probe = [[DeliveryProbe alloc] init];

	check("the-default-center-is-one-shared-one",
	      center != nil && center == [NSUserNotificationCenter defaultUserNotificationCenter],
	      [NSString stringWithFormat:@"center=%p shared=%p", (void *)center,
		(void *)[NSUserNotificationCenter defaultUserNotificationCenter]]);

	/* THE CONSTANT AND THE ENUM: the default sound name exists and is a name rather than a path, and the five
	 * activation types are distinct with None at zero. */
	check("the-default-sound-name-and-the-five-activation-types",
	      [NSUserNotificationDefaultSoundName length] > 0 &&
	      NSUserNotificationActivationTypeNone == 0 &&
	      NSUserNotificationActivationTypeContentsClicked != NSUserNotificationActivationTypeActionButtonClicked &&
	      NSUserNotificationActivationTypeReplied != NSUserNotificationActivationTypeAdditionalActionClicked,
	      [NSString stringWithFormat:@"sound=%@ none=%d", NSUserNotificationDefaultSoundName,
		(int)NSUserNotificationActivationTypeNone]);

	{
		NSUserNotificationAction *action = [NSUserNotificationAction
							actionWithIdentifier:@"reply" title:@"Reply"];

		check("an-action-carries-its-identifier-and-title",
		      [[action identifier] isEqualToString:@"reply"] && [[action title] isEqualToString:@"Reply"],
		      [NSString stringWithFormat:@"%@/%@", [action identifier], [action title]]);
	}

	/* THE VALUE, AND THAT ITS COPIES ARE SNAPSHOTS: a mutable string handed in and then changed must not change
	 * what the notification says — which is what Apple's `copy` ownership means and what a value object is for. */
	{
		NSUserNotification *note = [[NSUserNotification alloc] init];
		NSMutableString *mutable = [[NSMutableString alloc] initWithString:@"First"];
		NSUserNotification *copy;

		[note setTitle:mutable];
		[note setSubtitle:@"sub"];
		[note setInformativeText:@"body"];
		[note setIdentifier:@"com.example.probe"];
		[note setUserInfo:[NSDictionary dictionaryWithObject:@"v" forKey:@"k"]];
		[note setSoundName:NSUserNotificationDefaultSoundName];
		[note setDeliveryDate:[NSDate dateWithTimeIntervalSinceNow:600]];
		[mutable appendString:@" CHANGED"];
		check("a-notifications-values-are-snapshots-not-references",
		      [[note title] isEqualToString:@"First"] &&
		      [[note subtitle] isEqualToString:@"sub"] &&
		      [[note informativeText] isEqualToString:@"body"] &&
		      [[note identifier] isEqualToString:@"com.example.probe"] &&
		      [[[note userInfo] objectForKey:@"k"] isEqualToString:@"v"] &&
		      ![[note title] isEqualToString:@"First CHANGED"],
		      [NSString stringWithFormat:@"title=%@", [note title]]);

		copy = [note copy];
		[note setTitle:@"Changed after the copy"];
		check("a-copy-is-a-value-copy",
		      copy != note && [[copy title] isEqualToString:@"First"] &&
		      [[copy subtitle] isEqualToString:@"sub"] &&
		      [[copy identifier] isEqualToString:@"com.example.probe"] &&
		      ![[copy title] isEqualToString:@"Changed after the copy"],
		      [NSString stringWithFormat:@"copy=%p original=%p copyTitle=%@", (void *)copy, (void *)note,
			[copy title]]);
	}

	/* THE FRESH STATE, which is what the read-only properties answer before anything happens. */
	{
		NSUserNotification *fresh = [[NSUserNotification alloc] init];

		check("a-notification-starts-unpresented-unactivated-and-unremote",
		      ![fresh isPresented] && ![fresh isRemote] &&
		      [fresh activationType] == NSUserNotificationActivationTypeNone &&
		      [fresh actualDeliveryDate] == nil && [fresh response] == nil &&
		      [fresh additionalActivationAction] == nil,
		      [NSString stringWithFormat:@"presented=%d remote=%d type=%d date=%@", (int)[fresh isPresented],
			(int)[fresh isRemote], (int)[fresh activationType], [fresh actualDeliveryDate]]);
	}

	/* APPLE'S OWN SENTENCE, which the delegate cannot override: a delivered notification is PRESENTED, always. */
	{
		NSUserNotification *note = [[NSUserNotification alloc] init];

		[center setDelegate:probe];
		[probe setRefuse:YES];			/* the delegate would refuse a SCHEDULED presentation */
		[center deliverNotification:note];
		check("delivering-presents-and-records-a-delivery-date",
		      [note isPresented] && [note actualDeliveryDate] != nil &&
		      [[center deliveredNotifications] containsObject:note],
		      [NSString stringWithFormat:@"presented=%d date=%@ delivered=%lu", (int)[note isPresented],
			[note actualDeliveryDate], (unsigned long)[[center deliveredNotifications] count]]);
		check("the-delegate-hears-about-a-delivery",
		      [probe delivered] == 1,
		      [NSString stringWithFormat:@"didDeliver=%ld", (long)[probe delivered]]);
	}

	/* THE SCHEDULED PATH: a notification with a future date waits, and then arrives. The refusal rule is OURS and
	 * lives here, which is why the delegate above is refusing. */
	{
		NSUserNotification *soon = [[NSUserNotification alloc] init];
		NSUserNotification *refused = [[NSUserNotification alloc] init];

		[soon setIdentifier:@"soon"];
		[probe setRefuse:NO];		/* the previous check left the refusal on; this is the path it governs */
		[soon setDeliveryDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
		[center scheduleNotification:soon];
		check("a-scheduled-notification-is-queued-before-it-is-due",
		      [[center scheduledNotifications] count] == 1 &&
		      ![[center deliveredNotifications] containsObject:soon],
		      [NSString stringWithFormat:@"scheduled=%lu delivered=%lu",
			(unsigned long)[[center scheduledNotifications] count],
			(unsigned long)[[center deliveredNotifications] count]]);
		fn_tick();
		check("a-scheduled-notification-is-delivered-when-its-date-arrives",
		      [[center scheduledNotifications] count] == 0 &&
		      [[center deliveredNotifications] containsObject:soon] && [soon isPresented],
		      [NSString stringWithFormat:@"scheduled=%lu delivered=%lu presented=%d",
			(unsigned long)[[center scheduledNotifications] count], (int)[soon isPresented],
			(int)[[center deliveredNotifications] count]]);

		/* A DELEGATE'S REFUSAL IS ABOUT PRESENTATION AND NOT ABOUT DELIVERY: the notification is still delivered
		 * (it is in the record, and the delegate was told), it simply was not presented. */
		[refused setIdentifier:@"refused"];
		[probe setRefuse:YES];		/* now the delegate refuses, and this is the path it governs */
		[refused setDeliveryDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
		[center scheduleNotification:refused];
		fn_tick();
		check("a-refused-notification-is-delivered-without-being-presented",
		      [[center deliveredNotifications] containsObject:refused] && ![refused isPresented],
		      [NSString stringWithFormat:@"delivered=%d presented=%d",
			(int)[[center deliveredNotifications] containsObject:refused], (int)[refused isPresented]]);

		/* AND A DELEGATE THAT DOES NOT IMPLEMENT THE DOOR MEANS PRESENT — the other half of the same rule. */
		{
			SilentDelegate *silent = [[SilentDelegate alloc] init];
			NSUserNotification *note = [[NSUserNotification alloc] init];

			[center setDelegate:silent];
			[note setDeliveryDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
			[center scheduleNotification:note];
			fn_tick();
			check("a-delegate-without-the-door-means-present",
			      [note isPresented],
			      [NSString stringWithFormat:@"presented=%d", (int)[note isPresented]]);
		}
		[center setDelegate:probe];
	}

	/* THE REMOVALS: a scheduled one that is taken out of the queue does not arrive and is not present; removing
	 * something absent is quiet (Apple's own sentence); and bulk-setting the queue replaces it ENTIRELY, so the
	 * notification that was there before never arrives. */
	{
		NSUserNotification *cancelled = [[NSUserNotification alloc] init];
		NSUserNotification *replaced = [[NSUserNotification alloc] init];
		NSUserNotification *kept = [[NSUserNotification alloc] init];
		NSUserNotification *absent = [[NSUserNotification alloc] init];

		[cancelled setDeliveryDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
		[center scheduleNotification:cancelled];
		[center removeScheduledNotification:cancelled];
		[center removeScheduledNotification:absent];		/* not in the queue: nothing happens */
		check("removing-a-scheduled-notification-is-quiet-and-preventing",
		      [[center scheduledNotifications] count] == 0,
		      [NSString stringWithFormat:@"scheduled=%lu", (unsigned long)[[center scheduledNotifications] count]]);
		fn_tick();
		check("a-removed-notification-never-arrives",
		      ![[center deliveredNotifications] containsObject:cancelled] && ![cancelled isPresented],
		      [NSString stringWithFormat:@"presented=%d", (int)[cancelled isPresented]]);

		[replaced setDeliveryDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
		[center scheduleNotification:replaced];
		[kept setDeliveryDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
		[center setScheduledNotifications:[NSArray arrayWithObject:kept]];
		fn_tick();
		check("bulk-setting-the-queue-unschedules-what-was-there",
		      [[center deliveredNotifications] containsObject:kept] &&
		      ![[center deliveredNotifications] containsObject:replaced],
		      [NSString stringWithFormat:@"keptDelivered=%d replacedDelivered=%d",
			(int)[[center deliveredNotifications] containsObject:kept],
			(int)[[center deliveredNotifications] containsObject:replaced]]);
	}

	/* THE ACTIVATION, through the internal seam: the type, the response and the additional action are recorded on
	 * the notification and the delegate is told — which is everything this system can honestly provide without a
	 * notification UI. */
	{
		NSUserNotification *note = [[NSUserNotification alloc] init];
		NSUserNotificationAction *action = [NSUserNotificationAction actionWithIdentifier:@"later"
											title:@"Later"];

		[center fnActivateNotification:note
				      withType:NSUserNotificationActivationTypeReplied
				      response:fn_text(@"the reply")
			      additionalAction:action];
		check("an-activation-records-its-type-response-and-action",
		      [note activationType] == NSUserNotificationActivationTypeReplied &&
		      [[[note response] string] isEqualToString:@"the reply"] &&
		      [[[note additionalActivationAction] identifier] isEqualToString:@"later"],
		      [NSString stringWithFormat:@"type=%d response=%@ action=%@", (int)[note activationType],
			[[note response] string], [[note additionalActivationAction] identifier]]);
		check("the-delegate-hears-about-an-activation",
		      [probe activated] == 1 && [probe seen] == NSUserNotificationActivationTypeReplied,
		      [NSString stringWithFormat:@"didActivate=%ld type=%d", (long)[probe activated],
			(int)[probe seen]]);
	}

	/* THE DELIVERED RECORD'S OWN REMOVALS. */
	{
		NSUserNotification *note = [[NSUserNotification alloc] init];
		NSUInteger before;

		[center deliverNotification:note];
		before = [[center deliveredNotifications] count];
		[center removeDeliveredNotification:note];
		check("a-delivered-notification-can-be-removed",
		      before > 0 && ![[center deliveredNotifications] containsObject:note],
		      [NSString stringWithFormat:@"before=%lu after=%lu", (unsigned long)before,
			(unsigned long)[[center deliveredNotifications] count]]);
		[center removeAllDeliveredNotifications];
		check("all-delivered-notifications-can-be-removed",
		      [[center deliveredNotifications] count] == 0,
		      [NSString stringWithFormat:@"after=%lu", (unsigned long)[[center deliveredNotifications] count]]);
	}

	printf("FOUNDATION-USERNOTIFICATION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-USERNOTIFICATION DONE\n");
	return failc == 0 ? 0 : 1;
}
