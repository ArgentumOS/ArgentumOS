/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_stream — the NSStream head's acceptance. docs/design/foundation-plan.md W6's streams half, §45-Y.
 *
 * THE HEAD IS WHAT IS PROVED HERE, and the two things a head can get wrong are exactly what it is tested
 * for: the VALUES it invents (Apple publishes the case names and not the numbers, so ours are a contract
 * that has to be pinned somewhere), and the SEAM it offers a substream (whether a descriptor made ready
 * really reaches a delegate through the run loop without a thread blocking on a read).
 *
 * SO THE PROBE BUILDS A SUBSTREAM. `FnTestStream` is the smallest thing that uses the head the way a real
 * input stream will: it answers `-fnStreamDescriptor`, overrides `-open` to move the status through the
 * private hook, and then the run loop's own file-descriptor source fires the delegate when the pipe has a
 * byte in it. That is the whole point of scheduling a stream, and it is asserted rather than assumed.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-STREAM %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-STREAM %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* ---- a substream, the way an NSInputStream will be one ---- */

@interface FnTestStream : NSStream
{
	int _fd;
}
- (instancetype)initWithFileDescriptor:(int)fd;
@end

@implementation FnTestStream

- (instancetype)initWithFileDescriptor:(int)fd
{
	self = [super init];
	if (self != nil) {
		_fd = fd;
	}
	return self;
}

- (int)fnStreamDescriptor
{
	return _fd;
}

- (BOOL)fnStreamWatchesReadable
{
	return YES;
}

/* A SUBSTREAM MOVES THE STATUS, because only it knows whether its resource opened; the base stays inert. */
- (void)open
{
	[super open];
	[self fnStreamSetStatus:NSStreamStatusOpen error:nil];
}

@end

/* ---- a delegate that counts what it hears ---- */

@interface FnStreamDelegate : NSObject <NSStreamDelegate>
{
	int _events;
	NSStreamEvent _last;
}
- (int)events;
- (NSStreamEvent)last;
@end

@implementation FnStreamDelegate

- (void)stream:(NSStream *)aStream handleEvent:(NSStreamEvent)eventCode
{
	(void)aStream;
	_events++;
	_last = eventCode;
}

- (int)events
{
	return _events;
}

- (NSStreamEvent)last
{
	return _last;
}

@end

