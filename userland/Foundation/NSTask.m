/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSTask.m — a subprocess, a reaper thread, and one handshake. The design is in NSTask.h; what is HERE is
 * the three orders that decide whether it works.
 *
 * ONE: THE CHILD GETS A COPY OF THE PARENT'S MEMORY, SO IT MUST TOUCH ALMOST NOTHING. Every allocation —
 * argv, envp, the paths, the descriptor lookups — happens BEFORE `fork(2)`, and the child does only
 * `dup2`, `close`, `chdir`, `setpgid`, `execve` and `_exit`, all of which are async-signal-safe. A child
 * that allocated would be a child of a THREADED process doing it, and that is the classic way a fork/exec
 * hangs on a lock the child inherited.
 *
 * TWO: THE PARENT CLOSES THE CHILD'S END OF EACH PIPE, AND ONLY FOR THE CHILD. A pipe handed to
 * `-setStandardInput:` has a WRITE end the parent still holds; if the child kept a copy of it too, a child
 * reading its standard input would never see END OF FILE, because the pipe would still have a writer. So
 * the far end is closed IN THE CHILD. The caller's own copies are left alone — the other half of that rule
 * is Apple's and is the caller's: a parent that reads to the end of a pipe must close its own copy of the
 * write end, which the probe does.
 *
 * THREE: THE REAPER OWNS THE STATUS. It is the ONLY caller of `waitpid(2)`, it records the raw status
 * under the condition and broadcasts, and `-waitUntilExit` waits for THAT rather than reaping again — so
 * the status is read exactly once, `-terminationStatus` can be asked for ever afterwards, and the
 * termination handler and the notification happen without anybody waiting at all, which is Apple's
 * contract. The reaper HOLDS A REFERENCE to the task, because this library is MRC and nothing else
 * promises the object outlives its child.
 */

#import "FNCondition.h"
#import <Foundation/NSTask.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSFileHandle.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSPipe.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* THE PROCESS'S OWN ENVIRONMENT, declared where it is used as NSProcessInfo.m does it. */
extern char **environ;

/* THE BLOCKS RUNTIME, declared where it is used: <Block.h> is staged nowhere and libobjc2 exports both. */
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);

NSString *const NSTaskDidTerminateNotification = @"NSTaskDidTerminateNotification";

/* THE HOUSE ERROR, as NSFileManager and NSFileHandle spell it. */
static NSError *fn_task_error(int err)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:[NSDictionary dictionaryWithObject:
						[NSString stringWithUTF8String:strerror(err)]
								    forKey:NSLocalizedDescriptionKey]];
}

static BOOL fn_task_failed(NSError **errorPtr, int err)
{
	if (errorPtr != NULL) {
		*errorPtr = fn_task_error(err);
	}
	return NO;
}

/* WHAT A STANDARD DESCRIPTOR SETTING MEANS IN DESCRIPTORS. Apple allows an NSPipe or an NSFileHandle, and
 * the DIRECTION is why this is not one call to `-fileDescriptor`: standard OUTPUT is the pipe's WRITE end
 * while standard INPUT is its READ end. */
static int fn_standard_child_fd(id object, BOOL isInput)
{
	if ([object isKindOfClass:[NSPipe class]]) {
		return isInput ? [[(NSPipe *)object fileHandleForReading] fileDescriptor]
			       : [[(NSPipe *)object fileHandleForWriting] fileDescriptor];
	}
	if ([object isKindOfClass:[NSFileHandle class]]) {
		return [(NSFileHandle *)object fileDescriptor];
	}
	return -1;
}

/* THE END THE CHILD MUST CLOSE: see this file's second note. */
static int fn_standard_far_fd(id object, BOOL isInput)
{
	if ([object isKindOfClass:[NSPipe class]]) {
		return isInput ? [[(NSPipe *)object fileHandleForWriting] fileDescriptor]
			       : [[(NSPipe *)object fileHandleForReading] fileDescriptor];
	}
	return -1;
}

/* `argv[0]` IS THE EXECUTABLE, and the arguments follow it — Apple's `-arguments` does NOT include the
 * program's own name, which is a difference every caller has to be told once. */
