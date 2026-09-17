/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
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
#include <stdarg.h>
#include <stdio.h>
#import <foundation/NSArray.h>
#import <foundation/NSData.h>
#import <foundation/NSError.h>

/*
 * THE FORMAT ENGINE, and the one conversion it has to get right is the va_arg
 * TYPE: it must match the length modifier the caller wrote (C's own contract —
 * %d is an int, %ld a long, %lld a long long, %f a double, %s a char *, %@ an
 * id). Everything else — flags, width, precision — is copied verbatim into a
 * spec buffer and handed to snprintf, so C's semantics apply unchanged, and the
 * render is measured first (snprintf with a NULL buffer) so no arbitrary limit
 * caps how wide a %f can be. An unsupported conversion is copied LITERALLY
 * rather than guessed at.
 */
/* One measured render: snprintf twice, so width and precision are unbounded. */
static void string_append_rendered(NSMutableString *out, const char *spec, ...)
{
	va_list probe;
	va_list fill;
	int needed;
	char *buffer;

	va_start(probe, spec);
	needed = vsnprintf(NULL, 0, spec, probe);
	va_end(probe);
	if (needed < 0) {
		return;
	}
	buffer = (char *)malloc((size_t)needed + 1);
	if (buffer == NULL) {
		return;
	}
	va_start(fill, spec);
	vsnprintf(buffer, (size_t)needed + 1, spec, fill);
	va_end(fill);
	[out appendUTF8String:buffer];
	free(buffer);
}

static void string_append_conversion(NSMutableString *out, const char *spec, long long value)
{
	string_append_rendered(out, spec, value);
}

static void string_append_double(NSMutableString *out, const char *spec, double value)
{
	string_append_rendered(out, spec, value);
}

static void string_append_pointer(NSMutableString *out, const char *spec, void *value)
{
	string_append_rendered(out, spec, value);
}

static void string_append_format(NSMutableString *out, NSString *format, va_list args)
{
	size_t size = [format length];
	size_t i = 0;

	while (i < size) {
		unsigned char c = [format byteAtIndex:i];
		size_t start, j, specLen;
		char spec[48];
		char conv;

		if (c != '%') {
			char one[2];

			one[0] = (char)c;
			one[1] = '\0';
			[out appendUTF8String:one];
			i++;
			continue;
		}
		start = i;
		j = i + 1;
		specLen = 0;
		spec[specLen++] = '%';
		if (j < size && [format byteAtIndex:j] == '%') {
			[out appendUTF8String:"%"];	/* %% is a literal per cent */
			i = j + 1;
			continue;
		}
		while (j < size && specLen < sizeof spec - 4) {	/* flags/width/precision */
			unsigned char d = [format byteAtIndex:j];

			if (d == '-' || d == '+' || d == ' ' || d == '#' || d == '0' ||
			    d == '.' || (d >= '0' && d <= '9')) {
				spec[specLen++] = (char)d;
				j++;
				continue;
			}
			break;
		}
		while (j < size && specLen < sizeof spec - 4) {	/* length modifiers */
			unsigned char d = [format byteAtIndex:j];

			if (d == 'l' || d == 'h' || d == 'q' || d == 'z') {
				spec[specLen++] = (char)d;
				j++;
				continue;
			}
			break;
		}
		if (j >= size) {
			[out appendUTF8String:"%"];	/* a trailing % is literal */
			i = start + 1;
			continue;
		}
		conv = (char)[format byteAtIndex:j];
		spec[specLen++] = conv;
		spec[specLen] = '\0';
		i = j + 1;

		if (conv == '@') {
			id object = va_arg(args, id);

			/* Cocoa renders nil as (null), and every object describes itself. */
			[out appendString:(object == nil) ? @"(null)" : [object description]];
			continue;
		}
		if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' ||
		    conv == 'X' || conv == 'o' || conv == 'c') {
			long long value;

			if (specLen >= 3 && spec[specLen - 2] == 'l' && spec[specLen - 3] == 'l') {
				value = (conv == 'd' || conv == 'i')
					? va_arg(args, long long)
					: (long long)va_arg(args, unsigned long long);
			} else if (specLen >= 2 && spec[specLen - 2] == 'l') {
				value = (conv == 'd' || conv == 'i')
					? va_arg(args, long)
					: (long long)va_arg(args, unsigned long);
			} else {
				value = (conv == 'd' || conv == 'i')
					? va_arg(args, int)
					: (long long)va_arg(args, unsigned int);
			}
			string_append_conversion(out, spec, value);
			continue;
		}
		if (conv == 'f' || conv == 'g' || conv == 'e' || conv == 'F' ||
		    conv == 'G' || conv == 'E') {
			string_append_double(out, spec, va_arg(args, double));
			continue;
		}
		if (conv == 's') {
			const char *text = va_arg(args, const char *);

			/*
			 * THROUGH THE RENDERER: a plain append dropped width and precision,
			 * so [%8s] came out as [ab]. The string check caught it - which is
			 * what the checks are for.
			 */
			string_append_rendered(out, spec, (text == NULL) ? "(null)" : text);
			continue;
		}
		if (conv == 'p') {
			string_append_pointer(out, spec, va_arg(args, void *));
			continue;
		}
		[out appendUTF8String:spec];	/* unsupported: literal, not a guess */
	}
}

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

