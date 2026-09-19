/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsvalue.m — bytes with a type encoding (F13.8c). MANUAL OWNERSHIP.
 *
 * THE ONE PIECE OF REAL WORK IS THE SIZE. Cocoa's +valueWithBytes:objCType: takes a buffer and an
 * encoding and no length, because Foundation there can ask NSGetSizeAndAlignment — which THIS
 * library now answers too (the implementation is at the end of this file). This library has
 * no such function, so `fn_measure` WALKS the encoding — scalars, pointers, objects, C arrays and
 * structs/unions, RECURSIVELY, with C's own alignment rules — and raises on anything it cannot size
 * rather than guessing a length. Guessing a length is how a box silently reads past a caller's
 * structure.
 *
 * The walk is alignment-aware because a sum of field sizes is WRONG: {double,int} is 16 bytes, not
 * 12. That difference is what the probe measures, by giving the box an oversized source and a
 * canary-filled destination: whatever the size really is, the bytes past it are left untouched.
 */

#import <foundation/NSValue.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>
#include <stdlib.h>
#include <string.h>

/* The private doors the class methods use; declared here so they can be called before they are
 * defined, which is the house's pattern for a file-private protocol. */
@interface NSValue (FNPrivate)
- (instancetype)fnInitWithBytes:(const void *)value objCType:(const char *)type;
- (const void *)fnBytes;
@end

static NSUInteger fn_round_up(NSUInteger offset, NSUInteger align)
{
	if (align <= 1) {
		return offset;
	}
	return ((offset + align - 1) / align) * align;
}

static const char *fn_measure(const char *type, NSUInteger *outSize, NSUInteger *outAlign);

/* THE FUNCTION THIS FILE'S OWN COMMENT SAID WAS MISSING: Apple's Foundation can ask
 * NSGetSizeAndAlignment, which is why its encodings carry no length. This library answers it
 * now, on the SAME measurer the value classes use, so the two can never disagree. */
const char *NSGetSizeAndAlignment(const char *typePtr, NSUInteger *sizep, NSUInteger *alignp);

/* A field or struct NAME in an encoding is either absent or quoted — `{CGPoint="x"d"y"d}` — so the
 * quoted form has to be stepped over before the next type is measured. */
static const char *fn_skip_quoted(const char *p)
{
	if (*p != '"') {
		return p;
	}
	p++;
	while (*p != '\0' && *p != '"') {
		p++;
	}
	if (*p == '"') {
		p++;
	}
	return p;
}