static char **fn_task_argv(NSString *path, NSArray *arguments)
{
	NSUInteger count = [arguments count], i;
	char **argv = (char **)malloc((count + 2) * sizeof(char *));

	if (argv == NULL) {
		return NULL;
	}
	argv[0] = strdup([path UTF8String]);
	for (i = 0; i < count; i++) {
		argv[i + 1] = strdup([[arguments objectAtIndex:i] UTF8String]);
	}
	argv[count + 1] = NULL;
	return argv;
}

static char **fn_task_envp(NSDictionary *environment, BOOL *owned)
{
	NSArray *keys;
	NSUInteger count, i;
	char **envp;

	*owned = NO;
	if (environment == nil) {
		return environ;		/* INHERIT, which is Apple's default */
	}
	keys = [environment allKeys];
	count = [keys count];
	envp = (char **)malloc((count + 1) * sizeof(char *));
	if (envp == NULL) {
		return NULL;
	}
	for (i = 0; i < count; i++) {
		NSString *key = [keys objectAtIndex:i];
		NSString *pair = [NSString stringWithFormat:@"%@=%@", key,
							     [environment objectForKey:key]];

		envp[i] = strdup([pair UTF8String]);
	}
	envp[count] = NULL;
	*owned = YES;
	return envp;
}

static void fn_task_free_vector(char **vector, BOOL owned)
{
	NSUInteger i;

	if (vector == NULL || !owned) {
		return;
	}
	for (i = 0; vector[i] != NULL; i++) {
		free(vector[i]);
	}
	free(vector);
}

/* A SIGNAL TO THE CHILD'S PROCESS GROUP, which is how "and all of its subtasks" is honoured — with a
 * fallback, because a kernel without process groups should still get the child itself. */
static int fn_task_signal(pid_t pid, int sig)
{
	if (kill(-pid, sig) != 0) {
		return kill(pid, sig);
	}
	return 0;
}

@implementation NSTask

/* ---- the reaper --------------------------------------------------------- */

/* IT IS A C FUNCTION INSIDE THE IMPLEMENTATION, which is what lets it touch the task's ivars: the status
 * handshake is one object's private business, and a getter pair for it would be a public door for nobody. */
static void *fn_task_reaper(void *context)
{
	NSTask *task = (NSTask *)context;
	int status = 0;
	pid_t reaped;

	for (;;) {
		reaped = waitpid(task->_pid, &status, 0);
		if (reaped >= 0 || errno != EINTR) {
			break;
		}
	}
	[task->_condition lock];
	task->_status = status;
	task->_exited = YES;
	[task->_condition broadcast];
	[task->_condition unlock];

	[[NSNotificationCenter defaultCenter] postNotificationName:NSTaskDidTerminateNotification
							    object:task];
	if (task->_terminationHandler != NULL) {
		task->_terminationHandler(task);
	}
	[task release];		/* the reference -launchAndReturnError: took for exactly this */
	return NULL;
}

/* ---- creating and initializing ------------------------------------------ */

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_qualityOfService = NSQualityOfServiceDefault;
		_condition = [[FNCondition alloc] init];
	}
	return self;
}

+ (instancetype)launchedTaskWithExecutableURL:(NSURL *)url
				    arguments:(NSArray *)arguments
					error:(NSError **)errorPtr
			 terminationHandler:(void (^)(NSTask *task))terminationHandler
{
	NSTask *task;

	/* APPLE'S OWN SENTENCE: "If arguments is nil, the system raises an NSInvalidArgumentException." */
	if (arguments == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSTask: -arguments must not be nil"];
	}
	task = [[[self alloc] init] autorelease];
	[task setExecutableURL:url];
	[task setArguments:arguments];
	[task setTerminationHandler:terminationHandler];
	if (![task launchAndReturnError:errorPtr]) {
		return nil;
	}
	return task;
}

/* ---- configuring -------------------------------------------------------- */

- (NSURL *)executableURL
{
	return _executableURL;
}

- (void)setExecutableURL:(NSURL *)url
{
	[url retain];
	[_executableURL release];
	_executableURL = url;
}

- (NSArray *)arguments
{
	return _arguments;
}

- (void)setArguments:(NSArray *)arguments
{
	[arguments retain];
	[_arguments release];
	_arguments = arguments;
}

- (NSDictionary *)environment
{
	return _environment;
}

- (void)setEnvironment:(NSDictionary *)environment
{
	[environment retain];
	[_environment release];
	_environment = environment;
}

- (NSURL *)currentDirectoryURL
{
	return _currentDirectoryURL;
}

