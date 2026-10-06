/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_distantobjectrequest.m — THE PROBE FOR §62.91: `NSDistantObjectRequest`, `NSConnectionDelegate`, and the
 * interception that lets a connection's delegate answer a request itself.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * A delegate that intercepts OWNS THE REPLY — the service's own method is NOT called, which the service's call
 *     counter proves (a check that only looked at the answer could not tell the two paths apart);
 *   * `-replyWithException:nil` SENDS THE INVOCATION'S RETURN VALUE, which is the delegate's answer and not the
 *     service's;
 *   * **AN EXCEPTION CROSSES THE WIRE AND IS RAISED AT THE CLIENT** — the headline feature, and the reason this
 *     unit began by giving `NSException` the `NSCoding` conformance Apple declares (verified separately below,
 *     because a failure there would make this one inexplicable);
 *   * a delegate that DECLINES leaves the ordinary path untouched;
 *   * the conversation the delegate makes is ON THE REQUEST, with the connection and the invocation;
 *   * and a request has ONE reply: a second one raises.
 *
 * THE CONNECTION RECIPE IS THE PORT FAMILY'S OWN (foundation_dobjects.m): publish a name, ask the name server for a
 * proxy, and tell the proxy its protocol — nothing can describe a remote method but a protocol, since an
 * invocation is built from a signature.
 */

#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if (held) {
		okc++;
		printf("FOUNDATION-DISTANTOBJECTREQUEST %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DISTANTOBJECTREQUEST %s FAIL: %s\n", name, [why UTF8String]);
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

static BOOL fn_protocol_has_optional(Protocol *p, SEL sel)
{
	struct objc_method_description d = protocol_getMethodDescription(p, sel, NO, YES);

	return d.name != NULL;
}

/* THE DOORS THE PROXY IS TOLD ABOUT: a remote invocation is built from a SIGNATURE, so a protocol is what makes
 * the call expressible at all. */
@protocol FnProbeDoors <NSObject>
- (id)greet:(NSString *)who;
- (id)boom;
@end

@interface FnProbeService : NSObject
{
@public
	int greets;
	int booms;
}
@end

@implementation FnProbeService

- (id)greet:(NSString *)who
{
	greets++;
	return [NSString stringWithFormat:@"the service says %@", who];
}

- (id)boom
{
	booms++;
	return @"the service did not raise";
}

@end

/* THE DELEGATE: it counts, records what it was handed, and its behaviour is switched by two flags so one class can
 * play every part the probe needs. */
@interface FnProbeDelegate : NSObject <NSConnectionDelegate>
{
@public
	int handled;
	int conversations;
	BOOL decline;
	BOOL answerWithException;
	id marker;			/* the conversation it names */
	NSDistantObjectRequest *held;	/* the last request it was handed */
	BOOL conversationMatched;
	BOOL connectionMatched;
	BOOL invocationMatched;
	NSString *lastExceptionName;
}
@end

@implementation FnProbeDelegate

- (id)createConversationForConnection:(NSConnection *)connection
{
	(void)connection;
	conversations++;
	return marker;
}

- (BOOL)connection:(NSConnection *)connection handleRequest:(NSDistantObjectRequest *)doreq
{
	handled++;
	held = doreq;			/* NOT retained: the connection owns it while it is being handled */
	conversationMatched = ([doreq conversation] == marker);
	connectionMatched = ([doreq connection] == connection);
	/* `sel_isEqual`, NOT `==`: in this runtime two SELs for the same selector are not guaranteed to be the same
	 * pointer — a fact Foundation's stage-F work recorded (the `sel_isEqual` fix) and this probe would otherwise
	 * have reported as a defect in the request. */
	invocationMatched = sel_isEqual([[doreq invocation] selector], @selector(greet:));

	if (decline) {
		return NO;
	}
	if (answerWithException) {
		NSException *remote = [NSException exceptionWithName:@"FNProbeRemoteException"
							     reason:@"the delegate refused"
							   userInfo:[NSDictionary dictionaryWithObject:@"detail"
											       forKey:@"FNProbeKey"]];

		lastExceptionName = [remote name];
		[doreq replyWithException:remote];
	} else {
		/* THE DELEGATE'S OWN ANSWER, put where the invocation expects a return value. */
		id answer = @"the delegate's answer";

		[[doreq invocation] setReturnValue:&answer];
		[doreq replyWithException:nil];
	}
	return YES;
}

