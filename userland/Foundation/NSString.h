/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSString — the immutable string, with its concrete and constant subclasses.
 * docs/design/foundation-plan.md, F1 + the hard rule.
 *
 * UTF-16 IS THE STORAGE (the user's decision, 2026-09-18, later): "If Apple is
 * using UTF-16, so should we." NSOwnedString holds UTF-16 code units, the
 * runtime's constant strings already ARE UTF-16 (the compiler emits every
 * non-ASCII literal that way, and `_rlength` is a unit count), and `-UTF8String`
 * is now a CONVERSION with a materialised cache rather than the storage door.
 * docs/design/foundation-plan.md §13 is the design, its measured cost (340
 * `-UTF8String` sites), and its slices.
 *
 * THE CONTRACT IS STILL THE OLD ONE FOR ONE MORE SLICE — `-length` answers BYTES
 * and `-characterAtIndex:` indexes Unicode scalars — because flipping it moves
 * ~600 call sites and the storage had to land first. §13.6 slice 2 flips it to
 * UTF-16 code units, which is Apple's contract and where this ends up.
 *
 * NSString HAS NO INSTANCE VARIABLES, AND THAT IS NOT AN OVERSIGHT. The
 * compiler emits `@"..."` as an object whose fields sit at FIXED offsets from
 * the object pointer — measured, and pinned by our runtime's own Test/Test.h,
 * whose NSConstantString subclasses a BARE root class rather than a
 * storage-carrying one. A subclass therefore cannot inherit storage: its own
 * ivars would be pushed past the offsets the runtime fills, and every constant
 * string would read foreign words. That is exactly how F1's first two attempts
 * failed (a SEGV on a message send to a literal), and why the storage lives in
 * the concrete subclasses here, as it does in Cocoa:
 *
 *   NSString            the contract and the value semantics (no ivars)
 *   NSOwnedString       a string that owns a UTF-8 buffer
 *   NSMutableString     NSOwnedString plus mutation
 *   NSConstantString    the compiler's layout, filled in by the runtime
 *
 * EVERY METHOD IS BUILT ON THE ACCESSORS (-length, -byteAtIndex:,
 * -characterCount, -characterAtIndex:, -UTF8String), never on a field: an
 * NSConstantString's storage is the runtime's, so a method that read
 * NSOwnedString's offsets on one would read foreign memory.
 */

#ifndef FOUNDATION_NSSTRING_H
#define FOUNDATION_NSSTRING_H

#import <Foundation/NSObject.h>
#include <stddef.h>
#include <stdarg.h>		/* va_list, for -initWithFormat:arguments: */

/* The families that take or return other Foundation types: forward declarations
 * keep this header from dragging half the library in (and would deadlock the
 * imports if NSData or NSArray ever needed to come back here). */
@class NSData;
@class NSArray;
@class NSError;
@class NSCharacterSet;
@class NSURL;	/* §63.29: the two string-with-contents-of-URL doors take one */

/* NULLABILITY (F6, slice 2): NONNULL by default, and the exceptions are MEASURED —
 * every one below is a method NSString.m actually answers nil or NULL from (awk
 * over its `return nil;`/`return NULL;` sites), not a guess from the name:
 *   -stringWithUTF8String: and -initWithUTF8String: (a NULL or invalid argument),
 *   -initWithData:encoding: (data that is not valid in that encoding),
 *   +stringWithContentsOfFile:... (a file that did not read),
 *   -dataUsingEncoding: and -cStringUsingEncoding: (unconvertible), and
 *   -pathComponents (measured: an empty path answers nil here, where Cocoa
 *   answers an empty array).
 * The rest of the file RAISES rather than answering an absent thing. The locale
 * parameters are nullable because the header already says what a nil locale
 * means ("a nil locale means 'no locale-sensitive rules'"), and an NSError**
 * out-parameter is nullable at BOTH levels: the caller may pass NULL for
 * "no error report". */
#import <Foundation/NSException.h>	/* NSExceptionName, which two constants below are spelled with */
/* FOR `NSCoding`: ABOVE THE ASSUME-NONNULL REGION, which is where NSArray.h puts it too — a `#include` inside a
 * nullability region is refused by the compiler, because the imported header's declarations would be dragged
 * into somebody else's region. */
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

/* APPLE'S LEGACY CEILING FOR A STRING (§62.103), and its value is INT_MAX - 1 rather than INT_MAX because the
 * C representation behind it leaves room for a terminator. It was removed in macOS 10.5 and is in scope here
 * for the reason every deprecated name is: a program written against it is exactly who this library is for. */
#define NSMaximumStringLength (INT_MAX - 1)


/* Cocoa's option set. The first two are the ones a UTF-8 byte comparison honours; the rest are named so a
 * conforming call compiles and the ones this library cannot honour are REFUSED rather than silently ignored -
 * a search that quietly ignores NSRegularExpressionSearch answers a different question than the one asked.
 * Values ours (§11.6.1 D2), bit sets as their names say; NSCaseInsensitiveSearch keeps the value it always
 * had, so this changes no existing behaviour. */
typedef enum {
	NSLiteralSearch = 0,
	NSCaseInsensitiveSearch = 1 << 0,
	NSAnchoredSearch = 1 << 1,
	NSBackwardsSearch = 1 << 2,
	NSDiacriticInsensitiveSearch = 1 << 3,
	NSForcedOrderingSearch = 1 << 4,
	NSNumericSearch = 1 << 5,
	NSRegularExpressionSearch = 1 << 6,
	NSWidthInsensitiveSearch = 1 << 7
} NSStringCompareOptions;

/*
 * Cocoa's encodings. The three raw values in the first block are COCOA'S, kept so a numeric comparison in
 * existing code still means the same thing; the rest are OURS (§11.6.1 D2 - Apple publishes the names of these
 * constants and not a table this library can copy), numbered in one contiguous run above the legacy ones. Only
 * UTF-8 is SUPPORTED here (it is the storage); the others are named so the calls compile and can be rejected
 * honestly rather than silently mis-encoded.
 */
typedef enum {
	NSASCIIStringEncoding = 1,
	NSUTF8StringEncoding = 4,
	NSUnicodeStringEncoding = 10,
	NSNEXTSTEPStringEncoding = 100,
	NSJapaneseEUCStringEncoding = 101,
	NSShiftJISStringEncoding = 102,
	NSISOLatin1StringEncoding = 103,
	NSISOLatin2StringEncoding = 104,
	NSSymbolStringEncoding = 105,
	NSNonLossyASCIIStringEncoding = 106,
	NSISO2022JPStringEncoding = 107,
	NSMacOSRomanStringEncoding = 108,
	NSWindowsCP1250StringEncoding = 109,
	NSWindowsCP1251StringEncoding = 110,
	NSWindowsCP1252StringEncoding = 111,
	NSWindowsCP1253StringEncoding = 112,
	NSWindowsCP1254StringEncoding = 113,
	NSUTF16StringEncoding = 114,
	NSUTF16BigEndianStringEncoding = 115,
	NSUTF16LittleEndianStringEncoding = 116,
	NSUTF32StringEncoding = 117,
	NSUTF32BigEndianStringEncoding = 118,
	NSUTF32LittleEndianStringEncoding = 119,
	/* THE DEPRECATED ENCODING, which §62.24 keeps in scope: Apple publishes the name and no value, so the
	 * value is ours (§11.6.1 D2) and 2^16 leaves the enumerated values above it alone. */
	NSProprietaryStringEncoding = 65536
} NSStringEncoding;

