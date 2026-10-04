/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_task — W6d's acceptance: NSTask. docs/design/foundation-plan.md §45.
 *
 * ONE unit. THE CHILD IS THIS PROBE, RE-EXECUTED: the first thing main() does is look at argv for a
 * `--child…` mode, and that is the whole reason the checks can be exact — the child's exit code, its
 * output, its directory and the signal it dies by are all OUR choices rather than another program's
 * behaviour that could change under us. A test that ran `/bin/sh` would be testing dash.
 *
 * THE PROBE IS ARC AND THE LIBRARY IS MRC, so nothing here says release or retain; a task's lifetime is a
 * SCOPE, and the reaper thread inside the library holds its own reference to a running task.
 *
 *   task-runs-and-exits            a child exits 7: status 7, reason Exit, -isRunning NO afterwards
 *   task-captures-standard-output  standardOutput = an NSPipe: the child's bytes arrive, to the END OF FILE
 *                                  a closed parent write end produces
 *   task-feeds-standard-input      standardInput = an NSPipe: what the parent writes is `cat`-ed back
 *   task-arguments-reach-the-child the arguments arrive, WITHOUT the executable's own name in them
 *   task-environment-reaches-the-child  a set environment REPLACES the inherited one and arrives whole
 *   task-current-directory         `-currentDirectoryURL` is where the child actually is (getcwd(3))
 *   task-signal-death-is-uncaught  a child that raises SIGSEGV: reason UncaughtSignal, status the SIGNAL
 *   task-terminate                 `-terminate` kills a sleeping child with SIGTERM
 *   task-interrupt                 `-interrupt` kills it with SIGINT
 *   task-suspend-and-resume        suspend/resume round-trip, and the child still finishes normally
 *   task-termination-handler-fires THE REAPER'S REASON FOR EXISTING: the handler runs with NOBODY having
 *                                  called -waitUntilExit
 *   task-did-terminate-notification  and the notification is posted, with the task as its object
 *   task-refuses-a-second-launch   one instance runs ONCE — Apple's own rule — and the second answers NO
 *   task-reports-a-bad-executable  a missing executable answers NO with an NSPOSIXErrorDomain error
 *   task-nil-arguments-raises      the static launcher RAISES for nil arguments (Apple's own sentence)
 *   task-static-launcher           +launchedTaskWithExecutableURL:… returns a running task
 *   task-quality-of-service        the value is CARRIED and answered (the header says it is not applied)
 *
 * NOT GATED, AND WHY: `launchRequirement`/`launchRequirementData` are not declared at all (no code-signing
 * requirement subsystem exists here), and the Apple-deprecated four (`-launchPath`, `-setLaunchPath:`,
 * `-launch`, `-currentDirectoryPath`/`-setCurrentDirectoryPath:`, `+launchedTaskWithLaunchPath:arguments:`)
 * are OWED: they were "struck by §11.5 and absent by policy" until 2026-09-26, when the deprecation ground
 * was RETIRED (§62.24) - they are absent now because they are UNIMPLEMENTED.
 */

#import <Foundation/Foundation.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROBE_SELF "/System/Shared/tests/foundation_task"
#define PROBE_ROOT "/System/Temporary Files/nstask-probe"

/* THE TWO PATHS AS OBJECTS, through helpers, because `+stringWithUTF8String:` is declared NULLABLE and
 * every use below feeds a non-null parameter — which is an error under this tree's
 * -Werror=nullable-to-nonnull-conversion. */
static NSString *probe_self(void)
{
	return (NSString *)[NSString stringWithUTF8String:PROBE_SELF];
}

static NSString *probe_root(void)
{
	return (NSString *)[NSString stringWithUTF8String:PROBE_ROOT];
}

/* ---- THE CHILD MODES ---------------------------------------------------- */

/* Run BEFORE anything Foundation is made: a child is a tiny program here, and its whole contract is in
 * these few lines. */
static int fn_child(int argc, char *argv[])
{
	const char *mode = argv[1];

	if (strcmp(mode, "--child-exit") == 0) {
		_exit(argc > 2 ? atoi(argv[2]) : 0);
	}
	if (strcmp(mode, "--child-write") == 0) {
		const char *text = argc > 2 ? argv[2] : "";
		size_t length = strlen(text);

		(void)write(STDOUT_FILENO, text, length);
		_exit(0);
	}
	if (strcmp(mode, "--child-env") == 0) {
		const char *value = getenv("FN_TASK_PROBE");
		const char *text = value != NULL ? value : "<unset>";

		(void)write(STDOUT_FILENO, text, strlen(text));
		_exit(0);
	}
	if (strcmp(mode, "--child-cwd") == 0) {
		char buffer[1024];

		if (getcwd(buffer, sizeof(buffer)) != NULL) {
			(void)write(STDOUT_FILENO, buffer, strlen(buffer));
		}
		_exit(0);
	}
	if (strcmp(mode, "--child-cat") == 0) {
		char buffer[256];
		ssize_t got;

		while ((got = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
			(void)write(STDOUT_FILENO, buffer, (size_t)got);
		}
		_exit(0);
	}
	if (strcmp(mode, "--child-signal") == 0) {
		(void)raise(SIGSEGV);		/* the default action is death BY SIGNAL, no handler */
		_exit(0);
	}
	if (strcmp(mode, "--child-foundation") == 0) {
		/* THE COMPARISON: main calls +[NSFileManager defaultManager] BEFORE its first string
		 * constructor (trace 1a) and the next call dies at a wild address; this mode did the string
		 * first and passed. So mirror main's ORDER here - the only difference left. */
		[NSFileManager defaultManager];
		NSString *text = [NSString stringWithUTF8String:"foundation in a child"];
		NSMutableArray *list = [[NSMutableArray alloc] init];

		[list addObject:text];
		printf("FOUNDATION-TASK child-foundation ok=%lu\n", (unsigned long)[list count]);
		_exit(0);
	}
	if (strcmp(mode, "--probe-root-only") == 0) {
		/* THE SMALLEST PROGRAM: main's first three steps and exit - `+[NSFileManager defaultManager]`,
		 * then probe_root(). It was written when the probe died between trace 1a and 1b, which turned
		 * out to be `probe_root()` calling ITSELF (see the helper above): the fault was never in
		 * Foundation, in the kernel, or in the address. Kept, because it is still the cheapest
		 * statement that this binary can make a manager and a path at all. */
		NSFileManager *m = [NSFileManager defaultManager];
		NSString *t = probe_root();

		printf("FOUNDATION-TASK probe-root-only: manager=%s root=%s len=%d\n",
			m ? "ok" : "nil", [t UTF8String], (int)[t length]);
		_exit(0);
	}
	if (strcmp(mode, "--child-delay") == 0) {
		usleep(300000);			/* long enough for a SIGSTOP/SIGCONT round trip */
		_exit(5);
	}
	if (strcmp(mode, "--child-sleep") == 0) {
		for (;;) {
			(void)pause();
		}
	}
	_exit(64);				/* an unknown mode is a fixture error */
}

/* ---- the fixtures ------------------------------------------------------- */

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-TASK %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-TASK %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* WHAT THE REAPER'S TWO SIDE EFFECTS ARE OBSERVED THROUGH — a flag it sets and a count it posts into. */
@interface FnTaskFixture : NSObject
{
	volatile int _fired;
	NSUInteger _terminations;
	id _lastObject;
}
- (void)markFired:(NSTask *)task;
- (void)note:(NSNotification *)notification;
- (int)fired;
- (NSUInteger)terminations;
- (id)lastObject;
@end

@implementation FnTaskFixture

- (void)markFired:(NSTask *)task
{
	(void)task;
	_fired = 1;
}

- (void)note:(NSNotification *)notification
{
	_terminations++;
	_lastObject = [notification object];
}

- (int)fired
{
	return _fired;
}

- (NSUInteger)terminations
{
	return _terminations;
}

- (id)lastObject
{
	return _lastObject;
}

@end

/* THE OUTPUT OF A CHILD, captured the way Apple's own documentation describes: give it an NSPipe, close
 * YOUR copy of the write end, and read to the end of file. */
static NSString *run_capturing(NSArray *arguments, NSString *input, int *statusOut,
			       NSError **error)
{
	NSURL *self = [NSURL fileURLWithPath:probe_self()];
	NSTask *task = [[NSTask alloc] init];
	NSPipe *out = [NSPipe pipe];
	NSString *text = nil;

	[task setExecutableURL:self];
	[task setArguments:arguments];
	[task setStandardOutput:out];
	if (input != nil) {
		[task setStandardInput:[NSPipe pipe]];
	}
	if ([task launchAndReturnError:error]) {
		if (input != nil) {
			NSData *payload = [input dataUsingEncoding:NSUTF8StringEncoding];

			[[[task standardInput] fileHandleForWriting] writeData:payload error:NULL];
			[[[task standardInput] fileHandleForWriting] closeAndReturnError:NULL];
		}
		/* THE PARENT'S COPY OF THE WRITE END GOES NOW, or the read below never sees END OF FILE. */
		[[out fileHandleForWriting] closeAndReturnError:NULL];
		{
			NSData *bytes = [[out fileHandleForReading]
						readDataToEndOfFileAndReturnError:NULL];

			text = [[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
		}
		[task waitUntilExit];
	}
	if (statusOut != NULL) {
		*statusOut = [task terminationStatus];
	}
	return text;
}

int main(int argc, char *argv[])
{
	NSFileManager *manager;
	NSString *root;
	NSURL *self;

	/* THE CHILD BRANCH IS FIRST, BEFORE ANY OBJECT EXISTS: the child is a tiny program, and its whole
	 * contract is the few lines of fn_child(). THE DIAGNOSTIC MODE IS DISPATCHED HERE TOO, and that is
	 * not tidiness: its flag is `--probe…`, NOT `--child…`, so gating this branch on `--child` alone
	 * made the `--probe-root-only` arm below UNREACHABLE DEAD CODE - the invocation silently ran the
	 * whole probe instead (measured: no `probe-root-only:` line at all, and the check failed on output
	 * that belonged to the full run). */
	if (argc > 1 && (strncmp(argv[1], "--child", 7) == 0 ||
			 strncmp(argv[1], "--probe", 7) == 0)) {
		return fn_child(argc, argv);
	}

	printf("FOUNDATION-TASK trace 1: entered main rsp=%p\n", (void *)&manager);
	manager = [NSFileManager defaultManager];
	printf("FOUNDATION-TASK trace 1a: manager rsp=%p\n", (void *)&manager);
	root = probe_root();
	printf("FOUNDATION-TASK trace 1b: root rsp=%p\n", (void *)&root);
	self = [NSURL fileURLWithPath:probe_self()];
	printf("FOUNDATION-TASK trace 1c: self-url rsp=%p\n", (void *)&self);

	[manager removeItemAtPath:root error:NULL];
	printf("FOUNDATION-TASK trace 1d: remove rsp=%p\n", (void *)&root);
	[manager createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:NULL];
	printf("FOUNDATION-TASK trace 1e: create rsp=%p\n", (void *)&root);

	printf("FOUNDATION-TASK trace 2: scratch ready\n");

	/* ---- the plain run --------------------------------------------------- */
	{
		NSTask *task = [[NSTask alloc] init];
		NSError *error = nil;
		BOOL launched;

		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObjects:@"--child-exit", @"7", nil]];
		launched = [task launchAndReturnError:&error];
		/* NOT -isRunning HERE: a child that exits immediately could be reaped before this line runs, so
		 * the launch's own evidence is the BOOL and the PID. -isRunning is asserted BELOW, after a wait,
		 * where it is deterministic. */
		check("task-runs-and-exits",
		      [manager isExecutableFileAtPath:probe_self()] &&
		      launched && [task processIdentifier] > 0,
		      [NSString stringWithFormat:@"self=%d launched=%d pid=%d",
			(int)[manager isExecutableFileAtPath:probe_self()],
			(int)launched, [task processIdentifier]]);
		[task waitUntilExit];
		check("task-waits-and-reports",
		      [task terminationStatus] == 7 &&
		      [task terminationReason] == NSTaskTerminationReasonExit && ![task isRunning] &&
		      [task processIdentifier] > 0,
		      [NSString stringWithFormat:@"status=%d reason=%d running=%d pid=%d",
			[task terminationStatus], (int)[task terminationReason], (int)[task isRunning],
			[task processIdentifier]]);
	}

	printf("FOUNDATION-TASK trace 3: plain run done\n");

	/* ---- the pipes ------------------------------------------------------- */
	{
		int status = -1;
		NSError *error = nil;
		NSString *text = run_capturing([NSArray arrayWithObjects:@"--child-write",
						@"hello child", nil], nil, &status, &error);

		check("task-captures-standard-output",
		      [text isEqualToString:@"hello child"] && status == 0,
		      [NSString stringWithFormat:@"read %@ (status %d, error %@)", text, status,
			[error domain]]);
	}

	{
		int status = -1;
		NSString *text = run_capturing([NSArray arrayWithObjects:@"--child-cat", nil],
					       @"ping from the parent", &status, NULL);

		check("task-feeds-standard-input",
		      [text isEqualToString:@"ping from the parent"] && status == 0,
		      [NSString stringWithFormat:@"the child answered %@ (status %d)", text, status]);
	}

	{
		int status = -1;
		NSString *text = run_capturing([NSArray arrayWithObjects:@"--child-write",
						@"the argument", nil], nil, &status, NULL);

		/* THE ARGUMENTS DO NOT CARRY THE PROGRAM'S OWN NAME: `argv[0]` is built from the executable, and
		 * this check would pass trivially if the probe had passed itself twice. */
		check("task-arguments-reach-the-child",
		      [text isEqualToString:@"the argument"],
		      [NSString stringWithFormat:@"the child wrote %@", text]);
	}

	{
		NSURL *executable = self;
		NSTask *task = [[NSTask alloc] init];
		NSPipe *out = [NSPipe pipe];
		NSString *text;
		int status = -1;

		[task setExecutableURL:executable];
		[task setArguments:[NSArray arrayWithObject:@"--child-env"]];
		[task setEnvironment:[NSDictionary dictionaryWithObject:@"yes" forKey:@"FN_TASK_PROBE"]];
		[task setStandardOutput:out];
		[task launchAndReturnError:NULL];
		[[out fileHandleForWriting] closeAndReturnError:NULL];
		{
			NSData *bytes = [[out fileHandleForReading]
						readDataToEndOfFileAndReturnError:NULL];

			text = [[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
		}
		[task waitUntilExit];
		status = [task terminationStatus];
		check("task-environment-reaches-the-child",
		      [text isEqualToString:@"yes"],
		      [NSString stringWithFormat:@"the child read %@ (status %d)", text, status]);
	}

	{
		NSFileManager *fm = [NSFileManager defaultManager];
		NSURL *directory = [NSURL fileURLWithPath:root];
		NSTask *task = [[NSTask alloc] init];
		NSPipe *out = [NSPipe pipe];
		NSString *text;

		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObject:@"--child-cwd"]];
		[task setCurrentDirectoryURL:directory];
		[task setStandardOutput:out];
		[task launchAndReturnError:NULL];
		[[out fileHandleForWriting] closeAndReturnError:NULL];
		{
			NSData *bytes = [[out fileHandleForReading]
						readDataToEndOfFileAndReturnError:NULL];

			text = [[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
		}
		[task waitUntilExit];
		(void)fm;
		/* THE CHILD'S OWN ANSWER, from getcwd(3) — not the value we set, which would be a tautology. */
		check("task-current-directory",
		      [text isEqualToString:root],
		      [NSString stringWithFormat:@"the child's getcwd(3) answered %@, not %@", text, root]);
	}

	printf("FOUNDATION-TASK trace 4: pipes done\n");

	/* ---- how a task can end --------------------------------------------- */
	{
		NSTask *task = [[NSTask alloc] init];

		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObject:@"--child-signal"]];
		[task launchAndReturnError:NULL];
		[task waitUntilExit];
		check("task-signal-death-is-uncaught",
		      [task terminationReason] == NSTaskTerminationReasonUncaughtSignal &&
		      [task terminationStatus] == SIGSEGV,
		      [NSString stringWithFormat:@"reason=%d status=%d (SIGSEGV is %d)",
			(int)[task terminationReason], [task terminationStatus], SIGSEGV]);
	}

	{
		NSTask *task = [[NSTask alloc] init];

		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObject:@"--child-sleep"]];
		[task launchAndReturnError:NULL];
		[task terminate];
		[task waitUntilExit];
		check("task-terminate",
		      [task terminationReason] == NSTaskTerminationReasonUncaughtSignal &&
		      [task terminationStatus] == SIGTERM,
		      [NSString stringWithFormat:@"reason=%d status=%d (SIGTERM is %d)",
			(int)[task terminationReason], [task terminationStatus], SIGTERM]);
	}

	{
		NSTask *task = [[NSTask alloc] init];

		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObject:@"--child-sleep"]];
		[task launchAndReturnError:NULL];
		[task interrupt];
		[task waitUntilExit];
		check("task-interrupt",
		      [task terminationReason] == NSTaskTerminationReasonUncaughtSignal &&
		      [task terminationStatus] == SIGINT,
		      [NSString stringWithFormat:@"reason=%d status=%d (SIGINT is %d)",
			(int)[task terminationReason], [task terminationStatus], SIGINT]);
	}

	{
		NSTask *task = [[NSTask alloc] init];
		BOOL suspended, resumed;

		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObject:@"--child-delay"]];
		[task launchAndReturnError:NULL];
		suspended = [task suspend];
		usleep(20000);			/* long enough for the stop to land */
		resumed = [task resume];
		[task waitUntilExit];
		/* THE ROUND TRIP IS THE CLAIM: two BOOLs answered YES and the child finished NORMALLY, which a
		 * stopped-forever child could not do. */
		check("task-suspend-and-resume",
		      suspended && resumed && [task terminationStatus] == 5,
		      [NSString stringWithFormat:@"suspend=%d resume=%d status=%d", (int)suspended,
			(int)resumed, [task terminationStatus]]);
	}

	printf("FOUNDATION-TASK trace 5: endings done\n");

	/* ---- the two side effects the reaper exists for ---------------------- */
	{
		FnTaskFixture *fixture = [[FnTaskFixture alloc] init];
		NSTask *task = [[NSTask alloc] init];
		BOOL fired;

		[[NSNotificationCenter defaultCenter] addObserver:fixture
							selector:@selector(note:)
							    name:NSTaskDidTerminateNotification
							  object:nil];
		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObjects:@"--child-exit", @"9", nil]];
		[task setTerminationHandler:^(NSTask *finished) {
			[fixture markFired:finished];
		}];
		[task launchAndReturnError:NULL];

		/* NOBODY CALLS -waitUntilExit HERE, which is the whole point: Apple says the handler runs when the
		 * task completes, not when somebody asks. */
		{
			NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];

			while (![fixture fired] && [deadline timeIntervalSinceNow] > 0) {
				usleep(2000);
			}
		}
		fired = [fixture fired] != 0;
		check("task-termination-handler-fires", fired,
		      @"the handler never ran without -waitUntilExit (the reaper is missing)");

		{
			NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];

			while ([fixture terminations] == 0 && [deadline timeIntervalSinceNow] > 0) {
				usleep(2000);
			}
		}
		check("task-did-terminate-notification",
		      [fixture terminations] == 1 && [fixture lastObject] == task,
		      [NSString stringWithFormat:@"notifications=%lu object-is-task=%d",
			(unsigned long)[fixture terminations], (int)([fixture lastObject] == task)]);

		[[NSNotificationCenter defaultCenter] removeObserver:fixture];
		[task waitUntilExit];
	}

	printf("FOUNDATION-TASK trace 6: reaper done\n");

	/* ---- what is refused ------------------------------------------------- */
	{
		NSTask *task = [[NSTask alloc] init];
		NSError *error = nil;
		BOOL second;

		[task setExecutableURL:self];
		[task setArguments:[NSArray arrayWithObjects:@"--child-exit", @"1", nil]];
		[task launchAndReturnError:NULL];
		[task waitUntilExit];
		error = nil;
		second = [task launchAndReturnError:&error];
		check("task-refuses-a-second-launch",
		      !second && error != nil,
		      [NSString stringWithFormat:@"the second launch answered %d (error %@)", (int)second,
			[error domain]]);
	}

	{
		NSTask *task = [[NSTask alloc] init];
		NSError *error = nil;
		BOOL launched;

		{
			/* `+fileURLWithPath:` is NULLABLE too, so it lands in a local before it is handed on. */
			NSURL *missing = [NSURL fileURLWithPath:@"/System/Tools/no-such-program"];

			[task setExecutableURL:missing];
		}
		[task setArguments:[NSArray array]];
		launched = [task launchAndReturnError:&error];
		check("task-reports-a-bad-executable",
		      !launched && error != nil &&
		      [[error domain] isEqualToString:@"NSPOSIXErrorDomain"],
		      [NSString stringWithFormat:@"launched=%d error=%@", (int)launched, [error domain]]);
	}

	{
		int raised = 0;

		@try {
			(void)[NSTask launchedTaskWithExecutableURL:self
							  arguments:nil
							      error:NULL
						   terminationHandler:nil];
		} @catch (NSException *exception) {
			raised = [exception.name isEqualToString:NSInvalidArgumentException];
		}
		check("task-nil-arguments-raises", raised,
		      @"nil arguments were accepted: Apple's own sentence is that they RAISE");
	}

	{
		NSError *error = nil;
		NSTask *task = [NSTask launchedTaskWithExecutableURL:self
							   arguments:[NSArray arrayWithObjects:
									@"--child-exit", @"3", nil]
							       error:&error
						    terminationHandler:nil];
		BOOL running = [task isRunning];

		[task waitUntilExit];
		check("task-static-launcher",
		      task != nil && [task terminationStatus] == 3,
		      [NSString stringWithFormat:@"task=%d running-when-returned=%d status=%d error=%@",
			(int)(task != nil), (int)running, task != nil ? [task terminationStatus] : -1,
			[error domain]]);
	}

	{
		NSTask *task = [[NSTask alloc] init];

		[task setQualityOfService:NSQualityOfServiceUserInitiated];
		check("task-quality-of-service",
		      [task qualityOfService] == NSQualityOfServiceUserInitiated,
		      [NSString stringWithFormat:@"the value came back as %d",
			(int)[task qualityOfService]]);
	}

	printf("FOUNDATION-TASK trace 7: refusals done\n");
	[manager removeItemAtPath:root error:NULL];

	printf("FOUNDATION-TASK RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: the console can stop serving input after a probe, so
	 * `echo $?` may never run. This is the same value: failc ? 1 : 0 is the return below. */
	printf("FOUNDATION-TASK-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-TASK DONE\n");
	return failc ? 1 : 0;
}
