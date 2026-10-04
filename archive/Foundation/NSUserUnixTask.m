/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUserUnixTask.m — running an ordinary Unix script. The design, and the one deviation, are in
 * NSUserUnixTask.h.
 *
 * BUILT ON NSTask, DELIBERATELY: the execution itself is W6d's problem and W6d solved it - fork/exec, the
 * standard streams, one reaper thread that owns the status, and a termination handler that runs without
 * anybody waiting. This class is the SCRIPT-shaped door onto that, so it adds no second reaper and no second
 * set of rules for what a status means.
 *
 * MRC, LIKE THE REST OF THIS LIBRARY, AND BLOCKS WITH IT: a handler that outlives the call is COPIED, and the
 * copy is released by the block that uses it (the last time it is needed).
 */

#import <Foundation/NSUserUnixTask.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSError.h>
#import <Foundation/NSFileHandle.h>
#import <Foundation/NSString.h>
#import <Foundation/NSTask.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <unistd.h>

/* THE BLOCKS RUNTIME, declared where it is used: <Block.h> is staged nowhere and libobjc2 exports both. */
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);

/* THE HOUSE SPELLING: a domain is a string literal here, because this tree declares NSErrorDomain as a
 * typedef and not one constant per domain. THE CODE IS THE EXIT STATUS for a script that exited, or the
 * SIGNAL NUMBER for one that died by a signal - which is what NSTask's -terminationStatus already answers. */
static NSError *fn_unix_task_error(int code)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:code
			       userInfo:nil];
}

@implementation NSUserUnixTask

- (nullable instancetype)initWithScriptURL:(NSURL *)url error:(NSError **)errorPtr
{
	NSString *path = [url path];

	self = [super init];
	if (self != nil) {
		/* APPLE'S SHAPE: nil AND AN ERROR, rather than an object that can never run anything. The test is
		 * the one that matters for a script the KERNEL executes - it must be executable - so a plain data
		 * file is refused here rather than failing later with a confusing status. */
		if (path == nil) {
			if (errorPtr != NULL) {
				*errorPtr = fn_unix_task_error(EINVAL);
			}
			[self release];
			return nil;
		}
		if (access([path UTF8String], X_OK) != 0) {
			int err = errno;

			if (errorPtr != NULL) {
				*errorPtr = fn_unix_task_error(err);
			}
			[self release];
			return nil;
		}
		_scriptURL = [url retain];
	}
	return self;
}

- (void)dealloc
{
	[_scriptURL release];
	[_standardInput release];
	[_standardOutput release];
	[_standardError release];
	[super dealloc];
}

- (nullable NSURL *)scriptURL
{
	return _scriptURL;
}

- (nullable NSFileHandle *)standardInput
{
	return _standardInput;
}

- (void)setStandardInput:(nullable NSFileHandle *)handle
{
	[handle retain];
	[_standardInput release];
	_standardInput = handle;
}

- (nullable NSFileHandle *)standardOutput
{
	return _standardOutput;
}

- (void)setStandardOutput:(nullable NSFileHandle *)handle
{
	[handle retain];
	[_standardOutput release];
	_standardOutput = handle;
}

- (nullable NSFileHandle *)standardError
{
	return _standardError;
}

- (void)setStandardError:(nullable NSFileHandle *)handle
{
	[handle retain];
	[_standardError release];
	_standardError = handle;
}

- (void)executeWithArguments:(nullable NSArray *)arguments
	   completionHandler:(nullable NSUserUnixTaskCompletionHandler)handler
{
	NSTask *task = [[NSTask alloc] init];
	NSError *launchError = nil;
	NSUserUnixTaskCompletionHandler copied = NULL;

	[task setExecutableURL:_scriptURL];
	[task setArguments:arguments != nil ? arguments : [NSArray array]];
	if (_standardInput != nil) {
		[task setStandardInput:_standardInput];
	}
	if (_standardOutput != nil) {
		[task setStandardOutput:_standardOutput];
	}
	if (_standardError != nil) {
		[task setStandardError:_standardError];
	}
	if (handler != NULL) {
		/* COPIED, because this library is MRC and this handler outlives the call. The copy is released by
		 * the block that consumes it, which is the last time it is needed. */
		copied = (NSUserUnixTaskCompletionHandler)_Block_copy(handler);
		[task setTerminationHandler:^(NSTask *finished) {
			int status = [finished terminationStatus];
			NSError *error = (status != 0) ? fn_unix_task_error(status) : nil;

			copied(error);
			_Block_release(copied);
		}];
	}
	if (![task launchAndReturnError:&launchError]) {
		/* A LAUNCH THAT NEVER STARTED STILL HAS TO ANSWER, or a caller waiting on the handler waits for
		 * ever: there is no reaper to call it, so it is called here. */
		if (copied != NULL) {
			copied(launchError);
			_Block_release(copied);
		}
	}
	[task release];		/* THE REAPER TOOK ITS OWN REFERENCE AT LAUNCH, so the task outlives this */
}

@end