/* THE TWO OPTION SETS THE WINDOWS TAKE (2026-09-20). The doors that use them — a
 * lossy encoding conversion, and -enumerateSubstringsInRange:options:usingBlock: —
 * are not in this header, so the types ship ahead of their users, as in Cocoa's own
 * header. Names from Apple's documentation index; values are ours (§11.6.1 D2, see
 * NSFileManager.h), and both are bit sets as their names say. */
typedef enum {
	NSStringEncodingConversionAllowLossy = 1 << 0,
	NSStringEncodingConversionExternalRepresentation = 1 << 1
} NSStringEncodingConversionOptions;

typedef enum {
	NSStringEnumerationByLines = 1 << 0,
	NSStringEnumerationByParagraphs = 1 << 1,
	NSStringEnumerationByComposedCharacterSequences = 1 << 2,
	NSStringEnumerationByWords = 1 << 3,
	NSStringEnumerationBySentences = 1 << 4,
	NSStringEnumerationReverse = 1 << 5,
	NSStringEnumerationSubstringNotRequired = 1 << 6,
	NSStringEnumerationLocalized = 1 << 7,
	NSStringEnumerationByCaretPositions = 1 << 8,
	NSStringEnumerationByDeletionClusters = 1 << 9
} NSStringEnumerationOptions;



/* ===================================================================================================
 * THE STRING FAMILY AND §C.3 (2026-09-29, M5). The front is PUBLIC and has NO STORAGE of its own; the
 * compiler's LITERALS are NSConstantString (the runtime's fixed-offset layout), a string that needs
 * storage of its own is NSOwnedString, and NSMutableString is a public subclass of it.
 *
 * WHAT A CONCRETE CLASS MUST ANSWER, and what the front is written over:
 *
 *     - (size_t)length                            UTF-16 code units (Apple's contract)
 *     - (unsigned short)characterAtIndex:(size_t) the unit at a unit index
 *     - (const char *)UTF8String                  the bytes, NUL-terminated — this family's own fast
 *                                                 path: -byteAtIndex: defaults to a read of THIS, so a
 *                                                 subclass that answers these three is correct through
 *                                                 the derived doors without any storage of its own.
 *
 * `[[NSString alloc] init]` is legitimate (the door routes to NSOwnedString) and `-classForCoder` answers
 * the PUBLIC class, so a literal archives as NSString rather than as the compiler's class.
 * =================================================================================================== */

@interface NSString : NSObject <NSCopying, NSCoding>

/* THE NSCoding DOORS (§63.22). THE WIRE IS OURS: Apple's spelling for this class's coded form is not published
 * in a source this project may read, unlike NSDate's `NS.time` and NSData's `NS.data`, which are recorded here
 * as Apple's. What the pair owes is that it ROUND-TRIPS and that it uses the coder's primitives rather than
 * nesting another value type: the payload is the class's own UTF-8 form — which is also what `-UTF8String`
 * answers, so the two agree about an embedded NUL by construction. */
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;

/* UNICODE NORMALIZATION (§63.24) — APPLE'S FOUR FORMS, AND THE REASON THIS CLASS NEEDS ICU. A normalizer is a
 * TABLE, and this project's rule for a table is to take the library that has it: ICU is already linked (F13.6),
 * and §63.2's line — the ENCODING cluster stays blocked on a converter/repertoire table — does not apply here,
 * because `unorm2` is a self-contained algorithm over icuuc's data. THE FOUR ARE THE TWO AXES: canonical vs
 * COMPATIBILITY (the second also folds the ligatures and the like, which is the difference a check can see),
 * and composed vs DECOMPOSED. */
- (NSString *)precomposedStringWithCanonicalMapping;		/* NFC */
- (NSString *)decomposedStringWithCanonicalMapping;		/* NFD */
- (NSString *)precomposedStringWithCompatibilityMapping;	/* NFKC */
- (NSString *)decomposedStringWithCompatibilityMapping;		/* NFKD */

/* Creation. The `init` family returns +1, as ARC decides BY NAME. */
+ (id)string;
+ (id)stringWithString:(NSString *)other;
+ (nullable id)stringWithUTF8String:(const char * _Nullable)utf8;
+ (id)stringWithFormat:(NSString *)format, ...;
+ (nullable id)stringWithContentsOfFile:(NSString *)path
			  encoding:(NSStringEncoding)encoding
			     error:(NSError * _Nullable * _Nullable)errorPtr;
+ (nullable id)stringWithContentsOfFile:(NSString *)path
		  usedEncoding:(NSStringEncoding *)encoding
			 error:(NSError * _Nullable * _Nullable)errorPtr;
/* THE URL FORMS (§63.29): THE SAME TWO DOORS OVER A URL RATHER THAN A PATH. A FILE url goes through the path
 * doors above — one implementation of "read the bytes, then decide what they mean" — and any other scheme
 * through the one synchronous loader this library has. */
+ (nullable id)stringWithContentsOfURL:(NSURL *)url
			      encoding:(NSStringEncoding)encoding
				 error:(NSError * _Nullable * _Nullable)errorPtr;
+ (nullable id)stringWithContentsOfURL:(NSURL *)url
			  usedEncoding:(NSStringEncoding *)encoding
				 error:(NSError * _Nullable * _Nullable)errorPtr;
/* CREATION FROM A C STRING WITH AN ENCODING (§63.28). THE ENCODINGS ARE THE ONES THIS LIBRARY STORES, which is
 * the same rule `-cStringUsingEncoding:` already follows: UTF-8, ASCII, and NOTHING ELSE — a byte string in an
 * encoding this library cannot name returns nil here rather than being reinterpreted as UTF-8, because that is
 * what the same refusal means on the way out. */
+ (nullable id)stringWithCString:(const char *)cString encoding:(NSStringEncoding)encoding;
+ (id)stringWithFormat:(NSString *)format arguments:(va_list)arguments;
/* THE VALIDATED FORMS (§63.27): a format that must use ONLY the specifiers the caller names, with the refusal
 * reported the way Apple reports it — nil and an NSError in NSCocoaErrorDomain with code NSFormattingError
 * (5546 here, because that is Apple's own value). They are VARIADIC, so a caller writes them exactly as Apple's
 * documentation does; the two differ only in the name, and the header says why below. */
+ (nullable id)stringWithValidatedFormat:(NSString *)format
			 validFormatSpecifiers:(NSString *)validFormatSpecifiers
					 error:(NSError * _Nullable * _Nullable)errorPtr, ...;
+ (nullable id)localizedStringWithValidatedFormat:(NSString *)format
			      validFormatSpecifiers:(NSString *)validFormatSpecifiers
					      error:(NSError * _Nullable * _Nullable)errorPtr, ...;

- (id)init;
- (id)initWithString:(NSString *)other;
- (nullable id)initWithUTF8String:(const char * _Nullable)utf8;
- (id)initWithFormat:(NSString *)format, ...;
- (id)initWithFormat:(NSString *)format arguments:(va_list)arguments;
- (nullable id)initWithData:(NSData *)data encoding:(NSStringEncoding)encoding;

/* THE LOCALE-TAKING FORMAT DOORS (§63.27's neighbours). A locale is HONOURED FOR CASE AND NOTHING ELSE in
 * this library (the header's own stance at the compare:…locale: block), so both doors DELEGATE to the
 * locale-free -initWithFormat:… above rather than keeping a second rendering rule that could drift — the
 * locale is accepted and recorded as ignored, not silently threaded into formatting that does not exist.
 * +localizedStringWithFormat: is Apple's current-locale spelling of +stringWithFormat: and shares it for
 * the same reason. */