static const char *fn_measure(const char *type, NSUInteger *outSize, NSUInteger *outAlign)
{
	const char *p = type;
	NSUInteger size = 0;
	NSUInteger align = 1;

	if (p == NULL || *p == '\0') {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSValue: an empty type encoding"];
	}
	switch (*p) {
	case 'c': case 'C': case 'B':
		size = 1; align = 1; p++; break;
	case 's': case 'S':
		size = 2; align = 2; p++; break;
	case 'i': case 'I': case 'f':
		size = 4; align = 4; p++; break;
	case 'l': case 'L': case 'q': case 'Q': case 'd':
		size = 8; align = 8; p++; break;
	case 'j':			/* _Complex float: two floats */
		size = 8; align = 4; p++; break;
	case 'J':			/* _Complex double: two doubles */
		size = 16; align = 8; p++; break;
	case 'D':			/* long double, which on this target is 16 bytes */
		size = 16; align = 16; p++; break;
	case 'v':
		/* VOID. It has no size of its own, and it is NOT a mistake: @encode(void *) is "^v", so
		 * every `void *` reaches this case through the pointer arm above. Measured the hard way —
		 * the first version of this walk raised here and the probe ABORTED with SIGABRT. */
		size = 0; align = 1; p++; break;
	case '*': case '@': case '#': case ':': case '?':
		size = sizeof(void *); align = sizeof(void *); p++; break;
	case '^': {
		/* A pointer is pointer-wide WHATEVER it points at — but the pointee's encoding still has
		 * to be consumed, or the next field would be read as this one's type. */
		NSUInteger innerSize = 0, innerAlign = 1;

		p = fn_measure(p + 1, &innerSize, &innerAlign);
		size = sizeof(void *); align = sizeof(void *);
		break;
	}
	case '[': {
		unsigned long count = 0;
		NSUInteger innerSize = 0, innerAlign = 1;

		p++;
		while (*p >= '0' && *p <= '9') {
			count = count * 10 + (unsigned long)(*p - '0');
			p++;
		}
		p = fn_measure(p, &innerSize, &innerAlign);
		if (*p != ']') {
			[NSException raise:NSInvalidArgumentException
				    format:@"NSValue: an array encoding without its ']': \"%s\"", type];
		}
		p++;
		size = (NSUInteger)count * innerSize;
		align = innerAlign;
		break;
	}
	case '{': case '(': {
		char close = (*p == '{') ? '}' : ')';
		NSUInteger offset = 0;
		NSUInteger largest = 0;
		NSUInteger maxAlign = 1;

		p++;
		/* `{Name=…}`, `{?=…}`, or a bare `{…}`: the name (if any) runs to '=' or to the close. */
		while (*p != '\0' && *p != '=' && *p != close) {
			p++;
		}
		if (*p == '=') {
			p++;
		}
		while (*p != '\0' && *p != close) {
			NSUInteger fieldSize = 0, fieldAlign = 1;

			p = fn_skip_quoted(p);
			p = fn_measure(p, &fieldSize, &fieldAlign);
			if (fieldAlign > maxAlign) {
				maxAlign = fieldAlign;
			}
			if (close == '}') {
				offset = fn_round_up(offset, fieldAlign) + fieldSize;
			} else {
				NSUInteger member = fn_round_up(fieldSize, fieldAlign);

				if (member > largest) {
					largest = member;
				}
			}
		}
		if (*p == close) {
			p++;
		}
		size = (close == '}') ? fn_round_up(offset, maxAlign)
				      : fn_round_up(largest, maxAlign);
		align = maxAlign;
		break;
	}
	default:
		[NSException raise:NSInvalidArgumentException
			    format:@"NSValue: cannot size the type encoding \"%s\" (scalars, pointers, "
				   "objects, C arrays and structs/unions are supported; bitfields and "
				   "Objective-C++ encodings are not)", type];
		break;
	}
	*outSize = size;
	*outAlign = align;
	return p;
}

@implementation NSValue

+ (NSValue *)valueWithBytes:(const void *)value objCType:(const char *)type
{
	return [[self alloc] fnInitWithBytes:value objCType:type];
}

+ (NSValue *)value:(const void *)value withObjCType:(const char *)type
{
	return [[self alloc] fnInitWithBytes:value objCType:type];
}

+ (NSValue *)valueWithPointer:(nullable const void *)pointer
{
	const void *held = pointer;

	/* THE POINTER IS STORED BY VALUE: the box holds the ADDRESS inside its own bytes rather than a
	 * copy of whatever is at the other end. */
	return [[self alloc] fnInitWithBytes:&held objCType:@encode(void *)];
}

+ (NSValue *)valueWithRange:(NSRange)range
{
	return [[self alloc] fnInitWithBytes:&range objCType:@encode(NSRange)];
}

- (instancetype)fnInitWithBytes:(const void *)value objCType:(const char *)type
{
	NSUInteger size = 0, align = 1;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (value == NULL || type == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSValue: a value needs both bytes and a type encoding"];
	}
	(void)fn_measure(type, &size, &align);
	_bytes = malloc(size);
	if (_bytes == NULL) {
		[NSException raise:NSMallocException format:@"NSValue: out of memory"];
	}
	memcpy(_bytes, value, size);
	_size = size;
	/* THE ENCODING IS OURS: nothing in the signature promises the caller's string outlives the
	 * value, and -objCType has to stay answerable either way. */
	_objCType = strdup(type);
	return self;
}

