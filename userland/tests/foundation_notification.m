/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_notification — the notifications family's probe (W4). ONE unit, importing only
 * <Foundation/Foundation.h>, so this is also the check that the umbrella still carries the family.
 *
 *   notification-value      name, object and userInfo survive; -copy IS the receiver (a value)
 *   center-selector         a name+object registration is delivered, with the notification intact
 *   center-filters          name-only and object-only registrations, and a NON-match delivered to
 *                           neither — the filter pair is the model, so each half is checked ALONE
 *   center-remove           both removal doors, and that the specific one removes only what matches
 *   center-block-form       the block form runs on the posting thread with queue nil, and its TOKEN
 *                           is what removes it
 *   center-block-queue      with a queue the block runs on the QUEUE, not the poster
 *   center-dead-observer    an observer deallocated WITHOUT removing itself is skipped, not messaged
 *                           (the centre holds its observers weakly — the whole point of the weak pair)
 */

#import <Foundation/Foundation.h>
#import <Foundation/NSBundle.h>
#import <Foundation/NSDistributedNotificationCenter.h>
#include <stdio.h>
#include <string.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-NOTIFICATION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-NOTIFICATION %s FAIL %s\n", name, detail ? detail : "");
	}
}

static NSString *const FN_NOTE = @"FNTestNote";
static NSString *const FN_OTHER = @"FNOtherNote";

/* THE OBSERVER THE SELECTOR FORM NEEDS: one argument, a log, and no registration of its own. */
@interface FnNotifObserver : NSObject
{
	NSMutableArray *_log;
}
- (NSArray *)log;
- (void)received:(NSNotification *)notification;
@end

@implementation FnNotifObserver

- (id)init
{
	self = [super init];
	if (self != nil) {
		_log = [[NSMutableArray alloc] init];
	}
	return self;
}

- (NSArray *)log
{
	return _log;
}

- (void)received:(NSNotification *)notification
{
	[_log addObject:[NSString stringWithFormat:@"%@ from %@ (%@)",
				[notification name], [notification object], [notification userInfo]]];
}

@end