/* ASCII case mapping. This Foundation is UTF-8 and makes no Unicode case claims,
 * so case folding is the ASCII one and says so in the header. */
static unsigned char utf8_lower(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

static unsigned char utf8_upper(unsigned char c)
{
	return (c >= 'a' && c <= 'z') ? (unsigned char)(c - 32) : c;
}

/* A concrete string from a byte range of another string. */
static NSString *utf8_substring(NSString *source, size_t start, size_t length)
{
	size_t size = [source length];
	char *buffer;
	NSString *result;

	if (start > size) {
		start = size;
	}
	if (length > size - start) {
		length = size - start;
	}
	buffer = (char *)malloc(length + 1);
	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	{
		size_t i;

		for (i = 0; i < length; i++) {
			buffer[i] = (char)[source byteAtIndex:start + i];
		}
	}
	buffer[length] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

/* The first match of `needle` in `haystack` within `range`, or NSNotFound. */
static NSUInteger utf8_find(NSString *haystack, NSString *needle, NSRange range,
			    NSStringCompareOptions options)
{
	size_t haySize = [haystack length];
	size_t needleSize = [needle length];
	size_t start = range.location;
	size_t end;
	size_t i;

	if (needleSize == 0) {
		return (start <= haySize) ? start : NSNotFound;
	}
	if (start > haySize) {
		return NSNotFound;
	}
	end = start + range.length;
	if (end > haySize) {
		end = haySize;
	}
	for (i = start; i + needleSize <= end; i++) {
		size_t k;
		int match = 1;

		for (k = 0; k < needleSize; k++) {
			unsigned char a = [haystack byteAtIndex:i + k];
			unsigned char b = [needle byteAtIndex:k];

			if (options & NSCaseInsensitiveSearch) {
				a = utf8_lower(a);
				b = utf8_lower(b);
			}
			if (a != b) {
				match = 0;
				break;
			}
		}
		if (match) {
			return i;
		}
	}
	return NSNotFound;
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

/* ------------------------------------------------------------------ */
/* Creation. The class methods bound to a CONCRETE class (this class has no
 * storage of its own); the initialisers respect the receiver, so a
 * `[[NSMutableString alloc] initWithString:...]` stays mutable. */
+ (id)string
{
	return [[NSOwnedString alloc] initWithUTF8String:""];
}

+ (id)stringWithString:(NSString *)other
{
	return [[NSOwnedString alloc] initWithUTF8String:[other UTF8String]];
}

+ (id)stringWithFormat:(NSString *)format, ...
{
	va_list args;
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	NSString *result;

	va_start(args, format);
	string_append_format(built, format, args);
	va_end(args);
	result = [[NSOwnedString alloc] initWithUTF8String:[built UTF8String]];
	return result;
}

- (id)init
{
	/*
	 * `-init` RETURNS SELF, and that is load-bearing rather than lazy: the
	 * concrete subclasses' designated initialiser is -initWithUTF8String:, which
	 * begins with `self = [super init]`. Routing the base's -init back into that
	 * initialiser made the two call each other forever — measured, as the guest
	 * fault dump / "!!! halting" on the first owned-string allocation.
	 *
	 * A storage-less base returned as itself is honest: +alloc zeroes the
	 * allocation, so a bare NSOwnedString answers "" and length 0. Use +string
	 * (or a concrete subclass) to build a real one.
	 */
	return self;
}

- (id)initWithUTF8String:(const char *)utf8
{
	/* The abstract class has no storage; a SUBCLASS overrides this and never
	 * reaches here, which is what keeps the receiver's kind intact. */
	return [[NSOwnedString alloc] initWithUTF8String:utf8];
}

- (id)initWithString:(NSString *)other
{
	return [self initWithUTF8String:[other UTF8String]];
}

- (id)initWithFormat:(NSString *)format, ...
{
	va_list args;
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	id result;

	va_start(args, format);
	string_append_format(built, format, args);
	va_end(args);
	result = [self initWithUTF8String:[built UTF8String]];
	return result;
}

+ (id)stringWithFormat:(NSString *)format arguments:(va_list)arguments
{
	/*
	 * Cocoa's class-level form, and NOT decoration: +raise:format: in NSException
	 * goes through it, and the F4 probe is what noticed it missing.
	 *
	 * THE COPY IS REQUIRED BY THE STANDARD, not defensive habit: a va_list handed
	 * to a function that consumes it leaves the CALLER's copy indeterminate
	 * (C99 7.15.1.4), so the caller's va_end then runs on an indeterminate list —
	 * measured as a crash the moment this method was called. Consuming a copy
	 * leaves the incoming one valid for its owner.
	 */
	va_list copy;
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:@""];
	NSString *result;

	va_copy(copy, arguments);
	string_append_format(built, format, copy);
	va_end(copy);
	result = [[NSOwnedString alloc] initWithUTF8String:[built UTF8String]];
	return result;
}

- (id)initWithFormat:(NSString *)format arguments:(va_list)args
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];

	string_append_format(built, format, args);
	return [self initWithUTF8String:[built UTF8String]];
}

