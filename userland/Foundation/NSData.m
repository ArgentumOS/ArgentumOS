/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSData.m — the byte buffer.
 *
 * MANUAL OWNERSHIP. -copy of a MUTABLE data is a snapshot, as with the strings; -copy of
 * an immutable one is itself.
 */

#import <Foundation/NSData.h>
#import <Foundation/NSCoder.h>	/* the coder forms take NSCoder */
#import <Foundation/NSString.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>	/* the OOM path raises (D4, §11.6.1) */
#import <Foundation/NSURL.h>
#import <Foundation/NSDictionary.h>	/* the userInfo dictionaries below need the CLASS, not a @class */
#import "NSData.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===================================================================================================
 * THE PRIVATE CONCRETE CLASSES (plan §C.3, M6): the same shape as the other families. AGDataEmpty is the
 * SHARED empty instance, AGDataItems the general case (the front's own ivars are its storage), and
 * AGDataMutable the mutable one.
 *
 * NOTE WHAT IS DELIBERATELY MISSING: AGDataItems does NOT override -init. An -init that answered the shared
 * empty instance would capture every ALLOCATE-THEN-FILL path into the singleton itself - the failure the
 * dictionary family measured (three cases broke) - so here, as there, the singleton belongs to the COMPLETE
 * constructions: -initWithBytes:length: with a zero length. `[[NSData alloc] init]` answers a plain EMPTY
 * instance, which is what §C.3 item 1 asks for.
 * =================================================================================================== */
@interface AGDataEmpty : NSData
+ (AGDataEmpty *)emptyData;
@end

@interface AGDataItems : NSData
@end

@interface AGDataMutable : NSMutableData
@end

@implementation NSData

/* THE DOOR (§C.3 item 1), routed exactly once at the front. */
+ (id)alloc
{
	if (self != [NSData class]) {
		return [super alloc];
	}
	return [AGDataItems alloc];
}

/* §C.3 item 4: an archiver asks for THIS, never for -class. NSMutableData answers ITSELF below, being a
 * public subclass - the shape NSDecimalNumber, NSMutableString and NSMutableOrderedSet all need. */
- (Class)classForCoder
{
	return [NSData class];
}

/* THE COMPRESSION DOORS (F12). The codec itself lives in NSDataCodec.m — the binding to the zlib this
 * system already ships — and these two are the API's shape over it: nil plus an error on a
 * refusal, exactly as the header says. */
- (nullable NSData *)compressedDataUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
					    error:(NSError * _Nullable * _Nullable)errorPtr
{
	return fn_compressed_data(self, algorithm, errorPtr);
}

- (nullable NSData *)decompressedDataUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
					      error:(NSError * _Nullable * _Nullable)errorPtr
{
	return fn_decompressed_data(self, algorithm, errorPtr);
}

+ (NSData *)dataWithBytes:(const void *)bytes length:(size_t)length
{
	return [[self alloc] initWithBytes:bytes length:length];
}

- (id)initWithBytes:(const void *)bytes length:(size_t)length
{
	/* THE CLASS IS CHOSEN BY THE DATA (§C.3 item 2), for this family's general class only: a mutable
	 * receiver inherits this implementation and must keep it. A ZERO-LENGTH construction is a COMPLETE
	 * answer, which is what makes handing back the shared instance safe. */
	if ([self isMemberOfClass:[AGDataItems class]] && length == 0) {
		[self release];	/* never initialized: the storage was never built */
		return (id)[AGDataEmpty emptyData];
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_length = length;
	_bytes = NULL;
	if (length > 0) {
		_bytes = (unsigned char *)malloc(length);
		if (_bytes == NULL) {
			/* THE WRITER CANNOT ANSWER NIL (D4 of §11.6.1, fixed 2026-09-19). Apple declares
			 * +dataWithBytes:length: NONNULL, and a nullable here is a difference a CONSUMER
			 * sees: code written against Apple's contract — `NSData *d = [NSData
			 * dataWithBytes:…];`, no check — warns against this header. The only nil path this
			 * method had was an unsatisfiable allocation, and that is what NSMallocException is
			 * for. The OTHER nullable constructors of this class are Apple's own (a missing
			 * file, a bad base64 string) and are untouched. */
			[NSException raise:NSMallocException
				    format:@"-initWithBytes:length: could not allocate %lu byte(s)",
					   (unsigned long)length];
			return nil;	/* -raise: does not return */
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

+ (NSData *)data
{
	return [[self alloc] initWithBytes:NULL length:0];
}

+ (NSData *)dataWithBytesNoCopy:(void *)bytes length:(size_t)length
{
	return [[self alloc] initWithBytesNoCopy:bytes length:length];
}

+ (NSData *)dataWithBytesNoCopy:(void *)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone
{
	return [[self alloc] initWithBytesNoCopy:bytes length:length freeWhenDone:freeWhenDone];
}

+ (NSData *)dataWithData:(NSData *)other
{
	return [[self alloc] initWithData:other];
}

+ (NSData *)dataWithContentsOfFile:(NSString *)path
{
	return [[self alloc] initWithContentsOfFile:path];
}

+ (NSData *)dataWithBase64EncodedString:(NSString *)string
{
	return [[self alloc] initWithBase64EncodedString:string
						options:NSDataBase64DecodingDefault];
}

- (id)initWithData:(NSData *)other
{
	return [self initWithBytes:[other bytes] length:[other length]];
}

/*
 * NO-COPY IS HONOURED BY TAKING THE BYTES, not by adopting the pointer: v1 has no
 * flag to remember whether a buffer is ours to free, and the OBSERVABLE contract
 * is what matters — the caller hands the bytes over, and with freeWhenDone: the
 * buffer is consumed (freed) here. One copy, documented, rather than an ivar and a
 * lifetime rule to get wrong.
 */
- (id)initWithBytesNoCopy:(void *)bytes length:(size_t)length
{
	return [self initWithBytesNoCopy:bytes length:length freeWhenDone:NO];
}

- (id)initWithBytesNoCopy:(void *)bytes length:(size_t)length freeWhenDone:(BOOL)freeWhenDone
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	self = [self initWithBytes:bytes length:length];
	if (freeWhenDone && bytes != NULL) {
		free(bytes);
	}
	return self;
}

- (id)initWithContentsOfFile:(NSString *)path
{
	FILE *file = fopen([path UTF8String], "rb");
	long size;
	unsigned char *buffer;
	id result;

	if (file == NULL) {
		return nil;
	}
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return nil;
	}
	size = ftell(file);
	if (size < 0) {
		fclose(file);
		return nil;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return nil;
	}
	buffer = (unsigned char *)malloc((size_t)size + 1);
	if (buffer == NULL) {
		fclose(file);
		return nil;
	}
	{
		size_t got = fread(buffer, 1, (size_t)size, file);

		fclose(file);
		result = [self initWithBytes:buffer length:got];
	}
	free(buffer);
	return result;
}

- (void)getBytes:(void *)buffer length:(size_t)length
{
	[self getBytes:buffer range:NSMakeRange(0, length)];
}

- (void)getBytes:(void *)buffer range:(NSRange)range
{
	size_t n = range.length;

	if (buffer == NULL) {
		return;
	}
	if (range.location > [self length]) {
		return;
	}
	if (n > [self length] - range.location) {
		n = [self length] - range.location;
	}
	if (n > 0) {
		memcpy(buffer, [self bytes] + range.location, n);
	}
}

- (NSData *)subdataWithRange:(NSRange)range
{
	size_t start = range.location;
	size_t n = range.length;

	if (start > [self length]) {
		start = [self length];
	}
	if (n > [self length] - start) {
		n = [self length] - start;
	}
	return [[NSData alloc] initWithBytes:[self bytes] + start length:n];
}

- (NSRange)rangeOfData:(NSData *)other
	       options:(NSDataSearchOptions)options
		 range:(NSRange)range
{
	size_t haystack = [self length];
	size_t needle = [other length];
	size_t start = range.location;
	size_t end;
	size_t i;

	(void)options;		/* only the plain forward search is implemented */
	if (needle == 0) {
		return NSMakeRange(start, 0);
	}
	if (start > haystack) {
		return NSMakeRange(NSNotFound, 0);
	}
	end = start + range.length;
	if (end > haystack) {
		end = haystack;
	}
	for (i = start; i + needle <= end; i++) {
		if (memcmp([self bytes] + i, [other bytes], needle) == 0) {
			return NSMakeRange(i, needle);
		}
	}
	return NSMakeRange(NSNotFound, 0);
}

static const char base64Alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

- (NSString *)base64EncodedStringWithOptions:(NSDataBase64EncodingOptions)options
{
	const unsigned char *bytes = (const unsigned char *)[self bytes];
	size_t outSize = (([self length] + 2) / 3) * 4;
	char *out;
	size_t i;
	size_t o = 0;
	NSMutableString *wrapped = nil;
	NSString *result;

	out = (char *)malloc(outSize + 1);
	if (out == NULL) {
		return [[NSOwnedString alloc] initWithUTF8String:""];
	}
	for (i = 0; i + 2 < [self length]; i += 3) {
		unsigned long triple = ((unsigned long)bytes[i] << 16) |
				       ((unsigned long)bytes[i + 1] << 8) |
				       bytes[i + 2];

		out[o++] = base64Alphabet[(triple >> 18) & 0x3F];
		out[o++] = base64Alphabet[(triple >> 12) & 0x3F];
		out[o++] = base64Alphabet[(triple >> 6) & 0x3F];
		out[o++] = base64Alphabet[triple & 0x3F];
	}
	if (i < [self length]) {
		unsigned long triple = (unsigned long)bytes[i] << 16;
		int remaining = (int)([self length] - i);

		if (remaining == 2) {
			triple |= (unsigned long)bytes[i + 1] << 8;
		}
		out[o++] = base64Alphabet[(triple >> 18) & 0x3F];
		out[o++] = base64Alphabet[(triple >> 12) & 0x3F];
		out[o++] = (remaining == 2) ? base64Alphabet[(triple >> 6) & 0x3F] : '=';
		out[o++] = '=';
	}
	out[o] = '\0';

	if (options & NSDataBase64Encoding64CharacterLineLength) {
		size_t k;

		wrapped = [[NSMutableString alloc] initWithUTF8String:""];
		for (k = 0; k < o; k++) {
			if (k > 0 && k % 64 == 0) {
				[wrapped appendUTF8String:(options & NSDataBase64EncodingEndLineWithLineFeed)
					? "\n" : "\r\n"];
			}
			{
				char one[2];

				one[0] = out[k];
				one[1] = '\0';
				[wrapped appendUTF8String:one];
			}
		}
		free(out);
		return wrapped;
	}
	result = [[NSOwnedString alloc] initWithUTF8String:out];
	free(out);
	return result;
}

static int base64Value(unsigned char c)
{
	if (c >= 'A' && c <= 'Z') { return c - 'A'; }
	if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
	if (c >= '0' && c <= '9') { return c - '0' + 52; }
	if (c == '+') { return 62; }
	if (c == '/') { return 63; }
	return -1;
}

- (id)initWithBase64EncodedString:(NSString *)string options:(NSDataBase64DecodingOptions)options
{
	size_t n = [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	unsigned char *out = (unsigned char *)malloc(n + 1);
	size_t o = 0;
	size_t i;
	int carry = 0;
	int bits = 0;
	id result;

	if (out == NULL) {
		return nil;
	}
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)[string byteAtIndex:i];
		int value = base64Value(c);

		if (value < 0) {
			if (c == '=') {
				break;
			}
			if (options & NSDataBase64DecodingIgnoreUnknownCharacters) {
				continue;
			}
			free(out);
			return nil;
		}
		carry = (carry << 6) | value;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out[o++] = (unsigned char)((carry >> bits) & 0xFF);
		}
	}
	result = [self initWithBytes:out length:o];
	free(out);
	return result;
}

- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile
{
	const char *target = [path UTF8String];
	FILE *file;

	if (useAuxiliaryFile) {
		/* Write beside the target, then rename over it: FSH paths are ordinary
		 * files, and rename within a directory is the atomic step. */
		size_t length = strlen(target);
		char *staging = (char *)malloc(length + 9);

		if (staging == NULL) {
			return NO;
		}
		memcpy(staging, target, length);
		memcpy(staging + length, ".tmpdata", 9);
		file = fopen(staging, "wb");
		if (file == NULL) {
			free(staging);
			return NO;
		}
		if ([self length] > 0 && fwrite([self bytes], 1, [self length], file) != [self length]) {
			fclose(file);
			remove(staging);
			free(staging);
			return NO;
		}
		if (fclose(file) != 0) {
			remove(staging);
			free(staging);
			return NO;
		}
		if (rename(staging, target) != 0) {
			remove(staging);
			free(staging);
			return NO;
		}
		free(staging);
		return YES;
	}
	file = fopen(target, "wb");
	if (file == NULL) {
		return NO;
	}
	if ([self length] > 0 && fwrite([self bytes], 1, [self length], file) != [self length]) {
		fclose(file);
		return NO;
	}
	return fclose(file) == 0;
}

+ (NSData *)dataWithContentsOfFile:(NSString *)path options:(NSDataReadingOptions)options error:(NSError **)errorPtr
{
	return [[self alloc] initWithContentsOfFile:path options:options error:errorPtr];
}

+ (NSData *)dataWithBase64EncodedString:(NSString *)string options:(NSDataBase64DecodingOptions)options
{
	return [[self alloc] initWithBase64EncodedString:string options:options];
}

- (id)initWithContentsOfFile:(NSString *)path options:(NSDataReadingOptions)options error:(NSError **)errorPtr
{
	/*
	 * The OPTIONS ARE ACCEPTED AND IGNORED, and that is documented rather than
	 * silent: mapped-if-safe and uncached are hints about how the pages are held,
	 * and this implementation reads the file into its own buffer either way. The
	 * error out parameter is honoured, so a caller can tell why it failed.
	 */
	id result = [self initWithContentsOfFile:path];

	(void)options;
	if (result == nil && errorPtr != NULL) {
		*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain"
						code:260
					    userInfo:[NSDictionary dictionaryWithObject:
							@"The file could not be read."
								      forKey:NSLocalizedDescriptionKey]];
	}
	return result;
}

