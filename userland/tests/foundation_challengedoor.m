/*
 * foundation_challengedoor.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE TWO DELEGATE DOORS, driven rather than described. Three delegates are built on purpose - one that
 * implements both doors, one that implements only the session's, and one that implements neither - because
 * the interesting properties are WHICH door is asked and WHAT HAPPENS WHEN NONE IS.
 */
#import <Foundation/Foundation.h>
/* SO A DECLARATION CAN BE ASSERTED WHERE DECLARATIONS LIVE (§53, the §50.3 pattern): a protocol's own method
 * list is only readable from the runtime, and these two doors were DECLARED NOWHERE while the loop that uses
 * them passed every behavioural check. */
#import <objc/runtime.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-CHALLENGEDOOR %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CHALLENGEDOOR %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static NSURLAuthenticationChallenge *fn_challenge(void)
{
	NSURLProtectionSpace *space = [[NSURLProtectionSpace alloc]
		initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
		       realm:@"restricted" authenticationMethod:NSURLAuthenticationMethodHTTPBasic];

	return [[NSURLAuthenticationChallenge alloc]
			initWithProtectionSpace:space proposedCredential:nil
				 previousFailureCount:0 failureResponse:nil error:nil sender:nil];
}

/* BOTH DOORS, so the ORDER can be observed: it records which one ran. */
@interface FNBoth : NSObject <NSURLSessionTaskDelegate>
{
	@public
	int sessionDoor, taskDoor;
}
@end

@implementation FNBoth
- (void)URLSession:(NSURLSession *)session
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))handler
{
	sessionDoor++;
	handler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}
- (void)URLSession:(NSURLSession *)session
	      task:(NSURLSessionTask *)task
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))handler
{
	taskDoor++;
	handler(NSURLSessionAuthChallengeUseCredential,
		[NSURLCredential credentialWithUser:@"kyle" password:@"secret"
					persistence:NSURLCredentialPersistenceNone]);
}
@end

/* ONLY THE SESSION DOOR. */
@interface FNSessionOnly : NSObject <NSURLSessionDelegate>
{
	@public
	int sessionDoor;
}
@end

@implementation FNSessionOnly
- (void)URLSession:(NSURLSession *)session
didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
 completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))handler
{
	sessionDoor++;
	handler(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
}
@end

/* NEITHER DOOR. */
@interface FNSilent : NSObject <NSURLSessionDelegate>
@end

@implementation FNSilent
@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	{
		FNBoth *both = [[FNBoth alloc] init];
		FNSessionOnly *session = [[FNSessionOnly alloc] init];
		FNSilent *silent = [[FNSilent alloc] init];
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSession *s1 = [NSURLSession sessionWithConfiguration:configuration delegate:both delegateQueue:nil];
		NSURLSession *s2 = [NSURLSession sessionWithConfiguration:configuration delegate:session delegateQueue:nil];
		NSURLSession *s3 = [NSURLSession sessionWithConfiguration:configuration delegate:silent delegateQueue:nil];
		NSURLSessionTask *task = [[NSURLSessionTask alloc]
			fnInitWithRequest:[NSMutableURLRequest requestWithURL:
						[NSURL URLWithString:@"https://example.com/"]]
			       identifier:1];
		__block NSInteger disposition = -1;
		__block NSString *credentialUser = nil;

		[s1 fnAskForCredentialForTask:task challenge:fn_challenge()
		    completionHandler:^(NSURLSessionAuthChallengeDisposition d, NSURLCredential *c) {
			disposition = (NSInteger)d;
			credentialUser = [c user];
		}];
		check("the-task-door-is-asked-first",
		      both->taskDoor == 1 && both->sessionDoor == 0,
		      @"the task-level delegate is the more specific one, so it decides");
		check("and-its-answer-comes-back",
		      disposition == NSURLSessionAuthChallengeUseCredential,
		      @"the disposition the delegate chose is the one the caller gets");
		check("with-the-credential-it-handed-over",
		      [credentialUser isEqualToString:@"kyle"],
		      @"the credential travels through the handler");

		[s2 fnAskForCredentialForTask:task challenge:fn_challenge()
		    completionHandler:^(NSURLSessionAuthChallengeDisposition d, NSURLCredential *c) {
			disposition = (NSInteger)d;
			credentialUser = nil;
		}];
		check("the-session-door-is-the-fallback",
		      session->sessionDoor == 1 &&
		      disposition == NSURLSessionAuthChallengeCancelAuthenticationChallenge,
		      @"a delegate with only the session door is still asked, and its answer is used");

		disposition = -1;
		[s3 fnAskForCredentialForTask:task challenge:fn_challenge()
		    completionHandler:^(NSURLSessionAuthChallengeDisposition d, NSURLCredential *c) {
			(void)c;
			disposition = (NSInteger)d;
		}];
		check("no-door-means-default-without-waiting",
		      disposition == NSURLSessionAuthChallengePerformDefaultHandling,
		      @"a delegate that implements neither door is not waited for");

		check("the-disposition-values-are-ours-and-ordered",
		      NSURLSessionAuthChallengeUseCredential == 0 &&
		      NSURLSessionAuthChallengeRejectProtectionSpace == 3,
		      @"UseCredential is 0 so a zeroed decision is not the opposite of intent");

		/* AND THE TWO DOORS ARE DECLARED, WHICH IS THE WHOLE OF §53: every check above passed while BOTH
		 * selectors existed in NO HEADER AT ALL - the session dispatches through an `(id)` cast, which
		 * compiles whether or not a declaration exists - so the assertion has to read the PROTOCOL. A
		 * -respondsToSelector: on a class cannot see the difference, because a class that implements the
		 * method answers YES either way, and that is exactly how this defect survived its own tests. */
		{
			struct objc_method_description sessionDoor =
				protocol_getMethodDescription(@protocol(NSURLSessionDelegate),
					@selector(URLSession:didReceiveChallenge:completionHandler:),
					NO /* optional */, YES /* instance */);
			struct objc_method_description taskDoor =
				protocol_getMethodDescription(@protocol(NSURLSessionTaskDelegate),
					@selector(URLSession:task:didReceiveChallenge:completionHandler:),
					NO, YES);

			check("both-doors-are-declared-by-the-protocols",
			      sessionDoor.name != NULL && taskDoor.name != NULL,
			      @"the session's door and the task's door are DECLARED, which is what a caller can read");
		}
	}

	printf("FOUNDATION-CHALLENGEDOOR RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-CHALLENGEDOOR-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CHALLENGEDOOR DONE\n");
	return failc ? 1 : 0;
}