- (id)initWithData:(NSData *)data encoding:(NSStringEncoding)encoding
{
	size_t n;
	char *buffer;
	id result;

	if (encoding != NSUTF8StringEncoding) {
		return nil;	/* only the storage encoding is supported */
	}
	n = [data length];
	buffer = (char *)malloc(n + 1);
	if (buffer == NULL) {
		return nil;
	}
	if (n > 0) {
		memcpy(buffer, [data bytes], n);
	}
	buffer[n] = '\0';
	result = [self initWithUTF8String:buffer];
	free(buffer);
	return result;
}

- (size_t)lengthOfBytesUsingEncoding:(NSStringEncoding)encoding
{
	size_t i;

	if (encoding != NSUTF8StringEncoding && encoding != NSASCIIStringEncoding) {
		return 0;
	}
	if (encoding == NSASCIIStringEncoding) {
		for (i = 0; i < [self length]; i++) {
			if ([self byteAtIndex:i] > 0x7F) {
				return 0;
			}
		}
	}
	return [self length];
}

- (NSData *)dataUsingEncoding:(NSStringEncoding)encoding
{
	if (encoding != NSUTF8StringEncoding && encoding != NSASCIIStringEncoding) {
		return nil;
	}
	return [[NSData alloc] initWithBytes:[self UTF8String] length:[self length]];
}

/* ------------------------------------------------------------- value semantics */
- (NSComparisonResult)compare:(NSString *)other
{
	return [self compare:other options:NSLiteralSearch];
}

