/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBlockOperation.m — the implementation, MANUAL OWNERSHIP. docs/design/foundation-plan.md §62.22.
 *
 * THE WHOLE CLASS IS `-main`: it walks the blocks it was given, skips the rest when it has been cancelled, and
 * returns — at which point the inherited `-start` marks the operation finished, which is exactly the contract Apple
 * states. Everything else is bookkeeping about WHEN a block may be added.
 *
 * A BLOCK IS NEVER COPIED, RETAINED OR RELEASED WITH A MESSAGE HERE, and that is not a style preference: this
 * tree's gate refuses `copy`/`retain`/`release`/`autorelease` sent to a block-typed name, because `-copy` is a
 * MESSAGE SEND that makes the runtime read the block's ISA — measured in the URL session as a null-page fault and a
 * garbage instruction pointer that presented as "the library crashes for no reason"
 * (tools/foundation-gate.py records the investigation; userland/tests/fn_block_mrc.m is its discriminator).
 * `Block_copy`/`Block_release` are the runtime's own ENTRY POINTS and cannot depend on the isa, so the block this
 * class stores is a HEAP block whose isa is valid — which is exactly what the recording says the crashing case was
 * NOT. What remains is the array's own retain/release of an element, which is the blocks runtime's business (a
 * `_NSConcreteMallocBlock` implements both), and the probe asserts that path by reading `-executionBlocks`.
 */

#import <Foundation/NSBlockOperation.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSException.h>

#include <Block.h>

@implementation NSBlockOperation

+ (instancetype)blockOperationWithBlock:(void (^)(void))block
{
	NSBlockOperation *operation = [[[self alloc] init] autorelease];

	[operation addExecutionBlock:block];
	return operation;
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_blocks = [[NSMutableArray alloc] init];
	return self;
}

- (void)dealloc
{
	NSMutableArray *blocks = _blocks;

	_blocks = nil;
	[blocks release];
	[super dealloc];
}

- (void)addExecutionBlock:(void (^)(void))block
{
	if ([self isExecuting] || [self isFinished]) {
		/* APPLE'S DOCUMENTED RAISE, and the reason is worth restating where it happens: a block added after the run
		 * started would either be silently ignored or would move the moment "finished" meant, and a caller waiting
		 * on the operation has already been promised the first of those. */
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSBlockOperation addExecutionBlock:]: the operation is already %@",
				   [self isExecuting] ? @"executing" : @"finished"];
	}
	/* THE COPY IS A RUNTIME CALL, AND THE OWNERSHIP IS THE ARRAY'S: `Block_copy` answers +1 (the block is on the
	 * heap now, so the caller's stack frame going away cannot take it with it), the array retains it, and this
	 * method releases ITS OWN reference — so the array holds the only one and `-dealloc` above is the whole of the
	 * cleanup. */
	{
		void (^heap)(void) = Block_copy(block);

		if (heap != NULL) {
			[_blocks addObject:heap];
			Block_release(heap);
		}
	}
}

- (NSArray *)executionBlocks
{
	return [[_blocks copy] autorelease];
}

/* THE HOOK, and the order is the order the blocks were added. A CANCELLED OPERATION SKIPS WHAT IT HAS NOT REACHED —
 * the check is here because a block operation is the one concrete operation whose units a caller can count, so this
 * is the only place in the family where cancelling mid-run can be granular. A block already running is never
 * interrupted; no thread is killed here. */
- (void)main
{
	NSUInteger i;

	for (i = 0; i < [_blocks count]; i++) {
		void (^step)(void);

		if ([self isCancelled]) {
			return;
		}
		/* AN ASSIGNMENT, NOT A RETAIN: the array owns it for the length of this loop, which is all the call needs. */
		step = [_blocks objectAtIndex:i];
		step();
	}
}

@end
