/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSString.m — NSString, NSOwnedString, NSMutableString and NSConstantString.
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

#import <Foundation/NSString.h>
#import <Foundation/NSCoder.h>		/* §63.22: the coder PRIMITIVES the string door is written over */
#include <stdlib.h>
#include <string.h>
#include <objc/runtime.h>	/* class_getName, sel_getName, objc_getClass (W2a) */
#include <stdarg.h>
#include <stdio.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSError.h>
#import <Foundation/NSCharacterSet.h>
#import <Foundation/NSLocale.h>
#import <Foundation/FNTextBreaking.h>
/* §63.24: THE NORMALIZER. This file joined `FN_FOUNDATION_ICU` because of the four normalization doors below,
 * and on the GUEST that list is what puts ICU's headers on the include path (the host has them by default). */
#include <unicode/unorm2.h>
#import <Foundation/NSValue.h>
#import <Foundation/NSException.h>
/* §63.29: THE URL DOORS read a URL as bytes. A FILE url is handed to the path doors above, and any other scheme
 * goes through the one synchronous loader this library has — so both headers belong here. */
#import <Foundation/NSURL.h>
#import <Foundation/NSURLConnection.h>
#import <Foundation/NSURLRequest.h>
/* THE PATH DOORS' tilde forms expand `~` through the account database: NSHomeDirectory() and
 * NSHomeDirectoryForUser() live in NSFileManager.h, and the two are Apple's own spelling (there is no separate
 * NSPathUtilities.h here). */
#import <Foundation/NSFileManager.h>
/* §63.49's deprecated linguistic pair delegates to the tagger this library already ships, so the class and
 * the two doors it drives have to be visible here. */
#import <Foundation/NSLinguisticTagger.h>
#import <Foundation/NSOrthography.h>

/* A STRING COPY IS A COPY OF THE CHARACTERS, AND THE BUFFER IS BUILT IN ONE PLACE (§62.93).
 *
 * THE BUG THIS CLOSES WAS FOUND BY THE JSON PROBE, and it is worth stating plainly because the old
 * body looked natural: three copy doors built a UTF-8 C STRING from the source and re-parsed it
 * (`[self initWithUTF8String:[other UTF8String]]`), and `-UTF8String` answers a NUL-TERMINATED
 * buffer - so a string holding an embedded NUL was TRUNCATED at it. Measured: a one-character
 * U+0000 string copied with +stringWithString: answered length 0, while -initWithCharacters:length:
 * answered 1. Apple's -initWithString: copies every character, and a NUL is a character a string may
 * hold (JSON5's backslash-0 writes one), so the copy is now by characters.
 */