- (void)setCurrentDirectoryURL:(NSURL *)url
{
	[url retain];
	[_currentDirectoryURL release];
	_currentDirectoryURL = url;
}

- (id)standardInput
{
	return _standardInput;
}

- (void)setStandardInput:(id)object
{
	[object retain];
	[_standardInput release];
	_standardInput = object;
}

- (id)standardOutput
{
	return _standardOutput;
}

- (void)setStandardOutput:(id)object
{
	[object retain];
	[_standardOutput release];
	_standardOutput = object;
}

- (id)standardError
{
	return _standardError;
}

- (void)setStandardError:(id)object
{
	[object retain];
	[_standardError release];
	_standardError = object;
}

- (NSQualityOfService)qualityOfService
{
	return _qualityOfService;
}

- (void)setQualityOfService:(NSQualityOfService)qualityOfService
{
	_qualityOfService = qualityOfService;
}

- (void (^)(NSTask *))terminationHandler
{
	return _terminationHandler;
}

- (void)setTerminationHandler:(void (^)(NSTask *))handler
{
	void (^copied)(NSTask *) = (handler != NULL) ? (void (^)(NSTask *))_Block_copy(handler) : NULL;

	if (_terminationHandler != NULL) {
		_Block_release(_terminationHandler);
	}
	_terminationHandler = copied;
}

/* ---- running ------------------------------------------------------------ */

- (BOOL)launchAndReturnError:(NSError **)errorPtr
{
	NSString *path;
	char **argv;
	char **envp;
	BOOL envpOwned;
	int inFd, outFd, errFd, inFar, outFar, errFar;
	pthread_t reaper;
	pid_t pid;

	/* ONE INSTANCE RUNS ONCE — Apple: "You can only run the subprocess once per instance." */
	if (_launched) {
		return fn_task_failed(errorPtr, EALREADY);
	}
	if (_arguments == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSTask: -arguments must not be nil"];
	}
	if (_executableURL == nil) {
		return fn_task_failed(errorPtr, ENOENT);
	}
	path = [_executableURL path];
	if (path == nil || [path length] == 0) {
		return fn_task_failed(errorPtr, ENOENT);
	}
	if (access([path UTF8String], X_OK) != 0) {
		return fn_task_failed(errorPtr, errno);
	}

	/* EVERYTHING THAT ALLOCATES, BEFORE THE FORK. */
	argv = fn_task_argv(path, _arguments);
	envp = fn_task_envp(_environment, &envpOwned);
	if (argv == NULL || envp == NULL) {
		fn_task_free_vector(argv, YES);
		fn_task_free_vector(envp, envpOwned);
		return fn_task_failed(errorPtr, ENOMEM);
	}
	inFd = fn_standard_child_fd(_standardInput, YES);
	inFar = fn_standard_far_fd(_standardInput, YES);
	outFd = fn_standard_child_fd(_standardOutput, NO);
	outFar = fn_standard_far_fd(_standardOutput, NO);
	errFd = fn_standard_child_fd(_standardError, NO);
	errFar = fn_standard_far_fd(_standardError, NO);

	pid = fork();
	if (pid < 0) {
		int err = errno;

		fn_task_free_vector(argv, YES);
		fn_task_free_vector(envp, envpOwned);
		return fn_task_failed(errorPtr, err);
	}
	if (pid == 0) {
		/* THE CHILD: ASYNC-SIGNAL-SAFE CALLS ONLY, from here to execve. */
		if (inFd >= 0) {
			(void)dup2(inFd, STDIN_FILENO);
		}
		if (outFd >= 0) {
			(void)dup2(outFd, STDOUT_FILENO);
		}
		if (errFd >= 0) {
			(void)dup2(errFd, STDERR_FILENO);
		}
		if (inFar >= 0 && inFar != inFd) {
			(void)close(inFar);	/* or the child's stdin never ends */
		}
		if (outFar >= 0 && outFar != outFd) {
			(void)close(outFar);
		}
		if (errFar >= 0 && errFar != errFd) {
			(void)close(errFar);
		}
		if (_currentDirectoryURL != nil) {
			const char *directory = [[_currentDirectoryURL path] UTF8String];

			if (directory != NULL) {
				(void)chdir(directory);
			}
		}
		(void)setpgid(0, 0);		/* its own group, so one signal reaches the subtree */
		execve([path UTF8String], argv, envp);
		_exit(127);			/* the shell's own "cannot execute" */
	}

	fn_task_free_vector(argv, YES);
	fn_task_free_vector(envp, envpOwned);

	_pid = pid;
	_launched = YES;

	/* THE REAPER TAKES A REFERENCE, and gets it BEFORE the thread exists so the thread cannot outrun it. */
	[self retain];
	if (pthread_create(&reaper, NULL, fn_task_reaper, (void *)self) == 0) {
		_reaperStarted = YES;
		(void)pthread_detach(reaper);
	} else {
		/* NO REAPER: -waitUntilExit reaps instead, which is the whole fallback path. */
		[self release];
		_reaperStarted = NO;
	}
	return YES;
}

