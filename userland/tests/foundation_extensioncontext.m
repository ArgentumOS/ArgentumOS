/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_extensioncontext.m — THE PROBE FOR §62.92: `NSExtensionContext`, `NSExtensionRequestHandling` and
 * `NSExtensionItemsAndErrorsKey`, which close `App Support / Extension Support`.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * THE CLASS, THE PROTOCOL AND THE KEY ARE DECLARED, and the protocol's door is REQUIRED — a probe that only
 *     called a method would pass against a class that never adopted anything;
 *   * the host's items ARRIVE AND ARE COPIED — the check mutates the array it handed over afterwards, which is
 *     the only way to tell a copy from a reference;
 *   * a context with no items holds AN EMPTY ARRAY, NOT NIL, because "the host sent nothing" is a fact;
 *   * THE HOST HANDS THE CONTEXT OVER and the extension's principal object receives THE SAME OBJECT it was made
 *     from (identity, not equality — two contexts could hold equal items);
 *   * completing runs the handler BEFORE THE DOOR RETURNS with `expired` NO, and the ending the seam reads back
 *     is what the extension returned — the round trip that is the whole point of a request;
 *   * a nil item list and a nil handler are both accepted (Apple declares both nullable, and the probe's nil case
 *     is what keeps the header's annotation honest);
 *   * a cancellation carries its error and DOES NOT call the completion handler;
 *   * ONE ENDING PER REQUEST — complete-then-complete, complete-then-cancel and cancel-then-complete all raise;
 *   * a cancellation with NO error raises;
 *   * and there is NO PUBLIC CONSTRUCTOR: `-init` raises, which is Apple's own description of where a context
 *     comes from.
 *
 * THE HOST'S HALF IS THE INTERNAL SEAM `FNExtensionContext.h`, exactly as §62.85 and §62.91 did it: the system has
 * no host process, so the seam plays one — it makes the context, hands it over, and reads the ending. The two
 * checks that no host exists are therefore about the CLASS (no constructor) and about the PUBLIC header (which the
 * case asserts carries no host door), not about a fabricated host.
 */

#import <Foundation/Foundation.h>
#import <Foundation/FNExtensionContext.h>	/* the host's half: the seam §62.85 and §62.91 also use */
#include <stdio.h>
#include <objc/runtime.h>
#include <stdlib.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-EXTENSIONCONTEXT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-EXTENSIONCONTEXT %s FAIL: %s\n", name, [why UTF8String]);
	}
}

static BOOL fn_raises(void (^block)(void))
{
	@try {
		block();
	} @catch (NSException *e) {
		return YES;
	}
	return NO;
}

/* AN EXTENSION'S PRINCIPAL OBJECT: it adopts the lifecycle protocol, records the context it was handed, and then
 * ends the request one of the two ways. `ended` is the witness that the handler ran. */
@interface FnProbeExtension : NSObject <NSExtensionRequestHandling>
{
@public
	id received;
	int begins;
}
@end

@implementation FnProbeExtension
- (void)beginRequestWithExtensionContext:(NSExtensionContext *)context
{
	begins++;
	received = context;	/* ARC: a strong ivar is the whole of it */
}
@end

/* ANSWERS WITH ITS INPUT as the result items — the echo an Action extension performs. */
@interface FnEchoExtension : NSObject <NSExtensionRequestHandling>
@end

@implementation FnEchoExtension
- (void)beginRequestWithExtensionContext:(NSExtensionContext *)context
{
	[context completeRequestReturningItems:[context inputItems] completionHandler:nil];
}
@end

