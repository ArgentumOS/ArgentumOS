/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_urlsession, unit of 1 — W7 slice 2c's session half, row 2: NSURLSession and the task model.
 * docs/design/foundation-plan.md W7; docs/design/foundation-transport-plan.md §4, slice 2c.
 *
 * ONE unit, importing only <Foundation/Foundation.h>, and NOTHING TRANSFERS: this row is the session and
 * its tasks as a MODEL — identity, the configuration snapshot, the task state machine and the enumeration.
 * Running a task through FNCURLURLProtocol is the next row, and the probe asserts the model rather than
 * pretending to exercise a transfer.
 *
 * THE TWO CHECKS THAT EARN THEIR PLACE:
 *   session-snapshots-its-configuration  a session takes a COPY of the configuration it is made from, so
 *        a caller still editing that configuration cannot reach a running session. It is the same
 *        snapshot discipline the request headers and the cached response keep, and it is the one place
 *        this row's behaviour is observable rather than declarative;
 *   task-cancel-completes-and-records-the-error  the cancel simplification is ASSERTED rather than
 *        described: no transfer is running, so there is no Canceling period, and the task goes to
 *        Completed carrying NSURLErrorCancelled (-999). When the execution row lands, this check is what
 *        will have to change, which is exactly why it is written down.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-URLSESSION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLSESSION %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static NSURL *fn_url(NSString *string)
{
	return [NSURL URLWithString:string];
}

/* A DELEGATE THAT DOES NOTHING, so that "the session keeps what it was given" is asserted with a real
 * object rather than with nil in both positions. */
@interface FnSessionDelegate : NSObject <NSURLSessionDelegate>
@end
@implementation FnSessionDelegate
- (void)URLSession:(NSURLSession *)session didBecomeInvalidWithError:(NSError *)error { (void)error; }
@end

