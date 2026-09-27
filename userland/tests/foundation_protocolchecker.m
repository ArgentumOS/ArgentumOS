/*
 * foundation_protocolchecker.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSProtocolChecker` (§62.55) — a proxy that answers only for its protocol.
 *
 * THE PROPERTY THAT MATTERS IS NOT "A BAD CALL RAISES" BUT "A BAD CALL NEVER REACHES THE TARGET". A checker that
 * forwarded everything and let the runtime complain about the result would pass a check that only looked for an
 * exception, so every refusal here is measured TWICE: the call is refused, AND the target's own state shows the
 * method never ran. The target keeps a counter for the secret door precisely so that "the filter worked" is a
 * measurement rather than an inference.
 *
 * THE FILTER IS ASKED OF THE PROTOCOL, NOT OF THE TARGET, and the two checks that separate them are the optional
 * door the target DOES implement (forwarded) and the optional door it does NOT (refused) — passing both means the
 * class is asking the protocol's own description rather than "does the target respond".
 *
 * THE CAST IN THE REFUSAL CHECK IS ONLY TO MAKE THE SELECTOR KNOWN TO THE COMPILER. The object is the checker, and
 * what the checker does with the call is the entire point.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-PROTOCOLCHECKER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PROTOCOLCHECKER %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* ---- a protocol with a required door, an optional one, and a target with a door OUTSIDE it ---- */

@protocol FnCheckerDoors <NSObject>
- (int)ping;
- (void)bump;
@optional
- (int)optionalDoor;
@end

@interface FnCheckerTarget : NSObject <FnCheckerDoors>
{
	int _bumps;
	int _callsToSecret;
}
- (int)ping;
- (void)bump;
- (int)optionalDoor;
- (int)secret;			/* DECLARED HERE AND NOT IN THE PROTOCOL: the whole point of the probe */
- (int)bumps;
- (int)callsToSecret;
@end

@implementation FnCheckerTarget

- (int)ping { return 7; }
- (void)bump { _bumps++; }
- (int)optionalDoor { return 13; }
- (int)secret { _callsToSecret++; return 99; }
- (int)bumps { return _bumps; }
- (int)callsToSecret { return _callsToSecret; }

@end

/* The same protocol, a target that does NOT implement the optional door. */
@interface FnNarrowTarget : NSObject <FnCheckerDoors>
- (int)ping;
@end

@implementation FnNarrowTarget

- (int)ping { return 21; }

@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	FnCheckerTarget *target = [[FnCheckerTarget alloc] init];
	id checker = [NSProtocolChecker protocolCheckerWithTarget:target protocol:@protocol(FnCheckerDoors)];
	NSProtocolChecker *direct = [[NSProtocolChecker alloc] initWithTarget:target
								    protocol:@protocol(FnCheckerDoors)];

	/* --- WHAT A CHECKER IS MADE OF --------------------------------------------------------------------- */
	check("a-checker-is-made-for-a-target-and-a-protocol",
	      checker != nil && direct != nil && [direct target] == target &&
	      [direct protocol] == @protocol(FnCheckerDoors),
	      @"the factory and the initializer both answer a checker that reports the target and the protocol it "
	      @"was made for");

	/* --- A DOOR THE PROTOCOL DECLARES REACHES THE TARGET ----------------------------------------------- */
	{
		int pinged = [(id<FnCheckerDoors>)checker ping];

		[(id<FnCheckerDoors>)checker bump];
		check("a-selector-the-protocol-declares-reaches-the-target",
		      pinged == 7 && [target bumps] == 1,
		      [NSString stringWithFormat:@"ping answered %d (the target's 7) and the target's own counter moved "
			@"to %d", pinged, [target bumps]]);
	}

	/* --- AND SO DOES AN OPTIONAL DOOR THE TARGET IMPLEMENTS -------------------------------------------- */
	{
		int answered = [(id<FnCheckerDoors>)checker optionalDoor];

		check("an-optional-selector-the-target-implements-reaches-it",
		      answered == 13,
		      [NSString stringWithFormat:@"the optional door answered %d, so the filter asks the protocol for "
			@"OPTIONAL selectors too rather than treating everything undeclared-required as refused",
			answered]);
	}

	/* --- A DOOR OUTSIDE THE PROTOCOL IS REFUSED, AND IS NEVER INVOKED ---------------------------------- */
	{
		BOOL raised = NO;

		@try {
			/* the cast makes the SELECTOR visible to the compiler; the receiver is the checker */
			(void)[(FnCheckerTarget *)checker secret];
		} @catch (NSException *exception) {
			raised = YES;
		}
		check("a-selector-outside-the-protocol-is-refused-and-never-invoked",
		      raised && [target callsToSecret] == 0,
		      [NSString stringWithFormat:@"the call %@ and the target's secret door ran %d time(s) — a refusal "
			@"that still invoked the target would be no filter at all",
			raised ? @"raised" : @"DID NOT RAISE", [target callsToSecret]]);
	}

	/* --- THE BOUNDARY IS THE PROTOCOL, NOT THE TARGET --------------------------------------------------- */
	{
		check("responds-to-selector-follows-the-protocol-not-the-target",
		      [checker respondsToSelector:@selector(ping)] &&
		      [checker respondsToSelector:@selector(optionalDoor)] &&
		      ![checker respondsToSelector:@selector(secret)] &&
		      [target respondsToSelector:@selector(secret)],
		      @"the checker answers for the two protocol doors and denies the one the target has and the "
		      @"protocol does not");
	}

	/* --- A TARGET THAT LACKS AN OPTIONAL DOOR IS REFUSED FOR IT ----------------------------------------- */
	{
		FnNarrowTarget *narrow = [[FnNarrowTarget alloc] init];
		id narrowChecker = [NSProtocolChecker protocolCheckerWithTarget:narrow
								       protocol:@protocol(FnCheckerDoors)];
		BOOL raised = NO;

		@try {
			(void)[(id<FnCheckerDoors>)narrowChecker optionalDoor];
		} @catch (NSException *exception) {
			raised = YES;
		}
		check("an-optional-door-the-target-does-not-implement-is-refused",
		      [(id<FnCheckerDoors>)narrowChecker ping] == 21 && raised &&
		      ![narrowChecker respondsToSelector:@selector(optionalDoor)],
		      @"a checker is a view of what the target can ACTUALLY do: the required door answers 21 and the "
		      @"optional one the target lacks is refused");
	}

	printf("FOUNDATION-PROTOCOLCHECKER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-PROTOCOLCHECKER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-PROTOCOLCHECKER DONE\n");
	return failc ? 1 : 0;
}