- (id)initWithFormat:(NSString *)format locale:(nullable id)locale, ...;
- (id)initWithFormat:(NSString *)format locale:(nullable id)locale arguments:(va_list)argList;
+ (id)localizedStringWithFormat:(NSString *)format, ...;

/* THE INSTANCE VALIDATED-FORMAT DOORS (§63.27's siblings). The SAME rule the class doors above apply, reached
 * through -init: every directive in `format` must be one named in `validFormatSpecifiers`, and a refusal is nil
 * with an NSError in NSCocoaErrorDomain/NSFormattingError (Apple's own reporting). The locale-taking pair
 * accepts a locale and IGNORES it, exactly as -initWithFormat:locale:arguments: does (a locale is honoured for
 * case only in this library), so the locale form renders the locale-free answer rather than a second rule. */
- (nullable id)initWithValidatedFormat:(NSString *)format
		  validFormatSpecifiers:(NSString *)validFormatSpecifiers
				  error:(NSError * _Nullable * _Nullable)errorPtr, ...;
- (nullable id)initWithValidatedFormat:(NSString *)format
		  validFormatSpecifiers:(NSString *)validFormatSpecifiers
			      arguments:(va_list)arguments
				  error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable id)initWithValidatedFormat:(NSString *)format
		  validFormatSpecifiers:(NSString *)validFormatSpecifiers
			       locale:(nullable id)locale
				  error:(NSError * _Nullable * _Nullable)errorPtr, ...;
- (nullable id)initWithValidatedFormat:(NSString *)format
		  validFormatSpecifiers:(NSString *)validFormatSpecifiers
			       locale:(nullable id)locale
			    arguments:(va_list)arguments
				  error:(NSError * _Nullable * _Nullable)errorPtr;

/* The primitives every concrete subclass implements. */
- (const char *)UTF8String;
- (size_t)length;		/* UTF-16 CODE UNITS — Apple's contract (W1 slice 3) */
- (size_t)characterCount;	/* Unicode characters — an ADDITION: Cocoa has no such method */
- (unsigned char)byteAtIndex:(size_t)index;		/* a UTF-8 BYTE — the house door */
- (unsigned short)characterAtIndex:(size_t)index;	/* the UNIT at a unit index */

/*
 * THE four `…Characters:` FORMS (W1 slice 4) — the rows the unit CLOSES. They take
 * and give exactly the units the storage holds, so none of them converts anything:
 * a copy in, a copy out, and (in the no-copy form) ownership handed over.
 *
 * `-initWithCharactersNoCopy:length:freeWhenDone:` is Apple's ownership contract:
 * with YES the receiver owns the buffer and frees it; with NO it borrows it, never
 * writes it, and never frees it — and a MUTATION on such a string takes a copy
 * first, so a borrowed buffer is never written even through NSMutableString.
 */
+ (id)stringWithCharacters:(const unichar * _Nullable)characters length:(NSUInteger)length;
- (id)initWithCharacters:(const unichar * _Nullable)characters length:(NSUInteger)length;
- (id)initWithCharactersNoCopy:(unichar * _Nullable)characters length:(NSUInteger)length
		  freeWhenDone:(BOOL)freeBuffer;
- (void)getCharacters:(unichar *)buffer range:(NSRange)range;
- (size_t)lengthOfBytesUsingEncoding:(NSStringEncoding)encoding;
- (nullable NSData *)dataUsingEncoding:(NSStringEncoding)encoding;
- (nullable const char *)cStringUsingEncoding:(NSStringEncoding)encoding;

/* --- §63.47: THE BORROWED-BUFFER FAMILY -------------------------------------------------------------
 *
 * "NoCopy" IS A HINT, and Apple's own header says so at each of these declarations. WHAT THIS LIBRARY DOES
 * WITH THE HINT IS STATED RATHER THAN IMPLIED: it COPIES — the storage here is UTF-16 units and the payload
 * is owned whichever concrete class answers — and every ownership clause the door promises is honoured
 * anyway. `freeWhenDone:YES` disposes of the caller's buffer exactly once; the `deallocator:` spelling runs
 * the caller's own block instead of freeing, which is what separates the two spellings and why Apple added
 * the block form.
 *
 * AND THE TWO ENCODING-TAKING DOORS refuse what this library cannot STORE (UTF-8 and ASCII) rather than
 * approximating it: `-initWithBytesNoCopy:…` answers nil, and `-getBytes:…` answers NO while reporting the
 * whole range unconverted. That is the rule `-dataUsingEncoding:` and `-cStringUsingEncoding:` already
 * follow, at one more pair of doors. */
- (nullable id)initWithBytesNoCopy:(void *)bytes length:(NSUInteger)len
			  encoding:(NSStringEncoding)encoding freeWhenDone:(BOOL)freeBuffer;
- (nullable id)initWithBytesNoCopy:(void *)bytes length:(NSUInteger)len
			  encoding:(NSStringEncoding)encoding
		       deallocator:(void (^ _Nullable)(void *, NSUInteger))deallocator;
- (id)initWithCharactersNoCopy:(unichar * _Nullable)characters length:(NSUInteger)length
		   deallocator:(void (^ _Nullable)(unichar *, NSUInteger))deallocator;
/* Deprecated by Apple in favour of -initWithCString:encoding:; in scope under §62.24 like every other
 * deprecated name here, and it takes the one honest default C-string encoding (UTF-8) for the reason
 * §63.30's note gives. */
- (nullable id)initWithCStringNoCopy:(char *)bytes length:(NSUInteger)length freeWhenDone:(BOOL)freeBuffer;
/* `-getBytes:…` is the one door of this family that takes ANY encoding in Apple's header. A NULL buffer is
 * Apple's "tell me the size the conversion needs" form and is answered; `options` carries the
 * lossy/external-representation hints, and no lossy converter exists here. */
- (BOOL)getBytes:(nullable void *)buffer
       maxLength:(NSUInteger)maxBufferCount
      usedLength:(nullable NSUInteger *)usedBufferCount
	 encoding:(NSStringEncoding)encoding
	  options:(NSStringEncodingConversionOptions)options
	    range:(NSRange)range
   remainingRange:(nullable NSRangePointer)leftover;
/* The deprecated range form of -getCString:maxLength: (§63.47): it converts a RANGE rather than the whole
 * receiver, and `remainingRange` reports what it could not convert. */
- (void)getCString:(char *)buffer
	 maxLength:(NSUInteger)maxLength
	     range:(NSRange)range
    remainingRange:(nullable NSRangePointer)leftoverRange;

/* ===================================================================================================
 * THE C-STRING AND CHARACTER-COPY DOORS, AND ENCODING INTROSPECTION (§63.30).
 *
 * THE DEFAULT C-STRING ENCODING HERE IS UTF-8, AND THAT IS A DECISION RATHER THAN A COINCIDENCE: this
 * library STORES UTF-8 and ASCII and nothing else (§63.2's converter/repertoire line), so the "default
 * C-string encoding" the deprecated doors below take has exactly one honest value, and it is the storage.
 * A deprecated name here therefore answers what its encoding-taking sibling answers for UTF-8.
 *
 * THE DEPRECATED NAMES ARE IN SCOPE FOR THE REASON EVERY DEPRECATED NAME IS (§62.24): a program written
 * against them is exactly who this library is for. -getCString:maxLength:encoding: is the modern door and
 * the others delegate to it rather than keeping a second copy of the copy-out rule.
 * =================================================================================================== */
