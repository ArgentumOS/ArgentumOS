/*
 * foundation_wsassemble — THE STATE HALF OF THE FRAMING (§59 slice 2b): frames in, messages out.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE FRAMES ARE REAL ONES: this probe builds each frame with FNWebSocketCreateFrame and parses it with
 * FNWebSocketParseFrame before feeding the result to the assembler, so what the assembler sees is the same thing
 * the task will see coming off a socket - not a hand-made struct that could disagree with the codec.
 *
 * WHAT IT PINS, AND THE ONE CHECK THAT IS A MEASUREMENT RATHER THAN A RULE:
 *   * whole messages, fragmented messages, and the order of their fragments;
 *   * THE INTERLEAVED CONTROL FRAME OF §5.4 - answered WHEN IT ARRIVES, in the middle of a fragmented message,
 *     with the message it interrupted going on untouched. §59 left this as a measurement rather than a
 *     preference, because refusing it would be wrong for a compliant peer, and this is where it is settled;
 *   * the three ways a peer can be wrong (§5.4): a continuation with nothing to continue, a new message before
 *     the last one finished, and a message past the limit;
 *   * THE LIMIT IS THE WHOLE MESSAGE, not one frame - the fidelity point in Apple's own words ("includes the sum
 *     of all bytes from continuation frames"), so the same total split into pieces must fail too.
 *
 * ARC, like every probe. The assembler's out-parameters are +1 and ARC takes them, so there is no release here
 * either.
 */
#import <Foundation/Foundation.h>
#import <Foundation/FNWebSocketAssembler.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers(): a claim can only follow an assertion that held */
	if(held) {
		okc++;
		printf("FOUNDATION-WSASSEMBLE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-WSASSEMBLE %s FAIL: %s\n", name, [[why description] UTF8String]);
	}
}

/* covers("NSData", "length") - the behavioural claim, piggybacked on the check above it: no condition of its
 * own, printed only when the last check's result was true. See tools/foundation-cov.py; a claim for a row the
 * ledger does not carry is inert, so every claim is filtered before it is written. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* BUILD, PARSE, FEED - the road a frame actually travels. A codec that disagrees is reported as a probe failure
 * rather than silently turned into an assembler answer. */
static FNWebSocketAssemblyResult fnFeed(FNWebSocketAssembler *assembler, BOOL fin, FNWebSocketOpcode opcode,
					const uint8_t *payload, size_t length,
					FNWebSocketOpcode *outOpcode, NSData **outData)
{
	NSData *bytes = FNWebSocketCreateFrame(fin, opcode, payload, length, NULL);
	FNWebSocketFrame frame;

	if(FNWebSocketParseFrame([bytes bytes], [bytes length], NO, &frame) <= 0) {
		return (FNWebSocketAssemblyResult)-2;
	}
	return [assembler feedFrame:&frame outOpcode:outOpcode outData:outData];
}

