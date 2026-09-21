/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSOutputStream — WRITING, TO MEMORY, TO A BUFFER OR TO A FILE. docs/design/foundation-plan.md W6's streams
 * half, §45-Y sub-step 3.
 *
 * THREE DESTINATIONS, AND THEY DIFFER IN ONE INTERESTING WAY: a memory stream grows, a caller's buffer does
 * NOT (its capacity is the caller's, and -hasSpaceAvailable answers the truth about it), and a file is the
 * descriptor case on the same seam the input side uses - `-fnStreamDescriptor` answers it, the base registers
 * it with the run loop, and readiness arrives as `NSStreamEventHasSpaceAvailable` instead of a write blocking.
 * The writable half of the descriptor, with the file-readiness rule the kernel now has (§45-Z), is delivered
 * the same way the readable half is.
 *
 * AND THE DATA WRITTEN IS READ BACK THROUGH THE KEY THE HEAD ALREADY DECLARES: for a memory or buffer stream
 * `-propertyForKey:NSStreamDataWrittenToMemoryStreamKey` answers the bytes written, which is Apple's contract
 * for that key and the reason the head declared it as one of the two this library ACTS on.
 *
 * TWO THINGS ARE OURS, STATED BECAUSE APPLE PUBLISHES NEITHER:
 *
 *   * A BUFFER STREAM WRITES WHAT FITS AND SAYS SO. `-write:maxLength:` answers the number of bytes taken,
 *     which is 0 when the caller's buffer is full rather than an error - the buffer has no room, and the
 *     caller owns the capacity.
 *   * APPEND IS THE OPEN FLAG. `-initToFileAtPath:append:` chooses O_APPEND or O_TRUNC when the stream opens,
 *     because a stream opens a resource at -open rather than at -init (the same rule the input side follows).
 */

#ifndef FOUNDATION_NSOUTPUTSTREAM_H
#define FOUNDATION_NSOUTPUTSTREAM_H

#import <Foundation/NSStream.h>

@class NSMutableData;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

@interface NSOutputStream : NSStream
{
	NSMutableData *_memory;		/* the growing memory stream, or nil */
	uint8_t *_buffer;		/* THE CALLER'S buffer, not ours: never freed here */
	NSUInteger _capacity;		/* its size, 0 when there is no buffer */
	NSUInteger _written;		/* how much has gone into either of the two above */
	NSString *_path;		/* the file to open at -open, or nil */
	BOOL _append;			/* O_APPEND rather than O_TRUNC, for that file */
	int _fd;			/* the descriptor once open, or -1 */
}

+ (nullable instancetype)outputStreamToMemory;
+ (nullable instancetype)outputStreamToBuffer:(uint8_t *)buffer capacity:(NSUInteger)capacity;
+ (nullable instancetype)outputStreamToFileAtPath:(NSString *)path append:(BOOL)shouldAppend;
+ (nullable instancetype)outputStreamWithURL:(NSURL *)url append:(BOOL)shouldAppend;

- (nullable instancetype)initToMemory;
- (nullable instancetype)initToBuffer:(uint8_t *)buffer capacity:(NSUInteger)capacity;
- (nullable instancetype)initToFileAtPath:(NSString *)path append:(BOOL)shouldAppend;
- (nullable instancetype)initWithURL:(NSURL *)url append:(BOOL)shouldAppend;

/* THE PRIMITIVE: the number of bytes TAKEN, which may be fewer than asked for, and -1 for an error. A full
 * caller's buffer answers 0. */
- (NSInteger)write:(const uint8_t *)buffer maxLength:(NSUInteger)len;

/* NO means a write would block, or that the caller's buffer is full. */
- (BOOL)hasSpaceAvailable;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSOUTPUTSTREAM_H */