- (void)interrupt
{
	if (_launched && !_exited) {
		(void)fn_task_signal(_pid, SIGINT);
	}
}

- (void)terminate
{
	if (_launched && !_exited) {
		(void)fn_task_signal(_pid, SIGTERM);
	}
}

- (BOOL)suspend
{
	if (!_launched || _exited) {
		return NO;
	}
	return fn_task_signal(_pid, SIGSTOP) == 0;
}

- (BOOL)resume
{
	if (!_launched || _exited) {
		return NO;
	}
	return fn_task_signal(_pid, SIGCONT) == 0;
}

- (void)waitUntilExit
{
	if (!_launched) {
		return;
	}
	if (!_reaperStarted) {
		int status = 0;

		while (waitpid(_pid, &status, 0) < 0 && errno == EINTR) {
			;
		}
		[_condition lock];
		_status = status;
		_exited = YES;
		[_condition broadcast];
		[_condition unlock];
		return;
	}
	[_condition lock];
	while (!_exited) {
		[_condition wait];
	}
	[_condition unlock];
}

/* ---- querying ----------------------------------------------------------- */

- (BOOL)isRunning
{
	return _launched && !_exited;
}

- (int)processIdentifier
{
	return (int)_pid;
}

- (int)terminationStatus
{
	if (!_exited) {
		return 0;
	}
	if (WIFEXITED(_status)) {
		return WEXITSTATUS(_status);
	}
	if (WIFSIGNALED(_status)) {
		return WTERMSIG(_status);	/* OURS: see the header */
	}
	return 0;
}

- (NSTaskTerminationReason)terminationReason
{
	if (_exited && WIFSIGNALED(_status)) {
		return NSTaskTerminationReasonUncaughtSignal;
	}
	return NSTaskTerminationReasonExit;
}

- (void)dealloc
{
	/* REACHED ONLY WHEN THE REAPER HAS RELEASED ITS REFERENCE, so a running task's object is never torn
	 * down under its own child. */
	[_executableURL release];
	[_arguments release];
	[_environment release];
	[_currentDirectoryURL release];
	[_standardInput release];
	[_standardOutput release];
	[_standardError release];
	[_condition release];
	if (_terminationHandler != NULL) {
		_Block_release(_terminationHandler);
	}
	[super dealloc];
}


- (void)launch
{
	/* THE PRE-10.6 SPELLING: the modern door is where the work is. A failure surfaces the way the legacy API
	 * surfaced it — the task is simply not running. */
	(void)[self launchAndReturnError:NULL];
}

+ (instancetype)launchedTaskWithLaunchPath:(NSString *)path arguments:(NSArray *)arguments
{
	NSTask *task = [[self alloc] init];

	[task setLaunchPath:path];
	[task setArguments:arguments];
	[task launch];
	return [task autorelease];
}

- (NSString *)launchPath
{
	NSURL *url = [self executableURL];

	return url != nil ? [url path] : nil;
}

- (void)setLaunchPath:(NSString *)path
{
	if (path == nil) {
		[self setExecutableURL:(NSURL *)nil];
		return;
	}
	[self setExecutableURL:(NSURL *)[NSURL fileURLWithPath:path]];
}

- (NSString *)currentDirectoryPath
{
	NSURL *url = [self currentDirectoryURL];

	return url != nil ? [url path] : nil;
}

- (void)setCurrentDirectoryPath:(NSString *)path
{
	if (path == nil) {
		[self setCurrentDirectoryURL:(NSURL *)nil];
		return;
	}
	[self setCurrentDirectoryURL:(NSURL *)[NSURL fileURLWithPath:path]];
}
@end
