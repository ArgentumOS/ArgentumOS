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
#import <foundation/NSCharacterSet.h>
#import <foundation/NSLocale.h>
#import <foundation/NSException.h>

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
	size_t size = [format lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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

/*
 * THE UTF-16 INDEX SPACE (W1, docs/design/foundation-plan.md §13).
 *
 * APPLE'S NSString INDEXES UTF-16 CODE UNITS, and this library used to index
 * bytes (-length, the ranges) and scalars (-characterCount, -characterAtIndex:)
 * in the SAME CLASS — two spaces, so an NSRange from one API could not be handed
 * to the other. These four functions are the unit space's whole arithmetic: what
 * a unit count is, which byte a unit starts at, which unit a byte falls in, and
 * what the unit AT an index is. Every public method in slice 3 is a call into
 * one of them, and the byte-indexed internals never learn about units at all.
 *
 * A UNIT IS NOT A SCALAR: a character above U+FFFF is TWO units, a surrogate
 * pair, and -characterAtIndex: must answer either half on request (it is a
 * `unichar`, and that is what a unichar is). The old scalar helper answered
 * 0xFFFD for such a character, which is the SCALAR space's artefact and is why
 * this layer exists rather than a rename.
 */
static unsigned long fn_utf8_codepoint(const unsigned char *p, size_t seq)
{
	switch (seq) {
	case 1:  return p[0];
	case 2:  return ((unsigned long)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
	case 3:  return ((unsigned long)(p[0] & 0x0F) << 12) |
			((unsigned long)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
	default: return ((unsigned long)(p[0] & 0x07) << 18) |
			((unsigned long)(p[1] & 0x3F) << 12) |
			((unsigned long)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
	}
}

/* How many UTF-16 code units `size` bytes of UTF-8 are worth. */
static size_t fn_utf16_units(const char *bytes, size_t size)
{
	size_t i = 0, n = 0;

	while (i < size) {
		size_t seq = utf8_seq_length((unsigned char)bytes[i]);

		if (i + seq > size) {
			seq = 1;		/* a truncated tail is one unit, not none */
		}
		n += (fn_utf8_codepoint((const unsigned char *)bytes + i, seq) > 0xFFFF) ? 2 : 1;
		i += seq;
	}
	return n;
}

/* The byte offset a unit index starts at. Clamps past the end, like a byte
 * index into the buffer would. */
static size_t fn_utf16_unit_to_byte(const char *bytes, size_t size, size_t unit)
{
	size_t i = 0, n = 0;

	while (i < size && n < unit) {
		size_t seq = utf8_seq_length((unsigned char)bytes[i]);
		size_t width;

		if (i + seq > size) {
			seq = 1;
		}
		width = (fn_utf8_codepoint((const unsigned char *)bytes + i, seq) > 0xFFFF) ? 2 : 1;
		if (n + width > unit) {
			break;			/* mid-surrogate: the unit starts at this byte */
		}
		n += width;
		i += seq;
	}
	return i;
}

/* Which unit a byte offset falls in — the inverse, for a match found in bytes. */
static size_t fn_byte_to_utf16_unit(const char *bytes, size_t size, size_t byte)
{
	size_t i = 0, n = 0;

	if (byte > size) {
		byte = size;
	}
	while (i < byte) {
		size_t seq = utf8_seq_length((unsigned char)bytes[i]);

		if (i + seq > size) {
			seq = 1;
		}
		n += (fn_utf8_codepoint((const unsigned char *)bytes + i, seq) > 0xFFFF) ? 2 : 1;
		i += seq;
	}
	return n;
}

/* The UTF-16 code unit AT a unit index, surrogate halves included. Answers 0
 * past the end, which is what an out-of-range index deserves. */
static unsigned short fn_utf16_unit_at(const char *bytes, size_t size, size_t unit)
{
	size_t i = 0, n = 0;

	while (i < size) {
		size_t seq = utf8_seq_length((unsigned char)bytes[i]);
		unsigned long cp;

		if (i + seq > size) {
			seq = 1;
		}
		cp = fn_utf8_codepoint((const unsigned char *)bytes + i, seq);
		if (cp > 0xFFFF) {
			unsigned long v = cp - 0x10000;

			if (n == unit) {
				return (unsigned short)(0xD800 + (v >> 10));
			}
			if (n + 1 == unit) {
				return (unsigned short)(0xDC00 + (v & 0x3FF));
			}
			n += 2;
		} else {
			if (n == unit) {
				return (unsigned short)cp;
			}
			n += 1;
		}
		i += seq;
	}
	return 0;
}

/* THE DECODER: UTF-8 bytes -> UTF-16 units (W1 slice 1). `out` may be NULL, in
 * which case only the unit count is computed. Malformed sequences, and lone
 * surrogates decoded from CESU-ish input, become U+FFFD — the same substitute
 * the encoder uses, so a round trip through bad input is lossy in the direction
 * Apple's is. */
static size_t fn_utf8_to_utf16(const char *bytes, size_t size, unsigned short *out)
{
	size_t i = 0, n = 0;

	while (i < size) {
		size_t seq = utf8_seq_length((unsigned char)bytes[i]);
		unsigned long cp;

		if (i + seq > size) {
			seq = 1;
		}
		cp = fn_utf8_codepoint((const unsigned char *)bytes + i, seq);
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
			cp = 0xFFFD;
		}
		if (cp > 0xFFFF) {
			unsigned long v = cp - 0x10000;

			if (out != NULL) {
				out[n] = (unsigned short)(0xD800 + (v >> 10));
				out[n + 1] = (unsigned short)(0xDC00 + (v & 0x3FF));
			}
			n += 2;
		} else {
			if (out != NULL) {
				out[n] = (unsigned short)cp;
			}
			n += 1;
		}
		i += seq;
	}
	return n;
}

/* The ENCODER lives with the constant-string code below; these two are what
 * NSOwnedString's materialisation needs, and it is defined before them. */
static size_t fn_utf16_utf8_length(const unsigned char *data, size_t units);
static void fn_utf16_to_utf8(const unsigned char *data, size_t units, char *out);

/* THE FLIP'S BOUNDARY ARITHMETIC (W1 slice 3). The public API indexes UTF-16 UNITS;
 * every internal walker here indexes BYTES (`utf8_find`, `utf8_substring`, the
 * character-set scan). Both conversions live in this pair, so the two spaces cannot
 * drift apart one method at a time — which is what produced the two-index-space
 * incoherence this unit exists to remove. */
static NSUInteger fn_unit_to_byte(NSString *string, NSUInteger unit)
{
	const char *utf8 = [string UTF8String];
	size_t bytes = (utf8 != NULL) ? [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding] : 0;

	return (NSUInteger)fn_utf16_unit_to_byte(utf8 != NULL ? utf8 : "", bytes, (size_t)unit);
}

static NSUInteger fn_byte_to_unit(NSString *string, NSUInteger byte)
{
	const char *utf8 = [string UTF8String];
	size_t bytes = (utf8 != NULL) ? [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding] : 0;

	return (NSUInteger)fn_byte_to_utf16_unit(utf8 != NULL ? utf8 : "", bytes, (size_t)byte);
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
	size_t size = [source lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
	size_t haySize = [haystack lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	size_t needleSize = [needle lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
	if ([other lengthOfBytesUsingEncoding:NSUTF8StringEncoding] != [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]) {
		return NO;
	}
	{
		size_t i;

		for (i = 0; i < [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
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
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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

/* THE FOUR ...Characters: FORMS, declared and delegated the way the UTF-8 ones are:
 * the abstract class has no storage, a concrete subclass overrides them, and
 * -getCharacters:range: is written once here on -characterAtIndex: so EVERY concrete
 * class answers it correctly (NSOwnedString overrides it with a memcpy). */
+ (id)stringWithCharacters:(const unichar *)characters length:(NSUInteger)length
{
	return [[NSOwnedString alloc] initWithCharacters:characters length:length];
}

- (id)initWithCharacters:(const unichar *)characters length:(NSUInteger)length
{
	return [[NSOwnedString alloc] initWithCharacters:characters length:length];
}

- (id)initWithCharactersNoCopy:(unichar *)characters length:(NSUInteger)length
	  freeWhenDone:(BOOL)freeBuffer
{
	return [[NSOwnedString alloc] initWithCharactersNoCopy:characters length:length
					 freeWhenDone:freeBuffer];
}

- (void)getCharacters:(unichar *)buffer range:(NSRange)range
{
	NSUInteger i;

	if (buffer == NULL) {
		return;
	}
	for (i = 0; i < range.length; i++) {
		buffer[i] = [self characterAtIndex:range.location + i];
	}
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
	 * THROUGH THE INSTANCE FORM, and that is a MEASURED choice rather than style:
	 * the class-level body — the same three statements, consuming the passed list
	 * directly — was entered with the right format and then CRASHED, while
	 * -initWithFormat:arguments: consumes a passed va_list correctly (probe: it
	 * returns its rendered string). The two differ only in being a class method
	 * and an instance method, which is not a difference this class gets to
	 * explain; delegating to the one that is measured to work is the honest fix,
	 * and the crash's own cause is recorded in the plan as unexplained rather
	 * than papered over.
	 */
	id template = [[NSOwnedString alloc] initWithUTF8String:""];

	return [template initWithFormat:format arguments:arguments];
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

/* THE BYTE DOOR (W1 slice 2a). This is what every internal site that MEANS BYTES
 * asks for, and it is deliberately not `-length`: after slice 2b the two mean
 * different things (units vs bytes) and this one keeps them apart. The base
 * answer measures the materialised form; NSOwnedString overrides it with an O(1)
 * one. */
- (size_t)lengthOfBytesUsingEncoding:(NSStringEncoding)encoding
{
	const char *utf8;

	if (encoding != NSUTF8StringEncoding && encoding != NSASCIIStringEncoding) {
		return 0;
	}
	utf8 = [self UTF8String];
	if (utf8 == NULL) {
		return 0;
	}
	if (encoding == NSASCIIStringEncoding) {
		size_t i;

		for (i = 0; utf8[i] != '\0'; i++) {
			if ((unsigned char)utf8[i] > 0x7F) {
				return 0;
			}
		}
	}
	return strlen(utf8);
}

- (NSData *)dataUsingEncoding:(NSStringEncoding)encoding
{
	if (encoding != NSUTF8StringEncoding && encoding != NSASCIIStringEncoding) {
		return nil;
	}
	return [[NSData alloc] initWithBytes:[self UTF8String] length:[self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]];
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
	size_t a = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	size_t b = [other lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
	if ([prefix lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]) {
		return NO;
	}
	return utf8_find(self, prefix, NSMakeRange(0, [prefix lengthOfBytesUsingEncoding:NSUTF8StringEncoding]),
			 NSLiteralSearch) == 0;
}

- (BOOL)hasSuffix:(NSString *)suffix
{
	size_t n = [suffix lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

	if (n > [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]) {
		return NO;
	}
	return [self rangeOfString:suffix
			   options:NSLiteralSearch
			     range:NSMakeRange([self lengthOfBytesUsingEncoding:NSUTF8StringEncoding] - n, n)].location != NSNotFound;
}

- (BOOL)containsString:(NSString *)substring
{
	return [self rangeOfString:substring].location != NSNotFound;
}

- (NSRange)rangeOfString:(NSString *)substring
{
	return [self rangeOfString:substring options:NSLiteralSearch range:NSMakeRange(0, [self length])];
}

- (NSRange)rangeOfString:(NSString *)substring options:(NSStringCompareOptions)options
{
	return [self rangeOfString:substring options:options range:NSMakeRange(0, [self length])];
}

- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range
{
	/* THE RANGE COMES IN UNITS, THE SEARCH WALKS BYTES, THE ANSWER GOES BACK IN
	 * UNITS — so both edges of the range are mapped, and the length is the
	 * SUBSTRING's unit length (never its byte length). */
	NSUInteger byteStart = fn_unit_to_byte(self, range.location);
	NSUInteger byteEnd = fn_unit_to_byte(self, range.location + range.length);
	NSUInteger found = utf8_find(self, substring, NSMakeRange(byteStart, byteEnd - byteStart),
				     options);

	if (found == NSNotFound) {
		return NSMakeRange(NSNotFound, 0);
	}
	return NSMakeRange(fn_byte_to_unit(self, found), [substring length]);
}

/* ------------------------------------------------------------------- case */
- (NSString *)uppercaseString
{
	size_t i;
	char *buffer = (char *)malloc([self lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
	NSString *result;

	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i < [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
		buffer[i] = (char)utf8_upper([self byteAtIndex:i]);
	}
	buffer[[self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

- (NSString *)lowercaseString
{
	size_t i;
	char *buffer = (char *)malloc([self lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
	NSString *result;

	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i < [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
		buffer[i] = (char)utf8_lower([self byteAtIndex:i]);
	}
	buffer[[self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

- (NSString *)capitalizedString
{
	size_t i;
	int start = 1;
	char *buffer = (char *)malloc([self lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
	NSString *result;

	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i < [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
		unsigned char c = [self byteAtIndex:i];

		if (c == ' ' || c == '\t' || c == '-' || c == '_') {
			start = 1;
			buffer[i] = (char)c;
			continue;
		}
		buffer[i] = (char)(start ? utf8_upper(c) : utf8_lower(c));
		start = 0;
	}
	buffer[[self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

/* ------------------------------------------------------------- substrings */
- (NSString *)substringFromIndex:(NSUInteger)index
{
	size_t bytes = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	size_t from;

	if (index > [self length]) {
		index = [self length];
	}
	from = fn_unit_to_byte(self, index);
	return utf8_substring(self, from, bytes - from);
}

- (NSString *)substringToIndex:(NSUInteger)index
{
	if (index > [self length]) {
		index = [self length];
	}
	return utf8_substring(self, 0, fn_unit_to_byte(self, index));
}

- (NSString *)substringWithRange:(NSRange)range
{
	NSUInteger from = fn_unit_to_byte(self, range.location);

	return utf8_substring(self, from, fn_unit_to_byte(self, range.location + range.length) - from);
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
						    range:NSMakeRange(0, [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding])];
}

- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
					 withString:(NSString *)replacement
					    options:(NSStringCompareOptions)options
					      range:(NSRange)range
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	size_t targetSize = [target lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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

/* ------------------------------------------------- the character-set methods
 *
 * The RANGES these return are in the same units -length uses: BYTES. So each walk
 * carries a byte offset alongside a character index, because that is the only way
 * the two stay in step in a UTF-8 string.
 */
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet *)set
{
	size_t offset = 0;
	size_t character = 0;

	while (offset < [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]) {
		size_t width = utf8_seq_length([self byteAtIndex:offset]);

		if ([set characterIsMember:[self characterAtIndex:character]]) {
			/* THE SCAN WALKS BYTES AND COUNTS UNITS, so the answer is mapped
			 * back: a match's unit range, which is what a caller indexes with. */
			NSUInteger from = fn_byte_to_unit(self, offset);

			return NSMakeRange(from, fn_byte_to_unit(self, offset + width) - from);
		}
		offset += width;
		character++;
	}
	return NSMakeRange(NSNotFound, 0);
}

- (NSArray *)componentsSeparatedByCharactersInSet:(NSCharacterSet *)set
{
	/* A RUN of separators is ONE break, and empty components are not produced —
	 * Cocoa's rule, and the difference from -componentsSeparatedByString: with an
	 * empty separator, which this class rejects. */
	NSMutableArray *parts = [[NSMutableArray alloc] init];
	size_t offset = 0;
	size_t character = 0;
	size_t start = 0;
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

	while (offset <= size) {
		int breaking = 0;

		if (offset < size) {
			if ([set characterIsMember:[self characterAtIndex:character]]) {
				breaking = 1;
			}
		} else {
			breaking = 1;		/* the end closes the last component */
		}
		if (breaking) {
			if (offset > start) {
				[parts addObject:[self substringWithRange:NSMakeRange(start, offset - start)]];
			}
			{			/* skip the whole run of separators */
				while (offset < size &&
				       [set characterIsMember:[self characterAtIndex:character]]) {
					offset += utf8_seq_length([self byteAtIndex:offset]);
					character++;
				}
			}
			start = offset;
		}
		if (offset < size) {
			offset += utf8_seq_length([self byteAtIndex:offset]);
			character++;
		}
	}
	return parts;
}

- (NSString *)stringByTrimmingCharactersInSet:(NSCharacterSet *)set
{
	size_t offset = 0;
	size_t character = 0;
	size_t start = 0;
	size_t end = 0;
	int leading = 1;

	while (offset < [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]) {
		size_t width = utf8_seq_length([self byteAtIndex:offset]);
		BOOL member = [set characterIsMember:[self characterAtIndex:character]];

		if (member) {
			if (leading) {
				start = offset + width;
			}
		} else {
			leading = 0;
			end = offset + width;
		}
		offset += width;
		character++;
	}
	if (start > end) {
		start = end;
	}
	return [self substringWithRange:NSMakeRange(start, end - start)];
}

- (NSArray *)componentsSeparatedByString:(NSString *)separator
{
	NSMutableArray *parts = [[NSMutableArray alloc] init];
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	size_t separatorSize = [separator lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
	unsigned char c = ([self lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > 0) ? [self byteAtIndex:0] : 0;

	return (c == 'Y' || c == 'y' || c == 'T' || c == 't' ||
		(c >= '1' && c <= '9')) ? YES : NO;
}

/* -------------------------------------------------------------------- paths */
- (NSString *)lastPathComponent
{
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

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
	size_t size = [last lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

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
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
 * THE LOCALE ARGUMENTS ARE HONOURED FOR CASE, and that is the whole of what
 * "localised" means in this Foundation (stage E):
 *
 *   - CASE is localised. The Turkic languages (tr, az) are the ones whose case
 *     mapping is CONDITIONAL in Unicode's SpecialCasing: upper-case i is İ
 *     (U+0130) and lower-case I is ı (U+0131). A Turkic locale therefore changes
 *     -uppercaseStringWithLocale:, -lowercaseStringWithLocale: and every
 *     case-insensitive comparison. The fold maps CODE POINTS, because İ and ı are
 *     two bytes in UTF-8 and a byte-wise fold cannot express them;
 *   - ORDERING is NOT localised: this Foundation ships no collation tables, so
 *     -localizedCompare: compares by byte exactly as -compare: does;
 *   - SEARCH folding is NOT localised: -rangeOfString: answers a RANGE into the
 *     receiver, and the Turkic fold changes lengths, so a folded search would
 *     report offsets the string it searched does not have.
 *
 * A nil locale means "no locale-sensitive rules", which is the plain mapping.
 */
static NSString *fn_locale_language(id locale)
{
	if (locale == nil) {
		return nil;
	}
	if ([locale isKindOfClass:[NSString class]]) {
		locale = [NSLocale localeWithLocaleIdentifier:(NSString *)locale];
	}
	return (NSString *)[locale objectForKey:NSLocaleLanguageCode];
}

/* THE TURKIC LANGUAGES, and only these: Unicode's SpecialCasing marks exactly
 * Turkish, Azeri and their close relatives as conditional. */
static int fn_language_is_turkic(NSString *language)
{
	return language != nil &&
	       ([language isEqualToString:@"tr"] || [language isEqualToString:@"az"]);
}

/* One string's Turkic fold: upper != 0 upper-cases, otherwise it lower-cases.
 * Every other byte passes through, so nothing outside the rule moves. */
static NSString *fn_string_with_turkic_case(NSString *string, int upper)
{
	size_t len = [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	char *buffer = (char *)malloc(len * 2 + 1);
	size_t i, n = 0;
	NSString *result;

	if (buffer == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i < len; i++) {
		unsigned char c = [string byteAtIndex:i];

		if (upper && c == 0x69) {			/* i -> İ */
			buffer[n++] = (char)0xC4;
			buffer[n++] = (char)0xB0;
			continue;
		}
		if (upper && c == 0xC4 && i + 1 < len && [string byteAtIndex:i + 1] == 0xB1) {
			buffer[n++] = 'I';			/* ı -> I */
			i++;
			continue;
		}
		if (!upper && c == 0x49) {			/* I -> ı */
			buffer[n++] = (char)0xC4;
			buffer[n++] = (char)0xB1;
			continue;
		}
		if (!upper && c == 0xC4 && i + 1 < len && [string byteAtIndex:i + 1] == 0xB0) {
			buffer[n++] = 'i';			/* İ -> i */
			i++;
			continue;
		}
		buffer[n++] = (char)(upper ? utf8_upper(c) : utf8_lower(c));
	}
	buffer[n] = '\0';
	result = [[NSOwnedString alloc] initWithUTF8String:buffer];
	free(buffer);
	return result;
}

/* The Turkic case-insensitive comparison: fold BOTH sides to lower case with the
 * locale's rule, then compare the folded forms. */
static NSComparisonResult fn_compare_turkic(NSString *a, NSString *b, NSStringCompareOptions options)
{
	if (options & NSCaseInsensitiveSearch) {
		a = fn_string_with_turkic_case(a, 0);
		b = fn_string_with_turkic_case(b, 0);
		return [a compare:b];
	}
	return [a compare:b options:options];
}

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
	if (fn_language_is_turkic(fn_locale_language(locale))) {
		return fn_compare_turkic([self substringWithRange:range],
					 [other substringWithRange:range], options);
	}
	return [self compare:other options:options range:range];
}

- (NSComparisonResult)localizedCompare:(NSString *)other
{
	/* No collation tables: the order stays byte order, and the plan says so. */
	return [self compare:other];
}

- (NSComparisonResult)localizedCaseInsensitiveCompare:(NSString *)other
{
	if (fn_language_is_turkic(fn_locale_language([NSLocale currentLocale]))) {
		return fn_compare_turkic(self, other, NSCaseInsensitiveSearch);
	}
	return [self caseInsensitiveCompare:other];
}

- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range
		  locale:(id)locale
{
	(void)locale;	/* search folding stays byte-wise: see the note above */
	return [self rangeOfString:substring options:options range:range];
}

- (NSString *)uppercaseStringWithLocale:(id)locale
{
	if (fn_language_is_turkic(fn_locale_language(locale))) {
		return fn_string_with_turkic_case(self, 1);
	}
	return [self uppercaseString];
}

- (NSString *)lowercaseStringWithLocale:(id)locale
{
	if (fn_language_is_turkic(fn_locale_language(locale))) {
		return fn_string_with_turkic_case(self, 0);
	}
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
		for (i = 0; i < [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]; i++) {
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
	if ([extension lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
		return [[NSOwnedString alloc] initWithUTF8String:[self UTF8String]];
	}
	return [self stringByAppendingFormat:@".%@", extension];
}

- (NSString *)stringByPaddingToLength:(NSUInteger)newLength
			   withString:(NSString *)pad
		      startingAtIndex:(NSUInteger)index
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:[self UTF8String]];
	NSUInteger padLength = [pad lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

	if (newLength <= [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding] || padLength == 0) {
		return built;
	}
	if (index >= padLength) {
		index = 0;
	}
	while ([built lengthOfBytesUsingEncoding:NSUTF8StringEncoding] < newLength) {
		NSUInteger taken = 0;

		while (taken < padLength && [built lengthOfBytesUsingEncoding:NSUTF8StringEncoding] < newLength) {
			[built appendString:utf8_substring(pad, (index + taken) % padLength, 1)];
			taken++;
		}
	}
	return built;
}

- (NSString *)stringByReplacingCharactersInRange:(NSRange)range withString:(NSString *)replacement
{
	/* An EMPTY C STRING, not an EMPTY NSString: this is `initWithUTF8String:`, so a `@""` here
	 * would pass a tagged-pointer OBJECT where a `const char *` is expected — the same class of bug
	 * the predicate parser hit in F11b, and warning-as-noise was hiding this one since F1 (the
	 * 2026-09-18 sweep gave it a voice). */
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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
	return ([self lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > 0 && [self byteAtIndex:0] == '/') ? YES : NO;
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

		if ([part lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0 || [part isEqualToString:@"."]) {
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
	if ([built lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
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

/* THE STORAGE IS UNITS (W1 slice 1): a UTF-8 argument is DECODED once, here, and
 * the UTF-8 form is materialised lazily on the way out. */
- (id)initWithUTF8String:(const char *)utf8
{
	size_t n = (utf8 != NULL) ? strlen(utf8) : 0;
	size_t units;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	units = fn_utf8_to_utf16(utf8 != NULL ? utf8 : "", n, NULL);
	_units = (unsigned short *)malloc((units + 1) * sizeof(unsigned short));
	if (_units == NULL) {
		return nil;
	}
	_length = fn_utf8_to_utf16(utf8 != NULL ? utf8 : "", n, _units);
	_units[_length] = 0;		/* for a debugger's benefit, not a contract */
	_ownsUnits = 1;
	return self;
}

/* THE MATERIALISATION, and this class's one cache. Mutation invalidates it (see
 * -setString: and -appendUTF8String:), which is what makes caching safe here. */
- (const char *)UTF8String
{
	if (_utf8 == NULL) {
		_utf8size = fn_utf16_utf8_length((const unsigned char *)_units, _length);
		_utf8 = (char *)malloc(_utf8size + 1);
		if (_utf8 == NULL) {
			return "";
		}
		fn_utf16_to_utf8((const unsigned char *)_units, _length, _utf8);
		_utf8[_utf8size] = '\0';
	}
	return _utf8;
}

- (id)initWithBytes:(const char *)bytes length:(size_t)length
{
	size_t units;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	units = fn_utf8_to_utf16(bytes != NULL ? bytes : "", length, NULL);
	_units = (unsigned short *)malloc((units + 1) * sizeof(unsigned short));
	if (_units == NULL) {
		return nil;
	}
	_length = fn_utf8_to_utf16(bytes != NULL ? bytes : "", length, _units);
	_units[_length] = 0;
	_ownsUnits = 1;
	return self;
}

/* THE FLIP (W1 slice 3): the UNIT count, and it is a field read. This is the one
 * method whose MEANING changed, and slice 2a is why nothing internal depends on the
 * old one (docs/design/foundation-plan.md §13.6). */
- (size_t)length
{
	return _length;
}

/* THE BYTE DOOR, O(1): the materialised size, and the ASCII test walks it. */
- (size_t)lengthOfBytesUsingEncoding:(NSStringEncoding)encoding
{
	if (encoding != NSUTF8StringEncoding && encoding != NSASCIIStringEncoding) {
		return 0;
	}
	(void)[self UTF8String];
	if (encoding == NSASCIIStringEncoding) {
		size_t i;

		for (i = 0; i < _utf8size; i++) {
			if ((unsigned char)_utf8[i] > 0x7F) {
				return 0;
			}
		}
	}
	return _utf8size;
}

/* Scalars, counted from the UNITS — a surrogate pair is one character, which is
 * what this method has always meant. */
/* A COPY IN, and `_ownsUnits` is set because this class allocated the buffer. */
- (id)initWithCharacters:(const unichar *)characters length:(NSUInteger)length
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_units = (unsigned short *)malloc((length + 1) * sizeof(unsigned short));
	if (_units == NULL) {
		return nil;
	}
	if (length > 0 && characters != NULL) {
		memcpy(_units, characters, length * sizeof(unsigned short));
	}
	_length = length;
	_units[_length] = 0;
	_ownsUnits = 1;
	return self;
}

/* APPLE'S OWNERSHIP CONTRACT, and the only place `_ownsUnits` is 0: with freeWhenDone
 * the receiver frees the buffer in -dealloc; without it the receiver NEVER writes and
 * NEVER frees it, and a later mutation takes a copy first (see -appendUTF8String:). */
- (id)initWithCharactersNoCopy:(unichar *)characters length:(NSUInteger)length
	  freeWhenDone:(BOOL)freeBuffer
{
	if (characters == NULL) {
		return [self initWithCharacters:NULL length:0];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_units = characters;
	_length = length;
	_ownsUnits = freeBuffer ? 1 : 0;
	return self;
}

/* A memcpy, because the argument IS the storage's type — the whole point of the unit:
 * no conversion appears in this method at all (W1 slice 4). */
- (void)getCharacters:(unichar *)buffer range:(NSRange)range
{
	NSUInteger i;

	if (buffer == NULL) {
		return;
	}
	for (i = 0; i < range.length; i++) {
		NSUInteger at = range.location + i;

		buffer[i] = (at < _length) ? (unichar)_units[at] : (unichar)0;
	}
}


- (size_t)characterCount
{
	size_t i = 0, n = 0;

	while (i < _length) {
		if (_units[i] >= 0xD800 && _units[i] <= 0xDBFF && i + 1 < _length &&
		    _units[i + 1] >= 0xDC00 && _units[i + 1] <= 0xDFFF) {
			i += 2;
		} else {
			i += 1;
		}
		n++;
	}
	return n;
}

/* THE UNIT AT A UNIT INDEX, O(1) — and SURROGATE HALVES ARE ANSWERED AS THEMSELVES,
 * because that is what a unichar is and what Apple's contract says. The scalar space
 * this used to speak answered 0xFFFD for a character above U+FFFF; that limit is gone
 * with the space (docs/design/foundation-plan.md §13.4). */
- (unsigned short)characterAtIndex:(size_t)index
{
	if (index >= _length) {
		return 0;
	}
	return _units[index];
}

- (void)dealloc
{
	if (_ownsUnits) {
		free(_units);
	}
	free(_utf8);
}

@end

@implementation NSMutableString

- (void)setString:(NSString *)other
{
	const char *utf8 = [other UTF8String];
	size_t n = strlen(utf8);
	size_t units = fn_utf8_to_utf16(utf8, n, NULL);
	unsigned short *buf = (unsigned short *)malloc((units + 1) * sizeof(unsigned short));

	if (buf == NULL) {
		return;
	}
	_length = fn_utf8_to_utf16(utf8, n, buf);
	buf[_length] = 0;
	free(_units);
	free(_utf8);
	_units = buf;
	_utf8 = NULL;			/* THE INVALIDATION: every mutation ends here */
	_utf8size = 0;
}

- (void)appendUTF8String:(const char *)utf8
{
	size_t n, add, total;
	unsigned short *buf;

	if (utf8 == NULL || *utf8 == '\0') {
		return;
	}
	if (!_ownsUnits) {
		/* A BORROWED BUFFER IS NEVER WRITTEN TO (the no-copy contract): take a copy
		 * before anything can touch it. */
		unsigned short *mine = (unsigned short *)malloc((_length + 1) * sizeof(unsigned short));

		if (mine == NULL) {
			return;
		}
		memcpy(mine, _units, _length * sizeof(unsigned short));
		_units = mine;
		_ownsUnits = 1;
	}
	n = strlen(utf8);
	add = fn_utf8_to_utf16(utf8, n, NULL);
	total = _length + add;
	buf = (unsigned short *)realloc(_units, (total + 1) * sizeof(unsigned short));
	if (buf == NULL) {
		return;
	}
	(void)fn_utf8_to_utf16(utf8, n, buf + _length);
	_units = buf;
	_length = total;
	_units[_length] = 0;
	_ownsUnits = 1;		/* realloc gave us the buffer, so we own it now */
	free(_utf8);			/* the materialised form is stale now */
	_utf8 = NULL;
	_utf8size = 0;
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

/* ------------------------------------------------------------------ THE INHERITED CONSTRUCTORS
 *
 * NSString's class constructors and -initWithString: construct an NSOwnedString BY NAME, so
 * `[NSMutableString stringWithString:@"x"]` used to answer an IMMUTABLE string — and its first
 * mutator then aborted with "-[NSOwnedString appendString:] is not implemented". Measured by the
 * ordered-set probe, which is how this was found. Each override below builds with `self` (the class
 * the message was sent to) instead, which is what Cocoa's own mutable class does. */

+ (id)stringWithString:(NSString *)other
{
	return [[self alloc] initWithString:other];
}

+ (id)stringWithUTF8String:(const char *)utf8
{
	return [[self alloc] initWithUTF8String:utf8];
}

+ (id)stringWithFormat:(NSString *)format, ...
{
	va_list args;
	id built;

	va_start(args, format);
	built = [[self alloc] initWithFormat:format arguments:args];
	va_end(args);
	return built;
}

+ (id)stringWithFormat:(NSString *)format arguments:(va_list)arguments
{
	return [[self alloc] initWithFormat:format arguments:arguments];
}

- (id)initWithString:(NSString *)other
{
	return [self initWithUTF8String:[other UTF8String]];
}

- (id)initWithFormat:(NSString *)format, ...
{
	va_list args;

	va_start(args, format);
	self = [self initWithFormat:format arguments:args];
	va_end(args);
	return self;
}

- (id)initWithFormat:(NSString *)format arguments:(va_list)arguments
{
	/* BUILD WITH THE SUPERCLASS'S ENGINE, THEN ADOPT THE BYTES: the engine answers an NSOwnedString,
	 * and this object has to stay what it is. */
	NSString *built = [[NSOwnedString alloc] initWithFormat:format arguments:arguments];

	[self setString:built];
	return self;
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

	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];	/* BYTES: what utf8_substring speaks */

	if (index > size) {
		index = size;
	}
	[built appendString:utf8_substring(self, 0, index)];
	[built appendString:string];
	[built appendString:utf8_substring(self, index, size - index)];
	[self setString:built];
}

- (void)deleteCharactersInRange:(NSRange)range
{
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];	/* BYTES */
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
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];	/* BYTES */
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
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];	/* BYTES */
	size_t targetSize = [target lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
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

/*
 * THE ENCODING IS THE COMPILER'S CHOICE, and it is NOT always UTF-8. libobjc2's
 * loader.c defines the low two bits of `flags` as the encoding — 0 ASCII,
 * 1 UTF-8, 2 UTF-16, 3 UTF-32 — and clang's GenerateConstantString says which it
 * emits: "For now, all non-ASCII strings are represented as UTF-16." So a
 * non-ASCII literal arrives UTF-16 (`@"İ"` is the two bytes b0 01) and must be
 * CONVERTED, not reinterpreted: reading it as UTF-8 corrupts every non-ASCII
 * literal in the program, and the ASCII path cannot notice, because there
 * length == size.
 */
#define FN_CONST_ENCODING_MASK	3u
#define FN_CONST_ENCODING_ASCII	0u
#define FN_CONST_ENCODING_UTF8	1u
#define FN_CONST_ENCODING_UTF16	2u
#define FN_CONST_SLOTS		32u

/* One cached conversion per distinct constant, so a loop that reads the same
 * literal byte by byte hits the same buffer instead of refilling a shared one. */
static const void *fn_const_keys[FN_CONST_SLOTS];
static char *fn_const_buffers[FN_CONST_SLOTS];
static size_t fn_const_capacity[FN_CONST_SLOTS];
static unsigned int fn_const_next;

/* The UTF-8 byte count of a UTF-16 unit sequence, with NO buffer involved:
 * -length is called far more often than -UTF8String, and a shared buffer would
 * be refilled under the caller. */
static size_t fn_utf16_utf8_length(const unsigned char *data, size_t units)
{
	size_t n = 0;
	size_t i;

	for (i = 0; i < units; i++) {
		unsigned int unit = (unsigned int)data[i * 2] |
				    ((unsigned int)data[i * 2 + 1] << 8);

		if (unit < 0x80) {
			n += 1;
		} else if (unit < 0x800) {
			n += 2;
		} else if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < units) {
			n += 4;		/* a surrogate PAIR is one 4-byte character */
			i++;
		} else {
			n += 3;
		}
	}
	return n;
}

static void fn_utf16_to_utf8(const unsigned char *data, size_t units, char *out)
{
	size_t i;
	size_t n = 0;

	for (i = 0; i < units; i++) {
		unsigned int unit = (unsigned int)data[i * 2] |
				    ((unsigned int)data[i * 2 + 1] << 8);
		unsigned int code = unit;

		if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < units) {
			unsigned int low = (unsigned int)data[(i + 1) * 2] |
					   ((unsigned int)data[(i + 1) * 2 + 1] << 8);

			if (low >= 0xDC00 && low <= 0xDFFF) {
				code = 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u);
				i++;
			}
		}
		if (code < 0x80) {
			out[n++] = (char)code;
		} else if (code < 0x800) {
			out[n++] = (char)(0xC0u | (code >> 6));
			out[n++] = (char)(0x80u | (code & 0x3Fu));
		} else if (code < 0x10000) {
			out[n++] = (char)(0xE0u | (code >> 12));
			out[n++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
			out[n++] = (char)(0x80u | (code & 0x3Fu));
		} else {
			out[n++] = (char)(0xF0u | (code >> 18));
			out[n++] = (char)(0x80u | ((code >> 12) & 0x3Fu));
			out[n++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
			out[n++] = (char)(0x80u | (code & 0x3Fu));
		}
	}
	out[n] = '\0';
}

@implementation NSConstantString

/* The runtime's layout, read through its own fields. */
- (const char *)UTF8String
{
	unsigned int encoding = _rflags & FN_CONST_ENCODING_MASK;
	size_t units, wanted, i;

	if (_rstr == NULL) {
		return "";
	}
	if (encoding == FN_CONST_ENCODING_ASCII || encoding == FN_CONST_ENCODING_UTF8) {
		return _rstr;		/* already UTF-8: the pointer is stable */
	}
	if (encoding != FN_CONST_ENCODING_UTF16) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSConstantString: unsupported encoding %u", encoding];
	}
	units = _rsize / 2;
	wanted = fn_utf16_utf8_length((const unsigned char *)_rstr, units) + 1;
	for (i = 0; i < FN_CONST_SLOTS; i++) {
		if (fn_const_keys[i] == (const void *)_rstr) {
			break;
		}
	}
	if (i == FN_CONST_SLOTS) {
		i = (size_t)(fn_const_next++ % FN_CONST_SLOTS);
	}
	if (fn_const_capacity[i] < wanted) {
		char *fresh = (char *)realloc(fn_const_buffers[i], wanted);

		if (fresh == NULL) {
			return "";
		}
		fn_const_buffers[i] = fresh;
		fn_const_capacity[i] = wanted;
	}
	fn_const_keys[i] = (const void *)_rstr;
	fn_utf16_to_utf8((const unsigned char *)_rstr, units, fn_const_buffers[i]);
	return fn_const_buffers[i];
}

/* THE FLIP, AND FOR A CONSTANT IT IS THE BEST CASE IN THE WHOLE UNIT: the runtime
 * already counted the units in `_rlength`, so this is a FIELD READ with no conversion
 * at all — where the old code converted a UTF-16 literal to UTF-8 just to measure it
 * (§13.1's second reason for the switch). */
- (size_t)length
{
	unsigned int encoding = _rflags & FN_CONST_ENCODING_MASK;

	if (encoding == FN_CONST_ENCODING_ASCII || encoding == FN_CONST_ENCODING_UTF8 ||
	    encoding == FN_CONST_ENCODING_UTF16) {
		return _rlength;	/* UTF-16 CODE UNITS, per the runtime's struct */
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"NSConstantString: unsupported encoding %u", encoding];
	return 0;
}

- (size_t)characterCount
{
	return utf8_count_characters([self UTF8String], [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
}

- (unsigned short)characterAtIndex:(size_t)index
{
	unsigned int encoding = _rflags & FN_CONST_ENCODING_MASK;

	if (index >= [self length]) {
		return 0;
	}
	if (encoding == FN_CONST_ENCODING_UTF16 && _rstr != NULL) {
		const unsigned short *units = (const unsigned short *)(const void *)_rstr;

		return units[index];	/* the units ARE the storage here too */
	}
	return fn_utf16_unit_at([self UTF8String],
				[self lengthOfBytesUsingEncoding:NSUTF8StringEncoding], index);
}

- (void)dealloc
{
	/* The characters are the compiler's, not ours: free nothing. */
}

@end