- (id)initWithBase64EncodedData:(NSData *)base64Data options:(NSDataBase64DecodingOptions)options
{
	NSString *asText = [[NSString alloc] initWithData:base64Data
						  encoding:NSASCIIStringEncoding];

	if (asText == nil) {
		return nil;
	}
	return [self initWithBase64EncodedString:asText options:options];
}

- (NSData *)base64EncodedDataWithOptions:(NSDataBase64EncodingOptions)options
{
	/* The encoded form is ASCII, so its bytes and its characters are the same. */
	return [[self base64EncodedStringWithOptions:options]
		dataUsingEncoding:NSASCIIStringEncoding];
}

- (BOOL)writeToFile:(NSString *)path options:(NSDataWritingOptions)options error:(NSError **)errorPtr
{
	BOOL ok = [self writeToFile:path atomically:(options & NSDataWritingAtomic) ? YES : NO];

	if (!ok && errorPtr != NULL) {
		*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain"
						code:513
					    userInfo:[NSDictionary dictionaryWithObject:
							@"The file could not be written."
								      forKey:NSLocalizedDescriptionKey]];
	}
	return ok;
}

- (void)enumerateByteRangesUsingBlock:(void (^)(const void *bytes, NSRange byteRange, BOOL *stop))block
{
	BOOL stop = NO;

	if (block == NULL) {
		return;
	}
	/*
	 * ONE RANGE COVERING EVERYTHING, which the contract allows: it says the
	 * ranges may be any decomposition of the data, and this reads into one
	 * contiguous buffer, so there is nothing to split. `stop` is honoured by
	 * simply not calling again.
	 */
	block([self bytes], NSMakeRange(0, [self length]), &stop);
}