- (const char *)cString;
- (NSUInteger)cStringLength;
- (const char *)lossyCString;
- (void)getCString:(char *)buffer;
- (void)getCString:(char *)buffer maxLength:(NSUInteger)maxLength;
- (BOOL)getCString:(char *)buffer maxLength:(NSUInteger)maxLength encoding:(NSStringEncoding)encoding;
- (void)getCharacters:(unichar *)buffer;
+ (nullable id)stringWithCString:(const char *)cString;
+ (nullable id)stringWithCString:(const char *)cString length:(NSUInteger)length;
/* THE INSTANCE FORMS OF THE C-STRING AND BYTE DOORS (§63.30's C-string family). They are the -init mirrors of
 * `+stringWithCString:…` and `+stringWithCString:encoding:` above — the same two refusals in the same two places
 * (an encoding this library does not store, and a high byte under the ASCII label) — and the deprecated
 * no-encoding pair takes the one honest default C-string encoding here, UTF-8. `-initWithBytes:length:encoding:`
 * is the modern length-taking byte door and honours `length` rather than a terminating NUL. */
- (nullable id)initWithBytes:(const void *)bytes length:(NSUInteger)len encoding:(NSStringEncoding)encoding;
- (nullable id)initWithCString:(const char *)nullTerminatedCString encoding:(NSStringEncoding)encoding;
- (nullable id)initWithCString:(const char *)nullTerminatedCString;
- (nullable id)initWithCString:(const char *)bytes length:(NSUInteger)length;
/* INTROSPECTION over the same storage fact: a conversion is possible exactly when the storage IS the
 * encoding or when every byte is 7-bit ASCII, and the maximum length is the UTF-8 byte count — or 0 when
 * the conversion cannot happen at all, which is Apple's own answer for an impossible conversion. */
- (BOOL)canBeConvertedToEncoding:(NSStringEncoding)encoding;
- (NSUInteger)maximumLengthOfBytesUsingEncoding:(NSStringEncoding)encoding;

/* ===================================================================================================
 * ENCODING INTROSPECTION (§63.30's sibling), AND IT IS NOT THE BLOCKED CONVERSION CLUSTER (§63.2).
 * Every answer here is a fact about THIS library's storage rather than a conversion through a
 * converter/repertoire table, which is exactly why the `dataUsingEncoding:`/`-initWithData:encoding:`
 * line does not reach them: `-fastestEncoding` is the storage (UTF-8), `-smallestEncoding` is ASCII
 * exactly when every byte is 7-bit (the same test -canBeConvertedToEncoding: already makes, so the two
 * doors cannot disagree), `+defaultCStringEncoding` is the one honest default C-string encoding here
 * (§63.30 states it for the deprecated C-string doors), and `+availableStringEncodings` is the TWO the
 * storage can represent — ASCII and UTF-8 — zero-terminated, because naming an encoding this library
 * cannot honour would be the opposite of the refusal every other encoding door makes.
 * =================================================================================================== */
@property (readonly) NSStringEncoding fastestEncoding;
@property (readonly) NSStringEncoding smallestEncoding;
+ (NSStringEncoding)defaultCStringEncoding;
+ (const NSStringEncoding *)availableStringEncodings;
/* THE TWO DOORS §63.30's introspection family was still missing. +localizedNameOfStringEncoding: NAMES the
 * encodings this library can actually store — ASCII and UTF-8, the two +availableStringEncodings lists — and
 * answers nil for anything else, which is Apple's own nil-for-unknown contract and the honest answer for an
 * encoding this library would refuse to convert. -dataUsingEncoding:allowLossyConversion: ACCEPTS the lossy flag
 * and, because this library stores exactly the encodings it can represent losslessly, never invokes a lossy
 * conversion: the door answers what -dataUsingEncoding: answers for the same encoding. */
+ (nullable NSString *)localizedNameOfStringEncoding:(NSStringEncoding)encoding;
- (nullable NSData *)dataUsingEncoding:(NSStringEncoding)encoding allowLossyConversion:(BOOL)lossy;
- (BOOL)writeToFile:(NSString *)path
	 atomically:(BOOL)useAuxiliaryFile
	   encoding:(NSStringEncoding)encoding
	      error:(NSError * _Nullable * _Nullable)errorPtr;

/* ===================================================================================================
 * THE FILE/URL CONTENTS DOORS, AND THE DEPRECATED READ/WRITE NAMES (§63.29's neighbours). The
 * ENCODING-TAKING instance doors mirror the class doors above; the DEPRECATED pair of read names carries
 * no encoding, so it uses the one honest default C-string encoding here (§63.30), exactly as -cString
 * and friends do. Every one of these DELEGATES rather than keeping a second copy of the read/write rule.
 * =================================================================================================== */
- (nullable id)initWithContentsOfFile:(NSString *)path
			     encoding:(NSStringEncoding)encoding
				error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable id)initWithContentsOfFile:(NSString *)path
			 usedEncoding:(NSStringEncoding *)encoding
				error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable id)initWithContentsOfURL:(NSURL *)url
			    encoding:(NSStringEncoding)encoding
			       error:(NSError * _Nullable * _Nullable)errorPtr;
- (nullable id)initWithContentsOfURL:(NSURL *)url
			usedEncoding:(NSStringEncoding *)encoding
			       error:(NSError * _Nullable * _Nullable)errorPtr;
/* THE DEPRECATED NO-ENCODING NAMES: their default is the storage, i.e. -defaultCStringEncoding. */
- (nullable id)initWithContentsOfFile:(NSString *)path;
- (nullable id)initWithContentsOfURL:(NSURL *)url;
+ (nullable id)stringWithContentsOfFile:(NSString *)path;
+ (nullable id)stringWithContentsOfURL:(NSURL *)url;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)atomically;
- (BOOL)writeToURL:(NSURL *)url
	 atomically:(BOOL)atomically
	   encoding:(NSStringEncoding)encoding
	      error:(NSError * _Nullable * _Nullable)errorPtr;

/* Value semantics. */
- (BOOL)isEqualToString:(NSString *)other;
- (NSComparisonResult)compare:(NSString *)other;
- (NSComparisonResult)caseInsensitiveCompare:(NSString *)other;
- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options;
- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options
			range:(NSRange)range;
- (NSComparisonResult)compare:(NSString *)other
		      options:(NSStringCompareOptions)options
			range:(NSRange)range
			locale:(nullable id)locale;
/* The localised forms. A locale is HONOURED FOR CASE and for nothing else here.
 * The Turkic languages (tr, az) pair i with İ and I with ı — Unicode's
 * SpecialCasing makes their mapping conditional — so a Turkic locale changes the
 * case-insensitive result through the locale-taking form, and
 * -localizedCaseInsensitiveCompare: asks +[NSLocale currentLocale]. ORDERING is
 * NOT localised (no collation tables ship: the order is byte order), a nil locale
 * means "no locale-sensitive rules", and -rangeOfString:…locale: ignores it
 * because a search answers a RANGE and the Turkic fold changes lengths. */