static unichar *fn_string_character_copy(NSString *other, NSUInteger *length)
{
	NSUInteger count = [other length];
	unichar *buffer = (unichar *)malloc((count == 0 ? 1 : count) * sizeof(unichar));

	if (count != 0) {
		[other getCharacters:buffer range:NSMakeRange(0, count)];
	}
	*length = count;
	return buffer;
}

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
		if (conv == 'C') {
			/*
			 * A unichar, which varargs promotes to int. APPLE DOCUMENTS %C AND THIS ENGINE DID NOT
			 * IMPLEMENT IT, so the specifier was echoed LITERALLY - which corrupted every key and every
			 * string the JSON writer produced ("%C": "%C"), and is how the gap was found. The bytes are
			 * encoded here and rendered as %s, because string_append_rendered hands the spec straight
			 * to vsnprintf, which has no portable %C.
			 */
			unichar c = (unichar)va_arg(args, int);
			char utf8[4];
			char asString[sizeof spec];
			size_t n = 0;

			if (c < 0x80) {
				utf8[n++] = (char)c;
			} else if (c < 0x800) {
				utf8[n++] = (char)(0xC0 | (c >> 6));
				utf8[n++] = (char)(0x80 | (c & 0x3F));
			} else {
				utf8[n++] = (char)(0xE0 | (c >> 12));
				utf8[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
				utf8[n++] = (char)(0x80 | (c & 0x3F));
			}
			utf8[n] = '\0';
			memcpy(asString, spec, (size_t)specLen + 1);
			asString[specLen - 1] = 's';
			string_append_rendered(out, asString, utf8);
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
 * bytes (-length, the ranges) and scalars (-characterAtIndex:) in the SAME
 * CLASS — two spaces, so an NSRange from one API could not be handed to the other. These four functions are the unit space's whole arithmetic: what
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

/* THE ENUMERATION HELPERS, DECLARED BEFORE THEY ARE USED: the enumeration method moved into the
 * class's own block, which puts it ABOVE the three file-static helpers it calls - they stay at the end
 * of the file with the rest of the enumeration family. */
static FNTextUnit fn_enumeration_pick_unit(NSStringEnumerationOptions opts);
static FNTextUnit fn_enumeration_enclosing_unit(FNTextUnit unit);
static NSArray *fn_enumerate_units(NSString *string, FNTextUnit unit, NSRange range);

/* --- A LINE, AND WHERE IT ENDS ----------------------------------------------------------------
 *
 * FIVE TERMINATORS, AND NEL IS THE ONE THAT IS EASY TO FORGET: U+000A (LF), U+000D (CR), U+0085
 * (NEL), U+2028 (LS) and U+2029 (PS). The set is Apple's, from its published page for
 * -getLineStart:end:contentsEnd:forRange:, and -enumerateLinesUsingBlock: defers to that same
 * discussion — so there is ONE notion of a line behind three doors rather than three.
 *
 * THE ENGINE IN FNTextBreaking.m CARRIES THE SAME RULE FOR `-enumerateSubstringsInRange:options:`'s
 * ByLines, and it had FOUR of the five: it never treated NEL as a terminator, so a string carrying
 * U+0085 broke into lines by one door and not the other. That is fixed there in the same unit — the
 * rule is one rule, and a library that answers "how many lines" differently depending on which door
 * was used has two rules. */
static BOOL fn_is_line_terminator(unichar c)
{
	return c == 0x000a || c == 0x000d || c == 0x0085 || c == 0x2028 || c == 0x2029;
}

/* HOW MANY CODE UNITS THE TERMINATOR AT `i` OCCUPIES, or 0 when there is none. `\r\n` IS ONE
 * TERMINATOR AND NOT TWO, which is Apple's own sentence for it — "the longest possible sequence being
 * preferred to any shorter" — and it is why this is a length rather than a flag. */
static NSUInteger fn_line_terminator_length(NSString *string, NSUInteger i)
{
	NSUInteger length = [string length];

	if (i >= length || !fn_is_line_terminator([string characterAtIndex:i])) {
		return 0;
	}
	if ([string characterAtIndex:i] == 0x000d && (i + 1) < length &&
	    [string characterAtIndex:i + 1] == 0x000a) {
		return 2;
	}
	return 1;
}

/* --- PERCENT-ENCODING'S TWO PRIMITIVES -------------------------------------------------------- */

/* ONE HEX DIGIT, OR -1. Both directions need it: the reader to parse a `%XX` triple, and nothing else —
 * the writer never parses. It lives here rather than inside either door because a decoder that disagreed
 * with itself about `%0a` would be a bug waiting for a lowercase producer. */
static int fn_hex_value(unichar c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

/* IS THIS BYTE RUN WELL-FORMED UTF-8? The decoder needs the answer because it promises "the matching
 * UTF-8 characters", and a run of bytes that is not UTF-8 HAS no matching characters — so the honest
 * answer is nil rather than a guess. Structural rules AND the three a naive lead/continuation check
 * misses: OVERLONG forms (which decode to a character no encoder produced), SURROGATE code points (not
 * characters), and leads beyond U+10FFFF. */
static BOOL fn_is_utf8(const char *bytes, size_t size)
{
	size_t i = 0;

	while (i < size) {
		unsigned char lead = (unsigned char)bytes[i];
		size_t width, k;

		if (lead < 0x80) {
			i++;
			continue;
		}
		if ((lead & 0xE0) == 0xC0) {
			width = 2;
			if (lead < 0xC2) {
				return NO;				/* 0xC0/0xC1 can only start an overlong form */
			}
		} else if ((lead & 0xF0) == 0xE0) {
			width = 3;
		} else if ((lead & 0xF8) == 0xF0) {
			width = 4;
			if (lead > 0xF4) {
				return NO;				/* beyond U+10FFFF */
			}
		} else {
			return NO;					/* a stray continuation byte, or 0xF8+ */
		}
		if (i + width > size) {
			return NO;					/* truncated */
		}
		for (k = 1; k < width; k++) {
			if (((unsigned char)bytes[i + k] & 0xC0) != 0x80) {
				return NO;
			}
		}
		if (width == 3 && lead == 0xE0 && ((unsigned char)bytes[i + 1] & 0xE0) == 0x80) {
			return NO;					/* overlong */
		}
		if (width == 3 && lead == 0xED && ((unsigned char)bytes[i + 1] & 0xE0) == 0xA0) {
			return NO;					/* a surrogate code point */
		}
		if (width == 4 && lead == 0xF0 && ((unsigned char)bytes[i + 1] & 0xF0) == 0x80) {
			return NO;					/* overlong */
		}
		i += width;
	}
	return YES;
}

/* THE RANGE REFUSAL, SPELLED ONCE. Compared as `location` then `length` rather than as
 * `NSMaxRange`, because a range whose location is already past the end can have a length that
 * overflows the sum — the comparison is the same and this one cannot wrap. */
static void fn_line_check_range(NSString *string, NSRange range, SEL cmd)
{
	NSUInteger length = [string length];

	if (range.location > length || range.length > (length - range.location)) {
		[NSException raise:NSRangeException
		            format:@"-[NSString %@]: range {%lu, %lu} out of bounds for a string of length %lu",
		                   NSStringFromSelector(cmd), (unsigned long)range.location,
		                   (unsigned long)range.length, (unsigned long)length];
	}
}

/* UNICODE NORMALIZATION (§63.24): APPLE'S FOUR FORMS, AND THEY ARE WHAT PUTS ICU ON THIS FILE'S INCLUDE PATH.
 *
 * ICU IS THE ENGINE BECAUSE A NORMALIZER IS A TABLE, and this project's rule for a table is to take the library
 * that has it. §63.2 drew that line for the ENCODING cluster, which stays blocked on a converter/repertoire
 * table; NORMALIZATION is not the same case, because `unorm2` is a self-contained algorithm over icuuc's data,
 * which libfoundation has linked since F13.6 — so the four doors below are real work rather than a refusal.
 *
 * THE UNITS ARE UTF-16, WHICH IS WHAT THIS CLASS ALREADY SPEAKS: `-length` and its index doors are code units,
 * so the input is read with the class's own `-getCharacters:range:` and the answer is built with its own
 * `+stringWithCharacters:length:`. Nothing is transcoded, and no private storage is touched — the front cannot
 * see `NSOwnedString`'s ivars and does not need to. */
typedef enum {
	FNNormalFormNFC = 0,
	FNNormalFormNFD,
	FNNormalFormNFKC,
	FNNormalFormNFKD
} FNNormalForm;

static const UNormalizer2 *fn_normalizer_for(FNNormalForm form)
{
	UErrorCode status = U_ZERO_ERROR;
	const UNormalizer2 *normalizer;

	switch (form) {
	case FNNormalFormNFD:  normalizer = unorm2_getNFDInstance(&status); break;
	case FNNormalFormNFKC: normalizer = unorm2_getNFKCInstance(&status); break;
	case FNNormalFormNFKD: normalizer = unorm2_getNFKDInstance(&status); break;
	default:               normalizer = unorm2_getNFCInstance(&status); break;
	}
	return U_SUCCESS(status) ? normalizer : NULL;
}

/* `FNNormalFormNFC` IS THE DEFAULT ARM above on purpose: NFC is the form a caller is most likely to want, and a
 * typo in a caller would land there rather than on an undefined normalizer. */
static NSString *fn_normalized(NSString *source, FNNormalForm form)
{
	NSUInteger length = [source length];
	const UNormalizer2 *normalizer;
	UErrorCode status = U_ZERO_ERROR;
	unichar *in, *out;
	int32_t needed;
	NSString *result;

	normalizer = fn_normalizer_for(form);
	if (normalizer == NULL || length == 0) {
		/* NOTHING TO DO, OR ICU CANNOT ANSWER AT ALL: a normalization that cannot run leaves the string as it
		 * is. The empty string needs no walk and ICU would answer it one call later, so it short-circuits. */
		return source;
	}
	in = (unichar *)malloc(length * sizeof(unichar));
	if (in == NULL) {
		return source;
	}
	[source getCharacters:in range:NSMakeRange(0, length)];
	/* TWO CALLS, ICU'S OWN CONTRACT, BECAUSE A NORMALIZATION CAN GROW THE STRING: one composed code point is
	 * two units in NFD, so a capacity guessed from `length` would be a guess — and a short buffer here is how a
	 * normalizer silently truncates. The first call measures (into NULL) and U_BUFFER_OVERFLOW_ERROR is its
	 * expected answer, not a failure. */
	needed = unorm2_normalize(normalizer, (const UChar *)in, (int32_t)length, NULL, 0, &status);
	if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
		free(in);
		return source;
	}
	status = U_ZERO_ERROR;
	out = (unichar *)malloc(((size_t)needed + 1) * sizeof(unichar));
	if (out == NULL) {
		free(in);
		return source;
	}
	unorm2_normalize(normalizer, (const UChar *)in, (int32_t)length, (UChar *)out, needed + 1, &status);
	free(in);
	if (U_FAILURE(status)) {
		free(out);
		return source;
	}
	/* THE CONSTRUCTOR COPIES (which is why the NoCopy door exists), so the buffer is freed the moment it
	 * returns; the +1 it hands back is released here, and these four answer +0 exactly as Apple's do. */
	result = [NSString stringWithCharacters:out length:(NSUInteger)needed];
	free(out);
	return [result autorelease];
}


#include <unicode/ucnv.h>

/* ============================ THE CONVERTER ENGINE (§63.71) ============================
 * WHY THIS EXISTS: `-dataUsingEncoding:` and its family knew exactly TWO encodings, because this library stored
 * exactly two — UTF-8 and 7-bit ASCII — and refused everything else by name. The refusal was honest and it was
 * also a LIMIT: `converter/repertoire table` was the phrase the header used for what stood in the way (§63.2).
 * THE TABLE IS ICU'S, and it was already linked (since F13.6): `ucnv_countAvailable()` answers 232 converters
 * here, all fifteen this table names among them. And NO INTERMEDIATE FORM IS NEEDED, because this library's
 * storage is UTF-16 code units — exactly what `ucnv_fromUChars`/`ucnv_toUChars` take and give.
 *
 * ⚠ THE TWO STORAGE ENCODINGS KEEP THEIR OWN PATHS. UTF-8 and ASCII are what this library STORES; their
 * implementations are byte-exact and covered by the string probe, so routing them through a converter would
 * trade a tested path for an untested one. `fn_is_storage_encoding` is the line between the two. */
typedef struct {
	NSStringEncoding encoding;
	const char *icu;
	const char *display;
} FnEncodingEntry;

/* THE VALUES ARE APPLE'S (§63.70 fixed them), and the ICU names are the ones the converter was OPENED with and
 * found present. NSNEXTSTEP, Symbol, NonLossyASCII and ISO-2022-JP are absent because no converter here claims
 * them: those encodings are still refused, which is the honest answer rather than a wrong conversion. */
static const FnEncodingEntry fn_encoding_table[] = {
	{ NSISOLatin1StringEncoding,         "ISO-8859-1",   "Western (ISO Latin 1)" },
	{ NSISOLatin2StringEncoding,         "ISO-8859-2",   "Central European (ISO Latin 2)" },
	{ NSShiftJISStringEncoding,          "Shift_JIS",    "Japanese (Shift JIS)" },
	{ NSJapaneseEUCStringEncoding,       "EUC-JP",       "Japanese (EUC)" },
	{ NSMacOSRomanStringEncoding,        "x-mac-roman",  "Western (Mac OS Roman)" },
	{ NSWindowsCP1250StringEncoding,     "windows-1250", "Central European (Windows Latin 2)" },
	{ NSWindowsCP1251StringEncoding,     "windows-1251", "Cyrillic (Windows)" },
	{ NSWindowsCP1252StringEncoding,     "windows-1252", "Western (Windows Latin 1)" },
	{ NSWindowsCP1253StringEncoding,     "windows-1253", "Greek (Windows)" },
	{ NSWindowsCP1254StringEncoding,     "windows-1254", "Turkish (Windows)" },
	{ NSUnicodeStringEncoding,           "UTF-16",       "Unicode (UTF-16)" },
	{ NSUTF16BigEndianStringEncoding,    "UTF-16BE",     "Unicode (UTF-16BE)" },
	{ NSUTF16LittleEndianStringEncoding, "UTF-16LE",     "Unicode (UTF-16LE)" },
	{ NSUTF32StringEncoding,             "UTF-32",       "Unicode (UTF-32)" },
	{ NSUTF32BigEndianStringEncoding,    "UTF-32BE",     "Unicode (UTF-32BE)" },
	{ NSUTF32LittleEndianStringEncoding, "UTF-32LE",     "Unicode (UTF-32LE)" },
};

static size_t fn_encoding_count(void)
{
	return sizeof(fn_encoding_table) / sizeof(fn_encoding_table[0]);
}

static const FnEncodingEntry *fn_encoding_entry(NSStringEncoding encoding)
{
	size_t i;

	for (i = 0; i < fn_encoding_count(); i++) {
		if (fn_encoding_table[i].encoding == encoding) {
			return &fn_encoding_table[i];
		}
	}
	return NULL;
}

static int fn_is_storage_encoding(NSStringEncoding encoding)
{
	return encoding == NSUTF8StringEncoding || encoding == NSASCIIStringEncoding;
}

/* THE CONVERTERS ARE CACHED BY HAND: ucnv_open is not free, and the same handful of encodings is asked for
 * repeatedly. A NULL answer means this encoding has no converter HERE, which every door turns into its
 * documented refusal — so the cache changes speed and not meaning.
 * ⚠⚠ AND IT MUST BE ABLE TO HOLD EVERY ENCODING THE TABLE NAMES, WHICH IS THE ONE THING THIS CACHE GOT WRONG:
 * with eight slots and sixteen entries, `+availableStringEncodings` — which OPENS EACH ONE TO SEE WHAT IS
 * REALLY AVAILABLE — filled the cache and then starved itself, so UTF-16 (the eleventh entry) answered NULL
 * and EVERY UTF-16 CONVERSION WAS REFUSED. The engine's own introspection door poisoned its cache, and the
 * only symptom was a `canBeConvertedToEncoding:` that said NO for a string that can be converted. THE SIZE IS
 * THE TABLE'S, so growing the table can never repeat it. */
#define FN_UCNV_CACHE (sizeof(fn_encoding_table) / sizeof(fn_encoding_table[0]))
static struct { NSStringEncoding encoding; UConverter *cnv; } fn_ucnv_cache[FN_UCNV_CACHE];

static UConverter *fn_ucnv_for(NSStringEncoding encoding)
{
	const FnEncodingEntry *e = fn_encoding_entry(encoding);
	size_t i, slot = FN_UCNV_CACHE;
	UErrorCode st = U_ZERO_ERROR;

	if (e == NULL) {
		return NULL;
	}
	for (i = 0; i < FN_UCNV_CACHE; i++) {
		if (fn_ucnv_cache[i].cnv != NULL && fn_ucnv_cache[i].encoding == encoding) {
			return fn_ucnv_cache[i].cnv;
		}
		if (fn_ucnv_cache[i].cnv == NULL && slot == FN_UCNV_CACHE) {
			slot = i;
		}
	}
	if (slot == FN_UCNV_CACHE) {
		return NULL;			/* full: refuse rather than grow a global */
	}
	fn_ucnv_cache[slot].cnv = ucnv_open(e->icu, &st);
	if (fn_ucnv_cache[slot].cnv == NULL) {
		return NULL;
	}
	fn_ucnv_cache[slot].encoding = encoding;
	return fn_ucnv_cache[slot].cnv;
}

static unichar *fn_units_copy(NSString *s, NSUInteger *outLen)
{
	NSUInteger n = [s length];
	unichar *buf = (unichar *)malloc(sizeof(unichar) * (n == 0 ? 1 : n));

	if (buf == NULL) {
		*outLen = 0;
		return NULL;
	}
	if (n > 0) {
		[s getCharacters:buf range:NSMakeRange(0, n)];
	}
	*outLen = n;
	return buf;
}

/* BYTES IN AN ENCODING, or NULL when the text does not fit it. `lossy` picks ICU's SUBSTITUTE callback instead
 * of its STOP callback — WHICH IS EXACTLY THE `allowLossyConversion:` CONTRACT, and it is why that flag stops
 * being a no-op here: before this, the library stored only encodings in which nothing could be lost. */
static unsigned char *fn_bytes_in_encoding(NSString *s, NSStringEncoding encoding, BOOL lossy, size_t *outLen)
{
	UConverter *cnv = fn_ucnv_for(encoding);
	UErrorCode st = U_ZERO_ERROR;
	unichar *src;
	NSUInteger len = 0;
	unsigned char *out;
	int32_t need, got;

	*outLen = 0;
	if (cnv == NULL) {
		return NULL;
	}
	src = fn_units_copy(s, &len);
	if (src == NULL) {
		return NULL;
	}
	ucnv_setFromUCallBack(cnv, lossy ? UCNV_FROM_U_CALLBACK_SUBSTITUTE : UCNV_FROM_U_CALLBACK_STOP,
			      NULL, NULL, NULL, &st);
	st = U_ZERO_ERROR;
	need = ucnv_fromUChars(cnv, NULL, 0, src, (int32_t)len, &st);
	if (U_FAILURE(st) && st != U_BUFFER_OVERFLOW_ERROR) {
		free(src);
		return NULL;
	}
	need += 1;				/* room for the NUL the C-string doors want, and not a byte more */
	out = (unsigned char *)malloc((size_t)need);
	if (out == NULL) {
		free(src);
		return NULL;
	}
	st = U_ZERO_ERROR;
	got = ucnv_fromUChars(cnv, (char *)out, need, src, (int32_t)len, &st);
	free(src);
	if (U_FAILURE(st)) {
		free(out);
		return NULL;
	}
	out[got] = '\0';
	*outLen = (size_t)got;
	return out;
}

static size_t fn_length_in_encoding(NSString *s, NSStringEncoding encoding)
{
	size_t n = 0;
	unsigned char *b = fn_bytes_in_encoding(s, encoding, NO, &n);

	if (b == NULL) {
		return 0;
	}
	free(b);
	return n;
}

static NSData *fn_data_in_encoding(NSString *s, NSStringEncoding encoding, BOOL lossy)
{
	size_t n = 0;
	unsigned char *b = fn_bytes_in_encoding(s, encoding, lossy, &n);
	NSData *d;

	if (b == NULL) {
		return nil;
	}
	d = [[NSData alloc] initWithBytes:b length:n];
	free(b);
	return d;
}

/* THE MIRROR: bytes in some encoding become a string. The answer is OWNED, so every door releases it. */
static NSString *fn_string_from_bytes(const void *bytes, size_t n, NSStringEncoding encoding)
{
	UConverter *cnv = fn_ucnv_for(encoding);
	UErrorCode st = U_ZERO_ERROR;
	int32_t need, got;
	unichar *units;
	NSString *result;

	if (cnv == NULL || (bytes == NULL && n > 0)) {
		return nil;
	}
	need = ucnv_toUChars(cnv, NULL, 0, (const char *)bytes, (int32_t)n, &st);
	if (U_FAILURE(st) && st != U_BUFFER_OVERFLOW_ERROR) {
		return nil;
	}
	need += 1;
	units = (unichar *)malloc(sizeof(unichar) * (size_t)need);
	if (units == NULL) {
		return nil;
	}
	st = U_ZERO_ERROR;
	got = ucnv_toUChars(cnv, units, need, (const char *)bytes, (int32_t)n, &st);
	if (U_FAILURE(st)) {
		free(units);
		return nil;
	}
	result = [[NSString alloc] initWithCharacters:units length:(NSUInteger)got];
	free(units);
	return result;
}

@implementation NSString

/* UNICODE NORMALIZATION (§63.24). THE FOUR ARE TWO AXES — canonical vs COMPATIBILITY (which also folds the
 * ligatures and the like) and composed vs DECOMPOSED — and each door is one line over the engine above. */
- (NSString *)precomposedStringWithCanonicalMapping
{
	return fn_normalized(self, FNNormalFormNFC);
}

- (NSString *)decomposedStringWithCanonicalMapping
{
	return fn_normalized(self, FNNormalFormNFD);
}

- (NSString *)precomposedStringWithCompatibilityMapping
{
	return fn_normalized(self, FNNormalFormNFKC);
}

- (NSString *)decomposedStringWithCompatibilityMapping
{
	return fn_normalized(self, FNNormalFormNFKD);
}

/* THE TRANSFORM AND FOLDING DOORS (the transforms/folding slice). Both are written over machinery this file
 * already trusts: the ICU normalizer behind the four -…Mapping doors, and +[NSCharacterSet nonBaseCharacterSet],
 * whose categories Mn/Mc/Me are this family's one notion of a combining mark (the same set, and the same BMP
 * boundary, the composed-sequence door uses). */
- (NSString *)stringByApplyingTransform:(NSString *)transform reverse:(BOOL)reverse
{
	if ([transform isEqualToString:NSStringTransformStripCombiningMarks] ||
	    [transform isEqualToString:NSStringTransformStripDiacritics]) {
		/* NFD, then every mark comes off. `reverse` is meaningless for a strip and is ignored, which is also
		 * Apple's behaviour for these two. THE MARK SET IS BMP-BOUNDED (+nonBaseCharacterSet stops at U+FFFF),
		 * so a real combining mark ABOVE the BMP survives — the SAME measured boundary the composed-sequence
		 * door records, asserted there rather than hidden here. */
		NSString *decomposed = [self decomposedStringWithCanonicalMapping];
		NSCharacterSet *marks = [NSCharacterSet nonBaseCharacterSet];
		NSMutableString *out = [NSMutableString string];
		NSUInteger i, length = [decomposed length];

		(void)reverse;
		for (i = 0; i < length; i++) {
			unichar c = [decomposed characterAtIndex:i];

			if (![marks characterIsMember:c]) {
				[out appendString:[[NSString stringWithCharacters:&c length:1] autorelease]];
			}
		}
		return out;
	}
	/* A TRANSFORM THIS LIBRARY DOES NOT IMPLEMENT IS REFUSED, not silently ignored: the other fourteen names
	 * answer nil, which is Apple's own contract for a transform that cannot be applied. */
	return nil;
}

- (NSString *)stringByFoldingWithOptions:(NSStringCompareOptions)options locale:(id)locale
{
	BOOL foldCase = (options & NSCaseInsensitiveSearch) != 0;
	BOOL foldMarks = (options & NSDiacriticInsensitiveSearch) != 0;
	NSStringCompareOptions refused = options & ~(NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch);
	NSCharacterSet *marks = foldMarks ? [NSCharacterSet nonBaseCharacterSet] : nil;
	NSMutableString *out = [NSMutableString string];
	NSUInteger i, length = [self length];

	if (refused != 0) {
		/* WIDTH AND NUMERIC FOLDING ARE REFUSED RATHER THAN IGNORED: this library ships no width or numeric
		 * tables, so a fold that silently left them unfolded would answer a different question — the same
		 * refusal the option-taking comparisons make. */
		[NSException raise:NSInvalidArgumentException
			    format:@"-%s: an option this library cannot fold was passed (%lu)",
				   sel_getName(_cmd), (unsigned long)refused];
	}
	for (i = 0; i < length; i++) {
		unichar c = [self characterAtIndex:i];
		NSString *piece = [[NSString stringWithCharacters:&c length:1] autorelease];

		if (foldCase) {
			piece = [piece lowercaseStringWithLocale:locale];	/* THE ASCII CASE RULE this family states */
		}
		if (foldMarks) {
			NSUInteger j, pieceLength;

			piece = [piece decomposedStringWithCanonicalMapping];	/* §63.24: "é" becomes "e" + a mark */
			pieceLength = [piece length];
			for (j = 0; j < pieceLength; j++) {
				unichar f = [piece characterAtIndex:j];

				if (![marks characterIsMember:f]) {
					[out appendString:[[NSString stringWithCharacters:&f length:1] autorelease]];
				}
			}
		} else {
			[out appendString:piece];
		}
	}
	return out;
}

/* THE NSCoding DOORS (§63.22). IMPLEMENTED ON THIS FRONT, because the front is where this family's routing
 * lives and `NSOwnedString` — its subclass — inherits both. THE PAYLOAD IS THE CLASS'S OWN UTF-8 FORM, which is
 * what `-UTF8String` answers, so encode and decode agree about an embedded NUL BY CONSTRUCTION (both stop
 * there); the decode goes back through `-initWithBytes:length:`, whose own implementation is UTF-8 and treats a
 * NULL buffer as the empty string, so an archive that carries nothing yields the empty string rather than a
 * refusal — an empty string is a legitimate value, unlike a missing number type or type encoding. */
- (void)encodeWithCoder:(NSCoder *)coder
{
	const char *utf8 = [self UTF8String];

	if (utf8 != NULL && utf8[0] != '\0') {
		[coder encodeBytes:(const void *)utf8 length:strlen(utf8) forKey:@"NS.string"];
	}
}

- (id)initWithCoder:(NSCoder *)coder
{
	NSUInteger length = 0;
	const void *bytes = [coder decodeBytesForKey:@"NS.string" returnedLength:&length];

	/* RE-ROUTED LIKE EVERY OTHER DOOR ON THIS FRONT, with `-release` first so the abstract receiver does not
	 * leak — the pattern NSNumber's constructors spell out (§C.3 item 1). The concrete class overrides
	 * nothing here: it inherits this pair, so a string decoded as a mutable one stays mutable. */
	[self release];
	return [[NSOwnedString alloc] initWithBytes:(const char *)bytes length:length];
}

/* ===================================================================================================
 * THE DOOR AND THE ARCHIVER'S ANSWER (§C.3 items 1 and 4), and the string family needs a NOTE because its
 * shape is different from the array, dictionary, set and number families: the instances are NOT created by
 * a cluster that chooses among several private classes. The compiler creates the LITERALS (NSConstantString,
 * with the runtime's own fixed-offset layout, which this milestone must not disturb), the factories bind
 * themselves to NSOwnedString when a string needs storage of its own, and NSMutableString is its own public
 * subclass. What was missing was the two doors the contract names:
 *
 *   * `+alloc` - so `[[NSString alloc] init]` is a legitimate thing to write and answers an EMPTY instance
 *     with storage, rather than an instance of the storage-less front;
 *   * `-classForCoder` - so a LITERAL records as NSString. Without it the archiver asked the default
 *     (`[self class]`) and wrote "NSConstantString" into every archive that contained a string.
 * =================================================================================================== */
+ (id)alloc
{
	if (self != [NSString class]) {
		return [super alloc];
	}
	return [NSOwnedString alloc];
}

- (Class)classForCoder
{
	return [NSString class];
}

NSStringTransform const NSStringTransformFullwidthToHalfwidth = @"NSStringTransformFullwidthToHalfwidth";
NSStringTransform const NSStringTransformHiraganaToKatakana = @"NSStringTransformHiraganaToKatakana";
NSStringTransform const NSStringTransformLatinToArabic = @"NSStringTransformLatinToArabic";
NSStringTransform const NSStringTransformLatinToCyrillic = @"NSStringTransformLatinToCyrillic";
NSStringTransform const NSStringTransformLatinToGreek = @"NSStringTransformLatinToGreek";
NSStringTransform const NSStringTransformLatinToHangul = @"NSStringTransformLatinToHangul";
NSStringTransform const NSStringTransformLatinToHebrew = @"NSStringTransformLatinToHebrew";
NSStringTransform const NSStringTransformLatinToHiragana = @"NSStringTransformLatinToHiragana";
NSStringTransform const NSStringTransformLatinToKatakana = @"NSStringTransformLatinToKatakana";
NSStringTransform const NSStringTransformLatinToThai = @"NSStringTransformLatinToThai";
NSStringTransform const NSStringTransformMandarinToLatin = @"NSStringTransformMandarinToLatin";
NSStringTransform const NSStringTransformStripCombiningMarks = @"NSStringTransformStripCombiningMarks";
NSStringTransform const NSStringTransformStripDiacritics = @"NSStringTransformStripDiacritics";
NSStringTransform const NSStringTransformToLatin = @"NSStringTransformToLatin";
NSStringTransform const NSStringTransformToUnicodeName = @"NSStringTransformToUnicodeName";
NSStringTransform const NSStringTransformToXMLHex = @"NSStringTransformToXMLHex";

NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionAllowLossyKey = @"NSStringEncodingDetectionAllowLossyKey";
NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionDisallowedEncodingsKey = @"NSStringEncodingDetectionDisallowedEncodingsKey";
NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionFromWindowsKey = @"NSStringEncodingDetectionFromWindowsKey";
NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionLikelyLanguageKey = @"NSStringEncodingDetectionLikelyLanguageKey";
NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionLossySubstitutionKey = @"NSStringEncodingDetectionLossySubstitutionKey";
NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionSuggestedEncodingsKey = @"NSStringEncodingDetectionSuggestedEncodingsKey";
NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionUseOnlySuggestedEncodingsKey = @"NSStringEncodingDetectionUseOnlySuggestedEncodingsKey";


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
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
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
	unichar *buffer;
	NSUInteger length;
	id result;

	buffer = fn_string_character_copy(other, &length);
	result = [[NSOwnedString alloc] initWithCharacters:buffer length:length];
	free(buffer);
	return result;
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


/*
 * THE FOUR DOORS THE BASE DECLARES AND DOES NOT OWN, AND WHY THE ANSWER IS A REFUSAL RATHER THAN A BODY.
 *
 * This NSString is a CLASS CLUSTER: NSOwnedString, NSConstantString and NSMutableString hold the characters and
 * implement -length, -characterAtIndex: and -UTF8String. The base's -init returns SELF (its own
 * comment explains why: the concrete classes' designated initialiser begins with `self = [super init]`, and
 * routing it back made the two call each other forever), so a bare `[[NSString alloc] init]` is a real object
 * with no characters and no implementation of these doors - and until now the selector simply did not exist, so
 * the failure was a doesNotRecognizeSelector from nowhere. A NAMED refusal turns that into a diagnosis, which is
 * this tree's rule for a door it cannot honour: say which door, and the way out.
 */
- (NSUInteger)length
{
	[NSException raise:NSInvalidArgumentException
		    format:@"-[NSString length] is not implemented on the base class: a bare NSString holds no "
			   @"characters. Build one with -initWithString: or +stringWithUTF8String:, which answer a "
			   @"concrete NSString (NSOwnedString/NSConstantString)."];
	return 0;
}

- (unsigned short)characterAtIndex:(NSUInteger)index
{
	(void)index;
	[NSException raise:NSInvalidArgumentException
		    format:@"-[NSString characterAtIndex:] is not implemented on the base class: a bare NSString "
			   @"holds no characters (see -length for the same refusal and the way out)."];
	return 0;
}

- (const char *)UTF8String
{
	[NSException raise:NSInvalidArgumentException
		    format:@"-[NSString UTF8String] is not implemented on the base class: a bare NSString holds no "
			   @"characters (see -length for the same refusal and the way out)."];
	return "";
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
	unichar *buffer;
	NSUInteger length;
	id result;

	buffer = fn_string_character_copy(other, &length);
	result = [self initWithCharacters:buffer length:length];
	free(buffer);
	return result;
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

/* THE LOCALE-TAKING FORMAT DOORS. A locale is HONOURED FOR CASE AND NOTHING ELSE here, so both doors
 * DELEGATE to the locale-free -initWithFormat:… above: there is no locale-directed number, date or
 * measurement formatting in this library, and threading a locale into the renderer would be inventing a
 * rule. The locale is accepted and deliberately ignored (the header says so at the declaration). */
- (id)initWithFormat:(NSString *)format locale:(id)locale, ...
{
	va_list args;
	id result;

	va_start(args, locale);
	result = [self initWithFormat:format locale:locale arguments:args];
	va_end(args);
	return result;
}

- (id)initWithFormat:(NSString *)format locale:(id)locale arguments:(va_list)argList
{
	(void)locale;		/* accepted and ignored: no locale-directed formatting ships */
	return [self initWithFormat:format arguments:argList];
}

+ (id)localizedStringWithFormat:(NSString *)format, ...
{
	va_list args;
	id result;

	va_start(args, format);
	result = [self stringWithFormat:format arguments:args];
	va_end(args);
	/* +stringWithFormat:arguments: returns +1 (an init), so ONE autorelease gives Apple's +0 factory
	 * contract — the ownership this door's name promises; the sibling file/URL doors' known leak is not
	 * copied here. */
	return [result autorelease];
}

/* ============================ validated formats (§63.27) ============================
 *
 * WHAT IS VALIDATED: that every DIRECTIVE in the format is one the caller listed. A directive runs from a `%` to
 * the conversion character that ends it; `%%` is a literal percent and not a directive at all; and an
 * unterminated trailing `%` is reported AS a directive rather than dropped — because a format this door accepts
 * is a format the caller will hand straight to `-initWithFormat:`. */
- (NSArray *)fn_formatSpecifiers
{
	NSUInteger i, length = [self length];
	NSMutableArray *specifiers = [NSMutableArray array];
	NSMutableString *current = nil;

	for (i = 0; i < length; i++) {
		unichar c = [self characterAtIndex:i];

		if (current == nil) {
			if (c == '%') {
				current = [NSMutableString stringWithString:@"%"];
			}
			continue;
		}
		[current appendString:[[NSString stringWithCharacters:&c length:1] autorelease]];
		if (c == '%') {
			current = nil;			/* `%%` — A LITERAL PERCENT, not a specifier */
		} else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '@') {
			[specifiers addObject:current];
			current = nil;
		}
	}
	if (current != nil) {
		[specifiers addObject:current];	/* AN UNTERMINATED DIRECTIVE IS STILL A DIRECTIVE */
	}
	return specifiers;
}

/* THE VALIDATION RULE ITSELF, shared by the CLASS and the INSTANCE doors: every DIRECTIVE in the format must be
 * one the caller listed, and a refusal is an NSError in Apple's own vocabulary. Extracted so the four
 * -initWithValidatedFormat: doors (§63.27's siblings) apply ONE rule with the class doors rather than keeping a
 * second copy that could drift. */
static BOOL fn_format_is_allowed(NSString *format, NSString *validFormatSpecifiers, NSError **errorPtr)
{
	NSArray *specifiers = [format fn_formatSpecifiers];
	NSUInteger i;

	for (i = 0; i < [specifiers count]; i++) {
		NSString *specifier = [specifiers objectAtIndex:i];

		if ([validFormatSpecifiers rangeOfString:specifier].location == NSNotFound) {
			if (errorPtr != NULL) {
				*errorPtr = [NSError errorWithDomain:NSCocoaErrorDomain
							       code:NSFormattingError
							   userInfo:[NSDictionary dictionaryWithObject:specifier
										  forKey:NSLocalizedDescriptionKey]];
			}
			return NO;
		}
	}
	return YES;
}

/* THE VALIDATION ITSELF, TAKING A `va_list` RATHER THAN A `...`: both public doors share one rule, and the
 * variadic spellings have a single place to forward from. */
+ (nullable id)fn_stringWithValidatedFormat:(NSString *)format
		      validFormatSpecifiers:(NSString *)validFormatSpecifiers
				      error:(NSError **)errorPtr
				  arguments:(va_list)arguments
{
	if (!fn_format_is_allowed(format, validFormatSpecifiers, errorPtr)) {
		return nil;
	}
	return [self stringWithFormat:format arguments:arguments];
}

+ (id)stringWithValidatedFormat:(NSString *)format
	 validFormatSpecifiers:(NSString *)validFormatSpecifiers
			  error:(NSError **)errorPtr, ...
{
	va_list arguments;
	id result;

	va_start(arguments, errorPtr);
	result = [self fn_stringWithValidatedFormat:format
			      validFormatSpecifiers:validFormatSpecifiers
					      error:errorPtr
					  arguments:arguments];
	va_end(arguments);
	return result;
}

+ (id)localizedStringWithValidatedFormat:(NSString *)format
		      validFormatSpecifiers:(NSString *)validFormatSpecifiers
				      error:(NSError **)errorPtr, ...
{
	/* DELEGATING, AND SAYING WHAT "LOCALIZED" CAN MEAN HERE: this library's formatting is locale-independent —
	 * there is no localized-format-string machinery to consult — so this door's only difference is the name it
	 * is reached by. A deviation recorded rather than implied. */
	va_list arguments;
	id result;

	va_start(arguments, errorPtr);
	result = [self fn_stringWithValidatedFormat:format
			      validFormatSpecifiers:validFormatSpecifiers
					      error:errorPtr
					  arguments:arguments];
	va_end(arguments);
	return result;
}

/* ============================ validated formats, the INSTANCE doors (§63.27's siblings) ============================
 *
 * THE SAME RULE the class doors above apply, reached through -init. The two variadic doors forward to their
 * `arguments:` twin, which is the one place the validation lives; the locale-taking pair ACCEPTS a locale and
 * IGNORES it, exactly as -initWithFormat:locale:arguments: does, so the locale form renders the locale-free
 * answer rather than a second rendering rule that could drift. */
- (nullable id)fn_initWithValidatedFormat:(NSString *)format
		    validFormatSpecifiers:(NSString *)validFormatSpecifiers
				    error:(NSError **)errorPtr
				arguments:(va_list)arguments
{
	if (!fn_format_is_allowed(format, validFormatSpecifiers, errorPtr)) {
		return nil;
	}
	return [self initWithFormat:format arguments:arguments];
}

- (id)initWithValidatedFormat:(NSString *)format
	validFormatSpecifiers:(NSString *)validFormatSpecifiers
			error:(NSError **)errorPtr, ...
{
	va_list arguments;
	id result;

	va_start(arguments, errorPtr);
	result = [self fn_initWithValidatedFormat:format
			    validFormatSpecifiers:validFormatSpecifiers
					    error:errorPtr
					arguments:arguments];
	va_end(arguments);
	return result;
}

- (id)initWithValidatedFormat:(NSString *)format
	validFormatSpecifiers:(NSString *)validFormatSpecifiers
		    arguments:(va_list)arguments
			error:(NSError **)errorPtr
{
	return [self fn_initWithValidatedFormat:format
			  validFormatSpecifiers:validFormatSpecifiers
				  error:errorPtr
			      arguments:arguments];
}

- (id)initWithValidatedFormat:(NSString *)format
	validFormatSpecifiers:(NSString *)validFormatSpecifiers
		       locale:(id)locale
			error:(NSError **)errorPtr, ...
{
	/* the locale is accepted and IGNORED — the stance -initWithFormat:locale:arguments: records */
	va_list arguments;
	id result;

	va_start(arguments, errorPtr);
	result = [self fn_initWithValidatedFormat:format
			    validFormatSpecifiers:validFormatSpecifiers
					    error:errorPtr
					arguments:arguments];
	va_end(arguments);
	return result;
}

- (id)initWithValidatedFormat:(NSString *)format
	validFormatSpecifiers:(NSString *)validFormatSpecifiers
		       locale:(id)locale
		    arguments:(va_list)arguments
			error:(NSError **)errorPtr
{
	return [self fn_initWithValidatedFormat:format
			  validFormatSpecifiers:validFormatSpecifiers
				  error:errorPtr
			      arguments:arguments];
}

- (id)initWithData:(NSData *)data encoding:(NSStringEncoding)encoding
{
	if (!fn_is_storage_encoding(encoding)) {
		NSString *converted = fn_string_from_bytes([data bytes], (size_t)[data length], encoding);
		id result = nil;

		if (converted != nil) {
			result = [self initWithString:converted];
		}
		[converted release];
		return result;
	}
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
	if (!fn_is_storage_encoding(encoding)) {
		return fn_length_in_encoding(self, encoding);
	}
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
	if (!fn_is_storage_encoding(encoding)) {
		return fn_data_in_encoding(self, encoding, NO);
	}
	if (encoding != NSUTF8StringEncoding && encoding != NSASCIIStringEncoding) {
		return nil;
	}
	return [[NSData alloc] initWithBytes:[self UTF8String] length:[self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]];
}

- (NSData *)dataUsingEncoding:(NSStringEncoding)encoding allowLossyConversion:(BOOL)lossy
{
	if (!fn_is_storage_encoding(encoding)) {
		return fn_data_in_encoding(self, encoding, lossy);
	}
	/* THE LOSSY FLAG IS ACCEPTED AND CAN NEVER BE NEEDED: this library stores exactly the encodings it can
	 * represent losslessly (UTF-8 and 7-bit ASCII), so no character is ever replaced — the answer is the
	 * lossless one whatever the flag says. `lossy` is named so it is not mistaken for threaded-through state. */
	(void)lossy;
	return [self dataUsingEncoding:encoding];
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
	/* APPLE'S OWN DELEGATION: its page for this door says it "invokes with no options", so it calls the
	 * one that takes them rather than keeping a second copy of the walk. */
	return [self rangeOfCharacterFromSet:set options:0 range:NSMakeRange(0, [self length])];
}

- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet *)set
			   options:(NSStringCompareOptions)mask
{
	/* ...and this one "invokes with the entire extent of the receiver for the range". */
	return [self rangeOfCharacterFromSet:set options:mask range:NSMakeRange(0, [self length])];
}

- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet *)set
			   options:(NSStringCompareOptions)mask
			     range:(NSRange)range
{
	NSUInteger first = NSNotFound, firstLength = 0;
	NSUInteger last = NSNotFound, lastLength = 0;
	size_t offset, end;

	/* THE PAGE'S TWO REQUIREMENTS, EACH WITH ITS OWN EXCEPTION: a nil set is NSInvalidArgumentException
	 * and a range past the end is NSRangeException. They are different kinds of mistake — the first is a
	 * bad argument whatever the receiver is, the second is a bad argument FOR THIS receiver — and the
	 * page names each, so each gets its own. */
	if (set == nil) {
		[NSException raise:NSInvalidArgumentException
		            format:@"-[NSString %@]: the character set is nil", NSStringFromSelector(_cmd)];
	}
	fn_line_check_range(self, range, _cmd);

	/* THE SCAN STILL WALKS BYTES AND COUNTS UNITS, which is the arithmetic the no-options door has
	 * always had: a match's UNIT range is what a caller indexes with. Backwards does not mean a second
	 * walk — this one records the first hit and the last, and which of them is the answer is decided at
	 * the end. */
	offset = fn_unit_to_byte(self, range.location);
	end = fn_unit_to_byte(self, NSMaxRange(range));

	while (offset < end) {
		size_t width = utf8_seq_length([self byteAtIndex:offset]);
		NSUInteger from = fn_byte_to_unit(self, offset);
		NSUInteger to = fn_byte_to_unit(self, offset + width);

		if ([set characterIsMember:[self characterAtIndex:from]]) {
			if (first == NSNotFound) {
				first = from;
				firstLength = to - from;
			}
			last = from;
			lastLength = to - from;
		}
		offset += width;
	}

	if (first == NSNotFound) {
		return NSMakeRange(NSNotFound, 0);
	}

	/* ANCHORED RESTRICTS WHICH HIT COUNTS RATHER THAN ASKING A DIFFERENT QUESTION: the match must sit at
	 * the boundary the search starts from — the range's FIRST character going forward, its LAST going
	 * backward — so a hit anywhere else is not a match at all. */
	if (mask & NSAnchoredSearch) {
		if (mask & NSBackwardsSearch) {
			return (last + lastLength == NSMaxRange(range))
			       ? NSMakeRange(last, lastLength) : NSMakeRange(NSNotFound, 0);
		}
		return (first == range.location) ? NSMakeRange(first, firstLength) : NSMakeRange(NSNotFound, 0);
	}

	/* AND NO UNICODE NORMALIZATION IS DONE, which is the page's other rule and falls out of comparing
	 * units: canonically equivalent forms do not match, and the probe holds it to that. */
	if (mask & NSBackwardsSearch) {
		return NSMakeRange(last, lastLength);
	}
	return NSMakeRange(first, firstLength);
}