- (BOOL)isEqualToData:(NSData *)other
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
	if ([self length] == 0) {
		return YES;
	}
	return memcmp([other bytes], [self bytes], [self length]) == 0;
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

	for (i = 0; i < [self length]; i++) {
		h ^= bytes[i];
		h *= 16777619UL;
	}
	return h;
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

/*
 * The zone forms (the public-API audit, B4). Declared ONCE, on the base class:
 * each delegates to -copy / -mutableCopy, and DYNAMIC DISPATCH picks the right
 * one for a mutable subclass (whose -copy is the snapshot).
 */

- (id)mutableCopy
{
	NSMutableData *copy = [[NSMutableData alloc] initWithCapacity:[self length]];

	[copy appendBytes:[self bytes] length:[self length]];
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

	buffer = (char *)malloc([self length] * 2 + 3);	/* "<" + 2/byte + ">" + NUL */
	if (buffer == NULL) {
		return [NSString stringWithUTF8String:"<data>"];
	}
	buffer[0] = '<';
	for (i = 0; i < [self length]; i++) {
		buffer[1 + i * 2] = hex[bytes[i] >> 4];
		buffer[2 + i * 2] = hex[bytes[i] & 0xF];
	}
	buffer[1 + [self length] * 2] = '>';
	buffer[2 + [self length] * 2] = '\0';
	result = [NSString stringWithUTF8String:buffer];
	free(buffer);
	return result;
}