- (NSComparisonResult)localizedCompare:(NSString *)other;
- (NSComparisonResult)localizedCaseInsensitiveCompare:(NSString *)other;
- (BOOL)hasPrefix:(NSString *)prefix;
- (BOOL)hasSuffix:(NSString *)suffix;
- (BOOL)containsString:(NSString *)substring;
- (NSRange)rangeOfString:(NSString *)substring;
- (NSRange)rangeOfString:(NSString *)substring options:(NSStringCompareOptions)options;
- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range;
- (NSRange)rangeOfString:(NSString *)substring
		 options:(NSStringCompareOptions)options
		   range:(NSRange)range
		  locale:(nullable id)locale;

/* Case, substrings, appending and replacing. The BASE mapping is ASCII — this
 * Foundation is UTF-8 and makes no general Unicode case claims — and the two
 * locale-taking forms below add the one locale rule that does ship: the Turkic
 * pairing of i with İ and I with ı. */
- (NSString *)uppercaseString;
- (NSString *)lowercaseString;
- (NSString *)uppercaseStringWithLocale:(nullable id)locale;
- (NSString *)lowercaseStringWithLocale:(nullable id)locale;
- (NSString *)capitalizedString;
/* THE BOTTOM OF THE SAME FAMILY (§63.25). `-capitalizedStringWithLocale:` is the one LIVE door of the four; the
 * three `localized…` spellings are Apple's DEPRECATED names for the same three operations with the CURRENT
 * locale — un-deprecated here under §11.5's porting-target policy, and answered by DELEGATING to the
 * locale-taking door, so there is one rule rather than two that can drift. */
- (NSString *)capitalizedStringWithLocale:(nullable id)locale;
- (NSString *)localizedUppercaseString;
- (NSString *)localizedLowercaseString;
- (NSString *)localizedCapitalizedString;
/* THE LOCALISED SEARCH DOORS (§63.26). Apple's two "standard" ones fold CASE **and** DIACRITICS under the
 * current locale — which is what §63.24's normalization finally makes expressible here: diacritic-insensitivity
 * IS canonical decomposition plus dropping the combining marks. `-localizedCaseInsensitiveContainsString:`
 * folds case only, because that is what its name says. */
/* AN `NSRange` IS A STRUCT AND CANNOT CARRY A NULLABILITY SPECIFIER — its "not found" is a VALUE
 * (`NSNotFound`), not a null, which is why Apple's own declaration of this door has none either. */
- (NSRange)localizedStandardRangeOfString:(NSString *)string;
- (BOOL)localizedStandardContainsString:(NSString *)string;
- (BOOL)localizedCaseInsensitiveContainsString:(NSString *)string;
- (NSString *)substringFromIndex:(NSUInteger)index;
- (NSString *)substringToIndex:(NSUInteger)index;
- (NSString *)substringWithRange:(NSRange)range;
- (NSString *)stringByAppendingString:(NSString *)other;
- (NSString *)stringByAppendingPathExtension:(NSString *)extension;
- (NSString *)stringByPaddingToLength:(NSUInteger)newLength
			 withString:(NSString *)pad
		    startingAtIndex:(NSUInteger)index;
- (NSString *)stringByAppendingFormat:(NSString *)format, ...;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
					 withString:(NSString *)replacement;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
					 withString:(NSString *)replacement
					    options:(NSStringCompareOptions)options
					      range:(NSRange)range;
- (NSArray *)componentsSeparatedByString:(NSString *)separator;
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet *)set;
- (NSArray *)componentsSeparatedByCharactersInSet:(NSCharacterSet *)set;
- (NSString *)stringByTrimmingCharactersInSet:(NSCharacterSet *)set;
- (NSString *)stringByReplacingCharactersInRange:(NSRange)range withString:(NSString *)replacement;

/* ===================================================================================================
 * THE SEARCH AND COMPARISON DOORS THAT TAKE OPTIONS.
 *
 * A CHARACTER-SET SEARCH TAKES TWO OPTIONS AND NO OTHERS — NSAnchoredSearch and NSBackwardsSearch.
 * The pair is not a guess: Apple's page for -rangeOfCharacterFromSet:options: names exactly those two
 * in its parameter list, and the absence of a case option is the point — a SET already says which
 * characters match, so folding case would widen a question the caller answered precisely. ANCHORED
 * means the match must be at the boundary the search starts from: the range's FIRST character going
 * forward, its LAST going backward. Nothing is normalised: canonically equivalent forms do not match,
 * which Apple states on the page and which the probe checks.
 *
 * THE TWO RAISES ARE THE PAGE'S OWN REQUIREMENTS, written where a caller meets them: a NIL set raises
 * NSInvalidArgumentException ("this value must not be nil"), and a range past the end raises
 * NSRangeException ("aRange must not exceed the bounds of the receiver").
 *
 * AND THE DELEGATION IS APPLE'S, NOT A CONVENIENCE: the page for -rangeOfCharacterFromSet: says it
 * "invokes with no options", and this one says it invokes "with the entire extent of the receiver for
 * the range". So the three doors are ONE scan with the boundaries and the direction handed in, and the
 * oldest of them now calls the newest rather than keeping a second copy of the walk.
 *
 * -commonPrefixWithString:options: takes NSCaseInsensitiveSearch and NSLiteralSearch (also named on its
 * page), and the string it returns is built from the RECEIVER'S characters — Apple's own example is
 * "Mädchen" against "Mädchenschule" answering the receiver's spelling, not the argument's.
 *
 * AND ONE DEVIATION IS STATED RATHER THAN LEFT TO BE DISCOVERED, BECAUSE APPLE'S OWN EXAMPLE IS THE CASE
 * THAT TRIPS IT: the two spellings in that example are CANONICALLY EQUIVALENT rather than equal, and the
 * default (non-NSLiteralSearch) comparison Apple describes treats them as matching. **This library
 * normalises NOWHERE** — its ordering and its folding are byte-wise and it says so (the stance
 * NSLocale's own unit records) — so a canonically equivalent pair answers as DIFFERENT here and the
 * prefix stops where the spellings part. That is deliberately the same rule -compare: applies: a class
 * with two notions of "equal" would be worse than a class with one documented one. Passing
 * NSLiteralSearch changes nothing, because exactness is already the only thing this door does.
 *
 * (-propertyListFromStringsFileFormat WAS IN THIS UNIT AND IS WITHDRAWN, with the measurement that
 * withdrew it: a `.strings` body is brace-less — `"a" = "b";` — and this library's old-style plist
 * reader answers NIL for one, so the door cannot honour Apple's contract until the READER accepts a
 * brace-less dictionary. It is a defect in the reader rather than in this door, it is worth its own
 * unit, and the probe of this one records the nil rather than hiding it. THE ROW STAYS OPEN IN THE
 * LEDGER, which is the honest place for it: a door that cannot parse the format its name promises is
 * not shipped.)
 * =================================================================================================== */
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet *)set
			   options:(NSStringCompareOptions)mask;
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet *)set
			   options:(NSStringCompareOptions)mask
			     range:(NSRange)range;
- (NSString *)commonPrefixWithString:(NSString *)other options:(NSStringCompareOptions)mask;

/* THE TRANSFORM AND FOLDING DOORS. -stringByApplyingTransform:reverse: honours the TWO strip transforms this
 * library can perform — NSStringTransformStripCombiningMarks and NSStringTransformStripDiacritics, i.e. NFD
 * followed by dropping the combining marks — and answers nil for the other names, which is Apple's own contract
 * for a transform that cannot be applied (a refusal, not a silent no-op). Its parameter is spelled `NSString *`
 * rather than `NSStringTransform` because the typedef sits BELOW this interface, as it does in Apple's header.
 * -stringByFoldingWithOptions: folds case and/or diacritics (the same ASCII case rule and the same BMP
 * non-base-character set the rest of this family uses) and RAISES NSInvalidArgumentException for an option it
 * cannot fold — width or numeric — rather than silently leaving it unfolded. */