int main(void)
{
	/* --- THE SHARED SESSION IS ONE SESSION -------------------------------------------------------- */
	{
		NSURLSession *first = [NSURLSession sharedSession];
		NSURLSession *second = [NSURLSession sharedSession];

		check("shared-session-is-a-singleton",
		      first != nil && first == second &&
		      [[first configuration] timeoutIntervalForRequest] == 60.0 &&
		      [first delegate] == nil &&
		      [first delegateQueue] == nil,
		      @"+sharedSession answers the same object every time, built from the default configuration");
	}

	/* --- THE CONFIGURATION IS SNAPSHOTTED -------------------------------------------------------- */
	{
		NSURLSessionConfiguration *configuration =
			[NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSession *session;

		session = [NSURLSession sessionWithConfiguration:configuration];

		/* THE CALLER KEEPS EDITING the configuration it handed over... */
		[configuration setTimeoutIntervalForRequest:1.0];
		[configuration setHTTPShouldUsePipelining:YES];

		check("session-snapshots-its-configuration",
		      [[session configuration] timeoutIntervalForRequest] == 60.0 &&
		      [[session configuration] HTTPShouldUsePipelining] == NO &&
		      [[session configuration] class] == [NSURLSessionConfiguration class],
		      @"a session takes a COPY of its configuration, so later edits cannot reach it");
	}

	/* --- IT KEEPS ITS DELEGATE AND ITS QUEUE ----------------------------------------------------- */
	{
		FnSessionDelegate *delegate = [[FnSessionDelegate alloc] init];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		NSURLSession *session =
			[NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]
						      delegate:delegate
						 delegateQueue:queue];

		check("session-keeps-its-delegate-and-queue",
		      [session delegate] == delegate && [session delegateQueue] == queue,
		      @"the three-argument door stores the delegate and the queue it was given");
	}

	/* --- THE TASK STATE MACHINE ------------------------------------------------------------------ */
	{
		NSURLSession *session =
			[NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
		NSURLSessionDataTask *task =
			[session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/a")]];
		NSURLSessionDataTask *other =
			[session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/b")]];

		/* A NEW TASK IS SUSPENDED, which is what makes -resume an explicit act. */
		{
			int suspended = [task state] == NSURLSessionTaskStateSuspended;

			[task resume];
			{
				int running = [task state] == NSURLSessionTaskStateRunning;

				[task suspend];
				{
					int back = [task state] == NSURLSessionTaskStateSuspended;

					[task resume];
					check("task-starts-suspended-and-resumes",
					      suspended && running && back &&
					      [task state] == NSURLSessionTaskStateRunning,
					      @"a new task is suspended; resume, suspend and resume again move it");
				}
			}
		}

		/* AND CANCEL ENDS IT — with the error Apple's own cancel carries. */
		{
			[task cancel];
			check("task-cancel-completes-and-records-the-error",
			      [task state] == NSURLSessionTaskStateCompleted &&
			      [task error] != nil &&
			      [[task error] code] == -999 &&
			      [[[task error] domain] isEqual:@"NSURLErrorDomain"],
			      @"no transfer runs in this row, so a cancel goes straight to Completed with -999");

			[task resume];
			check("a-completed-task-does-not-resume",
			      [task state] == NSURLSessionTaskStateCompleted,
			      @"a task cannot be dragged back into life after it completes");
		}

		check("task-identifiers-are-unique-and-in-order",
		      [task taskIdentifier] != 0 &&
		      [other taskIdentifier] != 0 &&
		      [task taskIdentifier] != [other taskIdentifier] &&
		      [other taskIdentifier] > [task taskIdentifier],
		      @"identifiers come from the session, in creation order");

		/* THE REQUEST AS A VALUE, asserted against an EQUAL request rather than against itself: the task
		 * answers the request it was made from, which is the same value equality the pair already
		 * defines. (The local is narrowed first because a nullable pointer cannot be an argument to a
		 * nonnull door.) */
		{
			NSURLRequest *original = [task originalRequest];
			NSURLRequest *madeFrom = [NSURLRequest requestWithURL:fn_url(@"https://example.com/a")];

			check("task-carries-its-request-and-description",
			      original != nil &&
			      [original isEqual:madeFrom] &&
			      [task priority] == 0.5f &&
			      [task countOfBytesReceived] == 0,
			      @"the task carries the request it was made from (as a value), priority is Apple's "
			      @"default and no bytes exist yet");
		}

		[task setTaskDescription:@"a label"];
		check("task-description-round-trips",
		      [[task taskDescription] isEqual:@"a label"],
		      @"a caller's own label is carried and never interpreted");
	}

	/* --- THE SESSION REPORTS ITS TASKS ------------------------------------------------------------ */
	{
		NSURLSession *session =
			[NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
		__block NSUInteger dataCount = 0;
		__block NSUInteger otherCount = 0;

		(void)[session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/1")]];
		(void)[session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/2")]];

		[session getTasksWithCompletionHandler:^(NSArray *data, NSArray *upload, NSArray *download) {
			dataCount = [data count];
			otherCount = [upload count] + [download count];
		}];

		check("session-reports-its-tasks",
		      dataCount == 2 && otherCount == 0,
		      @"the data array holds what the session made, and the other two are ALWAYS empty here");
	}

	/* --- INVALIDATION ---------------------------------------------------------------------------- */
	{
		NSURLSession *session =
			[NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
		NSURLSessionDataTask *task =
			[session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/x")]];

		[session invalidateAndCancel];
		check("invalidate-and-cancel-cancels-the-tasks",
		      [task state] == NSURLSessionTaskStateCompleted &&
		      [session dataTaskWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/y")]]
			      == nil,
		      @"invalidation cancels the tasks and refuses new ones rather than handing back a dead task");
	}

	/* --- THE AUDITED INVENTORY -------------------------------------------------------------------- */
	{
		static const char *classSelectors[] = {
			"sharedSession", "sessionWithConfiguration:",
			"sessionWithConfiguration:delegate:delegateQueue:", NULL
		};
		static const char *sessionSelectors[] = {
			"configuration", "delegate", "delegateQueue", "sessionDescription",
			"setSessionDescription:", "dataTaskWithRequest:", "dataTaskWithURL:",
			"getTasksWithCompletionHandler:", "invalidateAndCancel",
			"finishTasksAndInvalidate", NULL
		};
		static const char *taskSelectors[] = {
			"taskIdentifier", "originalRequest", "currentRequest", "response", "error",
			"taskDescription", "setTaskDescription:", "priority", "setPriority:", "state",
			"countOfBytesReceived", "countOfBytesExpectedToReceive",
			"resume", "suspend", "cancel", NULL
		};
		/* REFUSED, EACH BECAUSE THE CLASS OR THE ROW BEHIND IT IS NOT SHIPPED: the completion-handler and
		 * download/upload/stream/websocket doors (execution and their own ledger rows), the challenge
		 * member and factories (NSURLAuthenticationChallenge is its own family) and the coder doors. */
		static const char *excluded[] = {
			"dataTaskWithRequest:completionHandler:", "dataTaskWithURL:completionHandler:",
			"downloadTaskWithRequest:", "uploadTaskWithRequest:fromData:",
			"streamTaskWithHostName:port:", "webSocketTaskWithURL:",
			"URLSession:didReceiveChallenge:completionHandler:",
			"URLSession:task:didReceiveChallenge:completionHandler:",
			"initWithCoder:", "encodeWithCoder:", NULL
		};
		NSURLSession *session = [NSURLSession sharedSession];
		NSURLSessionDataTask *task =
			[[NSURLSessionDataTask alloc] fnInitWithRequest:[NSURLRequest requestWithURL:fn_url(@"https://example.com/i")]
							     identifier:999];
		FnSessionDelegate *delegate = [[FnSessionDelegate alloc] init];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSURLSession respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLSESSION missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; sessionSelectors[i] != NULL; i++) {
			if (![session respondsToSelector:sel_registerName(sessionSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLSESSION missing -%s\n", sessionSelectors[i]);
			}
		}
		for (i = 0; taskSelectors[i] != NULL; i++) {
			if (![task respondsToSelector:sel_registerName(taskSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-URLSESSION missing -%s (task)\n", taskSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([session respondsToSelector:sel_registerName(excluded[i])] ||
			    [task respondsToSelector:sel_registerName(excluded[i])] ||
			    [delegate respondsToSelector:sel_registerName(excluded[i])] ||
			    [NSURLSession respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-URLSESSION present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("session-api-inventory", complete,
		      @"the audited inventory: every owed selector exists, and nothing listed as excluded does");
	}

	printf("FOUNDATION-URLSESSION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLSESSION-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLSESSION DONE\n");
	return failc ? 1 : 0;
}