- (NSString *)commonPrefixWithString:(NSString *)other options:(NSStringCompareOptions)mask
{
	NSUInteger n, i;

	if (other == nil) {
		/* A NIL ARGUMENT HAS NOTHING IN COMMON WITH ANYTHING, so the answer is the empty prefix. */
		return [NSString string];
	}
	n = [self length];
	if ([other length] < n) {
		n = [other length];
	}
	for (i = 0; i < n; i++) {
		unichar a = [self characterAtIndex:i];
		unichar b = [other characterAtIndex:i];

		if (mask & NSCaseInsensitiveSearch) {
			/* THE LIBRARY'S OWN FOLD, applied to a UNIT rather than a byte — the same rule
			 * -compare:options: applies, so the two doors cannot disagree about case. */
			if (a >= 'A' && a <= 'Z') {
				a = (unichar)(a - 'A' + 'a');
			}
			if (b >= 'A' && b <= 'Z') {
				b = (unichar)(b - 'A' + 'a');
			}
		}
		if (a != b) {
			break;
		}
	}
	/* THE RESULT IS TAKEN FROM THE RECEIVER — not a merge, and not the argument's characters — which is
	 * what makes Apple's "Mädchen" example answer the receiver's spelling. */
	return [self substringWithRange:NSMakeRange(0, i)];
}