- (void)dealloc
{
	free(_bytes);
	[super dealloc];	/* NSObject's -dealloc is what frees the instance */
}

/*
 * THE URL FORMS ARE THE FILE FORMS ONCE THE URL IS A PATH (D7's kind (D), 2026-09-19). A FILE url
 * maps through -path, which is the F8 rule for the FSH's paths; every OTHER scheme is REFUSED with
 * an NSError rather than silently answering nil, because this Foundation has no fetching machinery
 * at all — there is no NSURLSession anywhere in the library. That refusal is a DEVIATION and it is
 * REGISTERED as D9 of §11.6.1 with ground (ii), which is what the policy requires of a refusal
 * this library chooses rather than one it cannot avoid.
 */
static NSString *fn_path_for_url(NSURL *url, NSError **errorPtr)
{
	if (url == nil) {
		return nil;
	}
	if ([url isFileURL]) {
		return [url path];
	}
	if (errorPtr != NULL) {
		*errorPtr = [NSError errorWithDomain:@"NSCocoaErrorDomain"
						code:262	/* NSFileReadUnsupportedSchemeError */
					    userInfo:nil];
	}
	return nil;
}

+ (NSData *)dataWithContentsOfURL:(NSURL *)url
{
	return [[self alloc] initWithContentsOfURL:url];
}