- (NSComparisonResult)caseInsensitiveCompare:(NSString *)other
{
	return [self compare:other options:NSCaseInsensitiveSearch];
}

- (NSComparisonResult)compare:(NSString *)other options:(NSStringCompareOptions)options
{
	size_t a = [self length];
	size_t b = [other length];
	size_t n = (a < b) ? a : b;
	size_t i;

	for (i = 0; i < n; i++) {
		unsigned char ca = [self byteAtIndex:i];
		unsigned char cb = [other byteAtIndex:i];

		if (options & NSCaseInsensitiveSearch) {
			ca = utf8_lower(ca);
			cb = utf8_lower(cb);
		}
		if (ca != cb) {
			return (ca < cb) ? NSOrderedAscending : NSOrderedDescending;
		}
	}
	if (a == b) {
		return NSOrderedSame;
	}
	return (a < b) ? NSOrderedAscending : NSOrderedDescending;
}

- (BOOL)hasPrefix:(NSString *)prefix
{
	if ([prefix length] > [self length]) {
		return NO;
	}
	return utf8_find(self, prefix, NSMakeRange(0, [prefix length]),
			 NSLiteralSearch) == 0;
}

- (BOOL)hasSuffix:(NSString *)suffix
{
	size_t n = [suffix length];

	if (n > [self length]) {
		return NO;
	}
	return [self rangeOfString:suffix
			   options:NSLiteralSearch
			     range:NSMakeRange([self length] - n, n)].location != NSNotFound;
}

- (BOOL)containsString:(NSString *)substring
{
	return [self rangeOfString:substring].location != NSNotFound;
}

- (NSRange)rangeOfString:(NSString *)substring
{
	return [self rangeOfString:substring options:NSLiteralSearch
			     range:NSMakeRange(0, [self length])];
}

- (NSRange)rangeOfString:(NSString *)substring options:(NSStringCompareOptions)options
{
	return [self rangeOfString:substring options:options
			     range:NSMakeRange(0, [self length])];
}

- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range
{
	NSUInteger found = utf8_find(self, substring, range, options);

	if (found == NSNotFound) {
		return NSMakeRange(NSNotFound, 0);
	}
	return NSMakeRange(found, [substring length]);
}

/* ------------------------------------------------------------------- case */
- (NSString *)uppercaseString
{
	size_t i;
	char *buffer = (char *)malloc([self length] + 1);
	NSString *result;

	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i < [self length]; i++) {
		buffer[i] = (char)utf8_upper([self byteAtIndex:i]);
	}
	buffer[[self length]] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

- (NSString *)lowercaseString
{
	size_t i;
	char *buffer = (char *)malloc([self length] + 1);
	NSString *result;

	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i < [self length]; i++) {
		buffer[i] = (char)utf8_lower([self byteAtIndex:i]);
	}
	buffer[[self length]] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

- (NSString *)capitalizedString
{
	size_t i;
	int start = 1;
	char *buffer = (char *)malloc([self length] + 1);
	NSString *result;

	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i < [self length]; i++) {
		unsigned char c = [self byteAtIndex:i];

		if (c == ' ' || c == '\t' || c == '-' || c == '_') {
			start = 1;
			buffer[i] = (char)c;
			continue;
		}
		buffer[i] = (char)(start ? utf8_upper(c) : utf8_lower(c));
		start = 0;
	}
	buffer[[self length]] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

/* ------------------------------------------------------------- substrings */
- (NSString *)substringFromIndex:(NSUInteger)index
{
	size_t n = [self length];

	if (index > n) {
		index = n;
	}
	return utf8_substring(self, index, n - index);
}

- (NSString *)substringToIndex:(NSUInteger)index
{
	return utf8_substring(self, 0, index);
}

- (NSString *)substringWithRange:(NSRange)range
{
	return utf8_substring(self, range.location, range.length);
}