int main(void)
{
	/* 1. THE NAMES. */
	{
		Protocol *p = objc_getProtocol("NSExtensionRequestHandling");
		struct objc_method_description d = protocol_getMethodDescription(
			p, @selector(beginRequestWithExtensionContext:), YES, YES);
		check("the-class-the-protocol-and-the-key-are-declared",
		      [NSExtensionContext class] != nil && p != NULL && d.name != NULL &&
		      [NSExtensionItemsAndErrorsKey isEqualToString:@"NSExtensionItemsAndErrorsKey"],
		      @"the class, the protocol and its one REQUIRED door, and the key beside the class");
	}

	/* 2. THE ITEMS ARRIVE, AS A COPY. */
	{
		NSExtensionItem *item = [[NSExtensionItem alloc] init];
		NSMutableArray *sent = [NSMutableArray arrayWithObject:item];
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(sent);

		/* The host's array is mutated AFTER delivery: a context that kept a reference would show it here. */
		[sent removeAllObjects];

		check("the-host-makes-a-context-and-it-carries-what-it-sent",
		      [[ctx inputItems] count] == 1 && [[ctx inputItems] objectAtIndex:0] == item,
		      @"the host's items arrived, and they are a copy rather than a reference");
	}

	/* 3. NO ITEMS IS AN EMPTY ARRAY, NOT NIL. */
	{
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(nil);

		check("a-context-with-no-items-holds-an-empty-array-not-nil",
		      [ctx inputItems] != nil && [[ctx inputItems] count] == 0,
		      @"'the host sent nothing' is a fact worth representing");
	}

	/* 4. THE HOST HANDS IT OVER; THE EXTENSION RECEIVES THE SAME OBJECT. */
	{
		FnProbeExtension *ext = [[FnProbeExtension alloc] init];
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(@[[[NSExtensionItem alloc] init]]);

		FNExtensionContextBeginRequestWithHandler(ctx, ext);

		check("the-host-hands-the-context-to-the-extension-s-principal-object",
		      ext->begins == 1 && ext->received == ctx &&
		      [ext conformsToProtocol:@protocol(NSExtensionRequestHandling)],
		      @"the lifecycle door ran once, with the very context that was made");
	}

	/* 5-8. COMPLETING. */
	{
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(@[]);
		NSExtensionItem *result = [[NSExtensionItem alloc] init];
		__block int handlerRuns = 0;
		__block BOOL expiredSeen = YES;
		BOOL endedBefore = FNExtensionContextHasEnded(ctx);

		[ctx completeRequestReturningItems:@[result] completionHandler:^(BOOL expired) {
			handlerRuns++;
			expiredSeen = expired;
		}];

		check("completing-runs-the-handler-before-the-door-returns-with-expired-no",
		      endedBefore == NO && handlerRuns == 1 && expiredSeen == NO,
		      @"the handler had ALREADY run when the door returned, and nothing terminated this request early");
		check("the-ending-items-are-what-the-extension-returned",
		      FNExtensionContextHasEnded(ctx) && [FNExtensionContextEndingItems(ctx) count] == 1 &&
		      [FNExtensionContextEndingItems(ctx) objectAtIndex:0] == result &&
		      FNExtensionContextEndingError(ctx) == nil,
		      @"the seam reads back what the extension returned, and no error");
	}

	/* A nil item list is an empty ending and a nil handler is accepted — Apple declares both nullable. */
	{
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(nil);

		[ctx completeRequestReturningItems:nil completionHandler:nil];

		check("a-nil-item-list-and-a-nil-handler-are-both-accepted",
		      FNExtensionContextEndingItems(ctx) != nil &&
		      [FNExtensionContextEndingItems(ctx) count] == 0 &&
		      FNExtensionContextEndingError(ctx) == nil,
		      @"an empty result is not the same as no result, and the handler is optional");
	}

	/* 9-10. CANCELLING. */
	{
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(nil);
		NSError *why = [NSError errorWithDomain:@"FnProbe" code:7 userInfo:nil];
		__block int handlerRuns = 0;

		[ctx cancelRequestWithError:why];

		check("cancelling-carries-the-error-and-does-not-call-the-handler",
		      FNExtensionContextHasEnded(ctx) &&
		      FNExtensionContextEndingError(ctx) == why &&
		      FNExtensionContextEndingItems(ctx) == nil && handlerRuns == 0,
		      @"a cancelled request has an error and no items, and no completion handler was invoked");
	}

	/* 11. ONE ENDING PER REQUEST, all three orders. */
	{
		NSExtensionContext *a = FNExtensionContextMakeWithInputItems(nil);
		NSExtensionContext *b = FNExtensionContextMakeWithInputItems(nil);
		NSExtensionContext *c = FNExtensionContextMakeWithInputItems(nil);
		NSError *why = [NSError errorWithDomain:@"FnProbe" code:7 userInfo:nil];
		BOOL secondComplete, cancelAfterComplete, completeAfterCancel;

		[a completeRequestReturningItems:nil completionHandler:nil];
		secondComplete = fn_raises(^{ [a completeRequestReturningItems:nil completionHandler:nil]; });

		[b completeRequestReturningItems:nil completionHandler:nil];
		cancelAfterComplete = fn_raises(^{ [b cancelRequestWithError:why]; });

		[c cancelRequestWithError:why];
		completeAfterCancel = fn_raises(^{ [c completeRequestReturningItems:nil completionHandler:nil]; });

		check("a-request-has-one-ending-and-a-second-one-raises",
		      secondComplete && cancelAfterComplete && completeAfterCancel,
		      @"a host is told once: a second ending goes to nobody, so it raises instead of being ignored");
	}

	/* 12. A CANCELLATION MUST SAY WHY. */
	{
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(nil);
		id noReason = nil;	/* the annotation is non-null because the door REFUSES nil; this proves it */

		check("a-cancellation-without-an-error-raises",
		      fn_raises(^{ [ctx cancelRequestWithError:noReason]; }) && !FNExtensionContextHasEnded(ctx),
		      @"a cancellation nobody can explain is not a report, and the refused call left no ending");
	}

	/* 13. NO PUBLIC CONSTRUCTOR. */
	check("the-class-has-no-public-constructor",
	      fn_raises(^{ id made = [[NSExtensionContext alloc] init]; (void)made; }),
	      @"a context comes from the host, so -init refuses");

	/* 14. THE ECHO AN ACTION EXTENSION PERFORMS, end to end through the seam. */
	{
		FnEchoExtension *ext = [[FnEchoExtension alloc] init];
		NSExtensionItem *input = [[NSExtensionItem alloc] init];
		NSExtensionContext *ctx = FNExtensionContextMakeWithInputItems(@[input]);

		FNExtensionContextBeginRequestWithHandler(ctx, ext);

		check("an-extension-echoes-its-input-back-to-the-host",
		      FNExtensionContextHasEnded(ctx) &&
		      [FNExtensionContextEndingItems(ctx) count] == 1 &&
		      [FNExtensionContextEndingItems(ctx) objectAtIndex:0] == input,
		      @"the whole flow ran in one process: host, extension, and the items back");
	}

	/* 15. THE PUBLIC SURFACE IS APPLE'S DOORS AND NOTHING ELSE — every other method on the class is the seam's
	 * (`fn`-prefixed). A context comes from the host, so this class must not grow a second way to make one, and a
	 * runtime walk of the method list is what turns that from a claim in a comment into a check. */
	{
		unsigned int count = 0, i;
		Method *ms = class_copyMethodList([NSExtensionContext class], &count);
		NSMutableArray *strays = [NSMutableArray array];

		for (i = 0; i < count; i++) {
			NSString *name = NSStringFromSelector(method_getName(ms[i]));

			if ([name isEqualToString:@"inputItems"] ||
			    [name isEqualToString:@"completeRequestReturningItems:completionHandler:"] ||
			    [name isEqualToString:@"cancelRequestWithError:"] ||
			    [name isEqualToString:@"init"] || [name isEqualToString:@"dealloc"] ||
			    [name hasPrefix:@"fn"] || [name hasPrefix:@"."])	/* libobjc2's own dot-prefixed slots */
				continue;
			[strays addObject:name];
		}
		free(ms);

		check("the-public-surface-is-apple-s-doors-and-nothing-else",
		      [strays count] == 0,
		      [NSString stringWithFormat:@"neither Apple's doors nor the seam's: %@", strays]);
	}

	printf("FOUNDATION-EXTENSIONCONTEXT RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-EXTENSIONCONTEXT-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-EXTENSIONCONTEXT DONE\n");
	return failc ? 1 : 0;
}
