/*
 * foundation_distributedlock.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSDistributedLock` (§62.55) — a lock that is a file, and every check here is about WHAT A SECOND CLAIMANT SEES.
 * A lock is only a lock if somebody else is refused, so each check works through TWO OR THREE separate objects on
 * the same path rather than asking one object about itself: the first claims, the second cannot, the third unlocks
 * something it does not hold and changes nothing.
 *
 * THE THREE RULES THE HEADER STATES ARE THE THREE THINGS THIS PROBE MEASURES, because they are choices: calling
 * `-tryLock` twice on the holder is not a failure; `-unlock` releases only what the caller holds; and `-breakLock`
 * takes it from anybody, which is the door a lock left behind by a dead process needs.
 *
 * THE LOCK DATE IS READ FROM THE FILE, so the check that matters is that a NON-HOLDER can read when the lock was
 * taken — a date this object remembered would answer nil for every lock but its own.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <unistd.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if(held) {
		okc++;
		printf("FOUNDATION-DLOCK %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DLOCK %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	NSString *path = @"/System/Temporary Files/foundation-distributed-lock-probe";

	/* A LOCK LEFT BY AN EARLIER RUN IS THE THING THIS CLASS EXISTS TO SURVIVE, but a probe should not depend on
	 * one: the name is cleared first so the first claim is the first check. */
	(void)unlink([path UTF8String]);

	{
		NSDistributedLock *first = [NSDistributedLock lockWithPath:path];
		NSDistributedLock *second = [NSDistributedLock lockWithPath:path];
		NSDistributedLock *third = [NSDistributedLock lockWithPath:path];

		check("a-lock-is-made-and-does-not-start-held",
		      first != nil && [first lockDate] == nil,
		      [NSString stringWithFormat:@"a lock with a path was made and reports %@ as its date before anybody "
			@"has claimed it", [first lockDate] == nil ? @"nothing" : @"a date"]);
	covers("NSDistributedLock", "lockWithPath:");

		check("try-lock-claims-the-name-and-a-second-claimant-cannot-take-it",
		      [first tryLock] && ![second tryLock] && [second lockDate] != nil,
		      [NSString stringWithFormat:@"the first claim answered YES, the second NO, and the NON-HOLDER reads "
			@"the lock's date as %@ — the date comes off the file, so it answers for a lock this object does not "
			@"hold", [second lockDate]]);

		check("calling-try-lock-twice-on-the-holder-is-not-a-failure",
		      [first tryLock] && [first lockDate] != nil,
		      @"a caller that asks twice should not have to remember that it did");

		/* --- RELEASE, AND WHAT IT DOES NOT RELEASE ----------------------------------------------------- */
		[first unlock];
		{
			/* THE RELEASE IS MEASURED BY THE CLAIMANT THAT WAS JUST REFUSED, not by the holder asking itself:
			 * the first version of this check re-claimed the lock inside its own expression and would have
			 * broken every check after it. */
			BOOL secondTook = [second tryLock];

			check("unlocking-lets-the-other-claimant-in",
			      secondTook,
			      @"the name was released by its holder, so the claimant that was refused a moment ago takes it");
		}

		/* THE THIRD OBJECT HOLDS NOTHING, so its -unlock must change nothing: the second claimant's name has to
		 * survive it, and a fourth claimant is asked to confirm that it did. */
		{
			NSDistributedLock *fourth = [NSDistributedLock lockWithPath:path];
			BOOL fourthTook;

			[third unlock];
			fourthTook = [fourth tryLock];
			check("unlock-releases-only-what-the-caller-holds",
			      !fourthTook,
			      [NSString stringWithFormat:@"a non-holder's -unlock left the second claimant's name in "
				@"place, so a fourth claimant is still refused (%d)", (int)fourthTook]);

			/* --- AND THE DOOR A LOCK LEFT BEHIND NEEDS ------------------------------------------------- */
			[third breakLock];
			{
				NSDistributedLock *fifth = [NSDistributedLock lockWithPath:path];
				BOOL fifthTook = [fifth tryLock];

				check("break-lock-takes-it-from-anybody",
				      fifthTook,
				      @"a third object that held nothing broke the name, and the next claimant took it — which "
				      @"is the way past a lock whose holder died");
				[fifth unlock];
			}
			[fourth unlock];
		}
		[second unlock];
		[first unlock];
	}

	/* --- A LOCK WITH NO NAME IS NOT A LOCK ------------------------------------------------------------- */
	{
		NSDistributedLock *nameless = [[NSDistributedLock alloc] initWithPath:@""];

		check("a-lock-with-an-empty-path-is-refused",
		      nameless == nil,
		      @"a lock whose name is empty would be a claim on nothing, so the initializer answers nil");
	covers("NSDistributedLock", "initWithPath:");
	}

	printf("FOUNDATION-DLOCK RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DLOCK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DLOCK DONE\n");
	return failc ? 1 : 0;
}