/* --------------------------------------------------- appending and replacing */
- (NSString *)stringByAppendingString:(NSString *)other
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:[self UTF8String]];

	[built appendString:other];
	return [[NSOwnedString alloc] initWithUTF8String:[built UTF8String]];
}

- (NSString *)stringByAppendingFormat:(NSString *)format, ...
{
	va_list args;
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:[self UTF8String]];
	NSString *result;

	va_start(args, format);
	string_append_format(built, format, args);
	va_end(args);
	result = [[NSOwnedString alloc] initWithUTF8String:[built UTF8String]];
	return result;
}

- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
					 withString:(NSString *)replacement
{
	return [self stringByReplacingOccurrencesOfString:target
					       withString:replacement
						  options:NSLiteralSearch
						    range:NSMakeRange(0, [self length])];
}

- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
					 withString:(NSString *)replacement
					    options:(NSStringCompareOptions)options
					      range:(NSRange)range
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	size_t size = [self length];
	size_t targetSize = [target length];
	size_t cursor = 0;

	if (targetSize == 0) {
		return [[NSOwnedString alloc] initWithUTF8String:[self UTF8String]];
	}
	while (cursor < size) {
		NSUInteger found = utf8_find(self, target,
					     NSMakeRange(cursor, size - cursor), options);

		if (found == NSNotFound) {
			break;
		}
		[built appendString:utf8_substring(self, cursor, found - cursor)];
		[built appendString:replacement];
		cursor = found + targetSize;
	}
	[built appendString:utf8_substring(self, cursor, size - cursor)];
	return [[NSOwnedString alloc] initWithUTF8String:[built UTF8String]];
}

- (NSArray *)componentsSeparatedByString:(NSString *)separator
{
	NSMutableArray *parts = [[NSMutableArray alloc] init];
	size_t size = [self length];
	size_t separatorSize = [separator length];
	size_t cursor = 0;

	if (separatorSize == 0) {
		[parts addObject:[[NSOwnedString alloc] initWithUTF8String:[self UTF8String]]];
		return parts;
	}
	while (cursor <= size) {
		NSUInteger found = utf8_find(self, separator,
					     NSMakeRange(cursor, size - cursor),
					     NSLiteralSearch);

		if (found == NSNotFound) {
			[parts addObject:utf8_substring(self, cursor, size - cursor)];
			break;
		}
		[parts addObject:utf8_substring(self, cursor, found - cursor)];
		cursor = found + separatorSize;
	}
	return parts;
}

/* --------------------------------------------------------------- conversions */
- (long long)longLongValue
{
	return strtoll([self UTF8String], NULL, 10);
}

- (NSInteger)integerValue
{
	return (NSInteger)[self longLongValue];
}

- (int)intValue
{
	return (int)[self longLongValue];
}

- (double)doubleValue
{
	return strtod([self UTF8String], NULL);
}

- (float)floatValue
{
	return (float)[self doubleValue];
}

- (BOOL)boolValue
{
	/* Cocoa's documented rule: YES if it begins with Y, y, T, t, or a digit
	 * 1-9. Everything else — including "0" and the empty string — is NO. */
	unsigned char c = ([self length] > 0) ? [self byteAtIndex:0] : 0;

	return (c == 'Y' || c == 'y' || c == 'T' || c == 't' ||
		(c >= '1' && c <= '9')) ? YES : NO;
}

/* -------------------------------------------------------------------- paths */
- (NSString *)lastPathComponent
{
	size_t size = [self length];

	while (size > 1 && [self byteAtIndex:size - 1] == '/') {
		size--;
	}
	if (size == 1 && [self byteAtIndex:0] == '/') {
		return [[NSOwnedString alloc] initWithUTF8String:"/"];
	}
	{
		size_t start = 0;
		size_t i;

		for (i = 0; i < size; i++) {
			if ([self byteAtIndex:i] == '/') {
				start = i + 1;
			}
		}
		return utf8_substring(self, start, size - start);
	}
}

