/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSInputStream — READING, FROM DATA OR FROM A FILE. docs/design/foundation-plan.md W6's streams half, §45-Y
 * sub-step 2.
 *
 * THE HEAD OWNS THE STATUS MACHINE, THE PROPERTY BAG AND THE RUN-LOOP SEAM (NSStream.h); WHAT IS HERE IS THE
 * SOURCE: bytes in memory, or a descriptor this class opens, reads and closes.
 *
 * AND THE HALF THAT MAKES IT A STREAM RATHER THAN A READER is the run-loop source: `-fnStreamDescriptor`
 * answers the descriptor, the base registers it with the run loop, and readiness arrives as a DELEGATE CALL
 * instead of a thread blocked in read(2). That is why `-fnStreamEventForReadiness` is overridden here: a
 * descriptor that is ready and EMPTY is the END of the stream, and the delegate hears
 * `NSStreamEventEndEncountered` for it rather than bytes that are not there.
 *
 * TWO THINGS ARE OURS, STATED BECAUSE APPLE PUBLISHES NEITHER:
 *
 *   * HOW END IS DETECTED. A descriptor that select(2) calls ready and that has NOTHING to read is at end of
 *     file (FIONREAD answers 0), and that is the rule used - it is the only one that needs no read to consume
 *     a byte the caller has not asked for, which is the whole reason a stream PEERS at readiness.
 *   * A DATA STREAM HAS NO DESCRIPTOR. `-initWithData:` reads from the bytes it was given, so
 *     `-fnStreamDescriptor` answers -1: it is scheduled like any stream and simply never fires, because its
 *     bytes are already here. That is a refusal to invent an event, not a gap.
 *
 * NOT DECLARED, NAMED: the socket-bound initialisers Apple documents for NSInputStream (`-initWithSocket:`
 * and friends) need the network half this library has no service for (the network-bag decision in the head),
 * so they are a refusal rather than a stub.
 */

#ifndef FOUNDATION_NSINPUTSTREAM_H
#define FOUNDATION_NSINPUTSTREAM_H

#import <Foundation/NSStream.h>

@class NSData;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

@interface NSInputStream : NSStream
{
	NSData *_data;			/* the memory source, or nil for a descriptor */
	NSUInteger _offset;		/* how far into it we are */
	NSString *_path;		/* the file to open at -open time, or nil */
	int _fd;			/* the descriptor, or -1 */
}

+ (nullable instancetype)inputStreamWithData:(NSData *)data;
+ (nullable instancetype)inputStreamWithFileAtPath:(NSString *)path;
+ (nullable instancetype)inputStreamWithURL:(NSURL *)url;

- (nullable instancetype)initWithData:(NSData *)data;
- (nullable instancetype)initWithFileAtPath:(NSString *)path;
- (nullable instancetype)initWithURL:(NSURL *)url;

/* THE PRIMITIVE: 0 means END OF STREAM and -1 means an error, which is not the same thing and is why the
 * return is signed. Reading a closed stream answers -1 with the stream in Error. */
- (NSInteger)read:(uint8_t *)buffer maxLength:(NSUInteger)len;

/* A memory stream can hand out its own bytes without a copy; a descriptor-backed one answers NO, which is
 * Apple's contract rather than a limitation (the bytes are not in memory). */
- (BOOL)getBuffer:(uint8_t * _Nullable * _Nonnull)buffer length:(NSUInteger *)len;

/* NO means "a read would block" (Apple), and it is a PEEK: nothing is consumed to answer it. */
- (BOOL)hasBytesAvailable;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSINPUTSTREAM_H */