+ (NSData *)dataWithContentsOfURL:(NSURL *)url options:(NSDataReadingOptions)options error:(NSError **)errorPtr
{
	return [[self alloc] initWithContentsOfURL:url options:options error:errorPtr];
}

- (id)initWithContentsOfURL:(NSURL *)url
{
	NSString *path = fn_path_for_url(url, NULL);

	return (path == nil) ? nil : [self initWithContentsOfFile:path];
}

- (id)initWithContentsOfURL:(NSURL *)url options:(NSDataReadingOptions)options error:(NSError **)errorPtr
{
	NSString *path = fn_path_for_url(url, errorPtr);

	return (path == nil) ? nil : [self initWithContentsOfFile:path options:options error:errorPtr];
}

- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)useAuxiliaryFile
{
	NSString *path = fn_path_for_url(url, NULL);

	return (path == nil) ? NO : [self writeToFile:path atomically:useAuxiliaryFile];
}

- (BOOL)writeToURL:(NSURL *)url options:(NSDataWritingOptions)options error:(NSError **)errorPtr
{
	NSString *path = fn_path_for_url(url, errorPtr);

	return (path == nil) ? NO : [self writeToFile:path options:options error:errorPtr];
}

/*
 * NSCoding FOR DATA (D7's kind (D)): the bytes go out as one blob, which is what the byte form of
 * the coder is for. The key is spelled by this library (internal to our archive format). An EMPTY
 * data encodes no blob at all, so the decode path hands -initWithBytes:NULL length:0 the empty
 * constructor the header documents.
 */
- (void)encodeWithCoder:(NSCoder *)coder
{
	if ([self length] > 0) {
		[coder encodeBytes:(const void *)[self bytes] length:[self length] forKey:@"NS.data"];
	}
}

/* APPLE'S ANSWER, WITH THE GAP NAMED WHERE IT LIVES: NSCoding.h records that this tree's archiver does not yet
 * ENFORCE secure coding, so the conformance (NSData.h, §62.23) says what it can guard and the coder's own debt is
 * that file's. */
+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	NSUInteger length = 0;
	const void *bytes = [coder decodeBytesForKey:@"NS.data" returnedLength:&length];

	return [self initWithBytes:bytes length:length];
}

@end

@implementation NSMutableData

+ (id)alloc
{
	if (self != [NSMutableData class]) {
		return [super alloc];
	}
	return [AGDataMutable alloc];
}

- (Class)classForCoder
{
	return [NSMutableData class];
}

/* IN PLACE, and ONLY on success: the receiver becomes the result, or it is left exactly as it
 * was. A half-replaced buffer would be worse than a refusal, and the header promises this. */
