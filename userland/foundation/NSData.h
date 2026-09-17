/*
 * NSData / NSMutableData — an owned byte buffer.
 * docs/design/foundation-plan.md, F2.
 *
 * The buffer is the object's own: `-initWithBytes:length:` copies, so a caller
 * can hand over a stack array and walk away. `-copy` of a MUTABLE data is a
 * snapshot, the same rule the strings follow.
 */

#ifndef FOUNDATION_NSDATA_H
#define FOUNDATION_NSDATA_H

#import <foundation/NSObject.h>
#include <stddef.h>

@interface NSData : NSObject <NSCopying>
{
	unsigned char *_bytes;		/* owned; NULL only while empty */
	size_t _length;
}

+ (NSData *)dataWithBytes:(const void *)bytes length:(size_t)length;
- (id)initWithBytes:(const void *)bytes length:(size_t)length;

- (size_t)length;
- (const void *)bytes;

- (BOOL)isEqualToData:(NSData *)other;

@end

@interface NSMutableData : NSData <NSMutableCopying>
{
	size_t _capacity;
}

+ (NSMutableData *)dataWithCapacity:(size_t)capacity;
- (id)initWithCapacity:(size_t)capacity;

- (void)appendBytes:(const void *)bytes length:(size_t)length;
- (void)appendData:(NSData *)other;
- (void)setLength:(size_t)length;
- (void *)mutableBytes;

@end

#endif /* FOUNDATION_NSDATA_H */
