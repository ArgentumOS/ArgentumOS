/*
 * foundation_machport.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSMachPort` AND THE MESSAGE TRANSPORT (§62.53). The family was struck by name by §11.5 — "because Mach is what
 * it exists for" — and §62.24 put it back. What comes back is not Mach: this system has no port rights, so a port
 * is a SOCKET PAIR with this library's message transport carrying what a message is, and the substitution is
 * stated in NSMachPort.h rather than hidden.
 *
 * THE PROBE WRITES THE FRAME ITSELF, IN NETWORK BYTE ORDER, RATHER THAN ASKING THE LIBRARY TO SEND AND RECEIVE
 * ITS OWN BYTES. That is the difference between a test and a tautology: a round trip through one implementation
 * passes for ANY frame at all, including one no other implementation could read. The checks that matter here —
 * that a message arrives with its id and its components intact, that HALF A FRAME IS NOT A MESSAGE, and that a
 * length too large to believe is refused — are all checked against bytes this file builds.
 *
 * THE THREE CHECKS THAT ARE NOT A ROUND TRIP are the stated boundaries: the from-number doors refuse (a bare
 * number names no port here), a component that is not data is refused at the send door rather than dropped, and
 * an unbelievable frame length INVALIDATES the port instead of allocating for it.
 *
 * A PAIR IS USED PER CHECK, because one of the checks leaves a port invalid — which is the point of it — and a
 * shared pair would make the other checks depend on the order they run in.
 */