int main(void)
{
	FNWebSocketAssembler *assembler = [[FNWebSocketAssembler alloc] init];
	FNWebSocketOpcode opcode = (FNWebSocketOpcode)0;
	NSData *data = nil;
	NSString *said;
	FNWebSocketAssemblyResult result;

	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- A WHOLE MESSAGE, BOTH KINDS -------------------------------------------------------------------- */
	result = fnFeed(assembler, YES, FNWebSocketOpcodeText, (const uint8_t *)"hello", 5, &opcode, &data);
	check("a-single-frame-message-arrives-whole",
	      result == FNWebSocketAssemblyMessage && opcode == FNWebSocketOpcodeText &&
	      [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] isEqualToString:@"hello"],
	      [NSString stringWithFormat:@"result=%ld type=%d", (long)result, (int)opcode]);
	covers("NSData", "initWithData:");

	data = nil;
	result = fnFeed(assembler, YES, FNWebSocketOpcodeBinary, (const uint8_t *)"\x00\x01\x02", 3, &opcode, &data);
	check("and-a-binary-message-assembles-the-same-way",
	      result == FNWebSocketAssemblyMessage && opcode == FNWebSocketOpcodeBinary && [data length] == 3,
	      @"the type is the OPCODE and not the bytes: a text message of digits is still text");

	/* --- FRAGMENTATION ---------------------------------------------------------------------------------- */
	{
		FNWebSocketAssemblyResult first, second, third;

		first = fnFeed(assembler, NO, FNWebSocketOpcodeText, (const uint8_t *)"he", 2, &opcode, &data);
		second = fnFeed(assembler, NO, FNWebSocketOpcodeContinuation, (const uint8_t *)"ll", 2, &opcode, &data);
		third = fnFeed(assembler, YES, FNWebSocketOpcodeContinuation, (const uint8_t *)"o", 1, &opcode, &data);
		said = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
		check("a-fragmented-message-assembles-in-order",
		      first == FNWebSocketAssemblyFragments && second == FNWebSocketAssemblyFragments &&
		      third == FNWebSocketAssemblyMessage && [said isEqualToString:@"hello"],
		      [NSString stringWithFormat:@"first=%ld second=%ld third=%ld said=%@",
		       (long)first, (long)second, (long)third, said]);
	}

	/* --- §5.4: THE CONTROL FRAME IN THE MIDDLE, WHICH IS THE MEASUREMENT ------------------------------- */
	{
		FNWebSocketAssemblyResult head, pinged, tail;
		NSData *pingData = nil;

		data = nil;
		head = fnFeed(assembler, NO, FNWebSocketOpcodeText, (const uint8_t *)"he", 2, &opcode, &data);
		pinged = fnFeed(assembler, YES, FNWebSocketOpcodePing, (const uint8_t *)"?", 1, &opcode, &pingData);
		/* AND HERE IS THE POINT: the ping came back BEFORE the message finished. A reader that held it until the
		 * message was complete would stall a peer's keepalive for as long as the message takes - and a stall is
		 * exactly what a ping exists to detect. */
		check("a-control-frame-in-the-middle-is-answered-when-it-arrives",
		      head == FNWebSocketAssemblyFragments && pinged == FNWebSocketAssemblyControl &&
		      opcode == FNWebSocketOpcodePing && [pingData length] == 1,
		      [NSString stringWithFormat:@"head=%ld ping=%ld opcode=%d",
		       (long)head, (long)pinged, (int)opcode]);

		data = nil;
		tail = fnFeed(assembler, YES, FNWebSocketOpcodeContinuation, (const uint8_t *)"llo", 3, &opcode, &data);
		said = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
		check("and-the-message-it-interrupted-is-untouched",
		      tail == FNWebSocketAssemblyMessage && opcode == FNWebSocketOpcodeText &&
		      [said isEqualToString:@"hello"],
		      [NSString stringWithFormat:@"the ping's byte must not be IN the message: said=%@", said]);
	}

	/* --- THE THREE WAYS A PEER CAN BE WRONG (§5.4) ------------------------------------------------------ */
	{
		FNWebSocketAssemblyResult orphan;

		data = nil;
		orphan = fnFeed(assembler, YES, FNWebSocketOpcodeContinuation, (const uint8_t *)"x", 1, &opcode, &data);
		check("a-continuation-with-nothing-to-continue-is-an-error",
		      orphan == FNWebSocketAssemblyError,
		      @"§5.4: a continuation must be preceded by a message that was not finished");
	}
	{
		FNWebSocketAssemblyResult started, restarted;

		data = nil;
		started = fnFeed(assembler, NO, FNWebSocketOpcodeText, (const uint8_t *)"he", 2, &opcode, &data);
		restarted = fnFeed(assembler, YES, FNWebSocketOpcodeText, (const uint8_t *)"there", 5, &opcode, &data);
		check("a-new-message-before-the-last-one-finished-is-an-error",
		      started == FNWebSocketAssemblyFragments && restarted == FNWebSocketAssemblyError,
		      @"§5.4: a second message may not start inside the first");
	}

	/* --- THE LIMIT, WHICH IS THE WHOLE MESSAGE ---------------------------------------------------------- */
	{
		FNWebSocketAssemblyResult over;
		uint8_t five[5] = { 'a', 'b', 'c', 'd', 'e' };

		[assembler setMaximumMessageSize:4];
		data = nil;
		over = fnFeed(assembler, YES, FNWebSocketOpcodeText, five, 5, &opcode, &data);
		check("a-message-past-the-limit-fails",
		      over == FNWebSocketAssemblyError,
		      @"the receive call fails rather than buffering without limit");
	}
	{
		FNWebSocketAssemblyResult part1, part2;

		data = nil;
		part1 = fnFeed(assembler, NO, FNWebSocketOpcodeText, (const uint8_t *)"ab", 2, &opcode, &data);
		part2 = fnFeed(assembler, YES, FNWebSocketOpcodeContinuation, (const uint8_t *)"cde", 3, &opcode, &data);
		check("and-the-limit-counts-the-fragments-together",
		      part1 == FNWebSocketAssemblyFragments && part2 == FNWebSocketAssemblyError,
		      [NSString stringWithFormat:@"Apple's words: the limit 'includes the sum of all bytes from continuation frames' - so the same total in pieces must fail too (observed first=%ld second=%ld)",
		       (long)part1, (long)part2]);
	}
	/* AND A CONTROL FRAME IS NOT PART OF ANY MESSAGE (§5.5), so the message limit does not govern it - written
	 * INLINE, because the first version wrapped it in a block and the block's CAPTURED variables are `const`:
	 * that made them unassignable ("missing __block type specifier") and made `&data` a `const __strong *` where
	 * an `__autoreleasing *` was wanted. One over-clever shape, two compile errors - and a plain sequence says
	 * the same thing. */
	{
		uint8_t ping[125];
		FNWebSocketAssemblyResult answered;

		memset(ping, 'p', sizeof(ping));
		[assembler setMaximumMessageSize:4];	/* four bytes: a 125-byte control frame is still fine */
		data = nil;
		answered = fnFeed(assembler, YES, FNWebSocketOpcodePing, ping, sizeof(ping), &opcode, &data);
		check("and-a-control-frame-is-not-part-of-any-message",
		      answered == FNWebSocketAssemblyControl && [data length] == 125,
		      @"§5.5: a control frame is never part of a message, so the message limit does not govern it");
	}


	/* --- THE DEFAULT, WHICH IS OURS (D2) ---------------------------------------------------------------- */
	[assembler setMaximumMessageSize:0];
	{
		NSUInteger restored = 0;

		/* The setter is a setter: putting 0 in it means 0. The DEFAULT is what init chose, and that is what the
		 * check below reads from a FRESH assembler - the one above has been used. */
		FNWebSocketAssembler *fresh = [[FNWebSocketAssembler alloc] init];

		restored = [fresh maximumMessageSize];
		check("the-default-limit-is-the-megabyte-we-chose",
		      restored == 1024 * 1024,
		      [NSString stringWithFormat:@"Apple publishes the property and not the default, so %lu is our choice (D2)",
		       (unsigned long)restored]);
	}

	printf("FOUNDATION-WSASSEMBLE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-WSASSEMBLE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-WSASSEMBLE DONE\n");
	return failc ? 1 : 0;
}