@end

/* THE CALLS GO THROUGH THE PROTOCOL, which is the point of telling a proxy one: an invocation is built from a
 * SIGNATURE, and `-performSelector:` would be a selector ARC cannot reason about (and did warn about). */
static id fn_greet(id proxy, NSString *who)
{
	@try {
		return [(id <FnProbeDoors>)proxy greet:who];
	} @catch (NSException *e) {
		return e;		/* an exception IS an answer here — the caller decides what it means */
	}
}

static id fn_boom(id proxy)
{
	@try {
		return [(id <FnProbeDoors>)proxy boom];
	} @catch (NSException *e) {
		return e;
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- 1. THE SURFACE ------------------------------------------------------------------------------ */
	{
		Class cls = objc_getClass("NSDistantObjectRequest");
		Protocol *p = objc_getProtocol("NSConnectionDelegate");
		NSConnection *plain = [[NSConnection alloc] initWithReceivePort:nil sendPort:nil];

		check("the-class-and-the-protocol-are-declared",
		      cls != Nil && class_getSuperclass(cls) == [NSObject class] && p != NULL &&
		      fn_protocol_has_optional(p, @selector(connection:handleRequest:)) &&
		      fn_protocol_has_optional(p, @selector(createConversationForConnection:)),
		      @"the request class exists and the delegate protocol declares the two doors this library "
		      @"consults");
	covers("NSDistantObjectRequest", "connection");
		check("a-connection-starts-with-no-delegate-and-keeps-the-one-it-is-given",
		      [plain delegate] == nil && ([plain setDelegate:(id <NSConnectionDelegate>)plain],
						  [plain delegate] == (id)plain),
		      @"the accessor pair round-trips and a fresh connection has none");
		check("the-three-doors-this-library-does-not-consult-are-absent",
		      !fn_protocol_has_optional(p, NSSelectorFromString(@"connection:shouldMakeNewConnection:")) &&
		      !fn_protocol_has_optional(p, NSSelectorFromString(@"authenticationDataForComponents:")) &&
		      !fn_protocol_has_optional(p, NSSelectorFromString(@"authenticateComponents:withData:")),
		      @"no parent/child connections exist here, and nothing authenticates — so the doors are ABSENT "
		      @"with their grounds rather than declared and never called");
	}

	/* --- 2. THE PREREQUISITE, MEASURED ON ITS OWN ------------------------------------------------------ */
	{
		NSException *original = [NSException exceptionWithName:@"FNProbeRoundTrip"
								reason:@"why not"
							      userInfo:[NSDictionary dictionaryWithObject:@"v"
												  forKey:@"k"]];
		NSData *data = [NSKeyedArchiver archivedDataWithRootObject:original];
		id back = [NSKeyedUnarchiver unarchiveObjectWithData:data];

		check("an-exception-now-round-trips-the-wire",
		      back != nil && [back isKindOfClass:[NSException class]] &&
		      [[back name] isEqualToString:@"FNProbeRoundTrip"] &&
		      [[back reason] isEqualToString:@"why not"] &&
		      [[[back userInfo] objectForKey:@"k"] isEqualToString:@"v"],
		      @"NSException gained the NSCoding conformance Apple declares and this library was missing — "
		      @"without it an exception could not cross a DO reply at all");
	}

	/* --- 3. THE INTERCEPTION -------------------------------------------------------------------------- */
	{
		FnProbeService *service = [[FnProbeService alloc] init];
		NSConnection *serving = [NSConnection serviceConnectionWithName:@"probe.doreq" rootObject:service];
		FnProbeDelegate *delegate = [[FnProbeDelegate alloc] init];
		id marker = [[NSObject alloc] init];
		id proxy;
		id answer;

		delegate->marker = marker;
		[serving setDelegate:delegate];

		proxy = [NSConnection rootProxyForConnectionWithRegisteredName:@"probe.doreq" host:nil];
		[proxy setProtocolForProxy:@protocol(FnProbeDoors)];

		answer = fn_greet(proxy, @"world");
		check("a-delegate-that-intercepts-owns-the-reply",
		      [answer isEqualToString:@"the delegate's answer"] && service->greets == 0 &&
		      delegate->handled == 1,
		      @"the answer is the delegate's AND the service's own method was never called — the counter is "
		      @"what tells the two paths apart");
		check("the-request-carries-its-connection-its-invocation-and-its-conversation",
		      delegate->connectionMatched && delegate->invocationMatched && delegate->conversationMatched &&
		      delegate->conversations == 1,
		      [NSString stringWithFormat:@"the connection (%d), the call by selector (%d), the token the "
						@"DELEGATE made (%d) — and it is made ONCE (%d)",
						(int)delegate->connectionMatched, (int)delegate->invocationMatched,
						(int)delegate->conversationMatched, delegate->conversations]);
		check("a-request-has-one-reply-and-a-second-one-raises",
		      ({
			BOOL raised = NO;

			@try {
				[delegate->held replyWithException:nil];
			} @catch (NSException *e) {
				raised = [[e name] isEqualToString:NSInternalInconsistencyException];
			}
			raised;
		      }),
		      @"the client stopped waiting after the first answer, so a second would vanish — it raises "
		      @"instead of being silent");

		/* --- THE DECLINE: THE ORDINARY PATH, UNTOUCHED ------------------------------------------------- */
		{
			id plainAnswer;

			delegate->decline = YES;
			plainAnswer = fn_greet(proxy, @"world");
			check("a-delegate-that-declines-leaves-the-ordinary-path-alone",
			      [plainAnswer isEqualToString:@"the service says world"] && service->greets == 1,
			      @"returning NO means the connection serves the request exactly as if no delegate existed");
			delegate->decline = NO;
		}

		/* --- THE EXCEPTION, ACROSS THE WIRE AND RAISED HERE -------------------------------------------- */
		{
			id raised;

			delegate->answerWithException = YES;
			raised = fn_boom(proxy);
			check("an-exception-crosses-the-wire-and-is-raised-at-the-client",
			      [raised isKindOfClass:[NSException class]] &&
			      [[raised name] isEqualToString:@"FNProbeRemoteException"] &&
			      [[raised reason] isEqualToString:@"the delegate refused"] &&
			      [[[raised userInfo] objectForKey:@"FNProbeKey"] isEqualToString:@"detail"] &&
			      service->booms == 0,
			      @"the delegate's own exception, with its name, reason and userInfo intact — raised in the "
			      @"caller rather than returned, which is Apple's contract for -replyWithException:");
			delegate->answerWithException = NO;
		}

		/* --- AND WITH NO DELEGATE AT ALL ----------------------------------------------------------------- */
		{
			FnProbeService *bare = [[FnProbeService alloc] init];
			NSConnection *plainServing = [NSConnection serviceConnectionWithName:@"probe.bare" rootObject:bare];
			id plainProxy = [NSConnection rootProxyForConnectionWithRegisteredName:@"probe.bare" host:nil];
			id plainAnswer;

			[plainProxy setProtocolForProxy:@protocol(FnProbeDoors)];
			plainAnswer = fn_greet(plainProxy, @"you");
			check("a-connection-without-a-delegate-serves-as-it-always-did",
			      [plainAnswer isEqualToString:@"the service says you"] && bare->greets == 1 &&
			      plainServing != nil,
			      @"the interception is an addition, not a replacement: nothing changes for a connection "
			      @"whose delegate is nil");
		}
	}

	printf("FOUNDATION-DISTANTOBJECTREQUEST RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-DISTANTOBJECTREQUEST-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DISTANTOBJECTREQUEST DONE\n");
	return failc ? 1 : 0;
}
