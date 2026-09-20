/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSRegularExpression / NSTextCheckingResult — regular expressions as objects. F13.16,
 * docs/design/foundation-plan.md §10.
 *
 * THE ENGINE IS MUSL'S, which is the decision §10 recorded before this slice existed: the predicate
 * family's MATCHES already binds POSIX ERE through <regex.h>, the engine lives inside libc, and a
 * second engine would be a second set of behaviours to be wrong about. What this class adds is the
 * OBJECT: a compiled pattern, the matches it finds, and the capture groups inside them.
 *
 * WHAT THE OPTIONS MEAN HERE, and the ones that do not mean anything are named rather than accepted
 * silently:
 *   CaseInsensitive             REG_ICASE;
 *   AnchorsMatchLines           REG_NEWLINE, so ^ and $ see line boundaries;
 *   DotMatchesLineSeparators    ALREADY TRUE of this engine: POSIX ERE's "." matches a newline
 *                               unless the REG_NEWLINE flag is what suppressed it, so the option
 *                               changes nothing and is accepted;
 *   AllowCommentsAndWhitespace  NOT AVAILABLE, UseUnixLineSeparators NOT AVAILABLE,
 *   IgnoreMetacharacters        NOT AVAILABLE, UseUnicodeWordBoundaries NOT AVAILABLE — POSIX has no
 *                               spelling for any of them, and a caller who asks for one is told by
 *                               `-options` that it is not in force rather than being told nothing.
 *
 * RANGES ARE UTF-16 UNITS, as Cocoa's are, while the engine counts BYTES — so every range crosses
 * that boundary through one conversion, which is the only place in this file that has to know both.
 * A pattern that cannot compile is REFUSED at construction with an NSError carrying the engine's own
 * message, because that message names the offending part of the pattern.
 */

#ifndef FOUNDATION_NSREGULAREXPRESSION_H
#define FOUNDATION_NSREGULAREXPRESSION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>

@class NSArray;
@class NSError;
@class NSMutableString;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

typedef NSUInteger NSRegularExpressionOptions;
enum {
	NSRegularExpressionCaseInsensitive = 0x01,
	NSRegularExpressionAllowCommentsAndWhitespace = 0x02,
	NSRegularExpressionIgnoreMetacharacters = 0x04,
	NSRegularExpressionDotMatchesLineSeparators = 0x08,
	NSRegularExpressionAnchorsMatchLines = 0x10,
	NSRegularExpressionUseUnixLineSeparators = 0x20,
	NSRegularExpressionUseUnicodeWordBoundaries = 0x40
};

typedef NSUInteger NSMatchingOptions;
enum {
	NSMatchingReportProgress = 0x01,
	NSMatchingReportCompletion = 0x02,
	NSMatchingAnchored = 0x04,
	NSMatchingWithTransparentBounds = 0x08,
	NSMatchingWithoutAnchoringBounds = 0x10
};

typedef NSUInteger NSMatchingFlags;
enum {
	NSMatchingProgress = 0x01,
	NSMatchingCompleted = 0x02,
	NSMatchingHitEnd = 0x04,
	NSMatchingRequiredEnd = 0x08,
	NSMatchingInternalError = 0x10
};

/* ONE MATCH: the whole match is range 0, and each capture group follows in order. A group that did
 * not participate is NSNotFound's location, which is Cocoa's spelling of "this group matched
 * nothing". */
@interface NSTextCheckingResult : NSObject <NSCopying>
{
	NSRange *_ranges;
	NSUInteger _count;
}

+ (instancetype)resultWithRanges:(const NSRange *)ranges count:(NSUInteger)count;
- (NSUInteger)numberOfRanges;
- (NSRange)range;
- (NSRange)rangeAtIndex:(NSUInteger)index;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

@interface NSRegularExpression : NSObject <NSCopying>
{
	void *_compiled;		/* a regex_t, owned here so POSIX stays out of this header */
	NSString *_pattern;
	NSRegularExpressionOptions _options;
	NSUInteger _captures;
}

+ (nullable instancetype)regularExpressionWithPattern:(NSString *)pattern
					      options:(NSRegularExpressionOptions)options
						error:(NSError ** _Nullable)error;
- (nullable instancetype)initWithPattern:(NSString *)pattern
				 options:(NSRegularExpressionOptions)options
				   error:(NSError ** _Nullable)error;

- (NSString *)pattern;
- (NSRegularExpressionOptions)options;
- (NSUInteger)numberOfCaptureGroups;

- (NSArray *)matchesInString:(NSString *)string
		     options:(NSMatchingOptions)options
		       range:(NSRange)range;
- (NSUInteger)numberOfMatchesInString:(NSString *)string
			      options:(NSMatchingOptions)options
				range:(NSRange)range;
- (nullable NSTextCheckingResult *)firstMatchInString:(NSString *)string
					      options:(NSMatchingOptions)options
						range:(NSRange)range;
- (NSRange)rangeOfFirstMatchInString:(NSString *)string
			     options:(NSMatchingOptions)options
			       range:(NSRange)range;

- (NSString *)stringByReplacingMatchesInString:(NSString *)string
				       options:(NSMatchingOptions)options
					 range:(NSRange)range
				  withTemplate:(NSString *)template;
- (NSUInteger)replaceMatchesInString:(NSMutableString *)string
			     options:(NSMatchingOptions)options
			       range:(NSRange)range
			withTemplate:(NSString *)template;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSREGULAREXPRESSION_H */