int main(void)
{
	/* ---- 1. THE VALUES ARE OURS, SO THEY ARE PINNED HERE (plan D2) ---- */
	check("event-mask-values",
	      NSStreamEventNone == 0 && NSStreamEventOpenCompleted == (1 << 0) &&
	      NSStreamEventHasBytesAvailable == (1 << 1) && NSStreamEventHasSpaceAvailable == (1 << 2) &&
	      NSStreamEventErrorOccurred == (1 << 3) && NSStreamEventEndEncountered == (1 << 4),
	      [NSString stringWithFormat:@"none=%lu open=%lu bytes=%lu space=%lu error=%lu end=%lu",
		(unsigned long)NSStreamEventNone, (unsigned long)NSStreamEventOpenCompleted,
		(unsigned long)NSStreamEventHasBytesAvailable, (unsigned long)NSStreamEventHasSpaceAvailable,
		(unsigned long)NSStreamEventErrorOccurred, (unsigned long)NSStreamEventEndEncountered]);

	check("status-values",
	      NSStreamStatusNotOpen == 0 && NSStreamStatusOpening == 1 && NSStreamStatusOpen == 2 &&
	      NSStreamStatusReading == 3 && NSStreamStatusWriting == 4 && NSStreamStatusAtEnd == 5 &&
	      NSStreamStatusClosed == 6 && NSStreamStatusError == 7,
	      [NSString stringWithFormat:@"notopen=%d open=%d closed=%d error=%d",
		(int)NSStreamStatusNotOpen, (int)NSStreamStatusOpen, (int)NSStreamStatusClosed,
		(int)NSStreamStatusError]);

	/* THE KEY STRINGS ARE OUR CHOICE TOO, and each one is its own name - the convention a config reader
	 * would expect. Asserted individually so a later typo cannot pass as "a string". */
	check("property-key-strings",
	      [NSStreamFileCurrentOffsetKey isEqualToString:@"NSStreamFileCurrentOffsetKey"] &&
	      [NSStreamDataWrittenToMemoryStreamKey isEqualToString:@"NSStreamDataWrittenToMemoryStreamKey"] &&
	      [NSStreamSocketSecurityLevelKey isEqualToString:@"NSStreamSocketSecurityLevelKey"] &&
	      [NSStreamSOCKSProxyConfigurationKey isEqualToString:@"NSStreamSOCKSProxyConfigurationKey"] &&
	      [NSStreamSOCKSProxyVersionKey isEqualToString:@"NSStreamSOCKSProxyVersionKey"] &&
	      [NSStreamNetworkServiceType isEqualToString:@"NSStreamNetworkServiceType"],
	      @"a key's value is not its own name");

	check("network-bag-values",
	      [NSStreamSocketSecurityLevelTLSv1 isEqualToString:@"NSStreamSocketSecurityLevelTLSv1"] &&
	      [NSStreamSOCKSProxyVersion5 isEqualToString:@"NSStreamSOCKSProxyVersion5"] &&
	      [NSStreamNetworkServiceTypeVoice isEqualToString:@"NSStreamNetworkServiceTypeVoice"] &&
	      [NSStreamSocketSSLErrorDomain isEqualToString:@"NSStreamSocketSSLErrorDomain"] &&
	      [NSStreamSOCKSErrorDomain isEqualToString:@"NSStreamSOCKSErrorDomain"],
	      @"a carried constant's value is not its own name");

	/* ---- 2. THE BASE IS ABSTRACT AND INERT: -open MOVES NOTHING ---- */
	{
		NSStream *bare = [[NSStream alloc] init];

		check("bare-stream-starts-not-open",
		      [bare streamStatus] == NSStreamStatusNotOpen && [bare streamError] == nil,
		      @"a fresh NSStream is not NotOpen");
		[bare open];
		check("bare-stream-open-is-inert",
		      [bare streamStatus] == NSStreamStatusNotOpen,
		      [NSString stringWithFormat:@"-open moved a bare NSStream to %d", (int)[bare streamStatus]]);
	}

	/* ---- 3. THE PROPERTY BAG RECORDS, ANSWERS AND FORGETS ---- */
	{
		NSStream *stream = [[NSStream alloc] init];

		check("property-bag-round-trip",
		      [stream propertyForKey:NSStreamFileCurrentOffsetKey] == nil &&
		      [stream setProperty:@5 forKey:NSStreamFileCurrentOffsetKey] &&
		      [[stream propertyForKey:NSStreamFileCurrentOffsetKey] intValue] == 5,
		      @"the property bag did not round-trip a value");
		(void)[stream setProperty:nil forKey:NSStreamFileCurrentOffsetKey];
		check("property-bag-forgets",
		      [stream propertyForKey:NSStreamFileCurrentOffsetKey] == nil,
		      @"setting nil did not remove the property");
		check("property-bag-refuses-a-nil-key",
		      ![stream setProperty:@1 forKey:(NSStreamPropertyKey)nil] &&
		      [stream propertyForKey:(NSStreamPropertyKey)nil] == nil,
		      @"a nil key was accepted");
	}

	/* ---- 4. THE SEAM: A READY DESCRIPTOR REACHES THE DELEGATE THROUGH THE RUN LOOP ---- */
	{
		int fds[2];
		NSRunLoop *loop = [NSRunLoop currentRunLoop];

		if (pipe(fds) != 0) {
			check("source-fires-delegate", 0, @"pipe(2) failed, so the seam could not be tested");
		} else {
			FnTestStream *stream = [[FnTestStream alloc] initWithFileDescriptor:fds[0]];
			FnStreamDelegate *delegate = [[FnStreamDelegate alloc] init];

			[stream setDelegate:delegate];
			[stream scheduleInRunLoop:loop forMode:NSDefaultRunLoopMode];
			[stream open];
			check("substream-moves-the-status",
			      [stream streamStatus] == NSStreamStatusOpen,
			      [NSString stringWithFormat:@"a substream's -open left the status at %d",
				(int)[stream streamStatus]]);

			/* NOTHING IS READY YET, and the loop must simply return. */
			(void)[loop runMode:NSDefaultRunLoopMode
				 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
			check("no-event-without-readiness",
			      [delegate events] == 0,
			      [NSString stringWithFormat:@"%d event(s) fired with an empty pipe", [delegate events]]);

			/* NOW IT IS READY, and the SAME loop must fire the delegate - no thread blocked on a read. */
			if (write(fds[1], "x", 1) == 1) {
				(void)[loop runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:1.0]];
				check("source-fires-delegate",
				      [delegate events] > 0 &&
				      ([delegate last] & NSStreamEventHasBytesAvailable) != 0,
				      [NSString stringWithFormat:@"%d event(s), last=%lu",
					[delegate events], (unsigned long)[delegate last]]);
			} else {
				check("source-fires-delegate", 0, @"could not make the pipe readable");
			}

			[stream removeFromRunLoop:loop forMode:NSDefaultRunLoopMode];
			[stream close];
			(void)close(fds[0]);
			(void)close(fds[1]);
		}
	}

	printf("FOUNDATION-STREAM RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-STREAM-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-STREAM DONE\n");
	return failc ? 1 : 0;
}