- (nullable NSString *)stringByApplyingTransform:(NSString *)transform reverse:(BOOL)reverse;
- (NSString *)stringByFoldingWithOptions:(NSStringCompareOptions)options locale:(nullable id)locale;

/* ===================================================================================================
 * PERCENT-ENCODING, IN BOTH DIRECTIONS, ON APPLE'S OWN RULES.
 *
 * ENCODING replaces every character NOT in the set with percent-encoded characters, and THREE FACTS FROM
 * THE PAGE ARE THE WHOLE OF THE BEHAVIOUR: **UTF-8 is what determines the encoded bytes** (so one
 * character can become several `%XX` triples); **a member of `allowedCharacters` outside the 7-bit ASCII
 * range is IGNORED**, which means such a character is encoded even when the set names its code point —
 * the test is on the BYTE, not on the character; and the answer is nil when the transformation is not
 * possible, which in this library is exactly a NIL SET, because there is then no set to test against.
 *
 * DECODING replaces percent sequences with the matching UTF-8 characters and answers **nil for an invalid
 * percent-encoding sequence** — either a `%` not followed by two hex digits, or bytes that are not
 * well-formed UTF-8 once decoded (overlong forms, surrogate code points and beyond-U+10FFFF leads
 * included, so every answer is a string some encoder could have produced).
 *
 * THE DEPRECATED PAIR — `-stringByAddingPercentEscapesUsingEncoding:` and
 * `-stringByReplacingPercentEscapesUsingEncoding:` — is NOT here, and the reason is not the deprecation
 * (§62.24 makes deprecated API a porting target): their contract is "escape the characters that are not
 * legal in a URL", and **Apple does not publish WHICH characters those are** for the legacy call. Writing
 * the set from memory would be inventing a specification, and a set chosen for a URL component is exactly
 * the argument the modern door takes. They stay open in the ledger until that set can be cited.
 *
 * The declaring form is Apple's: `stringByRemovingPercentEncoding` is a READONLY PROPERTY there.
 * =================================================================================================== */
/* AND THE SET IS `_Nullable`, BECAUSE THE IMPLEMENTATION ACCEPTS NIL AND ANSWERS NIL — the probe asserts
 * exactly that, and it is the annotate-what-the-implementation-accepts rule doing its job: the first
 * version of this declaration said non-null and the probe, compiled under
 * -Werror=nullable-to-nonnull-conversion, refused to build. */
- (nullable NSString *)stringByAddingPercentEncodingWithAllowedCharacters:(nullable NSCharacterSet *)allowedCharacters;
@property (readonly, nullable) NSString *stringByRemovingPercentEncoding;

/* ===================================================================================================
 * COMPOSED CHARACTER SEQUENCES: A BASE LETTER AND THE COMBINING CHARACTERS THAT FOLLOW IT.
 *
 * THIS DOOR'S OWN PAGE DEFINES THE TERM, AND THE DEFINITION IS NOT UAX#29: "the composed character
 * sequence includes the first decomposed base letter found at or before `anIndex`, and its length includes
 * the decomposed base letter and all combining characters that follow". So the rule is a BASE plus the
 * MARKS THAT FOLLOW IT — and "combining characters" is this library's EXISTING notion of that,
 * `+[NSCharacterSet nonBaseCharacterSet]`, the general categories M* (Mn, Me, Mc) that NSCharacterSet
 * already ships and already probes. No second table, and no second notion of a mark.
 *
 * THE SET IS A BMP SET AND THE DOOR MEASURES ITS BOUNDARY RATHER THAN ASSUMING AROUND IT: NSCharacterSet's
 * own `FN_MAX_CHARACTER` is 0xFFFF, so a real combining mark above U+FFFF (U+1D165, a musical combining
 * stem) is INVISIBLE to `+nonBaseCharacterSet` and does not extend a sequence. The probe pins that measured
 * answer, and the door asks the SET for the scalar rather than testing a code unit, so it follows the day
 * the set gains astral coverage instead of having to be rediscovered.
 *
 * AND IT IS MEASURABLY NOT THE SET THIS LIBRARY'S ENGINE ANSWERS for
 * `-enumerateSubstringsInRange:options:` with `NSStringEnumerationByComposedCharacterSequences`
 * (FNTextBreaking's `FNTextUnitComposedCharacter`, which is ICU's `UBRK_CHARACTER`), where a unit is a
 * UAX#29 grapheme cluster. Under the rule above CRLF is TWO sequences, and an emoji ZWJ sequence, a
 * regional-indicator flag pair and conjoining Hangul jamo are several each; UAX#29 joins all four into one
 * cluster. The probe MEASURES that divergence rather than leaving it to this comment, because the reader
 * who assumed the two doors agree would be reasonable and wrong. Each door follows its own page.
 *
 * THE INDEX IS A CHARACTER'S, NOT A CODE UNIT'S: an index landing on a low surrogate still names the
 * character that begins at its high surrogate. An `anIndex` equal to the length is the end of the string
 * and answers an empty range; anything past that is out of bounds and raises NSRangeException.
 *
 * `-rangeOfComposedCharacterSequencesForRange:` is the page's "grow a range to include all composed
 * character sequences it overlaps" — from the start of the first sequence it touches to the end of the
 * last. AN EMPTY RANGE OVERLAPS NOTHING and the page publishes no answer for it: this door answers the
 * sequence CONTAINING the location, which is what the index-taking door answers and therefore what a
 * caller can predict, rather than handing the empty range back.
 * =================================================================================================== */
- (NSRange)rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)index;
- (NSRange)rangeOfComposedCharacterSequencesForRange:(NSRange)range;

/* Conversions. */
- (int)intValue;
- (NSInteger)integerValue;
- (long long)longLongValue;
- (float)floatValue;
- (double)doubleValue;
- (BOOL)boolValue;

/* Paths — pure string operations, and FSH-shaped paths are slash-separated. */
- (NSString *)lastPathComponent;
- (NSString *)pathExtension;
- (NSString *)stringByDeletingLastPathComponent;
- (NSString *)stringByDeletingPathExtension;
- (NSString *)stringByAppendingPathComponent:(NSString *)component;
- (nullable NSArray *)pathComponents;
- (NSString *)stringByStandardizingPath;
- (BOOL)isAbsolutePath;

