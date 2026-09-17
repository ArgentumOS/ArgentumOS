/*
 * nstring.m — NSString, NSOwnedString, NSMutableString and NSConstantString.
 *
 * ARC (the house default): none of the four implements -retain/-release, so
 * nothing stops it being ARC. The one -dealloc frees a C buffer, which ARC
 * allows, and it must not call [super dealloc] (the compiler emits that chain).
 *
 * EVERY STRING METHOD GOES THROUGH THE ACCESSORS rather than touching a field.
 * That is not style: NSConstantString's storage is the runtime's layout, so a
 * method reading NSOwnedString's offsets on a constant string would read — and
 * in -dealloc's case free — foreign memory.
 *
 * NSString is ABSTRACT: the primitives it declares are implemented in the
 * concrete subclasses, so the build suppresses -Wincomplete-implementation for
 * this file rather than filling NSString with stubs that could only lie.
 */

#import <foundation/NSString.h>
#include <stdlib.h>
#include <string.h>

/* The UTF-8 sequence length of a lead byte; an invalid byte counts as one and
 * the scan moves on (a broken string is not this class's problem to reject). */
static size_t utf8_seq_length(unsigned char lead)
{
	if (lead < 0x80) { return 1; }
	if ((lead & 0xE0) == 0xC0) { return 2; }
	if ((lead & 0xF0) == 0xE0) { return 3; }
	if ((lead & 0xF8) == 0xF0) { return 4; }
	return 1;
}

static size_t utf8_count_characters(const char *bytes, size_t size)
{
	size_t i = 0, n = 0;

	while (i < size) {
		i += utf8_seq_length((unsigned char)bytes[i]);
		n++;
	}
	return n;
}

/* The `index`-th CHARACTER as one UTF-16 code unit (0xFFFD outside the BMP,
 * because one unichar cannot say it). */
static unsigned short utf8_character_at(const char *bytes, size_t size, size_t index)
{
	size_t i = 0, n = 0;
	const unsigned char *p;
	unsigned long cp;

	while (i < size && n < index) {
		i += utf8_seq_length((unsigned char)bytes[i]);
		n++;
	}
	if (i >= size) {
		return 0;
	}
	p = (const unsigned char *)bytes + i;
	switch (utf8_seq_length(p[0])) {
	case 1:  cp = p[0]; break;
	case 2:  cp = ((unsigned long)(p[0] & 0x1F) << 6) | (p[1] & 0x3F); break;
	case 3:  cp = ((unsigned long)(p[0] & 0x0F) << 12) |
		      ((unsigned long)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); break;
	default: cp = ((unsigned long)(p[0] & 0x07) << 18) |
		      ((unsigned long)(p[1] & 0x3F) << 12) |
		      ((unsigned long)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); break;
	}
	return (cp > 0xFFFF) ? (unsigned short)0xFFFD : (unsigned short)cp;
}

@implementation NSString

+ (id)stringWithUTF8String:(const char *)utf8
{
	/* Bounded to a concrete class: NSString itself has no storage. */
	return [[NSOwnedString alloc] initWithUTF8String:utf8];
}

- (unsigned char)byteAtIndex:(size_t)index
{
	/*
	 * The DEFAULT: one byte read out of -UTF8String. Safe for every concrete
	 * string here precisely because the byte is consumed before another call
	 * can refill a materialised buffer — the aliasing bug was in comparing two
	 * such POINTERS, not in reading through one.
	 */
	return (unsigned char)[self UTF8String][index];
}

- (BOOL)isEqualToString:(NSString *)other
{
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if ([other length] != [self length]) {
		return NO;
	}
	{
		size_t i;

		for (i = 0; i < [self length]; i++) {
			if ([self byteAtIndex:i] != [other byteAtIndex:i]) {
				return NO;
			}
		}
	}
	return YES;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSString class]]) {
		return NO;
	}
	return [self isEqualToString:(NSString *)other];
}

- (unsigned long)hash
{
	/*
	 * FNV-1a over the bytes, through the accessors — so a constant string and
	 * an owned one with the same content hash alike, which is what makes them
	 * interchangeable as dictionary keys. Both -isEqual: and -hash are VALUE
	 * based here, unlike NSObject's identity defaults, because that is what a
	 * string is for.
	 */
	size_t size = [self length];
	unsigned long h = 2166136261UL;
	size_t i;

	for (i = 0; i < size; i++) {
		h ^= [self byteAtIndex:i];
		h *= 16777619UL;
	}
	return h;
}

- (id)copy
{
	return self;		/* immutable */
}

- (id)mutableCopy
{
	return [[NSMutableString alloc] initWithUTF8String:[self UTF8String]];
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

- (NSString *)description
{
	return self;		/* a string describes itself */
}

@end

@implementation NSOwnedString

+ (id)stringWithUTF8String:(const char *)utf8
{
	return [[self alloc] initWithUTF8String:utf8];
}

- (id)initWithUTF8String:(const char *)utf8
{
	size_t n = (utf8 != NULL) ? strlen(utf8) : 0;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	_bytes = (char *)malloc(n + 1);
	if (_bytes == NULL) {
		return nil;
	}
	if (n > 0) {
		memcpy(_bytes, utf8, n);
	}
	_bytes[n] = '\0';
	_length = n;
	return self;
}

- (const char *)UTF8String
{
	return (_bytes != NULL) ? _bytes : "";
}

- (size_t)length
{
	return _length;
}

- (size_t)characterCount
{
	return utf8_count_characters([self UTF8String], [self length]);
}

- (unsigned short)characterAtIndex:(size_t)index
{
	return utf8_character_at([self UTF8String], [self length], index);
}

- (void)dealloc
{
	free(_bytes);
}

@end

@implementation NSMutableString

- (void)setString:(NSString *)other
{
	const char *utf8 = [other UTF8String];
	size_t n = strlen(utf8);
	char *buf = (char *)malloc(n + 1);

	if (buf == NULL) {
		return;
	}
	memcpy(buf, utf8, n);
	buf[n] = '\0';
	free(_bytes);
	_bytes = buf;
	_length = n;
}

- (void)appendUTF8String:(const char *)utf8
{
	size_t n, total;
	char *buf;

	if (utf8 == NULL || *utf8 == '\0') {
		return;
	}
	n = strlen(utf8);
	total = _length + n;
	buf = (char *)realloc(_bytes, total + 1);
	if (buf == NULL) {
		return;
	}
	memcpy(buf + _length, utf8, n);
	buf[total] = '\0';
	_bytes = buf;
	_length = total;
}

- (void)appendString:(NSString *)other
{
	[self appendUTF8String:[other UTF8String]];
}

- (id)copy
{
	/* A copy of a MUTABLE string is immutable, and is a SNAPSHOT: mutating the
	 * original afterwards must not change it. */
	return [[NSOwnedString alloc] initWithUTF8String:[self UTF8String]];
}

@end

@implementation NSConstantString

/* The runtime's layout, read through its own fields. */
- (const char *)UTF8String
{
	return (_rstr != NULL) ? _rstr : "";
}

- (size_t)length
{
	return _rsize;		/* the runtime's `size` is BYTES */
}

- (size_t)characterCount
{
	return utf8_count_characters([self UTF8String], [self length]);
}

- (unsigned short)characterAtIndex:(size_t)index
{
	return utf8_character_at([self UTF8String], [self length], index);
}

- (void)dealloc
{
	/* The characters are the compiler's, not ours: free nothing. */
}

@end