/* (A -propertyListFromStringsFileFormat WAS TRIED HERE AND WITHDRAWN — see NSString.h for the
 * measurement: a `.strings` body is brace-less and this library's old-style plist reader answers NIL for
 * one, so the door cannot honour its contract until the READER accepts a brace-less dictionary. That is
 * a defect in the reader, and it is owed its own unit.) */

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

/* ===================================================================================================
 * PERCENT-ENCODING, BOTH DIRECTIONS. See NSString.h for the rules and for why the deprecated pair is
 * not here at all.
 * =================================================================================================== */
- (nullable NSString *)stringByAddingPercentEncodingWithAllowedCharacters:(NSCharacterSet *)allowedCharacters
{
	NSMutableString *out = [[NSMutableString alloc] init];
	size_t offset = 0;
	size_t total = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

	if (allowedCharacters == nil) {
		/* "nil if the transformation is not possible" is Apple's own wording for the failure answer, and a
		 * nil set is the one way this door cannot do its job: there is no set to test a character against. */
		return nil;
	}
	while (offset < total) {
		size_t width = utf8_seq_length([self byteAtIndex:offset]);
		unsigned char lead = [self byteAtIndex:offset];

		/* ONLY A 7-BIT ASCII CHARACTER CAN BE ALLOWED: the page says a member of the set outside that range
		 * is IGNORED, so a multi-byte sequence is encoded even when the set names the same code point. The
		 * test is on the BYTE, which is why it is written here and not in the set. */
		if (width == 1 && lead < 0x80 && [allowedCharacters characterIsMember:(unichar)lead]) {
			char one[2];

			one[0] = (char)lead;
			one[1] = '\0';
			[out appendUTF8String:one];
		} else {
			/* UTF-8 IS WHAT DETERMINES THE ESCAPE, so a character becomes as many triples as it has
			 * bytes — and a caller reading the answer sees the same bytes the URL would carry. */
			size_t i;

			for (i = offset; i < offset + width; i++) {
				char escape[4];

				snprintf(escape, sizeof escape, "%%%02X", [self byteAtIndex:i]);
				[out appendUTF8String:escape];
			}
		}
		offset += width;
	}
	return out;
}

- (nullable NSString *)stringByRemovingPercentEncoding
{
	size_t size = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	char *decoded = malloc(size + 1);
	size_t in = 0, out = 0;
	NSString *result;
	size_t units;
	unichar *buffer;

	if (decoded == NULL) {
		return nil;
	}
	while (in < size) {
		unsigned char c = [self byteAtIndex:in];

		if (c == '%') {
			/* TWO HEX DIGITS OR NOTHING: a `%` at the end, or one followed by a non-digit, is the "invalid
			 * percent-encoding sequence" the page names, and the answer is nil rather than a guess. */
			int hi = (in + 1 < size) ? fn_hex_value([self byteAtIndex:in + 1]) : -1;
			int lo = (in + 2 < size) ? fn_hex_value([self byteAtIndex:in + 2]) : -1;

			if (hi < 0 || lo < 0) {
				free(decoded);
				return nil;
			}
			decoded[out++] = (char)((hi << 4) | lo);
			in += 3;
			continue;
		}
		decoded[out++] = (char)c;
		in++;
	}

	/* THE BYTES MUST BE UTF-8, because the promise is "the matching UTF-8 CHARACTERS": a run that is not
	 * UTF-8 has no matching characters, and nil is the honest answer rather than a replacement character. */
	if (!fn_is_utf8(decoded, out)) {
		free(decoded);
		return nil;
	}

	units = fn_utf8_to_utf16(decoded, out, NULL);
	buffer = malloc((units + 1) * sizeof(unichar));
	if (buffer == NULL) {
		free(decoded);
		return nil;
	}
	fn_utf8_to_utf16(decoded, out, buffer);
	result = [NSString stringWithCharacters:buffer length:units];
	free(buffer);
	free(decoded);
	return result;
}

/* ===================================================================================================
 * COMPOSED CHARACTER SEQUENCES — a base letter and the combining characters that follow it. See
 * NSString.h for the page's own definition and for the measured difference from UAX#29 clustering.
 * =================================================================================================== */
- (NSRange)rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)index
{
	NSCharacterSet *marks = [NSCharacterSet nonBaseCharacterSet];
	NSUInteger length = [self length];
	NSUInteger start, end;

	if (index > length) {
		[NSException raise:NSRangeException
		            format:@"-[NSString %@]: index %lu out of bounds for a string of length %lu",
		                   NSStringFromSelector(_cmd), (unsigned long)index, (unsigned long)length];
	}
	if (index == length) {
		return NSMakeRange(length, 0);
	}

	/* THE SCALAR AT A POSITION AND HOW MANY UNITS IT TAKES, as a block rather than a file static: it is the
	 * one piece of arithmetic all three walks below share, and it is not a question any other file asks.
	 * A lone surrogate half answers as itself, which is what an ill-formed string deserves. */
	UTF32Char (^scalarAt)(NSUInteger, NSUInteger *) = ^UTF32Char(NSUInteger at, NSUInteger *units) {
		unichar first = [self characterAtIndex:at];
		unichar second;

		*units = 1;
		if (first < 0xD800 || first > 0xDBFF || at + 1 >= length) {
			return (UTF32Char)first;
		}
		second = [self characterAtIndex:at + 1];
		if (second < 0xDC00 || second > 0xDFFF) {
			return (UTF32Char)first;
		}
		*units = 2;
		return 0x10000 + (((UTF32Char)first - 0xD800) << 10) + ((UTF32Char)second - 0xDC00);
	};

	/* WHICH CHARACTER THE INDEX NAMES: one landing on a low surrogate belongs to the pair it ends. */
	start = index;
	if (index > 0) {
		unichar unit = [self characterAtIndex:index];
		unichar previous = [self characterAtIndex:index - 1];

		if (unit >= 0xDC00 && unit <= 0xDFFF && previous >= 0xD800 && previous <= 0xDBFF) {
			start = index - 1;
		}
	}
	{
		NSUInteger units;

		(void)scalarAt(start, &units);
		end = start + units;
	}

	/* BACK TO THE BASE LETTER, AND THE TEST IS ON THE CHARACTER **AT** `start` RATHER THAN THE ONE BEFORE IT —
	 * which is the correction the probe forced on its first run: a MARK can never be a base letter, so while
	 * the character at the position is a mark, the base lies earlier. The first version tested the character
	 * BEFORE the position, so it walked back only from a base and left an index ON a mark answering itself:
	 * four checks failed and every one of them was a check that starts at a mark. */
	while (start > 0) {
		NSUInteger units;
		UTF32Char scalar = scalarAt(start, &units);
		unichar low;

		if (![marks longCharacterIsMember:scalar]) {
			break;
		}
		start -= 1;
		low = [self characterAtIndex:start];
		if (low >= 0xDC00 && low <= 0xDFFF && start > 0) {
			unichar high = [self characterAtIndex:start - 1];

			if (high >= 0xD800 && high <= 0xDBFF) {
				start -= 1;
			}
		}
	}

	/* FORWARD OVER THE MARKS THAT FOLLOW. The set test is `-longCharacterIsMember:`, which is the right call
	 * for a SCALAR even though this library's `+nonBaseCharacterSet` is a BMP set — NSCharacterSet's own
	 * `FN_MAX_CHARACTER` is 0xFFFF and its `-longCharacterIsMember:` says so — so a mark above U+FFFF is
	 * invisible here and does NOT extend a sequence. That boundary belongs to NSCharacterSet and is measured
	 * by this probe rather than hidden; asking for the scalar means this door follows the day the set gains
	 * astral coverage instead of having to be rediscovered. */
	while (end < length) {
		NSUInteger units;
		UTF32Char scalar = scalarAt(end, &units);

		if (![marks longCharacterIsMember:scalar]) {
			break;
		}
		end += units;
	}
	return NSMakeRange(start, end - start);
}

