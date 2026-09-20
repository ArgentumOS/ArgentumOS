/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUUID.m — the implementation (W2h).
 */
#import <Foundation/NSUUID.h>
#import <Foundation/NSException.h>
#include <string.h>
#include <unistd.h>		/* getentropy */

@implementation NSUUID

+ (instancetype)UUID
{
	return [[self alloc] init];
}

/*
 * THE BYTES COME FROM THE KERNEL'S ENTROPY POOL, not from a clock or a counter: an identifier that
 * can repeat is not an identifier. getentropy is the door musl provides for exactly this, and it
 * either fills the buffer or fails — so a failure is LOUD rather than a silently weak UUID, which
 * is the one outcome nothing downstream could detect.
 */
- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (getentropy(_bytes, sizeof(_bytes)) != 0) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"+[NSUUID UUID] could not reach the kernel's entropy source"];
		return nil;	/* -raise: does not return */
	}
	/* THE VERSION AND THE VARIANT ARE A RULE, NOT RANDOM: version 4 (random) in the high nibble of
	 * byte 6, variant 10xx in the top two bits of byte 8. That is what makes the string form's
	 * third group start with 4 and its fourth with 8, 9, a or b. */
	_bytes[6] = (unsigned char)((_bytes[6] & 0x0F) | 0x40);
	_bytes[8] = (unsigned char)((_bytes[8] & 0x3F) | 0x80);
	return self;
}

- (instancetype)initWithUUIDBytes:(const unsigned char [16])bytes
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	memcpy(_bytes, bytes, sizeof(_bytes));
	return self;
}

- (instancetype)initWithUUIDString:(NSString *)string
{
	unsigned char parsed[16];
	const char *text = [string UTF8String];
	unsigned long out = 0;
	int high = -1;

	if (text == NULL) {
		return nil;
	}
	for (; *text != '\0'; text++) {
		int value;
		char c = *text;

		if (c == '-') {
			/* the separators sit at fixed positions, so a dash anywhere else is malformed */
			if (out != 4 && out != 6 && out != 8 && out != 10) {
				return nil;
			}
			continue;
		}
		if (c >= '0' && c <= '9') {
			value = c - '0';
		} else if (c >= 'a' && c <= 'f') {
			value = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			value = c - 'A' + 10;
		} else {
			return nil;
		}
		if (high < 0) {
			high = value;
		} else {
			if (out >= 16) {
				return nil;
			}
			parsed[out++] = (unsigned char)((high << 4) | value);
			high = -1;
		}
	}
	if (out != 16 || high >= 0) {
		return nil;
	}
	return [self initWithUUIDBytes:parsed];
}

- (void)getUUIDBytes:(unsigned char [16])uuid
{
	memcpy(uuid, _bytes, sizeof(_bytes));
}

- (NSString *)UUIDString
{
	return [NSString stringWithFormat:
		 @"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		 _bytes[0], _bytes[1], _bytes[2], _bytes[3], _bytes[4], _bytes[5], _bytes[6], _bytes[7],
		 _bytes[8], _bytes[9], _bytes[10], _bytes[11], _bytes[12], _bytes[13], _bytes[14],
		 _bytes[15]];
}

- (NSString *)description
{
	return [self UUIDString];
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSUUID class]]) {
		return NO;
	}
	return memcmp(_bytes, ((NSUUID *)other)->_bytes, sizeof(_bytes)) == 0;
}

/* THE HASH IS OURS AND DOCUMENTED AS SUCH: Cocoa's is unspecified, and the only requirement is
 * that equal identifiers hash equally, which folding the bytes satisfies. */
- (NSUInteger)hash
{
	unsigned long hash = 5381;
	unsigned long i;

	for (i = 0; i < sizeof(_bytes); i++) {
		hash = ((hash << 5) + hash) ^ _bytes[i];
	}
	return (NSUInteger)hash;
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — an immutable value: the copy IS the receiver, as Cocoa's own rule has it */
}

@end
