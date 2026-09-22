/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLSessionTask — A UNIT OF WORK, AND ITS STATE. docs/design/foundation-plan.md W7; the transport
 * plan's §4, slice 2c.
 *
 * THE TASK IS A MODEL HERE, NOT A TRANSFER: it carries an identity, the request it was made from, a
 * description, a priority and a state, and it can be resumed, suspended and cancelled. NOTHING MOVES
 * YET — the execution row of this slice is what runs a task through the bridge — and the header says
 * that rather than leaving a caller to discover it.
 *
 * THE STATE MACHINE IS THE PART WORTH READING, because it is the part that survives execution:
 *
 *   * A NEWLY CREATED TASK IS SUSPENDED. That is Apple's rule and it is why `-resume` exists as an
 *     explicit act rather than a side effect of creation; `resume-refuses-what-it-cannot-run` and
 *     `task-starts-suspended-and-resumes` pin both ends of it.
 *   * `-cancel` MOVES A TASK TO Completed IN THIS SLICE, and that is a simplification stated rather than
 *     hidden: with no transfer running there is no Canceling period to pass through, so the task goes
 *     straight to the state a cancel ends in. When the execution row lands, the Canceling state gets its
 *     real meaning and this comment changes with it.
 *   * A DELEGATE THE SESSION WAS NOT GIVEN IS NOT A FAILURE. Apple's delegate parameters are nullable
 *     throughout, and a task with no delegate and no completion handler simply reports to nobody.
 *
 * NSURLSessionTaskState'S VALUES ARE OURS UNDER §11.6.1 D2, as every enum's in this library are: Apple
 * publishes the case names, and the probe pins the numbers.
 *
 * REFUSED BY NAME: the coder doors (every class here refuses them) and `-init` — a task is created BY a
 * session, so it has no public initializer of its own and the inventory asserts that it has none.
 */

#ifndef FOUNDATION_NSURLSESSIONTASK_H
#define FOUNDATION_NSURLSESSIONTASK_H

#import <Foundation/NSObject.h>

@class NSError;
@class NSString;
@class NSURLRequest;
@class NSURLResponse;

NS_ASSUME_NONNULL_BEGIN

/* WHERE A TASK IS IN ITS LIFE. The values are ours (D2); the case names are Apple's. */
typedef NS_ENUM(NSInteger, NSURLSessionTaskState) {
	NSURLSessionTaskStateRunning = 0,
	NSURLSessionTaskStateSuspended = 1,
	NSURLSessionTaskStateCanceling = 2,
	NSURLSessionTaskStateCompleted = 3,
};

@interface NSURLSessionTask : NSObject
{
	NSUInteger _taskIdentifier;
	NSURLRequest *_originalRequest;
	NSURLRequest *_currentRequest;
	NSURLResponse *_response;
	NSError *_error;
	NSString *_taskDescription;
	float _priority;
	NSURLSessionTaskState _state;
	int64_t _countOfBytesReceived;
	int64_t _countOfBytesExpectedToReceive;
}

/* FNX: THE SESSION'S OWN DOOR, and the reason -init is not public here: a task is created BY a session.
 * This library's convention for a first-party door on a public class is the `fn` prefix (see
 * +[NSURLProtocol fnProtocolClassForRequest:]). */
- (instancetype)fnInitWithRequest:(NSURLRequest *)request identifier:(NSUInteger)identifier;

/* UNIQUE WITHIN THE SESSION THAT MADE IT, and assigned in creation order — which is what makes it usable
 * as a key, the only thing Apple promises about it. */
@property (readonly) NSUInteger taskIdentifier;
@property (nullable, readonly, copy) NSURLRequest *originalRequest;
@property (nullable, readonly, copy) NSURLRequest *currentRequest;
@property (nullable, readonly, copy) NSURLResponse *response;
@property (nullable, readonly, copy) NSError *error;
/* A caller's own label, readwrite and never interpreted here. */
@property (nullable, copy) NSString *taskDescription;
/* Apple's range is 0.0–1.0 with a default of 0.5; CARRIED, with nothing scheduling on it yet. */
@property float priority;
@property (readonly) NSURLSessionTaskState state;
/* 0 until a transfer runs: there is no transfer in this row, and these say so by staying zero. */
@property (readonly) int64_t countOfBytesReceived;
@property (readonly) int64_t countOfBytesExpectedToReceive;

- (void)resume;
- (void)suspend;
- (void)cancel;

@end

/* THE DATA TASK IS THE ONE THAT CARRIES A REQUEST, which is the only thing it adds here. */
@interface NSURLSessionDataTask : NSURLSessionTask

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSURLSESSIONTASK_H */
