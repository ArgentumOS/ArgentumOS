/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_operation_leaves — the two CONCRETE operations: `NSBlockOperation` and `NSInvocationOperation`
 * (docs/design/foundation-plan.md §62.22). ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THE TWO CLASSES ARE THE OPPOSITE HALVES OF ONE IDEA: a block operation is several units of work that belong to one
 * operation, and an invocation operation is one method call held as an object. What they share is that both replace
 * "subclass NSOperation and override -main" — which is why the checks are about what a caller can SEE (what ran,
 * what it answered, what it raised).
 *
 * THE INHERITED MACHINERY IS ASSERTED AS WELL AS THE NEW: `-isFinished`, `-isExecuting`, dependencies and
 * `-waitUntilFinished` are NSOperation's, and a subclass that broke them would be a defect this probe would find.
 *
 * AND THE TWO NAMES THAT HAVE BEEN IN `NSException.h` SINCE F4 FINALLY HAVE A RAISER: `-result` is the door that
 * raises `NSInvocationOperationVoidResultException` (a void return type has no value) and
 * `NSInvocationOperationCancelledException` (a cancelled operation has no result), and both are caught BY NAME.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "FOUNDATION-OPERATION-LEAVES"

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf(PREFIX " %s ok\n", name);
	} else {
		failc++;
		printf(PREFIX " %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* A LOG THE BLOCKS AND THE FIXTURE WRITE TO, because the ORDER is one of the things being asserted and a counter
 * cannot see it. */
static char fn_log[256];
static int fn_logged;

static void fn_note(const char *what)
{
	size_t n = strlen(what);

	if (fn_logged + (int)n + 2 < (int)sizeof fn_log) {
		memcpy(fn_log + fn_logged, what, n);
		fn_logged += (int)n;
		fn_log[fn_logged++] = ',';
		fn_log[fn_logged] = '\0';
	}
}

/* THE TARGET OF THE INVOCATION OPERATIONS: three methods, chosen for the three shapes `-result` has to tell apart —
 * an object return, a scalar return, and nothing at all — plus one that raises, and one that reports whether its
 * ARGUMENT survived being retained. THE FIXTURE IS A PLAIN NSObject SUBCLASS AND ITS METHODS ARE IN THIS PROBE, which
 * is the honest shape for testing a class whose whole job is to reach somebody else's method. */
@interface FNLeafTarget : NSObject
- (void)noArguments;
- (void)takeObject:(id)object;
- (id)answerObject;
- (int)answerInt;
- (void)raiseIt;
- (BOOL)sawArgument;
@end

@implementation FNLeafTarget
{
	id _saw;
}

- (void)noArguments
{
	fn_note("no-args");
}

- (void)takeObject:(id)object
{
	_saw = object;
	fn_note("took");
}

- (id)answerObject
{
	return @"the answer";
}

- (int)answerInt
{
	return 42;
}

- (void)raiseIt
{
	[NSException raise:NSInvalidArgumentException format:@"from inside the invocation"];
}

- (BOOL)sawArgument
{
	return _saw != nil;
}

@end

/* A BLOCK CAPTURING A LOCAL OF A FUNCTION THAT HAS RETURNED: the only way to see that the block was COPIED rather
 * than borrowed. */
static NSBlockOperation *fn_operation_over_a_dead_frame(void)
{
	NSString *captured = [NSString stringWithFormat:@"frame-%d", 7];

	return [NSBlockOperation blockOperationWithBlock:^{
		fn_note([captured UTF8String]);
	}];
}

int main(void)
{
	FNLeafTarget *target = [[FNLeafTarget alloc] init];

	/* ---- NSBlockOperation: WHAT RAN, IN WHAT ORDER --------------------------------------------------- */
	{
		__block int count = 0;
		NSBlockOperation *operation = [NSBlockOperation blockOperationWithBlock:^{
			count++;
		}];

		[operation start];
		check("a-block-operation-runs-its-block-and-finishes",
		      count == 1 && [operation isFinished] && ![operation isExecuting] &&
		      [operation isCancelled] == NO,
		      [[NSString stringWithFormat:@"count=%d finished=%d executing=%d",
			count, (int)[operation isFinished], (int)[operation isExecuting]] UTF8String]);
	}
	{
		NSBlockOperation *operation = [[NSBlockOperation alloc] init];

		fn_logged = 0;
		fn_log[0] = '\0';
		[operation addExecutionBlock:^{ fn_note("first"); }];
		[operation addExecutionBlock:^{ fn_note("second"); }];
		[operation addExecutionBlock:^{ fn_note("third"); }];
		[operation start];
		check("several-blocks-run-in-the-order-they-were-added",
		      strcmp(fn_log, "first,second,third,") == 0 && [operation isFinished] &&
		      [[operation executionBlocks] count] == 3,
		      [[NSString stringWithFormat:@"log=[%s] blocks=%lu", fn_log,
			(unsigned long)[[operation executionBlocks] count]] UTF8String]);
	}
	{
		/* APPLE'S SENTENCE, MEASURED FROM INSIDE: "when executing more than one block, the operation itself is
		 * considered finished only when all blocks have finished executing" — so DURING a block the operation is
		 * executing and not finished, and only after the last one is it finished. */
		NSBlockOperation *operation = [[NSBlockOperation alloc] init];
		__block BOOL executingDuring = NO;
		__block BOOL finishedDuring = NO;

		[operation addExecutionBlock:^{
			executingDuring = [operation isExecuting];
			finishedDuring = [operation isFinished];
		}];
		[operation addExecutionBlock:^{
			if (![operation isFinished] && [operation isExecuting]) {
				fn_note("still-running");
			}
		}];
		fn_logged = 0;
		fn_log[0] = '\0';
		[operation start];
		check("the-operation-is-not-finished-while-its-blocks-are-running",
		      executingDuring && !finishedDuring && [operation isFinished] &&
		      strcmp(fn_log, "still-running,") == 0,
		      [[NSString stringWithFormat:@"executingDuring=%d finishedDuring=%d finishedAfter=%d log=[%s]",
			(int)executingDuring, (int)finishedDuring, (int)[operation isFinished], fn_log]
			UTF8String]);
	}
	{
		/* APPLE'S DOCUMENTED REFUSAL, FROM BOTH SIDES: after the run it is FINISHED, and from inside a block it is
		 * EXECUTING — "calling this method while the receiver is executing or has already finished causes an
		 * NSInvalidArgumentException exception to be thrown". */
		NSBlockOperation *operation = [NSBlockOperation blockOperationWithBlock:^{ fn_note("ran"); }];
		BOOL afterFinished = NO;
		__block BOOL whileExecuting = NO;

		[operation start];
		@try {
			[operation addExecutionBlock:^{ fn_note("too-late"); }];
		} @catch (NSException *e) {
			afterFinished = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		{
			/* A WEAK ALIAS, because the block has to reach the operation it belongs to: capturing it strongly is a
			 * retain cycle (the operation owns the block), which ARC says so about — and a cycle in a probe is a leak
			 * that would outlive the check. */
			NSBlockOperation *running = [[NSBlockOperation alloc] init];
			__weak NSBlockOperation *watched = running;

			[running addExecutionBlock:^{
				@try {
					[watched addExecutionBlock:^{ fn_note("mid-run"); }];
				} @catch (NSException *e) {
					whileExecuting = [[e name] isEqualToString:NSInvalidArgumentException];
				}
			}];
			[running start];
		}
		check("adding-a-block-while-executing-or-finished-raises-by-name",
		      afterFinished && whileExecuting,
		      [[NSString stringWithFormat:@"afterFinished=%d whileExecuting=%d",
			(int)afterFinished, (int)whileExecuting] UTF8String]);
	}
	{
		/* THE BLOCK'S FRAME IS GONE BY THE TIME THIS RUNS, which is what the copy is for; and the array is an
		 * IMMUTABLE snapshot rather than the operation's own mutable list. */
		NSBlockOperation *operation = fn_operation_over_a_dead_frame();

		fn_logged = 0;
		fn_log[0] = '\0';
		[operation start];
		check("a-block-added-from-a-dead-frame-still-runs-and-the-array-is-a-snapshot",
		      strcmp(fn_log, "frame-7,") == 0 &&
		      ![[operation executionBlocks] isKindOfClass:[NSMutableArray class]] &&
		      [[operation executionBlocks] count] == 1 &&
		      [[operation executionBlocks] objectAtIndex:0] != nil,
		      [[NSString stringWithFormat:@"log=[%s] immutable=%d count=%lu",
			fn_log, (int)![[operation executionBlocks] isKindOfClass:[NSMutableArray class]],
			(unsigned long)[[operation executionBlocks] count]] UTF8String]);
	}
	{
		__block int count = 0;
		NSBlockOperation *operation = [NSBlockOperation blockOperationWithBlock:^{ count++; }];

		[operation cancel];
		[operation start];
		check("a-cancelled-operation-that-never-ran-runs-nothing-and-is-finished",
		      count == 0 && [operation isCancelled] && [operation isFinished],
		      [[NSString stringWithFormat:@"count=%d cancelled=%d finished=%d",
			count, (int)[operation isCancelled], (int)[operation isFinished]] UTF8String]);
	}
	{
		/* THE GRANULAR RULE THIS CLASS STATES: a block operation checks for cancellation BEFORE EACH BLOCK, so
		 * cancelling from inside one skips the rest. A block already running is never interrupted. */
		NSBlockOperation *operation = [[NSBlockOperation alloc] init];
		__weak NSBlockOperation *cancelling = operation;	/* the same reason as above */

		fn_logged = 0;
		fn_log[0] = '\0';
		[operation addExecutionBlock:^{
			fn_note("one");
			[cancelling cancel];
		}];
		[operation addExecutionBlock:^{ fn_note("two"); }];
		[operation addExecutionBlock:^{ fn_note("three"); }];
		[operation start];
		check("cancelling-from-inside-skips-the-blocks-not-yet-reached",
		      strcmp(fn_log, "one,") == 0 && [operation isCancelled] && [operation isFinished],
		      [[NSString stringWithFormat:@"log=[%s] cancelled=%d finished=%d",
			fn_log, (int)[operation isCancelled], (int)[operation isFinished]] UTF8String]);
	}
	{
		NSBlockOperation *empty = [[NSBlockOperation alloc] init];

		[empty start];
		check("an-operation-with-no-blocks-finishes-without-running-anything",
		      [empty isFinished] && [[empty executionBlocks] count] == 0,
		      [[NSString stringWithFormat:@"finished=%d blocks=%lu",
			(int)[empty isFinished], (unsigned long)[[empty executionBlocks] count]] UTF8String]);
	}
	{
		/* THE INHERITED MACHINERY, THROUGH A SUBCLASS THAT DID NOT TOUCH IT: before the dependency runs the
		 * dependent is NOT ready, and after it is. */
		NSBlockOperation *dependency = [NSBlockOperation blockOperationWithBlock:^{ fn_note("dep"); }];
		NSBlockOperation *dependent = [NSBlockOperation blockOperationWithBlock:^{ fn_note("after"); }];

		[dependent addDependency:dependency];
		{
			BOOL notReadyYet = ![dependent isReady];

			[dependency start];
			check("a-dependent-operation-is-ready-only-after-its-dependency-has-run",
			      notReadyYet && [dependent isReady] && [[dependent dependencies] count] == 1,
			      [[NSString stringWithFormat:@"notReadyYet=%d readyNow=%d dependencies=%lu",
				(int)notReadyYet, (int)[dependent isReady],
				(unsigned long)[[dependent dependencies] count]] UTF8String]);
		}
	}

	/* ---- NSInvocationOperation: WHAT IT CALLED, WHAT IT ANSWERED, WHAT IT RAISED --------------------- */
	{
		FNLeafTarget *zero = [[FNLeafTarget alloc] init];
		NSInvocationOperation *operation =
			[[NSInvocationOperation alloc] initWithTarget:zero
							     selector:@selector(noArguments)
							       object:nil];
		BOOL sawInvocation = ([operation invocation] != nil);

		fn_logged = 0;
		fn_log[0] = '\0';
		[operation start];
		check("an-invocation-operation-calls-a-selector-that-takes-nothing",
		      operation != nil && sawInvocation && strcmp(fn_log, "no-args,") == 0 &&
		      [operation isFinished] && [[operation invocation] argumentsRetained],
		      [[NSString stringWithFormat:@"log=[%s] finished=%d retained=%d",
			fn_log, (int)[operation isFinished],
			(int)[[operation invocation] argumentsRetained]] UTF8String]);
	}
	{
		NSString *argument = [NSString stringWithFormat:@"argument-%d", 99];
		NSInvocationOperation *operation =
			[[NSInvocationOperation alloc] initWithTarget:target
							     selector:@selector(takeObject:)
							       object:argument];

		fn_logged = 0;
		fn_log[0] = '\0';
		[operation start];
		check("the-object-is-passed-and-the-invocation-retains-it",
		      strcmp(fn_log, "took,") == 0 && [target sawArgument] &&
		      [[operation invocation] argumentsRetained] && [operation isFinished],
		      [[NSString stringWithFormat:@"log=[%s] saw=%d retained=%d",
			fn_log, (int)[target sawArgument],
			(int)[[operation invocation] argumentsRetained]] UTF8String]);
	}
	{
		/* APPLE'S DOCUMENTED NIL: a target with no such method has nothing to invoke. */
		NSInvocationOperation *missing =
			[[NSInvocationOperation alloc] initWithTarget:target
							     selector:@selector(thisSelectorDoesNotExist)
							       object:nil];
		id noTarget = nil;
		NSInvocationOperation *nilTarget =
			[[NSInvocationOperation alloc] initWithTarget:noTarget
							     selector:@selector(noArguments)
							       object:nil];

		check("the-convenience-door-answers-nil-when-there-is-no-method-to-invoke",
		      missing == nil && nilTarget == nil,
		      [[NSString stringWithFormat:@"missing=%s nilTarget=%s",
			missing == nil ? "nil" : "NOT NIL", nilTarget == nil ? "nil" : "NOT NIL"] UTF8String]);
	}
	{
		NSInvocationOperation *object =
			[[NSInvocationOperation alloc] initWithTarget:target
							     selector:@selector(answerObject)
							       object:nil];
		NSInvocationOperation *number =
			[[NSInvocationOperation alloc] initWithTarget:target
							     selector:@selector(answerInt)
							       object:nil];
		id beforeRun = [object result];
		int unboxed = 0;

		[object start];
		[number start];
		/* THE BYTES ARE THE ANSWER: a scalar return is carried as an NSValue, and reading it back is what proves
		 * the LENGTH and the TYPE were both right rather than that a pointer came back. */
		[(NSValue *)[number result] getValue:&unboxed];
		check("result-answers-an-object-as-itself-and-a-scalar-as-an-nsvalue",
		      beforeRun == nil &&
		      [[object result] isKindOfClass:[NSString class]] &&
		      [[object result] isEqualToString:@"the answer"] &&
		      [[number result] isKindOfClass:[NSValue class]] &&
		      [[number result] objCType] != NULL &&
		      [[number result] objCType][0] == 'i' && unboxed == 42,
		      [[NSString stringWithFormat:@"before=%@ object=%@ number=%@ type=%s",
			beforeRun, [object result], [number result],
			[[number result] isKindOfClass:[NSValue class]] ? [[number result] objCType] : "(not a value)"]
			UTF8String]);
	}
	{
		/* F4's TWO NAMES, RAISED BY THE DOOR THE HEADER THEY LIVE IN POINTED AT. */
		NSInvocationOperation *voidResult =
			[[NSInvocationOperation alloc] initWithTarget:target
							     selector:@selector(noArguments)
							       object:nil];
		BOOL voidRaised = NO;
		NSInvocationOperation *cancelled =
			[[NSInvocationOperation alloc] initWithTarget:target
							     selector:@selector(answerObject)
							       object:nil];
		BOOL cancelRaised = NO;

		[voidResult start];
		@try {
			(void)[voidResult result];
		} @catch (NSException *e) {
			voidRaised = [[e name] isEqualToString:NSInvocationOperationVoidResultException];
		}
		[cancelled cancel];
		[cancelled start];
		@try {
			(void)[cancelled result];
		} @catch (NSException *e) {
			cancelRaised = [[e name] isEqualToString:NSInvocationOperationCancelledException];
		}
		check("a-void-return-and-a-cancelled-operation-each-raise-their-own-name",
		      voidRaised && cancelRaised && [cancelled isFinished],
		      [[NSString stringWithFormat:@"void=%d cancelled=%d cancelledFinished=%d",
			(int)voidRaised, (int)cancelRaised, (int)[cancelled isFinished]] UTF8String]);
	}
	{
		/* AN EXCEPTION FROM THE RUN IS NOT SWALLOWED AND NOT THROWN OUT OF -start: it waits at -result, which is
		 * Apple's sentence. The run must therefore COMPLETE (the operation is finished) with the failure kept. */
		NSInvocationOperation *operation =
			[[NSInvocationOperation alloc] initWithTarget:target
							     selector:@selector(raiseIt)
							       object:nil];
		BOOL raisedOutOfStart = NO;
		BOOL raisedAtResult = NO;
		BOOL named = NO;

		@try {
			[operation start];
		} @catch (NSException *e) {
			raisedOutOfStart = YES;
		}
		@try {
			(void)[operation result];
		} @catch (NSException *e) {
			raisedAtResult = YES;
			named = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("an-exception-from-the-run-is-raised-again-by-result",
		      !raisedOutOfStart && [operation isFinished] && raisedAtResult && named,
		      [[NSString stringWithFormat:@"outOfStart=%d finished=%d atResult=%d named=%d",
			(int)raisedOutOfStart, (int)[operation isFinished], (int)raisedAtResult,
			(int)named] UTF8String]);
	}
	{
		/* THE DESIGNATED DOOR TAKES A READY-MADE INVOCATION, which is how a caller with a non-object argument
		 * reaches this class at all. */
		NSMethodSignature *signature = [target methodSignatureForSelector:@selector(takeObject:)];
		NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
		NSString *argument = [NSString stringWithFormat:@"hand-built-%d", 5];
		NSInvocationOperation *operation;
		id boxed = nil;

		[invocation setTarget:target];
		[invocation setSelector:@selector(takeObject:)];
		[invocation setArgument:&argument atIndex:2];
		operation = [[NSInvocationOperation alloc] initWithInvocation:invocation];
		fn_logged = 0;
		fn_log[0] = '\0';
		[operation start];
		{
			id noInvocation = nil;

			boxed = [[NSInvocationOperation alloc] initWithInvocation:noInvocation];
		}
		check("the-designated-door-accepts-a-hand-built-invocation-and-refuses-nil",
		      operation != nil && [[operation invocation] isEqual:invocation] &&
		      strcmp(fn_log, "took,") == 0 && boxed == nil,
		      [[NSString stringWithFormat:@"log=[%s] sameInvocation=%d nilInvocation=%s",
			fn_log, (int)[[operation invocation] isEqual:invocation],
			boxed == nil ? "nil" : "NOT NIL"] UTF8String]);
	}

	printf(PREFIX " RESULT ok=%d fail=%d\n", okc, failc);
	printf(PREFIX "-STATUS=%d\n", failc ? 1 : 0);
	printf(PREFIX " DONE\n");
	return failc ? 1 : 0;
}