- (BOOL)compressUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
			 error:(NSError * _Nullable * _Nullable)errorPtr
{
	NSData *result = fn_compressed_data(self, algorithm, errorPtr);

	if (result == nil) {
		return NO;
	}
	[self setData:result];
	return YES;
}

- (BOOL)decompressUsingAlgorithm:(NSDataCompressionAlgorithm)algorithm
			   error:(NSError * _Nullable * _Nullable)errorPtr
{
	NSData *result = fn_decompressed_data(self, algorithm, errorPtr);

	if (result == nil) {
		return NO;
	}
	[self setData:result];
	return YES;
}

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

+ (NSMutableData *)dataWithLength:(size_t)length
{
	return [[self alloc] initWithLength:length];
}

- (id)initWithLength:(size_t)length
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (length > 0) {
		_bytes = (unsigned char *)calloc(length, 1);
		if (_bytes == NULL) {
			return nil;
		}
		_capacity = length;
	}
	_length = length;
	return self;
}

- (void)increaseLengthBy:(size_t)extraLength
{
	size_t wanted = _length + extraLength;

	[self setLength:wanted];
}

- (void)replaceBytesInRange:(NSRange)range withBytes:(const void *)bytes
{
	[self replaceBytesInRange:range withBytes:bytes length:range.length];
}

- (void)replaceBytesInRange:(NSRange)range
		  withBytes:(const void *)bytes
		     length:(size_t)replacementLength
{
	size_t start = range.location;
	size_t oldLength;
	size_t newTotal;
	unsigned char *fresh;

	if (start > _length) {
		/* Cocoa extends with zeros first when the range is past the end. */
		[self setLength:start];
	}
	if (start > _length) {
		return;
	}
	oldLength = range.length;
	if (oldLength > _length - start) {
		oldLength = _length - start;
	}
	newTotal = _length - oldLength + replacementLength;
	fresh = (unsigned char *)malloc(newTotal > 0 ? newTotal : 1);
	if (fresh == NULL) {
		return;
	}
	if (start > 0) {
		memcpy(fresh, _bytes, start);
	}
	if (replacementLength > 0 && bytes != NULL) {
		memcpy(fresh + start, bytes, replacementLength);
	}
	if (_length > start + oldLength) {
		memcpy(fresh + start + replacementLength, _bytes + start + oldLength,
		       _length - start - oldLength);
	}
	free(_bytes);
	_bytes = fresh;
	_length = newTotal;
	_capacity = newTotal;
}

- (void)setData:(NSData *)other
{
	[self replaceBytesInRange:NSMakeRange(0, _length)
			withBytes:[other bytes]
			   length:[other length]];
}

- (void)resetBytesInRange:(NSRange)range
{
	size_t start = range.location;
	size_t n = range.length;

	if (start > _length) {
		return;
	}
	if (n > _length - start) {
		n = _length - start;
	}
	if (n > 0 && _bytes != NULL) {
		memset(_bytes + start, 0, n);
	}
}

- (id)copy
{
	/* A snapshot, like every other mutable type here. */
	return [[NSData alloc] initWithBytes:[self bytes] length:[self length]];
}



@end


/* ===================================================================================================
 * THE CONCRETE CLASSES (§C.3 items 2, 3 and 8).
 * =================================================================================================== */

@implementation AGDataItems

/* NO -init OVERRIDE, ON PURPOSE - see the note at the top of this file. */

@end

@implementation AGDataEmpty

+ (AGDataEmpty *)emptyData
{
	static AGDataEmpty *shared = nil;

	if (shared == nil) {
		shared = [[AGDataEmpty alloc] init];
	}
	return shared;
}

/* IMMORTAL, the price of a singleton in a library with no `+allocWithZone:` and no collector. */
- (id)retain { return self; }
- (void)release { }
- (id)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }

/* ITS STORAGE IS ALREADY THE EMPTY ANSWER: the front's ivars are zeroed, so -length is 0 and -bytes is NULL
 * without a line of code here - which is exactly why this class needs no primitives of its own. */

@end

@implementation AGDataMutable

/* NOTHING TO IMPLEMENT: NSMutableData's implementation IS the mutable storage implementation, and what a
 * caller gains is the NAME that -class answers. */

@end