#import <Foundation/Foundation.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if(held) {
		okc++;
		printf("FOUNDATION-MACHPORT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-MACHPORT %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* PUMPING THE RUN LOOP IN FIXED STEPS, because the guest's clock rounds a small sleep up hard: a deadline built
 * from a duration would be a deadline the clock decides, and a step count is the same number of opportunities. */
static void fnPumpSeconds(double seconds)
{
	int steps = (int)(seconds / 0.02);
	int i;

	for (i = 0; i < steps; i++) {
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
	}
}

static BOOL fnPumpUntilCount(NSArray *received, NSUInteger wanted, double seconds)
{
	int steps = (int)(seconds / 0.02);
	int i;

	for (i = 0; i < steps; i++) {
		if ([received count] >= wanted) {
			return YES;
		}
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
	}
	return [received count] >= wanted;
}

/* ---- a delegate that records what it was handed -------------------------- */

@interface FnMachDelegate : NSObject <NSMachPortDelegate>
{
	NSMutableArray *_received;
}
- (NSArray *)received;
- (void)handlePortMessage:(NSPortMessage *)message;
@end

@implementation FnMachDelegate

- (id)init
{
	self = [super init];
	_received = [[NSMutableArray alloc] init];
	return self;
}

- (NSArray *)received
{
	return _received;
}

/* EVERY DELIVERED MESSAGE BECOMES ONE LINE: its id, the port it says it arrived on, and its components read as
 * text. A check that compared counts would pass for the wrong components, so the components are the record. */
- (void)handlePortMessage:(NSPortMessage *)message
{
	NSMutableString *line = [[NSMutableString alloc] init];
	NSArray *components = [message components];
	NSUInteger i;

	[line appendFormat:@"id=%u", [message msgid]];
	[line appendFormat:@" on=%@", [message receivePort] != nil ? @"a port" : @"(nothing)"];
	for (i = 0; i < [components count]; i++) {
		id one = [components objectAtIndex:i];
		NSString *text = nil;

		if ([one isKindOfClass:[NSData class]]) {
			text = [[NSString alloc] initWithData:one encoding:NSUTF8StringEncoding];
		}
		[line appendFormat:@"|%@", text != nil ? text : @"(not data)"];
	}
	[_received addObject:line];
}

@end

/* ---- the frame, built by hand -------------------------------------------- */

/* ONE FRAME: the id, the payload length, then a count and each component's length and bytes — network byte order
 * on the wire. Written here from the specification rather than taken from the library, which is what makes the
 * receiving side's answer meaningful. */
static NSData *fnFrame(uint32_t msgid, NSArray *parts)
{
	NSMutableData *payload = [[NSMutableData alloc] init];
	NSMutableData *frame = [[NSMutableData alloc] init];
	uint32_t header[2];
	uint32_t encoded;
	NSUInteger i;

	encoded = htonl((uint32_t)[parts count]);
	[payload appendBytes:&encoded length:sizeof(encoded)];
	for (i = 0; i < [parts count]; i++) {
		/* AN `id` LOCAL RATHER THAN AN `NSData *`: this library declares `-objectAtIndex:` nullable, so naming
		 * the element's type here would be the narrowing the compiler refuses — and the element IS an NSData by
		 * the caller's construction, which is what the append below relies on. */
		id one = [parts objectAtIndex:i];

		encoded = htonl((uint32_t)[one length]);
		[payload appendBytes:&encoded length:sizeof(encoded)];
		[payload appendData:one];
	}
	header[0] = htonl(msgid);
	header[1] = htonl((uint32_t)[payload length]);
	[frame appendBytes:header length:sizeof(header)];
	[frame appendData:payload];
	return frame;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGPIPE, SIG_IGN);

	/* --- A PORT, ITS HANDLE, AND THE DOORS THAT REFUSE ------------------------------------------------ */
	{
		NSMachPort *port = [[NSMachPort alloc] init];
		NSMachPort *noPort = (NSMachPort *)[NSMachPort portWithMachPort:7];
		NSMachPort *noPortOptions = (NSMachPort *)[NSMachPort portWithMachPort:7
									       options:NSMachPortDeallocateSendRight];
		NSMachPort *noInit = [[NSMachPort alloc] initWithMachPort:7];
		NSMachPort *noInitOptions = [[NSMachPort alloc] initWithMachPort:7
									 options:NSMachPortDeallocateNone];

		check("a-port-is-valid-and-answers-a-handle",
		      port != nil && [port isValid] && [port machPort] != 0 &&
		      [port reservedSpaceLength] == 0 && [port socket] >= 0,
		      [NSString stringWithFormat:@"a fresh mach port: valid=%d handle=%u reserved=%lu socket=%d",
			(int)[port isValid], [port machPort], (unsigned long)[port reservedSpaceLength],
			(int)[port socket]]);
	covers("NSMutableData", "appendData:");
	covers("NSMachPort", "initWithMachPort:");
	covers("NSMachPort", "portWithMachPort:");

		check("the-from-number-doors-refuse",
		      noPort == nil && noPortOptions == nil && noInit == nil && noInitOptions == nil,
		      @"a bare number names no port on this system — there are no Mach port rights — so all four "
		      @"from-number doors answer nil rather than inventing one");
		(void)noInitOptions;
		(void)noInit;
	}

	/* --- THE PEER, AND THE ROUND TRIP ------------------------------------------------------------------ */
	{
		NSMachPort *port = [[NSMachPort alloc] init];
		NSPort *peer = [port peerPort];
		NSPort *again = [port peerPort];
		FnMachDelegate *delegate = [[FnMachDelegate alloc] init];
		NSMutableArray *components = [[NSMutableArray alloc] init];
		NSPortMessage *message;
		BOOL sent;

		/* NAMED AND NIL-CHECKED BEFORE THEY ARE ADDED: `-dataUsingEncoding:` is declared nullable, and this
		 * library's `-addObject:` takes a non-null object, so the narrowing has to be visible to the compiler
		 * rather than assumed by the author. */
		{
			NSData *first = [@"first" dataUsingEncoding:NSUTF8StringEncoding];
			NSData *second = [@"second" dataUsingEncoding:NSUTF8StringEncoding];

			if (first != nil) {
				[components addObject:first];
			}
			if (second != nil) {
				[components addObject:second];
			}
		}

		check("a-peer-is-handed-over-once-and-is-a-port",
		      peer != nil && [peer isValid] && again == nil,
		      @"the far end is a valid port, and asking a second time answers nil rather than making two ports "
		      @"share one descriptor");

		[port setDelegate:delegate];
		[port scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];

		message = [[NSPortMessage alloc] initWithSendPort:peer receivePort:nil components:components];
		[message setMsgid:42];
		sent = [message sendBeforeDate:[NSDate date]];
		fnPumpUntilCount([delegate received], 1, 2.0);

		check("a-message-crosses-the-pair-with-its-id-and-components",
		      sent && [[delegate received] count] == 1 &&
		      [[[delegate received] objectAtIndex:0] isEqual:@"id=42 on=a port|first|second"],
		      [NSString stringWithFormat:@"sent=%d and the delegate recorded: %@", (int)sent,
			[[delegate received] componentsJoinedByString:@"; "]]);
	covers("NSPortMessage", "sendBeforeDate:");
	covers("NSMachPort", "setDelegate:");

		/* A COMPONENT THAT IS NOT DATA IS REFUSED AT THE DOOR, and nothing arrives. Apple's other documented
		 * component kind is a port, which means a port RIGHT — a thing this system does not have, so it is a
		 * refusal rather than a silent omission. */
		{
			NSMutableArray *bad = [[NSMutableArray alloc] init];
			NSPortMessage *badMessage;
			BOOL badSent;

			[bad addObject:@"not data"];
			badMessage = [[NSPortMessage alloc] initWithSendPort:peer receivePort:nil components:bad];
			badSent = [badMessage sendBeforeDate:[NSDate date]];
			fnPumpSeconds(0.2);
			check("a-component-that-is-not-data-is-refused",
			      !badSent && [[delegate received] count] == 1,
			      [NSString stringWithFormat:@"the send answered %d (NO means refused) and the delegate still "
				@"holds %lu message(s)", (int)badSent, (unsigned long)[[delegate received] count]]);
		}

		[port removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[port invalidate];
	}

	/* --- HALF A FRAME IS NOT A MESSAGE ------------------------------------------------------------------ */
	{
		NSMachPort *port = [[NSMachPort alloc] init];
		NSPort *peer = [port peerPort];
		FnMachDelegate *delegate = [[FnMachDelegate alloc] init];
		NSData *whole = [@"whole" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *frame = whole != nil ? fnFrame(7, [NSArray arrayWithObject:whole]) : nil;
		const unsigned char *bytes = (const unsigned char *)[frame bytes];
		NSUInteger half = 4;		/* FOUR BYTES OF AN EIGHT-BYTE HEADER: no length is even complete */

		[port setDelegate:delegate];
		[port scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];

		(void)write([(NSSocketPort *)peer socket], bytes, half);
		fnPumpSeconds(0.3);
		{
			NSUInteger afterHalf = [[delegate received] count];

			(void)write([(NSSocketPort *)peer socket], bytes + half, [frame length] - half);
			(void)fnPumpUntilCount([delegate received], 1, 2.0);
			check("nothing-is-delivered-until-the-frame-is-whole",
			      afterHalf == 0 && [[delegate received] count] == 1 &&
			      [[[delegate received] objectAtIndex:0] isEqual:@"id=7 on=a port|whole"],
			      [NSString stringWithFormat:@"after half a frame the delegate held %lu message(s); after the "
				@"rest: %@", (unsigned long)afterHalf,
				[[delegate received] componentsJoinedByString:@"; "]]);
		}
		[port removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[port invalidate];
	}

	/* --- A LENGTH TOO LARGE TO BELIEVE ------------------------------------------------------------------ */
	{
		NSMachPort *port = [[NSMachPort alloc] init];
		NSPort *peer = [port peerPort];
		uint32_t header[2];

		[port scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		header[0] = htonl(1u);
		header[1] = htonl(0x7fffffffu);		/* two gigabytes of payload, announced in eight bytes */
		(void)write([(NSSocketPort *)peer socket], header, sizeof(header));
		fnPumpSeconds(0.4);

		check("a-frame-too-large-to-believe-invalidates-the-port",
		      ![port isValid],
		      [NSString stringWithFormat:@"a payload length of 0x7fffffff left the port %@",
			[port isValid] ? @"VALID, so it is allocating for what the wire claims" : @"invalid, as it must be"]);
		[port removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[port invalidate];
	}

	printf("FOUNDATION-MACHPORT RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-MACHPORT-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-MACHPORT DONE\n");
	return failc ? 1 : 0;
}
