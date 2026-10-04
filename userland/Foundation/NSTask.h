/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTask — A SUBPROCESS. docs/design/foundation-plan.md W6d and §45.
 *
 * IT IS THE ONE CLASS IN THIS LIBRARY THAT OWNS A PROCESS, so three things about it are unlike anything
 * else here:
 *
 *   * IT RE-EXECS, so what it needs from the system is `fork(2)` and `execve(2)` — which this kernel has,
 *     and which every other class in this unit only reads through a descriptor.
 *   * IT NEEDS A SECOND THREAD, and that is not an implementation detail: Apple's contract for
 *     `-terminationHandler` and `NSTaskDidTerminateNotification` is that both happen WHEN THE TASK ENDS,
 *     with nobody calling `-waitUntilExit`. So a launched task starts a REAPER thread that `waitpid(2)`s,
 *     records the status, posts the notification and runs the handler. `-waitUntilExit` then waits on a
 *     condition for that thread's answer rather than reaping a second time — one reaper, one status, and
 *     a status that survives being read twice.
 *   * ONE INSTANCE RUNS ONCE. Apple: *"You can only run the subprocess once per instance."* A second
 *     `-launchAndReturnError:` answers NO with an error rather than forking twice.
 *
 * THE DEPRECATED HALF IS STRUCK, AND IT IS THE HALF EVERYOONE REMEMBERS. §11.5 removes four names, and the
 * live spelling of each is beside it here: `+launchedTaskWithLaunchPath:arguments:` (→ the
 * `…WithExecutableURL:` form), `-launch` (→ `-launchAndReturnError:`), `-launchPath`/`-setLaunchPath:`
 * (→ `-executableURL`), and `-currentDirectoryPath`/`-setCurrentDirectoryPath:` (→ `-currentDirectoryURL`).
 * The probe's inventory names each one with that reason.
 *
 * TWO CONTRACTS ARE APPLE'S OWN SENTENCES AND ARE PINNED RATHER THAN PARAPHRASED: passing `nil` arguments
 * to the static launcher **raises NSInvalidArgumentException**, and the child's standard input, output and
 * error are set as an `NSPipe` (the other end becomes the caller's) or an `NSFileHandle`.
 *
 * AND TWO THINGS ARE OURS, STATED HERE BECAUSE APPLE PUBLISHES NEITHER:
 *
 *   * `-terminationStatus` ANSWERS `WEXITSTATUS` WHEN THE TASK EXITED and the SIGNAL NUMBER WHEN IT DID
 *     NOT. Apple's page says only "the exit status the executable returns", and `-terminationReason` is
 *     the API that tells the two apart, so the value for the signal case has to be chosen by somebody.
 *   * `-interrupt` and `-terminate` SIGNAL THE CHILD'S PROCESS GROUP, which is how Apple's "and all of its
 *     subtasks" is honoured: the child is put in a new group (`setpgid(0, 0)`) before it execs, and the
 *     signal goes to `-pid`. A child that spawns children of its own therefore dies with them.
 *
 * NOT DECLARED, NAMED: `launchRequirement` and `launchRequirementData` are code-signing requirement
 * objects, and this system has no code-signing requirement subsystem to express one with — a refusal
 * rather than a stub.
 */

#ifndef FOUNDATION_NSTASK_H
#define FOUNDATION_NSTASK_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>	/* NSQualityOfService */
#include <sys/types.h>

@class NSArray;
@class NSCondition;	/* the reaper's handshake with -waitUntilExit */
@class NSDictionary;
@class NSError;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* Posted when the task has stopped execution, with the task as the object. Posted by the REAPER, so it
 * arrives whether or not anybody called `-waitUntilExit`. */
extern NSString *const NSTaskDidTerminateNotification;

/* THE CASE NAMES ARE APPLE'S AND THE VALUES ARE OURS, which is this tree's rule for an enum nobody
 * publishes numbers for (§11.6.1 D2). They are the conventional pair: an exit, or a signal. */
typedef enum {
	NSTaskTerminationReasonExit = 1,
	NSTaskTerminationReasonUncaughtSignal = 2
} NSTaskTerminationReason;

@interface NSTask : NSObject
{
	NSURL *_executableURL;
	NSArray *_arguments;
	NSDictionary *_environment;
	NSURL *_currentDirectoryURL;
	id _standardInput;		/* an NSPipe or an NSFileHandle, per Apple */
	id _standardOutput;
	id _standardError;
	NSQualityOfService _qualityOfService;
	void (^_terminationHandler)(NSTask *);	/* COPIED */
	/* THE PROCESS STATE, AND THE HANDSHAKE THE REAPER AND -waitUntilExit MAKE OVER IT. */
	pid_t _pid;
	BOOL _launched;
	BOOL _exited;
	int _status;			/* the RAW wait(2) status */
	NSCondition *_condition;
	BOOL _reaperStarted;
}

/* --- creating and initializing ------------------------------------------- */

/* The environment of the current process, and nothing launched: Apple's `-init`. */
- (instancetype)init;

/* "Creates and runs a task with a specified executable and arguments." RAISES NSInvalidArgumentException
 * when `arguments` is nil — Apple's own sentence — and answers nil with an error when the launch fails. */
+ (nullable instancetype)launchedTaskWithExecutableURL:(NSURL *)url
					     arguments:(NSArray *)arguments
						 error:(NSError **)errorPtr
				      terminationHandler:(nullable void (^)(NSTask *task))terminationHandler;

/* --- running and stopping ------------------------------------------------ */

/* Runs the process once. A second call on the same instance answers NO with an error. */
- (BOOL)launchAndReturnError:(NSError **)errorPtr;

/* Each signals the child's PROCESS GROUP (see the header's note). suspend/resume answer whether the
 * signal was delivered, which is Apple's BOOL. */
- (void)interrupt;
- (BOOL)suspend;
- (BOOL)resume;
- (void)terminate;

/* Blocks until the task is finished. A no-op on a task that was never launched. */
- (void)waitUntilExit;

/* --- querying the process state ------------------------------------------ */

- (BOOL)isRunning;
- (int)processIdentifier;

/* `WEXITSTATUS` when the task exited; the SIGNAL NUMBER when it did not — see the header's note. */
- (int)terminationStatus;
- (NSTaskTerminationReason)terminationReason;

/* --- configuring, BEFORE the launch -------------------------------------- */

- (nullable NSURL *)executableURL;
- (void)setExecutableURL:(NSURL *)url;

/* The arguments, WITHOUT the executable's own name: `argv[0]` is built from the executable. */
- (nullable NSArray *)arguments;
- (void)setArguments:(NSArray *)arguments;

/* nil means "inherit this process's environment", which is Apple's default. */
- (nullable NSDictionary *)environment;
- (void)setEnvironment:(NSDictionary *)environment;

- (nullable NSURL *)currentDirectoryURL;
- (void)setCurrentDirectoryURL:(NSURL *)url;

- (nullable id)standardInput;
- (void)setStandardInput:(nullable id)object;
- (nullable id)standardOutput;
- (void)setStandardOutput:(nullable id)object;
- (nullable id)standardError;
- (void)setStandardError:(nullable id)object;

/* CARRIED AND REPORTED, NOT APPLIED: there is no scheduler class to hand a quality of service to, so the
 * value is stored and answered and the launch does not consult it. Stated because a caller could
 * otherwise believe it had asked for something. */
- (NSQualityOfService)qualityOfService;
- (void)setQualityOfService:(NSQualityOfService)qualityOfService;

/* COPIED, and called by the REAPER when the task ends. */
- (nullable void (^)(NSTask *task))terminationHandler;
- (void)setTerminationHandler:(nullable void (^)(NSTask *task))handler;

/* §63.210: THE PRE-10.6 SPELLINGS — one mechanism: each legacy door is its modern counterpart with the path
 * spelled as a path rather than a URL. `launchRequirementData` is NOT here: it names the code-signing
 * requirement a task should be verified against, and this system has no signing substrate to consult. */
- (void)launch;
+ (nullable instancetype)launchedTaskWithLaunchPath:(NSString *)path
					 arguments:(nullable NSArray *)arguments;
@property (nullable, copy) NSString *launchPath;
@property (nullable, copy) NSString *currentDirectoryPath;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSTASK_H */