/* THE PATH DOORS THE LEDGER STILL HAD OPEN, each a pure string operation over FSH's slash-separated shape:
 *
 *   +pathWithComponents:            JOINS components with "/", and a leading "/" component makes the result
 *                                   absolute rather than doubled (Apple's own joining rule);
 *   -stringsByAppendingPaths:       MAPS -stringByAppendingPathComponent: over an array, so the two doors share
 *                                   one joining rule rather than keeping a second;
 *   -fileSystemRepresentation and   THE C-STRING FORM THIS FILESYSTEM ACTUALLY USES, which here is UTF-8 — the
 *   -getFileSystemRepresentation:   storage itself (§63.30), so neither door transcodes; the length-taking form
 *                                   is the -getCString:maxLength:encoding: rule with the encoding fixed;
 *   -stringByExpandingTildeInPath   EXPAND `~` and `~user` through the account database (NSHomeDirectory /
 *   and …Abbreviating…              NSHomeDirectoryForUser), and the reverse abbreviates a path under the home
 *                                   directory back to `~`. A path with no leading `~`, and a path whose `~user`
 *                                   is unknown, are answered UNCHANGED — Apple's own behaviour for both.
 *
 * ⚠ AND `-stringByResolvingSymlinksInPath` IS HERE NOW, WHICH IS A CORRECTION OF THIS NOTE (the user's
 * decision, 2026-10-01, §63.48). It read: "resolving a symlink is a FILESYSTEM LOOKUP, and this library's
 * string path doors are deliberately lexical (see -stringByStandardizingPath's own note). It stays open."
 * **THAT WAS A DESIGN PREFERENCE, NOT A NECESSITY**, and §11's rule is that a difference from Apple is a
 * failure unless Argentum CANNOT have Apple's behaviour — which is a claim about the PLATFORM, and this
 * platform can: `lstat`/`readlink` are here. So the door is implemented, **THE STRING PATH DOORS STOP BEING
 * LEXICAL-ONLY**, and that boundary change is registered in §11.6. What keeps it safe is the documented half:
 * a path whose links cannot be resolved is returned UNMODIFIED, which is the lexical answer. */
+ (NSString *)pathWithComponents:(NSArray *)components;
- (NSArray *)stringsByAppendingPaths:(NSArray *)paths;
- (const char *)fileSystemRepresentation;
- (BOOL)getFileSystemRepresentation:(char *)buffer maxLength:(NSUInteger)maxLength;
- (NSString *)stringByExpandingTildeInPath;
- (NSString *)stringByAbbreviatingWithTildeInPath;
/* Apple declares this as a PROPERTY — the ledger's row is a `property` row and §11.0 says match the
 * declaration — and its behaviour is the filesystem walk the note above used to refuse. */
@property (readonly, copy) NSString *stringByResolvingSymlinksInPath;

/* THE OTHER TWO §63.48 ROWS, beside the path doors they belong to. `-localizedStandardCompare:` is the
 * Finder-style ordering Apple documents for file names in lists and tables; `-completePathIntoString:…`
 * completes a partial path against the filesystem and reports how many candidates it found. Both take their
 * shapes from a citable source rather than from memory: the first from Apple's own header, the second from
 * GNUstep's published reference, which §2 admits as a secondary spec. */
- (NSComparisonResult)localizedStandardCompare:(NSString *)string;
/* ⚠ THE TYPES ARE WRITTEN PLAIN `NSArray *`, WHICH IS THIS HEADER'S OWN RULE RATHER THAN AN OVERSIGHT: NSArray
 * is only FORWARD-DECLARED here, so a type argument is a compile error ("type arguments cannot be applied to
 * non-parameterized class") — and the sibling door above (`+pathWithComponents:`) is spelled the same way for
 * the same reason. Importing NSArray.h to buy the decoration would put the collections family on this
 * header's include path. */
- (NSUInteger)completePathIntoString:(NSString * _Nullable * _Nullable)outputName
		       caseSensitive:(BOOL)flag
		    matchesIntoArray:(NSArray * _Nullable * _Nullable)outputArray
			 filterTypes:(nullable NSArray *)filterTypes;


/* §63.49's TWO DEPRECATED LINGUISTIC DOORS ARE DECLARED — in `NSLinguisticTagger.h`'s
 * `NSString (NSLinguisticAnalysis)` category, WHICH IS APPLE'S OWN HOME FOR THEM and the only arrangement
 * whose types are in scope: their parameters name `NSLinguisticTagScheme` and `NSLinguisticTaggerOptions`,
 * which are that header's own types, so declaring them here would put the linguistics family on this header's
 * include path to buy two deprecated doors. A caller cannot tell: the umbrella header includes both, so
 * `[string linguisticTagsInRange:…]` is the same call either way. */


/* ===================================================================================================
 * THE DOOR THAT CONSUMES THE OPTIONS ABOVE (§62.48)
 *
 * Apple's enumeration. The UNITS COME FROM `FNTextBreaking`, which is the library's one answer to where a word, a
 * sentence, a paragraph, a line and a composed character begin and end — `FNTextBreaking.h` names this method as one
 * of its two callers, and this is that caller arriving.
 *
 * AND ITS BOUNDARIES ARE STATED RATHER THAN DISCOVERED. Three of the ten options are REFUSED with an exception
 * rather than honoured approximately: `NSStringEnumerationLocalized`, `…ByCaretPositions` and `…ByDeletionClusters`.
 * The first asks for locale-directed breaking where this library breaks with the root locale (the stance
 * NSLocale's own unit records); the other two name a caret and a deletion cluster, which are a text-input layout's
 * units and not a string's. A caller that needs them needs them from the toolkit that owns the layout.
 *
 * `enclosingRange` IS THE NEXT LARGER UNIT THIS ENGINE CAN NAME, and which unit that is depends on what is being
 * enumerated: a LINE or a SENTENCE is enclosed by its PARAGRAPH, a WORD by its SENTENCE, and a composed CHARACTER
 * by its WORD. A PARAGRAPH is enclosed by nothing larger, so its enclosing range is its own.
 * =================================================================================================== */
- (void)enumerateSubstringsInRange:(NSRange)range
			   options:(NSStringEnumerationOptions)opts
			usingBlock:(void (^)(NSString * _Nullable substring,
					     NSRange substringRange,
					     NSRange enclosingRange,
					     BOOL *stop))block;

/* ===================================================================================================
 * THE LINE RANGES — AND A LINE'S TERMINATOR SET IS APPLE'S, TAKEN FROM ITS PUBLISHED PAGE RATHER THAN
 * INVENTED: a line is delimited by ANY of U+000A (LF), U+000D (CR), U+0085 (NEL), U+2028 (LS) and
 * U+2029 (PS), "the longest possible sequence being preferred to any shorter" — Apple's own sentence,
 * and the rule that makes CRLF ONE terminator rather than two. `-enumerateLinesUsingBlock:` defers to
 * that same discussion on its own page, so it shares this rule instead of inventing a second one.
 *
 * THE TERMINATOR IS NOT PART OF THE LINE'S TEXT: `contentsEnd` is where the text stops and `end` where
 * the terminator stops — the distinction the three out-parameters exist to carry — and
 * `-enumerateLinesUsingBlock:` hands back the text WITHOUT the terminator.
 *
 * AN INVALID RANGE RAISES NSRangeException: Apple's page says the method "detects all invalid ranges
 * (including those with negative lengths)" and that this "causes an exception", which is the same
 * refusal every other range door in this library makes.
 *
 * ⚠ AND THE PARAGRAPH DOORS THAT USED TO BE REFUSED HERE ARE DECLARED BELOW, WHICH IS A CORRECTION OF THIS
 * COMMENT (the user's decision, 2026-10-01, §63.46). It read: they are "NOT here yet, and the reason is a real
 * disagreement rather than an omission: Apple's page defines a paragraph as text 'delimited by a carriage
 * return, newline, or paragraph separator' — three single characters — while this library's own breaking engine
 * defines it as 'a run of lines with no blank line between them' and records that as Apple's definition too.
 * They answer differently for a string holding a blank line, so writing one would be picking a winner by
 * guesswork."
 *
 * **THE DISAGREEMENT WAS NEVER BETWEEN TWO EQUAL READINGS — IT WAS BETWEEN A DOOR AND AN ENGINE, and Apple
 * settles it.** The door's rule is published in its own page, and Apple's own header says
 * `NSStringEnumerationByParagraphs` is "Equivalent to paragraphRangeForRange:" — so the ENGINE was the side
 * that had to move, and it did: FNTextBreaking's paragraph walk now uses the same three-character rule. **A
 * BLANK LINE IS THEREFORE AN EMPTY PARAGRAPH rather than a skipped separator**, which is the observable half
 * of the decision and the half the probe asserts.
 *
 * AND THE PARAGRAPH'S TERMINATOR SET IS NARROWER THAN A LINE'S: a line ends at LF, CR, NEL, LS or PS; a
 * paragraph at LF, CR or PS. NEL (U+0085) and LINE SEPARATOR (U+2028) end a LINE and NOT a paragraph — a
 * difference only a pair of doors with two published rules can have.
 * =================================================================================================== */