- (NSString *)pathExtension
{
	NSString *last = [self lastPathComponent];
	size_t size = [last length];
	size_t i;

	for (i = size; i > 0; i--) {
		if ([last byteAtIndex:i - 1] == '.') {
			return utf8_substring(last, i, size - i);
		}
		if ([last byteAtIndex:i - 1] == '/') {
			break;
		}
	}
	return [[NSOwnedString alloc] initWithUTF8String:""];
}

- (NSString *)stringByDeletingLastPathComponent
{
	size_t size = [self length];

	while (size > 1 && [self byteAtIndex:size - 1] == '/') {
		size--;
	}
	while (size > 0 && [self byteAtIndex:size - 1] != '/') {
		size--;
	}
	while (size > 1 && [self byteAtIndex:size - 1] == '/') {
		size--;
	}
	if (size == 0) {
		return [[NSOwnedString alloc] initWithUTF8String:"."];
	}
	return utf8_substring(self, 0, size);
}

- (NSString *)stringByDeletingPathExtension
{
	size_t size = [self length];
	size_t i;

	for (i = size; i > 0; i--) {
		unsigned char c = [self byteAtIndex:i - 1];

		if (c == '/') {
			break;
		}
		if (c == '.') {
			return utf8_substring(self, 0, i - 1);
		}
	}
	return [[NSOwnedString alloc] initWithUTF8String:[self UTF8String]];
}

- (NSString *)stringByAppendingPathComponent:(NSString *)component
{
	size_t size = [self length];
	int needsSlash = (size > 0 && [self byteAtIndex:size - 1] != '/');

	if (size == 0) {
		return [[NSOwnedString alloc] initWithUTF8String:[component UTF8String]];
	}
	if (needsSlash) {
		return [self stringByAppendingFormat:@"/%@", component];
	}
	return [self stringByAppendingString:component];
}

- (NSArray *)pathComponents
{
	return [self componentsSeparatedByString:@"/"];
}

/* ------------------------------------------------- the ranged and localised forms
 *
 * The locale arguments are ACCEPTED AND IGNORED, and the header says so: this
 * Foundation ships no locale data, so the localised comparisons answer exactly as
 * the unlocalised ones do. Returning the unlocalised answer is honest; pretending
 * to localise would not be.
 */
- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options
			range:(NSRange)range
{
	/* Compared as the two ranged substrings, which is what the range means. */
	return [[self substringWithRange:range] compare:[other substringWithRange:range]
						options:options];
}

- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options
			range:(NSRange)range
		       locale:(id)locale
{
	(void)locale;
	return [self compare:other options:options range:range];
}

- (NSComparisonResult)localizedCompare:(NSString *)other
{
	return [self compare:other];
}

- (NSComparisonResult)localizedCaseInsensitiveCompare:(NSString *)other
{
	return [self caseInsensitiveCompare:other];
}

- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range
		  locale:(id)locale
{
	(void)locale;
	return [self rangeOfString:substring options:options range:range];
}

- (NSString *)uppercaseStringWithLocale:(id)locale
{
	(void)locale;
	return [self uppercaseString];
}

- (NSString *)lowercaseStringWithLocale:(id)locale
{
	(void)locale;
	return [self lowercaseString];
}

- (const char *)cStringUsingEncoding:(NSStringEncoding)encoding
{
	size_t i;

	if (encoding == NSUTF8StringEncoding) {
		/*
		 * The storage itself. For an owned or constant string the pointer is
		 * stable; for a TAGGED one it is the shared scratch ring, so two calls in
		 * one expression alias — the F1 lesson, and the reason -byteAtIndex:
		 * exists for comparisons.
		 */
		return [self UTF8String];
	}
	if (encoding == NSASCIIStringEncoding) {
		for (i = 0; i < [self length]; i++) {
			if ([self byteAtIndex:i] > 0x7F) {
				return NULL;
			}
		}
		return [self UTF8String];
	}
	return NULL;		/* an encoding we do not store */
}

