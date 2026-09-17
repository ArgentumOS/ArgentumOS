/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * ndata.m — the byte buffer.
 *
 * ARC file. -copy of a MUTABLE data is a snapshot, as with the strings; -copy of
 * an immutable one is itself.
 */

#import <foundation/NSData.h>
#import <foundation/NSString.h>
#include <stdlib.h>
#include <string.h>

@implementation NSData

+ (NSData *)dataWithBytes:(const void *)bytes length:(size_t)length
{
	return [[self alloc] initWithBytes:bytes length:length];
}

- (id)initWithBytes:(const void *)bytes length:(size_t)length
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_length = length;
	_bytes = NULL;
	if (length > 0) {
		_bytes = (unsigned char *)malloc(length);
		if (_bytes == NULL) {
			return nil;
		}
		if (bytes != NULL) {
			memcpy(_bytes, bytes, length);
		} else {
			memset(_bytes, 0, length);
		}
	}
	return self;
}

- (size_t)length
{
	return _length;
}

- (const void *)bytes
{
	return _bytes;
}

- (BOOL)isEqualToData:(NSData *)other
{
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if ([other length] != _length) {
		return NO;
	}
	if (_length == 0) {
		return YES;
	}
	return memcmp([other bytes], _bytes, _length) == 0;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSData class]]) {
		return NO;
	}
	return [self isEqualToData:(NSData *)other];
}

- (unsigned long)hash
{
	/* FNV-1a over the bytes, so equal data hashes alike. */
	const unsigned char *bytes = (const unsigned char *)[self bytes];
	unsigned long h = 2166136261UL;
	size_t i;

	for (i = 0; i < _length; i++) {
		h ^= bytes[i];
		h *= 16777619UL;
	}
	return h;
}

- (id)copy
{
	return self;		/* immutable */
}

/*
 * The zone forms (the public-API audit, B4). Declared ONCE, on the base class:
 * each delegates to -copy / -mutableCopy, and DYNAMIC DISPATCH picks the right
 * one for a mutable subclass (whose -copy is the snapshot).
 */
- (id)copyWithZone:(NSZone *)zone
{
	(void)zone;
	return [self copy];
}

- (id)mutableCopyWithZone:(NSZone *)zone
{
	(void)zone;
	return [self mutableCopy];
}

- (id)mutableCopy
{
	NSMutableData *copy = [[NSMutableData alloc] initWithCapacity:_length];

	[copy appendBytes:_bytes length:_length];
	return copy;
}

- (NSString *)description
{
	/* Cocoa's shape: <aabbcc>. Built by hand rather than through a format
	 * string, because there is no +stringWithFormat: in v1. */
	static const char hex[] = "0123456789abcdef";
	const unsigned char *bytes = (const unsigned char *)[self bytes];
	size_t i;
	char *buffer;
	NSString *result;

	buffer = (char *)malloc(_length * 2 + 3);	/* "<" + 2/byte + ">" + NUL */
	if (buffer == NULL) {
		return [NSString stringWithUTF8String:"<data>"];
	}
	buffer[0] = '<';
	for (i = 0; i < _length; i++) {
		buffer[1 + i * 2] = hex[bytes[i] >> 4];
		buffer[2 + i * 2] = hex[bytes[i] & 0xF];
	}
	buffer[1 + _length * 2] = '>';
	buffer[2 + _length * 2] = '\0';
	result = [NSString stringWithUTF8String:buffer];
	free(buffer);
	return result;
}

- (void)dealloc
{
	free(_bytes);
}

@end

@implementation NSMutableData

+ (NSMutableData *)dataWithCapacity:(size_t)capacity
{
	return [[self alloc] initWithCapacity:capacity];
}

- (id)initWithCapacity:(size_t)capacity
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/*
	 * CAPACITY MEANS AN ALLOCATED BUFFER. Recording the number without
	 * allocating it is what let the first version of this class memcpy through
	 * a NULL pointer: the growth test compared the needed length against a
	 * capacity that nothing was behind.
	 */
	if (capacity > 0) {
		_bytes = (unsigned char *)malloc(capacity);
		if (_bytes == NULL) {
			return nil;
		}
		_capacity = capacity;
	}
	return self;
}

- (void)appendBytes:(const void *)bytes length:(size_t)length
{
	size_t needed;

	if (bytes == NULL || length == 0) {
		return;
	}
	needed = _length + length;
	if (_bytes == NULL || needed > _capacity) {
		size_t grown = (_capacity == 0) ? 16 : _capacity;
		unsigned char *grownBytes;

		while (grown < needed) {
			grown *= 2;
		}
		grownBytes = (unsigned char *)realloc(_bytes, grown);
		if (grownBytes == NULL) {
			return;
		}
		_bytes = grownBytes;
		_capacity = grown;
	}
	memcpy(_bytes + _length, bytes, length);
	_length = needed;
}

- (void)appendData:(NSData *)other
{
	[self appendBytes:[other bytes] length:[other length]];
}

- (void)setLength:(size_t)length
{
	if (_bytes == NULL || length > _capacity) {
		unsigned char *grownBytes;
		size_t grown = (_capacity == 0) ? 16 : _capacity;

		while (grown < length) {
			grown *= 2;
		}
		grownBytes = (unsigned char *)realloc(_bytes, grown);
		if (grownBytes == NULL) {
			return;
		}
		_bytes = grownBytes;
		_capacity = grown;
	}
	if (length > _length && _bytes != NULL) {
		memset(_bytes + _length, 0, length - _length);
	}
	_length = length;
}

- (void *)mutableBytes
{
	return _bytes;
}

- (id)copy
{
	/* A snapshot, like every other mutable type here. */
	return [[NSData alloc] initWithBytes:[self bytes] length:[self length]];
}

@end