- (void)getLineStart:(NSUInteger *)startPtr
		 end:(NSUInteger *)lineEndPtr
	 contentsEnd:(NSUInteger *)contentsEndPtr
	    forRange:(NSRange)range;
- (NSRange)lineRangeForRange:(NSRange)range;
- (void)enumerateLinesUsingBlock:(void (^)(NSString *line, BOOL *stop))block;

/* THE PARAGRAPH PAIR, the same shape as the line pair above and for the same reason the three out-parameters
 * exist: `end` is where the TERMINATOR stops (a paragraph owns the break it ends with), `contentsEnd` is where
 * its TEXT stops. CRLF counts once. */
- (void)getParagraphStart:(nullable NSUInteger *)startPtr
		      end:(nullable NSUInteger *)paragraphEndPtr
	      contentsEnd:(nullable NSUInteger *)contentsEndPtr
		 forRange:(NSRange)range;
- (NSRange)paragraphRangeForRange:(NSRange)range;
@end

@interface NSOwnedString : NSString
{
	unsigned short *_units;	/* NOT NUL-terminated: UTF-16 CODE UNITS */
	size_t _length;		/* UNITS — the storage's own count */
	char *_utf8;		/* LAZY: the materialised UTF-8 form, owned, NUL-terminated */
	size_t _utf8size;	/* its byte count, excluding the terminating NUL */
	unsigned char _ownsUnits;	/* 0 only for -initWithCharactersNoCopy:…:freeWhenDone:NO */
}
+ (nullable id)stringWithUTF8String:(const char * _Nullable)utf8;
- (nullable id)initWithUTF8String:(const char * _Nullable)utf8;
- (nullable id)initWithBytes:(const char *)bytes length:(size_t)length;
- (id)initWithCharacters:(const unichar * _Nullable)characters length:(NSUInteger)length;
- (id)initWithCharactersNoCopy:(unichar * _Nullable)characters length:(NSUInteger)length
		  freeWhenDone:(BOOL)freeBuffer;
@end

@interface NSMutableString : NSOwnedString <NSMutableCopying>
+ (id)string;
+ (id)stringWithCapacity:(NSUInteger)capacity;
- (id)initWithCapacity:(NSUInteger)capacity;
- (void)setString:(NSString *)other;
- (void)appendString:(NSString *)other;
- (void)appendUTF8String:(const char *)utf8;
- (void)appendFormat:(NSString *)format, ...;
- (void)insertString:(NSString *)string atIndex:(NSUInteger)index;
- (void)deleteCharactersInRange:(NSRange)range;
- (void)replaceCharactersInRange:(NSRange)range withString:(NSString *)string;
- (NSUInteger)replaceOccurrencesOfString:(NSString *)target
			      withString:(NSString *)replacement
				 options:(NSStringCompareOptions)options
				   range:(NSRange)range;
@end

/*
 * NSConstantString — the class `@"..."` names, pinned by
 * `-fconstant-string-class=NSConstantString` in tools/musl-clang-objc64.sh.
 *
 * THE IVARS ARE THE RUNTIME'S. The compiler emits these objects and the runtime
 * fills them in, so the layout is fixed by our runtime's own Test/Test.h
 * (NEW_ABI): flags, length (UTF-16 code units), size (BYTES), hash, then a
 * POINTER to the characters — not trailing character data, which is what
 * loader.c's older `struct nsstr` comment suggests.
 *
 * `flags`' LOW TWO BITS ARE THE ENCODING (loader.c: 0 ASCII, 1 UTF-8, 2 UTF-16,
 * 3 UTF-32), and clang uses UTF-16 for EVERY non-ASCII literal — its
 * GenerateConstantString says "all non-ASCII strings are represented as UTF-16".
 * So the character data is not always UTF-8, and NSString.m CONVERTS a UTF-16
 * constant rather than reinterpreting it. An all-ASCII literal cannot notice the
 * difference (there length == size), which is why this went unseen until a
 * non-ASCII literal was asserted byte by byte.
 *
 * Instances are marked permanent by the runtime (class_table.c special-cases
 * this name), so they take no part in reference counting.
 */
@interface NSConstantString : NSString
{
	unsigned int _rflags;
	unsigned int _rlength;	/* UTF-16 code units */
	unsigned int _rsize;	/* bytes */
	unsigned int _rhash;
	const char * const _rstr;
}
@end

/* THE TRANSFORMS AND THE DETECTION KEYS (the coverage slice). A transform is a NAME a -stringByApplyingTransform:
 * call takes, and this library implements NONE of them, so each constant is declared and the door that would use
 * it is refused rather than silently ignored; the detection keys are the option keys of a similar door. Names
 * Apple's, values ours (§11.6.1 D2), and the two types come with them because no caller can name a transform
 * without them. */
typedef NSString *NSStringTransform;
typedef NSString *NSStringEncodingDetectionOptionsKey;

extern NSStringTransform const NSStringTransformFullwidthToHalfwidth;
extern NSStringTransform const NSStringTransformHiraganaToKatakana;
extern NSStringTransform const NSStringTransformLatinToArabic;
extern NSStringTransform const NSStringTransformLatinToCyrillic;
extern NSStringTransform const NSStringTransformLatinToGreek;
extern NSStringTransform const NSStringTransformLatinToHangul;
extern NSStringTransform const NSStringTransformLatinToHebrew;
extern NSStringTransform const NSStringTransformLatinToHiragana;
extern NSStringTransform const NSStringTransformLatinToKatakana;
extern NSStringTransform const NSStringTransformLatinToThai;
extern NSStringTransform const NSStringTransformMandarinToLatin;
extern NSStringTransform const NSStringTransformStripCombiningMarks;
extern NSStringTransform const NSStringTransformStripDiacritics;
extern NSStringTransform const NSStringTransformToLatin;
extern NSStringTransform const NSStringTransformToUnicodeName;
extern NSStringTransform const NSStringTransformToXMLHex;

extern NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionAllowLossyKey;
extern NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionDisallowedEncodingsKey;
extern NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionFromWindowsKey;
extern NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionLikelyLanguageKey;
extern NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionLossySubstitutionKey;
extern NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionSuggestedEncodingsKey;
extern NSStringEncodingDetectionOptionsKey const NSStringEncodingDetectionUseOnlySuggestedEncodingsKey;

/* Apple declares these two in THIS header, which is why they are here rather than in NSException.h. */
extern NSExceptionName const NSCharacterConversionException;
extern NSExceptionName const NSParseErrorException;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSSTRING_H */