/* ------------------------------------------------------------------ composition */
- (NSString *)stringByAppendingPathExtension:(NSString *)extension
{
	if ([extension length] == 0) {
		return [[NSOwnedString alloc] initWithUTF8String:[self UTF8String]];
	}
	return [self stringByAppendingFormat:@".%@", extension];
}

- (NSString *)stringByPaddingToLength:(NSUInteger)newLength
			   withString:(NSString *)pad
		      startingAtIndex:(NSUInteger)index
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:[self UTF8String]];
	NSUInteger padLength = [pad length];

	if (newLength <= [self length] || padLength == 0) {
		return built;
	}
	if (index >= padLength) {
		index = 0;
	}
	while ([built length] < newLength) {
		NSUInteger taken = 0;

		while (taken < padLength && [built length] < newLength) {
			[built appendString:utf8_substring(pad, (index + taken) % padLength, 1)];
			taken++;
		}
	}
	return built;
}

- (NSString *)stringByReplacingCharactersInRange:(NSRange)range withString:(NSString *)replacement
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:@""];
	size_t size = [self length];
	size_t start = range.location;
	size_t end;

	if (start > size) {
		start = size;
	}
	end = start + range.length;
	if (end > size) {
		end = size;
	}
	[built appendString:[self substringToIndex:start]];
	[built appendString:replacement];
	[built appendString:[self substringFromIndex:end]];
	return built;
}

/* ------------------------------------------------------------------------ paths */
- (BOOL)isAbsolutePath
{
	return ([self length] > 0 && [self byteAtIndex:0] == '/') ? YES : NO;
}

- (NSString *)stringByStandardizingPath
{
	/*
	 * LEXICAL AND FILESYSTEM-FREE, which is what the name promises here: collapse
	 * repeated slashes, drop "." components, and resolve ".." by popping the last
	 * component. No symlinks are followed and nothing is looked up — there is no
	 * filesystem in this library to look anything up in.
	 */
	NSArray *parts = [(NSString *)self pathComponents];
	NSMutableArray *stack = [[NSMutableArray alloc] init];
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	NSUInteger i;
	int absolute = [self isAbsolutePath];

	for (i = 0; i < [parts count]; i++) {
		NSString *part = [parts objectAtIndex:i];

		if ([part length] == 0 || [part isEqualToString:@"."]) {
			continue;
		}
		if ([part isEqualToString:@".."]) {
			if ([stack count] > 0) {
				[stack removeLastObject];
			}
			continue;
		}
		[stack addObject:part];
	}
	if (absolute) {
		[built appendString:@"/"];
	}
	for (i = 0; i < [stack count]; i++) {
		if (i > 0) {
			[built appendString:@"/"];
		}
		[built appendString:[stack objectAtIndex:i]];
	}
	if ([built length] == 0) {
		return absolute ? [[NSOwnedString alloc] initWithUTF8String:"/"]
				: [[NSOwnedString alloc] initWithUTF8String:"."];
	}
	return built;
}

/* -------------------------------------------------------------------- the files
 *
 * Through NSData, which owns the byte-level file handling: this class only has to
 * decide what the bytes MEAN, and the error out parameter is honoured so a caller
 * can tell why the read or write failed.
 */
+ (id)stringWithContentsOfFile:(NSString *)path
		      encoding:(NSStringEncoding)encoding
			 error:(NSError **)errorPtr
{
	NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingDefault error:errorPtr];
	NSString *result;

	if (data == nil) {
		return nil;
	}
	result = [[self alloc] initWithData:data encoding:encoding];
	if (result == nil && errorPtr != NULL) {
		*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain"
						code:261
					    userInfo:[NSDictionary dictionaryWithObject:
							@"The file could not be decoded in the requested encoding."
								      forKey:NSLocalizedDescriptionKey]];
	}
	return result;
}