- (NSRange)rangeOfComposedCharacterSequencesForRange:(NSRange)range
{
	NSRange first, last;

	/* THE REFUSAL IS THE ONE SPELLED ONCE above; its name carries its first user, and a range past the end
	 * is the same out-of-bounds question whichever door asks it. */
	fn_line_check_range(self, range, _cmd);
	if (range.length == 0) {
		return [self rangeOfComposedCharacterSequenceAtIndex:range.location];
	}
	first = [self rangeOfComposedCharacterSequenceAtIndex:range.location];
	last = [self rangeOfComposedCharacterSequenceAtIndex:range.location + range.length - 1];
	return NSMakeRange(first.location, NSMaxRange(last) - first.location);
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

/* THE CAPITALIZATION RULE IS THE EXISTING ONE — `-capitalizedString`'s: a word starts after a space, a tab, a
 * hyphen or an underscore — AND THE LOCALE CHANGES ONLY THE FOLD APPLIED TO IT. In this library that means
 * exactly one language family, the Turkic i/İ and I/ı pairing, which is the same rule the two doors above use.
 *
 * ⚠ THE FOLD IS COMPUTED IN UNITS, NOT BYTES, AND THAT IS NOT A STYLE CHOICE: `-capitalizedString` walks UTF-8
 * BYTES, and the Turkic uppercase of "i" is "İ" — ONE byte becoming TWO — so that door's fixed-size byte buffer
 * cannot express the answer at all. The word rule is therefore restated here over units, which is also why it is
 * spelled as the same four separators rather than as a call into that door. */
- (NSString *)capitalizedStringWithLocale:(id)locale
{
	NSUInteger i, length;
	NSMutableString *out;
	int start = 1;

	if (!fn_language_is_turkic(fn_locale_language(locale))) {
		return [self capitalizedString];	/* THE WHOLE ANSWER FOR EVERY OTHER LOCALE */
	}
	length = [self length];
	out = [NSMutableString string];
	for (i = 0; i < length; i++) {
		unichar c = [self characterAtIndex:i];
		unichar mapped;
		NSString *one;

		if (c == ' ' || c == '\t' || c == '-' || c == '_') {
			start = 1;
			mapped = c;
		} else if (c > 0x7F) {
			/* THE BASE MAPPING MAKES NO GENERAL UNICODE CASE CLAIMS (the header says so), so a letter outside
			 * ASCII passes through — except the one Turkic form that a word-internal position must lower. */
			mapped = (c == 0x0130 && !start) ? 'i' : c;
			start = 0;
		} else if (start) {
			mapped = (c == 'i') ? 0x0130 : (unichar)utf8_upper((unsigned char)c);
			start = 0;
		} else {
			mapped = (c == 'I') ? 0x0131 : (unichar)utf8_lower((unsigned char)c);
		}
		/* THE ROUTER HANDS BACK +1 (see §63.24: `+stringWith…` leaks its string), so the +1 is released here
		 * rather than left to accumulate one per character. */
		one = [[NSString stringWithCharacters:&mapped length:1] autorelease];
		[out appendString:one];
	}
	return out;
}

/* THE THREE DEPRECATED SPELLINGS: ONE DELEGATION EACH TO THE LOCALE-TAKING DOOR, with the CURRENT locale, which
 * is what Apple's names mean — and no second copy of any rule. */
- (NSString *)localizedUppercaseString
{
	return [self uppercaseStringWithLocale:[NSLocale currentLocale]];
}

- (NSString *)localizedLowercaseString
{
	return [self lowercaseStringWithLocale:[NSLocale currentLocale]];
}

- (NSString *)localizedCapitalizedString
{
	return [self capitalizedStringWithLocale:[NSLocale currentLocale]];
}

/* ================================ the localised search fold (§63.26) ================================
 *
 * WHAT THE FOLD IS: the receiver's own case rule (the locale's, so the Turkic pair applies) and, for the
 * "standard" doors, canonical decomposition with the combining marks dropped — which is exactly what
 * diacritic-insensitivity means, and why §63.24's door is what made these expressible at all.
 *
 * ⚠ AND THE MAP IS THE POINT OF THE WHOLE METHOD. A fold does not preserve length ("é" is one unit and folds to
 * one, but "İ" lower-cases to one and a ligature's decomposition may not be one), so a range found in the
 * folded string is NOT a range in the receiver — and Apple's contract is the receiver's units. `-capitalizedString`
 * walks bytes; THIS DOOR IS A LOCALISED SEARCH, so it is built ONE RECEIVER UNIT AT A TIME and remembers, for
 * each unit, the folded index its contribution begins at (`markMap[i]`, with `markMap[length]` the folded
 * length). The range door then walks that map back.
 *
 * `markMap` MAY BE NULL (the contains doors need no way back), and the needles' folds pass NULL for that reason. */
- (NSString *)fn_foldForSearchWithLocale:(id)locale
			  foldDiacritics:(BOOL)foldDiacritics
				  markMap:(NSUInteger *)markMap
{
	NSMutableString *out = [NSMutableString string];
	NSCharacterSet *marks = foldDiacritics ? [NSCharacterSet nonBaseCharacterSet] : nil;
	NSUInteger i, length = [self length];

	for (i = 0; i < length; i++) {
		unichar c = [self characterAtIndex:i];
		NSString *folded = [[NSString stringWithCharacters:&c length:1] autorelease];

		if (markMap != NULL) {
			markMap[i] = [out length];
		}
		folded = [folded lowercaseStringWithLocale:locale];	/* THE LOCALE'S CASE RULE, the Turkic pair included */
		if (foldDiacritics) {
			folded = [folded decomposedStringWithCanonicalMapping];	/* §63.24: "é" becomes "e" + a mark */
		}
		if (marks != nil) {
			/* THE MARKS COME OFF, AND THE MAP DOES NOT MOVE: dropping a mark takes no folded index with it, so
			 * the next unit's entry still points at where the caller's own unit begins. */
			NSUInteger j, pieceLength = [folded length];

			for (j = 0; j < pieceLength; j++) {
				unichar f = [folded characterAtIndex:j];

				if (![marks characterIsMember:f]) {
					[out appendString:[[NSString stringWithCharacters:&f length:1] autorelease]];
				}
			}
		} else {
			[out appendString:folded];
		}
	}
	if (markMap != NULL) {
		markMap[length] = [out length];
	}
	return out;
}

- (NSRange)localizedStandardRangeOfString:(NSString *)string
{
	id locale = [NSLocale currentLocale];
	NSUInteger length = [self length];
	NSUInteger *markMap;
	NSString *haystack, *needle;
	NSRange found, answer = NSMakeRange(NSNotFound, 0);
	NSUInteger i;

	if (string == nil) {
		return answer;
	}
	markMap = (NSUInteger *)malloc((length + 1) * sizeof(NSUInteger));
	if (markMap == NULL) {
		return [self rangeOfString:string];
	}
	haystack = [self fn_foldForSearchWithLocale:locale foldDiacritics:YES markMap:markMap];
	needle = [string fn_foldForSearchWithLocale:locale foldDiacritics:YES markMap:NULL];
	found = [haystack rangeOfString:needle];
	if (found.location == NSNotFound) {
		free(markMap);
		return found;			/* NSNotFound, and a length of 0 */
	}
	/* BACK INTO THE RECEIVER'S UNITS: the first receiver index whose folded entry starts the match, and the
	 * first one after it whose entry reaches its end. THIS IS WHAT MAKES THE ANSWER THE CALLER'S: with the
	 * folded range alone, searching "café" for "cafe" would answer a range into a string nobody holds. */
	for (i = 0; i <= length; i++) {
		if (markMap[i] == found.location) {
			answer.location = i;
			break;
		}
	}
	for (i = answer.location; i <= length; i++) {
		if (markMap[i] >= found.location + found.length) {
			answer.length = i - answer.location;
			break;
		}
	}
	free(markMap);
	return answer;
}

- (BOOL)localizedStandardContainsString:(NSString *)string
{
	return [self localizedStandardRangeOfString:string].location != NSNotFound;
}

- (BOOL)localizedCaseInsensitiveContainsString:(NSString *)string
{
	/* CASE, NOT DIACRITICS: the name says which fold, and the locale still supplies the CASE rule — which is
	 * why this is not `-rangeOfString:options:NSCaseInsensitiveSearch` on the raw string. */
	id locale = [NSLocale currentLocale];
	NSString *folded = [self fn_foldForSearchWithLocale:locale foldDiacritics:NO markMap:NULL];

	if (string == nil) {
		return NO;
	}
	return [folded rangeOfString:
		[string fn_foldForSearchWithLocale:locale foldDiacritics:NO markMap:NULL]].location != NSNotFound;
}

- (const char *)cStringUsingEncoding:(NSStringEncoding)encoding
{
	size_t i;

	if (!fn_is_storage_encoding(encoding)) {
		/* ⚠ §63.72 — THE BORROWED POINTER FOR A CONVERTED ENCODING, AND WHY IT IS THE POOL'S: this
		 * door promises a pointer valid "until the receiver is freed, OR UNTIL THE POOL IS EMPTIED" — and the
		 * base class has no per-instance buffer to hold one, because the storage belongs to the concrete
		 * classes. The storage encodings keep returning the STORAGE's own bytes (below), which is why the UTF-8
		 * answer is free and per-instance; everything else answers the bytes of a buffer that lives as long as
		 * the pool does. THAT IS THE SECOND HALF OF APPLE'S OWN CONTRACT, chosen over putting an ivar on a base
		 * whose subclasses own their storage. */
		NSData *converted = fn_data_in_encoding(self, encoding, NO);

		return (converted != nil) ? (const char *)[converted bytes] : NULL;
	}

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

/* ===================================================================================================
 * THE C-STRING AND CHARACTER-COPY DOORS, AND ENCODING INTROSPECTION (§63.30).
 *
 * ALL OF THESE GO THROUGH ONE STORAGE FACT: this library's C-string encoding is UTF-8, and UTF-8 IS the
 * storage, so nothing here transcodes. The deprecated names take "the default C-string encoding", whose
 * one honest value here is UTF-8 (it is the only encoding this library stores, and §62.24 keeps deprecated
 * API in scope as a porting target); the modern -getCString:maxLength:encoding: is the door they delegate
 * to, so the copy-out rule is spelled once.
 * =================================================================================================== */
- (const char *)cString
{
	/* Apple frees the answer with the receiver's autorelease pool and tells a caller to copy it if the
	 * string must outlive that. Here it is NSOwnedString's OWN materialised cache, which lives as long as
	 * the receiver — a STRONGER guarantee than Apple's, and the pointer stays valid for that whole life. */
	return [self UTF8String];
}

- (const char *)lossyCString
{
	/* Lossiness matters only where a character has no byte in the target encoding; UTF-8 has a byte for
	 * every character, so the lossy answer is the lossless one. */
	return [self UTF8String];
}

- (NSUInteger)cStringLength
{
	/* BYTES, NOT UNITS: the length of the C-string representation, excluding the terminating NUL. */
	return [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
}

- (void)getCString:(char *)buffer
{
	/* THE OLDEST AND MOST DANGEROUS FORM: no length is passed, so Apple's contract is "buffer is large
	 * enough". This library follows that contract rather than inventing a bound only it could know. */
	size_t bytes = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];

	if (buffer == NULL) {
		return;
	}
	memcpy(buffer, [self UTF8String], bytes);
	buffer[bytes] = '\0';
}

- (void)getCString:(char *)buffer maxLength:(NSUInteger)maxLength
{
	/* Apple: "Invokes -getCString:maxLength:encoding: with the default encoding", and the receiver "does
	 * nothing" when the conversion fails — so the BOOL is dropped and a failure leaves the buffer alone. */
	if (buffer == NULL) {
		return;
	}
	(void)[self getCString:buffer maxLength:maxLength encoding:NSUTF8StringEncoding];
}

- (BOOL)getCString:(char *)buffer maxLength:(NSUInteger)maxLength encoding:(NSStringEncoding)encoding
{
	if (!fn_is_storage_encoding(encoding)) {
		/* §63.72: THIS DOOR COPIES, so a converted encoding is a plain conversion and NO LIFETIME QUESTION
		 * ARISES — which is exactly why it is the door Apple tells a caller to use when the buffer has to
		 * outlive the call. The length test is the same one below: the STRING AND ITS TERMINATOR must fit. */
		size_t n = 0;
		unsigned char *b = fn_bytes_in_encoding(self, encoding, NO, &n);
		BOOL fits;

		if (buffer == NULL || b == NULL) {
			free(b);
			return NO;
		}
		fits = (n + 1 <= maxLength);
		if (fits) {
			memcpy(buffer, b, n);
			buffer[n] = '\0';
		}
		free(b);
		return fits;
	}
	/* -cStringUsingEncoding: answers NULL for an unstored encoding AND for ASCII content carrying a high
	 * byte, so one guard refuses both — the same refusal as the outgoing door, spelled where it lives. */
	const char *bytes = [self cStringUsingEncoding:encoding];
	size_t n;

	if (buffer == NULL || bytes == NULL) {
		return NO;
	}
	/* `bytes` was non-NULL, so the encoding is UTF-8 or ASCII and BOTH store the SAME bytes here (an
	 * ASCII string's UTF-8 form is its ASCII form); the byte count is this class's UTF-8 one. */
	n = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	if (n + 1 > maxLength) {
		return NO;		/* the string AND its terminating NUL do not fit */
	}
	memcpy(buffer, bytes, n);
	buffer[n] = '\0';
	return YES;
}

- (void)getCharacters:(unichar *)buffer
{
	/* The deprecated no-range form copies EVERY unit and adds no terminator — which Apple's page says is
	 * exactly the range form over the whole extent, so this is a delegation rather than a second walk. */
	[self getCharacters:buffer range:NSMakeRange(0, [self length])];
}

/* ===================================================================================================
 * §63.47: THE BORROWED-BUFFER FAMILY — SIX DOORS THAT ARE ONE CONTRACT, SO THEY LAND TOGETHER.
 *
 * "NoCopy" IS A HINT, which Apple's own header says in as many words. This library COPIES (it has to: its
 * storage is UTF-16 units and a payload is owned whichever class answers), and the hint is honoured where it
 * MATTERS TO THE CALLER — the buffer is still released by whoever took it, through `free` or through the
 * caller's own block, which is what the two spellings mean. A caller that passes `freeWhenDone:YES` is
 * handing over ownership and gets it disposed of exactly once, whether or not the copy happened.
 *
 * AND THE TWO ENCODING DOORS REFUSE WHAT THIS LIBRARY CANNOT STORE, which is the same rule the outgoing
 * doors already follow (UTF-8 and ASCII, and nothing else) — named at the door rather than answered with a
 * lossy approximation invented here.
 * =================================================================================================== */

- (nullable id)initWithBytesNoCopy:(void *)bytes length:(NSUInteger)length encoding:(NSStringEncoding)encoding freeWhenDone:(BOOL)freeBuffer
{
	id string = [self initWithBytes:bytes length:length encoding:encoding];

	if (freeBuffer && bytes != NULL) {
		free(bytes);
	}
	return string;
}

- (nullable id)initWithBytesNoCopy:(void *)bytes length:(NSUInteger)length encoding:(NSStringEncoding)encoding deallocator:(void (^)(void *, NSUInteger))deallocator
{
	id string = [self initWithBytes:bytes length:length encoding:encoding];

	if (deallocator != NULL) {
		deallocator(bytes, length);
	}
	return string;
}

/* THE DEPRECATED NO-ENCODING FORM: Apple's replacement note names `-initWithCString:encoding:`, and the
 * encoding to use in its absence is the one every other encoding-less C-string door here uses — this
 * library's UTF-8 rule — so it is that door with the encoding fixed, not a second decoder. */
- (nullable id)initWithCStringNoCopy:(char *)bytes length:(NSUInteger)length freeWhenDone:(BOOL)freeBuffer
{
	id string = [self initWithBytes:bytes length:length encoding:NSUTF8StringEncoding];

	if (freeBuffer && bytes != NULL) {
		free(bytes);
	}
	return string;
}

- (id)initWithCharactersNoCopy:(unichar *)characters length:(NSUInteger)length deallocator:(void (^)(unichar *, NSUInteger))deallocator
{
	/* THE COPY IS THE ONE THE freeWhenDone: FORM ALREADY MAKES, with ownership explicitly NOT taken — this
	 * door's caller says what to do with the buffer instead, and it is called even when the buffer is
	 * NULL, because a block that was handed over is a block that runs. */
	id string = [self initWithCharactersNoCopy:characters length:length freeWhenDone:NO];

	if (deallocator != NULL) {
		deallocator(characters, length);
	}
	return string;
}

/* `-getBytes:…` IS THE ONE DOOR HERE THAT TAKES ANY ENCODING IN APPLE'S HEADER, and this library answers it
 * for the encodings it can STORE and refuses the rest rather than approximating them: a NULL buffer is
 * Apple's "tell me the size" form and is answered, and `options` carries the lossy/external-representation
 * hints nothing here takes. */
- (BOOL)getBytes:(nullable void *)buffer
       maxLength:(NSUInteger)maxBufferCount
      usedLength:(nullable NSUInteger *)usedBufferCount
	 encoding:(NSStringEncoding)encoding
	  options:(NSStringEncodingConversionOptions)options
	    range:(NSRange)range
   remainingRange:(nullable NSRangePointer)leftover
{
	NSString *piece = [self substringWithRange:range];
	const char *bytes = [piece cStringUsingEncoding:encoding];
	NSUInteger n;

	if (!fn_is_storage_encoding(encoding)) {
		/* ⚠⚠ §63.72 — AND THIS DOOR HAD TO LEARN THE ENGINE FOR A REASON WORSE THAN A REFUSAL.
		 * It used to refuse implicitly, because `-cStringUsingEncoding:` answered NULL for everything but the
		 * storage encodings; once that door CONVERTED, this one went on to take `n` from
		 * `-lengthOfBytesUsingEncoding:NSUTF8StringEncoding` — THE WRONG ENCODING'S COUNT — and memcpy that
		 * many bytes OUT OF THE CONVERTED BUFFER. UTF-16 "abc" is 8 bytes and n said 3, so it would have
		 * written the first three bytes of a UTF-16 string: a silent corruption instead of an honest refusal.
		 * APPLE'S CONTRACT IS A CONVERSION — "converts the receiver's characters into a given C string
		 * encoding and stores the result" — so the answer is to convert, with THIS door's own length, and to
		 * honour the lossy option it is passed. */
		BOOL lossy = (options & NSStringEncodingConversionAllowLossy) != 0;
		size_t n = 0;
		unsigned char *b = fn_bytes_in_encoding(piece, encoding, lossy, &n);

		if (b == NULL) {
			if (leftover != NULL) {
				*leftover = range;	/* nothing was converted */
			}
			return NO;
		}
		if (buffer != NULL && n > maxBufferCount) {
			free(b);
			if (leftover != NULL) {
				*leftover = range;
			}
			return NO;
		}
		if (buffer != NULL) {
			memcpy(buffer, b, n);
		}
		free(b);
		if (usedBufferCount != NULL) {
			*usedBufferCount = n;
		}
		if (leftover != NULL) {
			*leftover = NSMakeRange(NSMaxRange(range), 0);
		}
		return YES;
	}
	(void)options;		/* the storage encodings have no lossy form to choose: nothing can be lost */

	if (buffer == NULL) {
		/* THE SIZE FORM. Apple: "If buffer is NULL, the method returns the number of bytes required" — this
		 * library answers the question it can answer exactly and reports the whole range consumed. */
		if (usedBufferCount != NULL) {
			*usedBufferCount = [piece lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
		}
		if (leftover != NULL) {
			*leftover = NSMakeRange(NSMaxRange(range), 0);
		}
		return YES;
	}
	if (bytes == NULL) {
		if (leftover != NULL) {
			*leftover = range;	/* nothing was converted */
		}
		return NO;
	}
	n = [piece lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	if (n > maxBufferCount) {
		if (leftover != NULL) {
			*leftover = range;
		}
		return NO;
	}
	memcpy(buffer, bytes, n);
	if (usedBufferCount != NULL) {
		*usedBufferCount = n;
	}
	if (leftover != NULL) {
		*leftover = NSMakeRange(NSMaxRange(range), 0);
	}
	return YES;
}

/* THE DEPRECATED RANGE FORM of -getCString:maxLength:, and it differs from that door in the two ways its
 * name says: it converts a RANGE rather than the whole receiver, and it REPORTS what it could not convert.
 * The conversion itself is the modern door's over the substring, so a failure leaves the buffer alone. */
- (void)getCString:(char *)buffer
	 maxLength:(NSUInteger)maxLength
	     range:(NSRange)range
    remainingRange:(nullable NSRangePointer)leftoverRange
{
	NSString *piece = [self substringWithRange:range];

	if (buffer == NULL) {
		return;
	}
	if (![piece getCString:buffer maxLength:maxLength encoding:NSUTF8StringEncoding]) {
		if (leftoverRange != NULL) {
			*leftoverRange = range;
		}
		return;
	}
	if (leftoverRange != NULL) {
		*leftoverRange = NSMakeRange(NSMaxRange(range), 0);
	}
}

+ (id)stringWithCString:(const char *)cString
{
	/* The default encoding is UTF-8, so this is the §63.28 door with that encoding named — one refusal
	 * rule rather than two, and a NULL answers nil through it. */
	return [self stringWithCString:cString encoding:NSUTF8StringEncoding];
}

+ (id)stringWithCString:(const char *)cString length:(NSUInteger)length
{
	/* ⚠ THE LENGTH IS HONOURED AND THE NUL IS NOT: the bytes need not be NUL-terminated, so this reads
	 * EXACTLY `length` of them through -initWithBytes:length:, the constructor that takes a count. */
	if (cString == NULL) {
		return nil;
	}
	return [[[self alloc] initWithBytes:cString length:length] autorelease];
}

/* THE INSTANCE FORMS OF THE SAME TWO CREATION DOORS (§63.30's C-string family). Each is the -init mirror of a
 * `+stringWithCString:…` above, so the two refusals live in the same two places: an encoding this library does
 * not store, and a high byte under the ASCII label. The deprecated no-encoding pair takes the one honest default
 * C-string encoding here, UTF-8 — the same value its class twin names by hand. */
- (id)initWithCString:(const char *)nullTerminatedCString encoding:(NSStringEncoding)encoding
{
	if (!fn_is_storage_encoding(encoding)) {
		NSString *converted = (nullTerminatedCString != NULL)
			? fn_string_from_bytes(nullTerminatedCString, strlen(nullTerminatedCString), encoding)
			: nil;
		id result = nil;

		if (converted != nil) {
			result = [self initWithString:converted];
		}
		[converted release];
		return result;
	}
	size_t i, length;

	if (nullTerminatedCString == NULL) {
		return nil;
	}
	if (encoding == NSUTF8StringEncoding) {
		return [self initWithUTF8String:nullTerminatedCString];
	}
	if (encoding == NSASCIIStringEncoding) {
		/* ⚠ THE BYTES ARE CHECKED, NOT THE LABEL: a high byte under the ASCII name is refused rather than
		 * reinterpreted, exactly as +stringWithCString:encoding: refuses it. */
		for (length = strlen(nullTerminatedCString), i = 0; i < length; i++) {
			if (((const unsigned char *)nullTerminatedCString)[i] > 0x7F) {
				return nil;
			}
		}
		return [self initWithUTF8String:nullTerminatedCString];
	}
	return nil;			/* an encoding we do not store */
}

- (id)initWithCString:(const char *)nullTerminatedCString
{
	/* the deprecated no-encoding name: the default C-string encoding is UTF-8 */
	return [self initWithCString:nullTerminatedCString encoding:NSUTF8StringEncoding];
}

- (id)initWithBytes:(const void *)bytes length:(NSUInteger)len encoding:(NSStringEncoding)encoding
{
	if (!fn_is_storage_encoding(encoding)) {
		NSString *converted = fn_string_from_bytes(bytes, (size_t)len, encoding);
		id result = nil;

		if (converted != nil) {
			result = [self initWithString:converted];
		}
		[converted release];
		return result;
	}
	/* THE MODERN LENGTH-TAKING BYTE DOOR. Only the stored encodings are accepted, and the ASCII case checks the
	 * BYTES rather than the label, the same rule the C-string doors apply. */
	if (encoding != NSUTF8StringEncoding && encoding != NSASCIIStringEncoding) {
		return nil;
	}
	if (encoding == NSASCIIStringEncoding) {
		const unsigned char *b = (const unsigned char *)bytes;
		NSUInteger i;

		for (i = 0; i < len; i++) {
			if (b[i] > 0x7F) {
				return nil;
			}
		}
	}
	/* Re-routed like the other doors on this front: the storage is the concrete subclass's, so the abstract
	 * receiver is released and the length-taking UTF-8 constructor is used (it treats the bytes as UTF-8 and a
	 * NULL buffer as the empty string). `[self release]` first, so the front does not leak. */
	[self release];
	return [[NSOwnedString alloc] initWithBytes:(const char *)bytes length:(size_t)len];
}

- (id)initWithCString:(const char *)bytes length:(NSUInteger)length
{
	/* ⚠ THE LENGTH IS HONOURED AND THE NUL IS NOT, the same rule +stringWithCString:length: records: the bytes
	 * need not be NUL-terminated, so exactly `length` of them are read. Deprecated name; default encoding UTF-8. */
	if (bytes == NULL) {
		return nil;
	}
	[self release];
	return [[NSOwnedString alloc] initWithBytes:(const char *)bytes length:(size_t)length];
}

- (BOOL)canBeConvertedToEncoding:(NSStringEncoding)encoding
{
	if (!fn_is_storage_encoding(encoding)) {
		/* A CONVERSION THAT WOULD LOSE SOMETHING IS NOT ONE THIS DOOR CALLS POSSIBLE, which is why it
		 * asks for the LOSSLESS answer: the same call `-dataUsingEncoding:` makes. */
		size_t n = 0;
		unsigned char *b = fn_bytes_in_encoding(self, encoding, NO, &n);

		if (b == NULL) {
			return NO;
		}
		free(b);
		return YES;
	}
	size_t i, n;

	if (encoding == NSUTF8StringEncoding) {
		return YES;			/* the storage IS UTF-8 */
	}
	if (encoding != NSASCIIStringEncoding) {
		return NO;			/* an encoding this library does not store */
	}
	n = [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	for (i = 0; i < n; i++) {
		if ([self byteAtIndex:i] > 0x7F) {
			return NO;		/* a high byte has no 7-bit ASCII representation */
		}
	}
	return YES;
}

- (NSUInteger)maximumLengthOfBytesUsingEncoding:(NSStringEncoding)encoding
{
	if (!fn_is_storage_encoding(encoding)) {
		/* THE EXACT LENGTH IS A VALID UPPER BOUND, which is what this door promises: the conversion is
		 * done once here rather than estimated, so the answer is never a guess. */
		return (NSUInteger)fn_length_in_encoding(self, encoding);
	}
	/* Apple answers the MAXIMUM byte count a conversion could take, and 0 when the encoding "cannot be
	 * used" to convert the receiver. This library stores UTF-8 and ASCII, so the maximum is the UTF-8 byte
	 * count for a UTF-8 conversion and for an ASCII one that is possible; an ASCII conversion of a
	 * high-byte string, and any unstored encoding, answer 0. */
	if (encoding == NSUTF8StringEncoding) {
		return [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	}
	if (encoding == NSASCIIStringEncoding && [self canBeConvertedToEncoding:NSASCIIStringEncoding]) {
		return [self lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	}
	return 0;
}

/* ===================================================================================================
 * ENCODING INTROSPECTION (§63.30's sibling). Four facts about this library's storage, and NOTHING here
 * needs a converter or a repertoire table — which is exactly why these rows are not the blocked
 * conversion cluster (§63.2). The values follow the header's note: the storage is UTF-8, so the fastest
 * encoding is UTF-8; the smallest is ASCII exactly when every byte is 7-bit (the same test
 * -canBeConvertedToEncoding: makes, so the two doors cannot disagree); the default C-string encoding is
 * the storage (§63.30 already states it for the deprecated C-string doors); and the available list is
 * the TWO the storage can represent, terminated by a zero, because naming an encoding this library
 * cannot honour would be the opposite of the refusal every other encoding door here makes.
 * =================================================================================================== */
- (NSStringEncoding)fastestEncoding
{
	return NSUTF8StringEncoding;		/* the storage IS UTF-8 */
}

- (NSStringEncoding)smallestEncoding
{
	return [self canBeConvertedToEncoding:NSASCIIStringEncoding]
		? NSASCIIStringEncoding : NSUTF8StringEncoding;
}

+ (NSStringEncoding)defaultCStringEncoding
{
	return NSUTF8StringEncoding;		/* §63.30: the one honest default, since the storage is UTF-8 */
}

+ (const NSStringEncoding *)availableStringEncodings
{
	/* A zero-terminated list (Apple's own shape). The terminator is a ZERO, not NSASCIIStringEncoding, because
	 * that value is 1. ⚠§63.71: THE LIST IS BUILT FROM THE TABLE AND FROM WHAT THE CONVERTERS ACTUALLY OPEN,
	 * so it names what this library can CONVERT — a list that named an encoding `-dataUsingEncoding:` refuses
	 * would be the same lie in the other direction. */
	static NSStringEncoding encodings[FN_UCNV_CACHE * 4 + 4];
	static int built = 0;

	if (!built) {
		size_t i, n = 0;

		encodings[n++] = NSASCIIStringEncoding;
		encodings[n++] = NSUTF8StringEncoding;
		for (i = 0; i < fn_encoding_count() && n < (FN_UCNV_CACHE * 4 + 3); i++) {
			if (fn_ucnv_for(fn_encoding_table[i].encoding) != NULL) {
				encodings[n++] = fn_encoding_table[i].encoding;
			}
		}
		encodings[n] = 0;
		built = 1;
	}
	return encodings;
}

+ (NSString *)localizedNameOfStringEncoding:(NSStringEncoding)encoding
{
	/* THE NAMES ARE OURS (§11.6.1 D2 — Apple publishes the method and not a name table this library may copy),
	 * and they now name EVERY encoding this library can convert, which is what `+availableStringEncodings`
	 * answers: ⚠§63.71 — THE TWO MUST AGREE OR ONE OF THEM LIES, and that is why the table carries a display
	 * name beside the converter name. An encoding with no converter, and an unknown one, are answered nil,
	 * which is Apple's contract for one it does not know. */
	const FnEncodingEntry *e;

	if (encoding == NSASCIIStringEncoding) {
		return @"ASCII";
	}
	if (encoding == NSUTF8StringEncoding) {
		return @"UTF-8";
	}
	e = fn_encoding_entry(encoding);
	if (e != NULL && fn_ucnv_for(encoding) != NULL) {
		return [NSString stringWithUTF8String:e->display];
	}
	return nil;
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

+ (NSString *)pathWithComponents:(NSArray *)components
{
	/* APPLE'S JOINING RULE: the components are joined with "/", and a leading "/" component makes the result
	 * absolute rather than doubled — the slash is already there, so none is inserted before the next one. */
	NSMutableString *built = [[NSMutableString alloc] initWithUTF8String:""];
	NSUInteger i;

	for (i = 0; i < [components count]; i++) {
		NSString *component = [components objectAtIndex:i];

		if (i == 0 || [built hasSuffix:@"/"]) {
			[built appendString:component];
		} else {
			[built appendFormat:@"/%@", component];
		}
	}
	return built;
}

- (NSArray *)stringsByAppendingPaths:(NSArray *)paths
{
	/* ONE JOINING RULE: each answer is -stringByAppendingPathComponent: for the matching element, so this door
	 * and that one cannot disagree about where the slash goes. */
	NSMutableArray *result = [[NSMutableArray alloc] init];
	NSUInteger i;

	for (i = 0; i < [paths count]; i++) {
		[result addObject:[self stringByAppendingPathComponent:[paths objectAtIndex:i]]];
	}
	return result;
}

- (const char *)fileSystemRepresentation
{
	/* THIS FILESYSTEM'S PATH FORM IS UTF-8 — the storage itself (§63.30) — so the representation is the storage,
	 * and the pointer has NSOwnedString's own lifetime. */
	return [self UTF8String];
}

- (BOOL)getFileSystemRepresentation:(char *)buffer maxLength:(NSUInteger)maxLength
{
	/* THE -getCString:maxLength:encoding: RULE with the encoding fixed to the one this filesystem uses. */
	return [self getCString:buffer maxLength:maxLength encoding:NSUTF8StringEncoding];
}

- (NSString *)stringByExpandingTildeInPath
{
	NSRange slash;
	NSString *head, *tail, *home;

	if ([self length] == 0 || [self characterAtIndex:0] != '~') {
		return self;			/* no leading tilde: unchanged */
	}
	slash = [self rangeOfString:@"/"];
	if (slash.location == NSNotFound) {
		head = [self substringFromIndex:1];
		tail = @"";
	} else {
		head = [self substringWithRange:NSMakeRange(1, slash.location - 1)];
		tail = [self substringFromIndex:slash.location];
	}
	if ([head length] == 0) {
		home = NSHomeDirectory();
	} else {
		home = NSHomeDirectoryForUser(head);
		if (home == nil) {
			return self;		/* an unknown ~user is left unchanged (Apple's own behaviour) */
		}
	}
	return [home stringByAppendingString:tail];
}

- (NSString *)stringByAbbreviatingWithTildeInPath
{
	NSString *home = NSHomeDirectory();
	NSUInteger homeLen = [home length];

	if ([self isEqualToString:home]) {
		return @"~";
	}
	if ([self length] > homeLen && [self hasPrefix:home] &&
	    [self characterAtIndex:homeLen] == '/') {
		return [@"~" stringByAppendingString:[self substringFromIndex:homeLen]];
	}
	return self;			/* not under the home directory: unchanged */
}

/* ===================================================================================================
 * §63.48: THE TWO FILESYSTEM DOORS, AND THE BOUNDARY THEY MOVE.
 *
 * **THE STRING PATH DOORS WERE LEXICAL-ONLY BY A NOTE, NOT BY NECESSITY** — the header said
 * `-stringByResolvingSymlinksInPath` "is NOT here: resolving a symlink is a FILESYSTEM LOOKUP, and this
 * library's string path doors are deliberately lexical". §11's rule is that a difference from Apple is a
 * FAILURE unless Argentum cannot have Apple's behaviour, which is a claim about the PLATFORM — and this one
 * can (`realpath` is here). The user decided (2026-10-01) that fidelity wins, so the door exists, the
 * boundary change is registered in §11.6, and the header note is corrected rather than kept.
 *
 * AND WHAT KEEPS IT SAFE IS THE DOCUMENTED FAILURE: a path whose links cannot be resolved comes back
 * UNMODIFIED — which is exactly the lexical answer this class gave before — so nothing that read these doors
 * as strings starts reading them as lookups by accident.
 * =================================================================================================== */
- (NSString *)stringByResolvingSymlinksInPath
{
	NSString *expanded = [self stringByExpandingTildeInPath];
	const char *fs = [expanded fileSystemRepresentation];
	char *resolved;

	if (fs == NULL) {
		return [expanded copy];
	}
	/* ONE CALL DOES THE WHOLE WALK — every component, links and all — which is the answer Apple's door
	 * describes ("path components representing symbolic links have been replaced by their referents"). The
	 * NULL second argument is POSIX.1-2008's "allocate the buffer for me", so no PATH_MAX buffer is guessed
	 * at here. */
	resolved = realpath(fs, NULL);
	if (resolved == NULL) {
		return [expanded copy];
	}
	{
		NSString *answer = [NSString stringWithUTF8String:resolved];

		free(resolved);
		return (answer != nil) ? answer : [expanded copy];
	}
}

- (NSComparisonResult)localizedStandardCompare:(NSString *)string
{
	/* APPLE'S OWN HEADER NOTE IS MOSTLY A WARNING, AND IT IS WORTH REPEATING VERBATIM: this door "should be
	 * used whenever file names or other strings are presented in lists and tables where Finder-like sorting
	 * is appropriate. The exact behavior of this method may be tweaked in future releases, and will be
	 * different under different localizations, so clients should not depend on the exact sorting order."
	 *
	 * WHAT THIS LIBRARY CAN HONOUR EXACTLY IS THE PART THE SAME NOTE NAMES: case-insensitive, and NUMERIC —
	 * "abc2" sorts before "abc100". Both are options this class already implements, so the door is that
	 * comparison rather than a second ordering rule.
	 *
	 * AND THE LOCALISATION-DEPENDENT HALF IS STATED RATHER THAN FAKED: `-localizedCompare:` here compares by
	 * BYTE (its own header says so), so there is no collation table to consult and none is invented. A caller
	 * who needs Apple's exact ordering under a particular locale does not get it, and this is where that is
	 * written down. */
	return [self compare:string options:(NSCaseInsensitiveSearch | NSNumericSearch)];
}

- (NSUInteger)completePathIntoString:(NSString * _Nullable * _Nullable)outputName
		       caseSensitive:(BOOL)flag
		    matchesIntoArray:(NSArray * _Nullable * _Nullable)outputArray
			 filterTypes:(nullable NSArray *)filterTypes
{
	NSString *directory = [self stringByDeletingLastPathComponent];
	NSString *partial = [self lastPathComponent];
	NSError *ignored = nil;
	NSArray *entries;
	NSMutableArray *matches = [[NSMutableArray alloc] init];
	NSUInteger i;

	if (outputName != NULL) {
		*outputName = nil;
	}
	if (outputArray != NULL) {
		*outputArray = nil;
	}
	/* THE DIRECTORY LISTING IS `NSFileManager`'S, not a second `opendir` walk: one reader for "what is in this
	 * directory" is the rule every other door here follows. An unreadable directory completes NOTHING and
	 * says so with a zero — the honest answer rather than an exception. */
	entries = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:directory error:&ignored];
	if (entries == nil) {
		return 0;
	}
	for (i = 0; i < [entries count]; i++) {
		NSString *name = [entries objectAtIndex:i];
		BOOL hit;

		if (flag) {
			hit = [name hasPrefix:partial];
		} else {
			hit = [name rangeOfString:partial
					  options:NSCaseInsensitiveSearch].location == 0;
		}
		if (hit && filterTypes != nil && [filterTypes count] > 0 &&
		    ![filterTypes containsObject:[name pathExtension]]) {
			hit = NO;
		}
		if (hit) {
			[matches addObject:[directory stringByAppendingPathComponent:name]];
		}
	}
	/* APPLE'S TWO ANSWERS TO TWO QUESTIONS: a UNIQUE completion is the one the door hands back by name, and
	 * every match is handed back in the array — which is how a caller learns there was more than one. */
	if ([matches count] == 1 && outputName != NULL) {
		*outputName = [matches objectAtIndex:0];
	}
	if (outputArray != NULL) {
		*outputArray = matches;
	}
	return [matches count];
}

- (NSString *)stringByStandardizingPath
{
	/*
	 * LEXICAL, WHICH IS WHAT THIS NAME PROMISES — and the sentence that used to follow it, "there is no
	 * filesystem in this library to look anything up in", was simply FALSE: NSFileManager has always read the
	 * disk, and §63.48 gave this class a door that resolves links. WHAT REMAINS TRUE IS THE BOUNDARY ITSELF:
	 * collapsing repeated slashes, dropping "." components and resolving ".." by POPPING are LEXICAL
	 * operations, and following a symlink is not one — so they stay two doors, and this is the one that never
	 * touches the disk.
	 */
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

/* CREATION FROM A C STRING WITH AN ENCODING (§63.28): THE MIRROR OF `-cStringUsingEncoding:` ABOVE, and it
 * refuses in the same two places for the same two reasons — the encodings this library stores are UTF-8 and
 * ASCII, and a byte string under any other name is NOT reinterpreted as UTF-8.
 *
 * ⚠ AND THE ASCII CASE CHECKS THE BYTES IT IS HANDED RATHER THAN TRUSTING THE LABEL: a C string with a high byte
 * in it is not ASCII, and answering a string for it would be exactly the mistake the outbound door refuses in
 * the other direction.
 *
 * ⚠ AND THIS DOOR RETURNS +0 (autoreleased) WHERE THE FILE DOORS BELOW RETURN +1: Apple's contract for a
 * `+stringWith…` factory is +0, and this family's file/URL doors leak their string (measured, named in §63.24).
 * Adding a fifth leak to keep the neighbours company is not consistency worth having — the fix belongs to them,
 * and the plan's NSString unit carries it. */
+ (id)stringWithCString:(const char *)cString encoding:(NSStringEncoding)encoding
{
	size_t i, length;

	if (cString == NULL) {
		return nil;
	}
	if (!fn_is_storage_encoding(encoding)) {
		/* §63.71: THE ENGINE, and +0 like every other answer from this door (see the note above). This
		 * was the THIRD place the conversion had to be taught: the class factory does not go through
		 * `-initWithCString:encoding:`, so teaching the initializer left this door refusing Latin-1 while the
		 * instance door converted it. */
		return [fn_string_from_bytes(cString, strlen(cString), encoding) autorelease];
	}
	if (encoding == NSUTF8StringEncoding) {
		return [[[self alloc] initWithUTF8String:cString] autorelease];
	}
	if (encoding == NSASCIIStringEncoding) {
		for (length = strlen(cString), i = 0; i < length; i++) {
			if (((const unsigned char *)cString)[i] > 0x7F) {
				return nil;
			}
		}
		return [[[self alloc] initWithUTF8String:cString] autorelease];
	}
	return nil;			/* an encoding we do not store */
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

/* -------------------------------------------------------------------- the URLs (§63.29)
 *
 * THE SAME TWO DOORS OVER A URL RATHER THAN A PATH, and the split is the URL's own: a FILE url is handed
 * straight to the path doors above — one implementation of "read the bytes, then decide what they mean" — while
 * any other scheme goes through the ONE synchronous loader this library has,
 * `+[NSURLConnection sendSynchronousRequest:returningResponse:error:]`, which is what Apple's contract means by
 * reading a URL.
 *
 * ⚠ AND WHAT THE CALLER SEES IS WHAT WENT WRONG: the transport's error, or the decoder's, is passed through
 * unchanged, so a missing file, a refused scheme and a bad encoding stay three different answers. */
+ (id)stringWithContentsOfURL:(NSURL *)url encoding:(NSStringEncoding)encoding error:(NSError **)errorPtr
{
	NSData *data;

	if (url == nil) {
		return nil;
	}
	if ([url isFileURL]) {
		/* +0 OUT, as this door's contract has it, even though the file door hands back +1 (its leak, §63.24). */
		return [[self stringWithContentsOfFile:[url path] encoding:encoding error:errorPtr] autorelease];
	}
	data = [NSURLConnection sendSynchronousRequest:[NSURLRequest requestWithURL:url]
				     returningResponse:NULL
						 error:errorPtr];
	if (data == nil) {
		return nil;
	}
	return [[[self alloc] initWithData:data encoding:encoding] autorelease];
}

+ (id)stringWithContentsOfURL:(NSURL *)url
		 usedEncoding:(NSStringEncoding *)encoding
			error:(NSError **)errorPtr
{
	NSData *data;
	id result;

	if (url == nil) {
		return nil;
	}
	if ([url isFileURL]) {
		return [[self stringWithContentsOfFile:[url path] usedEncoding:encoding error:errorPtr] autorelease];
	}
	data = [NSURLConnection sendSynchronousRequest:[NSURLRequest requestWithURL:url]
				     returningResponse:NULL
						 error:errorPtr];
	if (data == nil) {
		return nil;
	}
	/* ONE ENCODING IS STORED, so that is what was used — the same sentence the file door above makes. */
	result = [[[self alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
	if (result != nil && encoding != NULL) {
		*encoding = NSUTF8StringEncoding;
	}
	return result;
}

/* ===================================================================================================
 * THE FILE/URL CONTENTS DOORS, AND THE DEPRECATED READ/WRITE NAMES (§63.29's neighbours).
 *
 * THE ENCODING-TAKING INSTANCE DOORS MIRROR THE CLASS DOORS ABOVE and keep ONE implementation of "read
 * the bytes, then decide what they mean": a file through NSData, a URL split on -isFileURL with the
 * transport door for any other scheme — the same division +stringWithContentsOfURL:…: makes.
 *
 * THE DEPRECATED NO-ENCODING NAMES (read, and write) take the one honest default C-string encoding here,
 * -defaultCStringEncoding (the storage, §63.30), exactly as -cString and friends do, and they DELEGATE
 * rather than keeping a second copy of the rule. They are +1 for the -init… forms (an init consumes and
 * returns +1) and +0 for the +stringWith… factories, which is Apple's own ownership for each.
 *
 * ⚠ -writeToURL:atomically:encoding:error: ENCODES THROUGH -dataUsingEncoding: EXACTLY AS ITS FILE
 * SIBLING DOES, so the same two refusals (an unstored encoding, and a string that will not convert)
 * answer NO with the same NSCocoaErrorDomain code (517) rather than a second, drifting rule.
 * =================================================================================================== */
- (id)initWithContentsOfFile:(NSString *)path
		    encoding:(NSStringEncoding)encoding
		       error:(NSError **)errorPtr
{
	NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingDefault error:errorPtr];

	if (data == nil) {
		return nil;
	}
	return [self initWithData:data encoding:encoding];
}

- (id)initWithContentsOfFile:(NSString *)path
		usedEncoding:(NSStringEncoding *)encoding
		       error:(NSError **)errorPtr
{
	NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingDefault error:errorPtr];

	if (data == nil) {
		return nil;
	}
	/* One encoding is stored, so that is what was used — the same sentence the class door makes. */
	if (encoding != NULL) {
		*encoding = NSUTF8StringEncoding;
	}
	return [self initWithData:data encoding:NSUTF8StringEncoding];
}

- (id)initWithContentsOfURL:(NSURL *)url
		   encoding:(NSStringEncoding)encoding
		      error:(NSError **)errorPtr
{
	NSData *data;

	if (url == nil) {
		return nil;
	}
	if ([url isFileURL]) {
		return [self initWithContentsOfFile:[url path] encoding:encoding error:errorPtr];
	}
	data = [NSURLConnection sendSynchronousRequest:[NSURLRequest requestWithURL:url]
				     returningResponse:NULL
					     error:errorPtr];
	if (data == nil) {
		return nil;
	}
	return [self initWithData:data encoding:encoding];
}

- (id)initWithContentsOfURL:(NSURL *)url
	       usedEncoding:(NSStringEncoding *)encoding
		      error:(NSError **)errorPtr
{
	NSData *data;

	if (url == nil) {
		return nil;
	}
	if ([url isFileURL]) {
		return [self initWithContentsOfFile:[url path] usedEncoding:encoding error:errorPtr];
	}
	data = [NSURLConnection sendSynchronousRequest:[NSURLRequest requestWithURL:url]
				     returningResponse:NULL
					     error:errorPtr];
	if (data == nil) {
		return nil;
	}
	if (encoding != NULL) {
		*encoding = NSUTF8StringEncoding;
	}
	return [self initWithData:data encoding:NSUTF8StringEncoding];
}

- (id)initWithContentsOfFile:(NSString *)path
{
	return [self initWithContentsOfFile:path
				   encoding:[NSString defaultCStringEncoding]
				      error:NULL];
}

- (id)initWithContentsOfURL:(NSURL *)url
{
	return [self initWithContentsOfURL:url
				  encoding:[NSString defaultCStringEncoding]
				     error:NULL];
}

+ (id)stringWithContentsOfFile:(NSString *)path
{
	return [[[self alloc] initWithContentsOfFile:path] autorelease];
}

+ (id)stringWithContentsOfURL:(NSURL *)url
{
	return [[[self alloc] initWithContentsOfURL:url] autorelease];
}

- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile
{
	return [self writeToFile:path
		      atomically:useAuxiliaryFile
			encoding:[NSString defaultCStringEncoding]
			   error:NULL];
}

- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)atomically
{
	return [self writeToURL:url
		     atomically:atomically
		       encoding:[NSString defaultCStringEncoding]
			  error:NULL];
}

- (BOOL)writeToURL:(NSURL *)url
	atomically:(BOOL)atomically
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
	return [encoded writeToURL:url
			   options:(atomically ? NSDataWritingAtomic : NSDataWritingDefault)
			     error:errorPtr];
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



/* ===================================================================================================
 * §63.49: THE DEPRECATED LINGUISTIC PAIR. Both are ONE WALK of the tagger this library already ships, and
 * the `sentenceRange` the enumerate block carries is the tagger's own `-sentenceRangeForRange:` — the same
 * answer its `-tagAtIndex:scheme:tokenRange:sentenceRange:` door gives, so the two spellings of "which
 * sentence is this token in" cannot disagree.
 *
 * ⚠ `orthography` IS ACCEPTED AND NOT USED, and the header says why at the declaration: this tagger has no
 * door that takes a caller's orthography (it determines one itself), so the difference is registered
 * (§11.6.1 D18) rather than hidden behind a parameter that looks honoured.
 * =================================================================================================== */
- (void)enumerateLinguisticTagsInRange:(NSRange)range
				scheme:(NSLinguisticTagScheme)tagScheme
			       options:(NSLinguisticTaggerOptions)opts
			   orthography:(nullable NSOrthography *)orthography
			   usingBlock:(void (^)(NSLinguisticTag _Nullable tag,
						NSRange tokenRange,
						NSRange sentenceRange,
						BOOL *stop))block
{
	NSLinguisticTagger *tagger;
	NSArray *schemes;

	(void)orthography;	/* D18: no door on the tagger takes one */
	if (block == NULL) {
		return;
	}
	schemes = [NSArray arrayWithObject:tagScheme];
	tagger = [[NSLinguisticTagger alloc] initWithTagSchemes:schemes options:opts];
	[tagger setString:self];
	[tagger enumerateTagsInRange:range
			      scheme:tagScheme
			     options:opts
			  usingBlock:^(NSLinguisticTag tag, NSRange tokenRange, BOOL *stop) {
		block(tag, tokenRange, [tagger sentenceRangeForRange:tokenRange], stop);
	}];
}

- (NSArray *)linguisticTagsInRange:(NSRange)range
			    scheme:(NSLinguisticTagScheme)tagScheme
			   options:(NSLinguisticTaggerOptions)opts
		       orthography:(nullable NSOrthography *)orthography
		      tokenRanges:(NSArray * _Nullable * _Nullable)tokenRanges
{
	NSMutableArray *tags = [[NSMutableArray alloc] init];
	NSMutableArray *ranges = [[NSMutableArray alloc] init];

	if (tokenRanges != NULL) {
		*tokenRanges = nil;
	}
	/* THE SAME WALK THE BLOCK FORM MAKES, so the two doors cannot answer differently — this one collects
	 * where the other calls, which is the whole difference between them. */
	[self enumerateLinguisticTagsInRange:range
				      scheme:tagScheme
				     options:opts
				 orthography:orthography
				  usingBlock:^(NSLinguisticTag tag, NSRange tokenRange,
					       NSRange sentenceRange, BOOL *stop) {
		(void)sentenceRange;
		(void)stop;
		if (tag != nil) {
			[tags addObject:tag];
		}
		[ranges addObject:[NSValue valueWithRange:tokenRange]];
	}];
	if (tokenRanges != NULL) {
		*tokenRanges = ranges;
	}
	return tags;
}

/* MOVED HERE FROM A CATEGORY (§62.75): this is a method the class's INTERFACE declares, so it belongs
 * in the class's own block - not because the compiler is happier, but because a category body
 * silently WINS over a class body at runtime, and a reader cannot tell which one ran. */
- (void)enumerateSubstringsInRange:(NSRange)range
			   options:(NSStringEnumerationOptions)opts
			usingBlock:(void (^)(NSString * _Nullable substring,
					     NSRange substringRange,
					     NSRange enclosingRange,
					     BOOL *stop))block
{
	FNTextUnit unit;
	FNTextUnit encloser;
	BOOL noSubstring = (opts & NSStringEnumerationSubstringNotRequired) != 0;
	BOOL reverse = (opts & NSStringEnumerationReverse) != 0;
	__block BOOL callerStopped = NO;

	if (block == nil) {
		return;
	}
	unit = fn_enumeration_pick_unit(opts);
	encloser = fn_enumeration_enclosing_unit(unit);

	/* ONE BODY FOR BOTH WALKS: the forward walk hands each unit straight to the caller, the reversed walk hands it
	 * to a buffer first, and the difference between the two is which order they are replayed in rather than what a
	 * caller is told. */
	void (^emit)(NSRange) = ^(NSRange unitRange) {
		BOOL stop = NO;
		NSString *substring = nil;
		NSRange enclosing = [FNTextBreaking fnUnitContaining:encloser inString:self
							     atIndex:unitRange.location];

		/* A UNIT IN NO LARGER UNIT IS ITS OWN ENCLOSING RANGE, which is what a BLANK LINE is: it is a line and
		 * it is in no paragraph. The engine answers NSNotFound for "nothing contains this", so the two rules
		 * meet without either guessing. */
		if (enclosing.location == NSNotFound) {
			enclosing = unitRange;
		}

		if (!noSubstring) {
			substring = [self substringWithRange:unitRange];
		}
		block(substring, unitRange, enclosing, &stop);
		if (stop) {
			callerStopped = YES;
		}
	};

	{
		NSArray *units = fn_enumerate_units(self, unit, range);
		NSUInteger i;

		if (!reverse) {
			for (i = 0; i < [units count]; i++) {
				NSRange unitRange = [[units objectAtIndex:i] rangeValue];

				emit(unitRange);
				if (callerStopped) {
					break;
				}
			}
		} else {
			/* A REVERSED WALK IS THE SAME LIST READ BACKWARDS: an ICU iterator is a forward cursor, so the
			 * units are decided first and replayed in whichever order the caller asked for. */
			i = [units count];
			while (i > 0) {
				NSRange unitRange = [[units objectAtIndex:i - 1] rangeValue];

				emit(unitRange);
				i--;
				if (callerStopped) {
					break;
				}
			}
		}
	}
}

/* ===================================================================================================
 * THE LINE DOORS, ON THE RULE ABOVE.
 *
 * `contentsEnd` IS WHERE THE TEXT STOPS AND `end` IS WHERE THE TERMINATOR STOPS, which is the whole
 * reason the three out-parameters exist rather than one: a caller building a range for the line wants
 * `end` (the terminator belongs to the line it ends), and a caller that wants the line's TEXT wants
 * `contentsEnd`. Both answers come from one walk.
 *
 * THE LINE IS THE ONE CONTAINING `range`, NOT THE ONE STARTING AT IT: a range that covers several
 * characters of one line answers that whole line, and that is why the end is found from the range's
 * LAST character rather than its first.
 * =================================================================================================== */
- (void)getLineStart:(NSUInteger *)startPtr
		 end:(NSUInteger *)lineEndPtr
	 contentsEnd:(NSUInteger *)contentsEndPtr
	    forRange:(NSRange)range
{
	NSUInteger length = [self length];
	NSUInteger start, contentsEnd, end, i;

	fn_line_check_range(self, range, _cmd);

	/* THE START IS FOUND BY WALKING BACK OVER THE TEXT AND NOT OVER A TERMINATOR RUN: the first
	 * terminator BEFORE `range.location` ends the previous line, and whether that terminator was one
	 * unit or a CRLF pair makes no difference here — either way this line starts just past it. */
	start = range.location;
	while (start > 0 && !fn_is_line_terminator([self characterAtIndex:start - 1])) {
		start--;
	}

	/* AN EMPTY RANGE ASKS ABOUT THE LINE THAT BEGINS AT ITS LOCATION, so the walk starts there; a
	 * non-empty one asks about the line its LAST character is in. */
	i = (range.length == 0) ? range.location : NSMaxRange(range) - 1;
	while (i < length && !fn_is_line_terminator([self characterAtIndex:i])) {
		i++;
	}
	contentsEnd = i;
	end = i + fn_line_terminator_length(self, i);

	if (startPtr != NULL) {
		*startPtr = start;
	}
	if (contentsEndPtr != NULL) {
		*contentsEndPtr = contentsEnd;
	}
	if (lineEndPtr != NULL) {
		*lineEndPtr = end;
	}
}

- (NSRange)lineRangeForRange:(NSRange)range
{
	NSUInteger start = 0, end = 0, contentsEnd = 0;

	[self getLineStart:&start end:&end contentsEnd:&contentsEnd forRange:range];
	return NSMakeRange(start, end - start);
}

/* ===================================================================================================
 * THE PARAGRAPH PAIR (§63.46). THE RULE IS THE ENGINE'S, NOT A SECOND COPY OF IT: the extent comes from
 * `FNTextBreaking`'s paragraph walk — the SAME walk `-enumerateSubstringsInRange:options:` answers
 * `NSStringEnumerationByParagraphs` with — so the door and the enumeration cannot disagree, which is exactly
 * what Apple's header claims for them ("Equivalent to paragraphRangeForRange:").
 *
 * AND THE RULE ITSELF IS APPLE'S THREE CHARACTERS — CR, LF, PS — which the engine was ALIGNED to in the same
 * unit (it used to read a paragraph as a run of lines with no blank line between them, and SKIP blank lines).
 * The consequences a caller can see, both asserted by the probe: a blank line is now an EMPTY PARAGRAPH, and
 * NEL (U+0085) / LINE SEPARATOR (U+2028) end a LINE without ending a paragraph.
 * =================================================================================================== */
- (NSRange)paragraphRangeForRange:(NSRange)range
{
	fn_line_check_range(self, range, _cmd);
	return [FNTextBreaking fnUnitContaining:FNTextUnitParagraph inString:self atIndex:range.location];
}

- (void)getParagraphStart:(nullable NSUInteger *)startPtr
		      end:(nullable NSUInteger *)paragraphEndPtr
	      contentsEnd:(nullable NSUInteger *)contentsEndPtr
		 forRange:(NSRange)range
{
	NSRange paragraph = [self paragraphRangeForRange:range];
	NSUInteger contentsEnd = NSMaxRange(paragraph);

	/* `end` IS WHERE THE TERMINATOR STOPS — a paragraph owns the break it ends with — and `contentsEnd` is
	 * where its TEXT stops, so the one terminator at the end is stripped here. CRLF IS ONE TERMINATOR and is
	 * stripped as one, which is the same rule the line door applies through `fn_line_terminator_length`. */
	if (contentsEnd > paragraph.location) {
		unichar last = [self characterAtIndex:contentsEnd - 1];

		if (last == 0x000a && (contentsEnd - 2) >= paragraph.location &&
		    [self characterAtIndex:contentsEnd - 2] == 0x000d) {
			contentsEnd -= 2;
		} else if (last == 0x000a || last == 0x000d || last == 0x2029) {
			contentsEnd -= 1;
		}
	}
	if (startPtr != NULL) {
		*startPtr = paragraph.location;
	}
	if (contentsEndPtr != NULL) {
		*contentsEndPtr = contentsEnd;
	}
	if (paragraphEndPtr != NULL) {
		*paragraphEndPtr = NSMaxRange(paragraph);
	}
}

/* THE ENUMERATOR HANDS BACK THE TEXT WITHOUT ITS TERMINATOR — Apple's parameter page says so in as
 * many words, "the line contains just the contents of the line, without the line terminators" — so
 * each element is `contentsEnd - start` long even though the line it walked spans to `end`.
 *
 * A TRAILING TERMINATOR DOES NOT MAKE A TRAILING EMPTY LINE. "one\n" is ONE line, not two: the walk
 * stops when it reaches the end, so the empty text after the last terminator is never handed over.
 * A BLANK LINE IN THE MIDDLE IS A LINE, though, and is handed over as an empty string — dropping it
 * would change a caller's line count, which is half of what the door is for. */
- (void)enumerateLinesUsingBlock:(void (^)(NSString *line, BOOL *stop))block
{
	NSUInteger length = [self length];
	NSUInteger at = 0;

	if (block == nil) {
		return;
	}
	while (at < length) {
		NSUInteger start = 0, end = 0, contentsEnd = 0;
		BOOL stop = NO;

		[self getLineStart:&start end:&end contentsEnd:&contentsEnd forRange:NSMakeRange(at, 0)];
		block([self substringWithRange:NSMakeRange(start, contentsEnd - start)], &stop);
		if (stop) {
			return;
		}
		if (end <= at) {
			break;      /* no forward progress is possible; stopping beats spinning */
		}
		at = end;
	}
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
	/* ⚠ §63.71: THE OVERRIDE NEEDS THE ENGINE TOO. This class answers the storage encodings
	 * in O(1) — that is why it overrides at all — but a CONVERTED encoding has no stored count, so it
	 * must go through the same converter `-dataUsingEncoding:` uses, or the two doors would disagree
	 * about the same string. */
	if (!fn_is_storage_encoding(encoding)) {
		return fn_length_in_encoding(self, encoding);
	}
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
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

@end

@implementation NSMutableString

/* §C.3 item 4, and the same shape NSDecimalNumber has in the number family: the front's answer is
 * NSString, so a PUBLIC subclass must override it or its own name would never reach an archive. */
- (Class)classForCoder
{
	return [NSMutableString class];
}

/* THE RECEIVER'S KIND IS KEPT, AND THIS IS WHERE IT WAS NOT. NSString's abstract -initWithUTF8String: (and
 * its -...Characters:length: sibling) SUBSTITUTES an NSOwnedString - "a SUBCLASS overrides this and never
 * reaches here", says the comment there - and NSMutableString did not, so
 * `[[NSMutableString alloc] initWithUTF8String:@""]` answered an IMMUTABLE string and its first mutator went
 * through the root class and DIED. That is the whole formatter path: -initWithFormat:arguments: builds
 * through an NSMutableString and appends to it, so EVERY -stringWithFormat: with a %@ in it aborted the
 * guest. The override is two lines because NSOwnedString's storage init respects `self` (it begins
 * `self = [super init]`, and NSString's -init returns self precisely so that it can) - so delegating to the
 * SUPERCLASS there builds the units on the mutable instance itself. */
- (id)initWithUTF8String:(const char *)utf8
{
	return [super initWithUTF8String:utf8];
}

- (id)initWithCharacters:(const unichar *)characters length:(NSUInteger)length
{
	return [super initWithCharacters:characters length:length];
}

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
	unichar *buffer;
	NSUInteger length;
	id result;

	buffer = fn_string_character_copy(other, &length);
	result = [self initWithCharacters:buffer length:length];
	free(buffer);
	return result;
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
	/* The characters are the compiler's, not ours: free nothing of OURS - but the instance itself is still
	 * NSObject's to free, and a -dealloc that returns without saying so leaks the whole allocation. */
	[super dealloc];
}


/*
 * THE C ACCESSORS (W2a): the runtime's names, and a range's spelling.
 *
 * They live here rather than in a C file because each one BUILDS a string, and this is
 * where the string class is implemented; the runtime calls go through objc/runtime.h.
 * `NSSelectorFromString` on a name nothing registered ANSWERS a selector (the runtime
 * registers it), which is Apple's behaviour and worth asserting rather than assuming.
 */
NSString *NSStringFromClass(Class aClass)
{
	const char *name;

	if (aClass == Nil) {
		return nil;		/* Apple: nil in, nil out */
	}
	name = class_getName(aClass);
	return (name != NULL) ? [[NSOwnedString alloc] initWithUTF8String:name] : nil;
}

Class NSClassFromString(NSString *aClassName)
{
	if (aClassName == nil) {
		return Nil;
	}
	return objc_getClass([aClassName UTF8String]);
}

NSString *NSStringFromSelector(SEL aSelector)
{
	const char *name = sel_getName(aSelector);

	/*
	 * THE NONNULL CONTRACT IS APPLE'S, AND THIS USED TO ANSWER NIL (D4's sibling, found by the
	 * zero-warning rule the moment NSData's pair became nonnull — see §11.6.1). A nil from a
	 * function declared nonnull is a difference a CONSUMER sees: `NSString *n =
	 * NSStringFromSelector(sel);` warns against this header, and Apple's own header does not
	 * declare it nullable. Apple leaves the unnameable-selector case UNDEFINED, so the choice
	 * here is in permitted space, and it is the LOUD one this library uses wherever a value
	 * would otherwise lie: name the problem rather than return something that means nothing.
	 * The fallback assignment keeps the call below well-typed, because -raise: does not return.
	 */
	if (name == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSStringFromSelector: the runtime cannot name the selector %p",
				   (const void *)aSelector];
		name = "";
	}
	return [[NSOwnedString alloc] initWithUTF8String:name];
}

SEL NSSelectorFromString(NSString *aSelectorName)
{
	if (aSelectorName == nil) {
		/* APPLE'S DOCUMENTED BEHAVIOUR, AND APPLE'S HEADER SAYS NONNULL (D8, §11.6.1). The
		 * declaration follows Apple's header because the API surface is the specification; the
		 * writer follows Apple's documentation because that is the behaviour a program depends
		 * on; and the build keeps its zero-warning rule here, which is a house rule about THIS
		 * build and not a claim about the API. Apple's own build must do the same thing. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
		return (SEL)0;
#pragma clang diagnostic pop
	}
	return sel_registerName([aSelectorName UTF8String]);
}

/* APPLE'S SPELLING EXACTLY: "{location, length}" — a space after the comma and none
 * before it. */
NSString *NSStringFromRange(NSRange range)
{
	char buffer[64];

	snprintf(buffer, sizeof buffer, "{%lu, %lu}", (unsigned long)range.location,
		 (unsigned long)range.length);
	return [[NSOwnedString alloc] initWithUTF8String:buffer];
}
@end

/* ===================================================================================================
 * THE ENUMERATION (§62.48)
 *
 * The units come from `FNTextBreaking`, which is the library's one answer to where a word, a line, a sentence, a
 * paragraph and a composed character begin and end; nothing here second-guesses it. `FNTextBreaking.h` names this
 * method as one of its two callers, and this is that caller arriving.
 * =================================================================================================== */

/* WHICH UNIT THE OPTIONS ASK FOR, AND THEY MAY ASK FOR EXACTLY ONE. Zero names no unit at all and two name two
 * answers: `ByWords | BySentences` is a contradiction rather than a default, so it raises instead of guessing. */
static FNTextUnit fn_enumeration_pick_unit(NSStringEnumerationOptions opts)
{
	FNTextUnit unit = FNTextUnitWord;
	int named = 0;

	if (opts & NSStringEnumerationLocalized) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-enumerateSubstringsInRange:options:usingBlock: cannot break for a locale: this "
				   @"library breaks with the root locale's rules, and a locale-directed walk is not "
				   @"something it can quietly approximate"];
	}
	if (opts & NSStringEnumerationByCaretPositions) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-enumerateSubstringsInRange:options:usingBlock: cannot enumerate by caret "
				   @"positions: a caret is a text-input layout's unit, and this library has no layout"];
	}
	if (opts & NSStringEnumerationByDeletionClusters) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-enumerateSubstringsInRange:options:usingBlock: cannot enumerate by deletion "
				   @"clusters: a cluster is a text-input layout's unit, and this library has no layout"];
	}
	if (opts & NSStringEnumerationByLines) { unit = FNTextUnitLine; named++; }
	if (opts & NSStringEnumerationByParagraphs) { unit = FNTextUnitParagraph; named++; }
	if (opts & NSStringEnumerationByComposedCharacterSequences) { unit = FNTextUnitComposedCharacter; named++; }
	if (opts & NSStringEnumerationByWords) { unit = FNTextUnitWord; named++; }
	if (opts & NSStringEnumerationBySentences) { unit = FNTextUnitSentence; named++; }

	if (named != 1) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-enumerateSubstringsInRange:options:usingBlock: needs exactly one unit option, and "
				   @"%d were given", named];
	}
	return unit;
}

/* THE NEXT LARGER UNIT THIS ENGINE CAN NAME. A paragraph is the largest, so its own; a line and a sentence are
 * enclosed by their paragraph, a word by its sentence, and a composed character by its word. */
static FNTextUnit fn_enumeration_enclosing_unit(FNTextUnit unit)
{
	switch (unit) {
	case FNTextUnitLine:
	case FNTextUnitSentence:
		return FNTextUnitParagraph;
	case FNTextUnitWord:
		return FNTextUnitSentence;
	case FNTextUnitComposedCharacter:
		return FNTextUnitWord;
	case FNTextUnitParagraph:
	default:
		return FNTextUnitParagraph;
	}
}

/* --- WHAT A CALLER MEANT BY "WORDS" ------------------------------------------------ */

/* THE TWO PREDICATES ARE SMALL AND STATED RATHER THAN BORROWED. ASCII letters and digits count, and ANY code unit
 * above ASCII is taken as a letter rather than as punctuation: a string of non-Latin words is enumerated correctly
 * by that rule, and the cost is that an isolated typographic quotation mark is treated as one. The approximation
 * is named here because the alternative — a table of every script's letters — is not what this method is. */
static BOOL fn_enumerate_is_alnum(unichar c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c > 127;
}

static BOOL fn_enumerate_is_space(unichar c)
{
	return c == 0x0020 || c == 0x0009 || c == 0x000a || c == 0x000d || c == 0x000b || c == 0x000c;
}

static BOOL fn_enumerate_has_alnum(NSString *string, NSRange range)
{
	NSUInteger i;

	for (i = range.location; i < NSMaxRange(range); i++) {
		if (fn_enumerate_is_alnum([string characterAtIndex:i])) {
			return YES;
		}
	}
	return NO;
}

/* A UNIT IS ITS TEXT AND NOT THE WHITESPACE AROUND IT: Apple's options name words and sentences, and ICU's
 * boundaries sit after the space that follows one. */
static NSRange fn_enumerate_trim(NSString *string, NSRange range)
{
	NSUInteger start = range.location;
	NSUInteger end = NSMaxRange(range);

	while (start < end && fn_enumerate_is_space([string characterAtIndex:start])) {
		start++;
	}
	while (end > start && fn_enumerate_is_space([string characterAtIndex:end - 1])) {
		end--;
	}
	return NSMakeRange(start, end - start);
}

/* THE UNITS TO EMIT, IN ORDER: the engine's units with this method's policy applied. IT IS A LIST FOR BOTH WALKS —
 * forward and reversed — because the policy needs a unit's neighbours (interior punctuation joins its word) and a
 * walk that had to look ahead while emitting could not honour `stop` without having already decided. */
static NSArray *fn_enumerate_units(NSString *string, FNTextUnit unit, NSRange range)
{
	NSMutableArray *raw = [NSMutableArray array];
	NSMutableArray *out = [NSMutableArray array];
	NSUInteger i;

	[FNTextBreaking fnEnumerate:unit inString:string range:range usingBlock:^(NSRange unitRange, BOOL *stop) {
		(void)stop;
		[raw addObject:[NSValue valueWithRange:unitRange]];
	}];

	if (unit == FNTextUnitWord) {
		i = 0;
		while (i < [raw count]) {
			NSRange current = fn_enumerate_trim(string, [[raw objectAtIndex:i] rangeValue]);

			if (current.length > 0) {
				/* INTERIOR PUNCTUATION BELONGS TO ITS WORD: ICU answers "don't" as three units, and a
				 * punctuation unit that TOUCHES a word on both sides is part of it. */
				while (i + 2 < [raw count]) {
					NSRange next = fn_enumerate_trim(string, [[raw objectAtIndex:i + 1] rangeValue]);
					NSRange after = fn_enumerate_trim(string, [[raw objectAtIndex:i + 2] rangeValue]);

					if (next.length > 0 && !fn_enumerate_has_alnum(string, next) &&
					    fn_enumerate_has_alnum(string, current) &&
					    fn_enumerate_has_alnum(string, after) &&
					    NSMaxRange(current) == next.location && NSMaxRange(next) == after.location) {
						current.length = NSMaxRange(after) - current.location;
						i += 2;
						continue;
					}
					break;
				}
				if (fn_enumerate_has_alnum(string, current)) {
					[out addObject:[NSValue valueWithRange:current]];
				}
			}
			i++;
		}
		return out;
	}
	/* A BLANK LINE IS A LINE AND IS KEPT: dropping it would change a caller's LINE COUNT, and counting lines is
	 * half of what the option is for. Only a SENTENCE is trimmed here, and only a trimmed-to-nothing unit goes. */
	for (i = 0; i < [raw count]; i++) {
		NSRange unitRange = (unit == FNTextUnitSentence)
			? fn_enumerate_trim(string, [[raw objectAtIndex:i] rangeValue])
			: [[raw objectAtIndex:i] rangeValue];

		if (unit == FNTextUnitSentence && unitRange.length == 0) {
			continue;
		}
		[out addObject:[NSValue valueWithRange:unitRange]];
	}
	return out;
}

/* THE ENUMERATION CATEGORY THAT WAS HERE IS GONE: its one method is in the class's own
 * implementation block above. A category implementing a method the INTERFACE declares is what
 * -Wobjc-protocol-method-implementation reports, and what it costs is real: the compiler cannot
 * tell which of the two bodies wins, because at runtime the category does.
 *
 * AND THE KEYWORD IS NOT SPELLED IN THIS COMMENT ON PURPOSE: the sweep attributes a file's
 * blocks by the LAST occurrence of the block keyword, so a comment that spells it steals the
 * whole file's method list and every method then reads as declared-and-never-defined. It cost a
 * gate run in §62.75. */