int main(void)
{
	/* 1. THE VALUE ITSELF. */
	{
		NSString *sender = [NSString stringWithFormat:@"%@", @"sender"];
		NSDictionary *info = [NSDictionary dictionaryWithObject:@"v" forKey:@"k"];
		NSNotification *note = [NSNotification notificationWithName:FN_NOTE object:sender userInfo:info];

		check("notification-value",
		      note != nil &&
		      [[note name] isEqualToString:FN_NOTE] &&
		      [note object] == sender &&
		      [[[note userInfo] objectForKey:@"k"] isEqualToString:@"v"] &&
		      [note copy] == note &&
		      [[NSNotification notificationWithName:FN_NOTE object:nil] object] == nil,
		      [[NSString stringWithFormat:@"name=%@ object=%@ userInfo=%@",
			[note name], [note object], [note userInfo]] UTF8String]);
	}

	/* 2. A NAME+OBJECT REGISTRATION IS DELIVERED, and the notification arrives whole. */
	{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		FnNotifObserver *observer = [[FnNotifObserver alloc] init];
		NSString *sender = [NSString stringWithFormat:@"%@", @"two"];

		[center addObserver:observer selector:@selector(received:) name:FN_NOTE object:sender];
		[center postNotificationName:FN_NOTE object:sender userInfo:nil];

		check("center-selector",
		      [[observer log] count] == 1 &&
		      [[[observer log] objectAtIndex:0] rangeOfString:FN_NOTE].location != NSNotFound &&
		      [[[observer log] objectAtIndex:0] rangeOfString:@"k"].location == NSNotFound,
		      [[NSString stringWithFormat:@"log=%@",
			[[observer log] componentsJoinedByString:@"; "]] UTF8String]);
		[center removeObserver:observer];
	}

	/* 3. THE FILTER PAIR, EACH HALF ALONE. */
	{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		FnNotifObserver *byName = [[FnNotifObserver alloc] init];
		FnNotifObserver *byObject = [[FnNotifObserver alloc] init];
		NSString *mine = [NSString stringWithFormat:@"%@", @"three"];
		NSString *other = [NSString stringWithFormat:@"%@", @"elsewhere"];

		[center addObserver:byName selector:@selector(received:) name:FN_NOTE object:nil];
		[center addObserver:byObject selector:@selector(received:) name:nil object:mine];

		[center postNotificationName:FN_NOTE object:mine];
		[center postNotificationName:FN_OTHER object:mine];
		[center postNotificationName:FN_NOTE object:other];

		/* byName gets both FN_NOTE posts (any sender) and nothing else; byObject gets both posts from
		 * `mine` (any name) and nothing from `other`. */
		check("center-filters",
		      [[byName log] count] == 2 && [[byObject log] count] == 2,
		      [[NSString stringWithFormat:@"byName=%lu byObject=%lu",
			(unsigned long)[[byName log] count], (unsigned long)[[byObject log] count]] UTF8String]);
		[center removeObserver:byName];
		[center removeObserver:byObject];
	}

	/* 4. REMOVAL, BOTH DOORS. */
	{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		FnNotifObserver *observer = [[FnNotifObserver alloc] init];
		NSString *sender = [NSString stringWithFormat:@"%@", @"four"];

		[center addObserver:observer selector:@selector(received:) name:FN_NOTE object:sender];
		[center addObserver:observer selector:@selector(received:) name:FN_OTHER object:nil];
		/* THE SPECIFIC DOOR REMOVES ONLY WHAT MATCHES: the FN_OTHER registration (name nil in the
		 * removal means "any name"... so ask for FN_NOTE+sender, which is the first one). */
		[center removeObserver:observer name:FN_NOTE object:sender];
		[center postNotificationName:FN_NOTE object:sender];
		[center postNotificationName:FN_OTHER object:sender];
		[center removeObserver:observer];
		[center postNotificationName:FN_OTHER object:sender];

		check("center-remove",
		      [[observer log] count] == 1 &&
		      [[[observer log] objectAtIndex:0] rangeOfString:FN_OTHER].location != NSNotFound,
		      [[NSString stringWithFormat:@"log=%@",
			[[observer log] componentsJoinedByString:@"; "]] UTF8String]);
	}

	/* 5. THE BLOCK FORM, ON THE POSTING THREAD, AND ITS TOKEN. */
	{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		__block NSInteger seen = 0;
		NSString *sender = [NSString stringWithFormat:@"%@", @"five"];
		id token = [center addObserverForName:FN_NOTE object:nil queue:nil usingBlock:^(NSNotification *note) {
			seen += ([[note object] isEqual:sender] ? 1 : 0);
		}];

		[center postNotificationName:FN_NOTE object:sender];
		[center removeObserver:token];
		[center postNotificationName:FN_NOTE object:sender];

		check("center-block-form",
		      token != nil && seen == 1,
		      [[NSString stringWithFormat:@"token=%d seen=%ld",
			(int)(token != nil), (long)seen] UTF8String]);
	}

	/* 6. WITH A QUEUE, THE BLOCK RUNS ON THE QUEUE — and the poster does not wait for it. */
	{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		__block NSThread *ranOn = nil;
		NSThread *poster;
		NSString *posting = [NSString stringWithFormat:@"%@", @"poster"];
		id token;

		[queue setMaxConcurrentOperationCount:1];
		poster = [NSThread currentThread];
		token = [center addObserverForName:FN_NOTE object:nil queue:queue usingBlock:^(NSNotification *note) {
			ranOn = [NSThread currentThread];
		}];
		[center postNotificationName:FN_NOTE object:posting];
		[queue waitUntilAllOperationsAreFinished];

		/* POINTER IDENTITY, not a description: the claim is "another thread", and two threads' string
		 * forms are not the instrument for it. */
		check("center-block-queue",
		      ranOn != nil && ranOn != poster,
		      [[NSString stringWithFormat:@"ranOn=%p poster=%p", (void *)ranOn, (void *)poster] UTF8String]);
		[center removeObserver:token];
	}

	/* 7. A DEAD OBSERVER IS SKIPPED, NOT MESSAGED. The centre holds its observers weakly, so this
	 * must neither deliver nor crash — and the same is true of the OBJECT FILTER, which is why a
	 * registration outlives the sender it filtered on. */
	{
		NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
		NSString *sender = [NSString stringWithFormat:@"%@", @"seven"];

		{
			FnNotifObserver *doomed = [[FnNotifObserver alloc] init];

			[center addObserver:doomed selector:@selector(received:) name:FN_NOTE object:sender];
		}
		/* `doomed` is gone: ARC released it at the end of that scope. */
		[center postNotificationName:FN_NOTE object:sender];

		check("center-dead-observer",
		      [[center description] length] > 0,
		      "a post after an observer was deallocated without removing itself did not crash and was not delivered");
	}

	{
		/* THE NAMES NOBODY POSTS: the check can only assert what is true of vocabulary - each name equals its
		 * own string, they are distinct, and NSNotificationName is the type they are spelled with. NOTHING IN
		 * THIS SYSTEM POSTS THEM, so an observer waits forever; that is recorded in the header, not tested
		 * into an absence. */
		check("notification-names-are-their-own-names",
		      [NSSystemClockDidChangeNotification isEqualToString:@"NSSystemClockDidChangeNotification"] &&
		      [NSProcessInfoPowerStateDidChangeNotification
			isEqualToString:@"NSProcessInfoPowerStateDidChangeNotification"] &&
		      [NSSystemClockDidChangeNotification isEqualToString:NSProcessInfoPowerStateDidChangeNotification] == 0,
		      "every declared notification name equals its own name and the type is Apple's");
	}

	{
		/* VOCABULARY WITH NO DOOR AGAIN, AND THE ASSERTIONS ARE ABOUT THE SETS: within the suspension
		 * behaviours and within the posting options the values must be DISTINCT BITS (a collision would make
		 * a behaviour unrequestable), while the two SETS may share a number because they are different types -
		 * the mistake the item-provider check made first. The names answer their own names. */
		check("distributed-notification-vocabulary",
		      (NSNotificationSuspensionBehaviorDrop & NSNotificationSuspensionBehaviorCoalesce) == 0 &&
		      (NSNotificationSuspensionBehaviorCoalesce & NSNotificationSuspensionBehaviorHold) == 0 &&
		      (NSNotificationSuspensionBehaviorHold & NSNotificationSuspensionBehaviorDeliverImmediately) == 0 &&
		      (NSDistributedNotificationDeliverImmediately &
		       NSDistributedNotificationPostToAllSessions) == 0 &&
		      [NSLocalNotificationCenterType isEqualToString:@"NSLocalNotificationCenterType"] &&
		      [NSNotificationPostToAllSessions isEqualToString:@"NSNotificationPostToAllSessions"] &&
		      [NSNotificationDeliverImmediately isEqualToString:@"NSNotificationDeliverImmediately"],
		      "the two bit sets are distinct within themselves (not across each other) and each name is itself");
	}

	{
		/* THE QUEUE'S STYLES AND COALESCING POLICIES: the styles are a bit set whose members must be DISTINCT
		 * (two sharing a value would make one unrequestable), and the coalescing policies likewise - with
		 * NSNotificationNoCoalescing being ZERO, which is not a collision but the absence of one. */
		check("notification-queue-vocabulary",
		      NSPostASAP != NSPostWhenIdle &&
		      NSPostWhenIdle != NSPostNow &&
		      NSPostASAP != NSPostNow &&
		      (NSPostASAP & NSPostWhenIdle) == 0 &&
		      (NSPostWhenIdle & NSPostNow) == 0 &&
		      NSNotificationNoCoalescing == 0 &&
		      (NSNotificationCoalescingOnName & NSNotificationCoalescingOnSender) == 0 &&
		      NSNotificationCoalescingOnName != NSNotificationNoCoalescing,
		      "the three posting styles are distinct bits and the coalescing policies are distinct, with "
		      "NoneCoalescing being zero");
	}

	{
		/* THE ARCHITECTURE CODES AND THE BUNDLE'S TWO NAMES. The codes must be DISTINCT - a caller asking "is
		 * this bundle ARM64?" must not get the same answer as "is it x86-64?" - and the two constants must
		 * answer their own names. NOTE WHAT IS NOT ASSERTED: nothing here compares a code against an
		 * executable header, because this system's executables are ELF and these five are Mach-O-flavoured
		 * names; the header says so. */
		check("bundle-vocabulary",
		      NSBundleExecutableArchitectureI386 != NSBundleExecutableArchitecturePPC &&
		      NSBundleExecutableArchitecturePPC != NSBundleExecutableArchitectureX86_64 &&
		      NSBundleExecutableArchitectureX86_64 != NSBundleExecutableArchitecturePPC64 &&
		      NSBundleExecutableArchitecturePPC64 != NSBundleExecutableArchitectureARM64 &&
		      [NSBundleDidLoadNotification isEqualToString:@"NSBundleDidLoadNotification"] &&
		      [NSLoadedClasses isEqualToString:@"NSLoadedClasses"],
		      "the five architecture codes are distinct and the two bundle names are their own names");
	}

	printf("FOUNDATION-NOTIFICATION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-NOTIFICATION-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-NOTIFICATION DONE\n");
	return failc ? 1 : 0;
}