+ (id)stringWithContentsOfFile:(NSString *)path
		  usedEncoding:(NSStringEncoding *)encoding
			 error:(NSError **)errorPtr
{
	NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingDefault error:errorPtr];
	NSString *result;

	if (data == nil) {
		return nil;
	}
	/* One encoding is stored, so that is what was used. */
	result = [[self alloc] initWithData:data encoding:NSUTF8StringEncoding];
	if (result != nil && encoding != NULL) {
		*encoding = NSUTF8StringEncoding;
	}
	return result;
}

- (BOOL)writeToFile:(NSString *)path
	 atomically:(BOOL)useAuxiliaryFile
	   encoding:(NSStringEncoding)encoding
	      error:(NSError **)errorPtr
{
	NSData *encoded = [self dataUsingEncoding:encoding];

	if (encoded == nil) {
		if (errorPtr != NULL) {
			*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain"
							code:517
						    userInfo:[NSDictionary dictionaryWithObject:
								@"The string cannot be represented in that encoding."
									  forKey:NSLocalizedDescriptionKey]];
		}
		return NO;
	}
	return [encoded writeToFile:path
			    options:(useAuxiliaryFile ? NSDataWritingAtomic : NSDataWritingDefault)
			      error:errorPtr];
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

- (id)initWithBytes:(const char *)bytes length:(size_t)length
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_bytes = (char *)malloc(length + 1);
	if (_bytes == NULL) {
		return nil;
	}
	if (length > 0) {
		memcpy(_bytes, bytes, length);
	}
	_bytes[length] = '\0';
	_length = length;
	return self;
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

+ (id)string
{
	return [[self alloc] initWithUTF8String:""];
}

+ (id)stringWithCapacity:(NSUInteger)capacity
{
	(void)capacity;		/* the buffer grows on demand; capacity is a hint */
	return [[self alloc] initWithUTF8String:""];
}

- (id)initWithCapacity:(NSUInteger)capacity
{
	(void)capacity;
	return [self initWithUTF8String:""];
}

- (void)appendFormat:(NSString *)format, ...
{
	va_list args;

	va_start(args, format);
	string_append_format(self, format, args);
	va_end(args);
}

- (void)insertString:(NSString *)string atIndex:(NSUInteger)index
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];

	if (index > _length) {
		index = _length;
	}
	[built appendString:utf8_substring(self, 0, index)];
	[built appendString:string];
	[built appendString:utf8_substring(self, index, _length - index)];
	[self setString:built];
}

- (void)deleteCharactersInRange:(NSRange)range
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	size_t size = _length;
	size_t start = range.location;
	size_t end;

	if (start > size) {
		start = size;
	}
	end = start + range.length;
	if (end > size) {
		end = size;
	}
	[built appendString:utf8_substring(self, 0, start)];
	[built appendString:utf8_substring(self, end, size - end)];
	[self setString:built];
}

- (void)replaceCharactersInRange:(NSRange)range withString:(NSString *)string
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	size_t size = _length;
	size_t start = range.location;
	size_t end;

	if (start > size) {
		start = size;
	}
	end = start + range.length;
	if (end > size) {
		end = size;
	}
	[built appendString:utf8_substring(self, 0, start)];
	[built appendString:string];
	[built appendString:utf8_substring(self, end, size - end)];
	[self setString:built];
}

- (NSUInteger)replaceOccurrencesOfString:(NSString *)target
			      withString:(NSString *)replacement
				 options:(NSStringCompareOptions)options
				   range:(NSRange)range
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	size_t size = _length;
	size_t targetSize = [target length];
	size_t cursor = 0;
	NSUInteger replaced = 0;

	if (targetSize == 0) {
		return 0;
	}
	while (cursor < size) {
		NSUInteger found = utf8_find(self, target,
					     NSMakeRange(cursor, size - cursor), options);

		if (found == NSNotFound) {
			break;
		}
		[built appendString:utf8_substring(self, cursor, found - cursor)];
		[built appendString:replacement];
		cursor = found + targetSize;
		replaced++;
	}
	[built appendString:utf8_substring(self, cursor, size - cursor)];
	[self setString:built];
	return replaced;
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