- (void)dealloc
{
	free(_bytes);
	free((void *)_objCType);
}

- (const void *)fnBytes
{
	return _bytes;
}

- (void)getValue:(void *)value
{
	if (value == NULL) {
		return;
	}
	memcpy(value, _bytes, _size);
}

- (void)getValue:(void *)value size:(NSUInteger)size
{
	if (value == NULL) {
		return;
	}
	/* COCOA'S RULE: the encoding must fit in what the caller offered. */
	if (size < _size) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSValue: the buffer is %lu bytes and the value is %lu",
				   (unsigned long)size, (unsigned long)_size];
	}
	memcpy(value, _bytes, _size);
}

- (const char *)objCType
{
	return _objCType;
}

- (nullable void *)pointerValue
{
	return *((void **)_bytes);
}

- (NSRange)rangeValue
{
	return *((NSRange *)_bytes);
}

- (BOOL)isEqualToValue:(NSValue *)value
{
	if (value == nil) {
		return NO;
	}
	if (strcmp(_objCType, [value objCType]) != 0) {
		return NO;
	}
	return memcmp(_bytes, [value fnBytes], _size) == 0;
}

- (BOOL)isEqual:(nullable id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSValue class]]) {
		return NO;
	}
	return [self isEqualToValue:(NSValue *)other];
}

/* Two values are equal when their BYTES are, so the hash has to be a function of those bytes too —
 * otherwise equal values would land in different buckets of any container built on them. */
- (NSUInteger)hash
{
	const unsigned char *bytes = _bytes;
	NSUInteger hash = 5381;
	NSUInteger i;

	for (i = 0; i < _size; i++) {
		hash = ((hash << 5) + hash) ^ (NSUInteger)bytes[i];
	}
	return hash ^ _size;
}

/* The bytes, in Cocoa's shape: the encoding, then the bytes. A RANGE is the one value Cocoa gives a
 * readable description, so it gets the same treatment here. The exact bytes form is a claim this
 * library does not make against a specific macOS release; the shape is Cocoa's. */
- (NSString *)description
{
	NSMutableString *out;
	NSUInteger i;

	if (strcmp(_objCType, @encode(NSRange)) == 0) {
		NSRange range = [self rangeValue];

		return [NSString stringWithFormat:@"NSRange: {%lu, %lu}",
				(unsigned long)range.location, (unsigned long)range.length];
	}
	out = [NSMutableString stringWithFormat:@"<%s: ", _objCType];
	for (i = 0; i < _size; i++) {
		if (i > 0) {
			[out appendString:@" "];
		}
		[out appendString:[NSString stringWithFormat:@"%02x",
				   (unsigned)((const unsigned char *)_bytes)[i]]];
	}
	[out appendString:@">"];
	return out;
}

- (id)copy
{
	/* COCOA'S RULE FOR AN IMMUTABLE BOX: a copy is itself. The bytes cannot be reached from outside,
	 * so a second box would protect nothing. */
	return self;
}


/*
 * NSGetSizeAndAlignment (W2e): the encoder walker this file already had, under Apple's name.
 * It answers the size and alignment of a type ENCODING — `@encode(...)`'s string — and returns
 * the pointer past what it consumed, which is Apple's contract for qualifiers and unions.
 */
const char *NSGetSizeAndAlignment(const char *typePtr, NSUInteger *sizep, NSUInteger *alignp)
{
	NSUInteger size = 0;
	NSUInteger align = 1;
	const char *end;

	if (typePtr == NULL) {
		if (sizep != NULL) {
			*sizep = 0;
		}
		if (alignp != NULL) {
			*alignp = 1;
		}
		return NULL;
	}
	end = fn_measure(typePtr, &size, &align);
	if (sizep != NULL) {
		*sizep = size;
	}
	if (alignp != NULL) {
		*alignp = align;
	}
	return end;
}
@end
