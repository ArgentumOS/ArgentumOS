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
 *
 * WHAT §62.18 ADDED HERE, and why each addition had to be here rather than somewhere tidier:
 *
 *   * EVERY MATCH IS NOW A `NSTextCheckingTypeRegularExpression` RESULT, built through Apple's own
 *     `+regularExpressionCheckingResultWithRanges:count:regularExpression:` instead of the bare ranges
 *     constructor this file used to call. `NSTextCheckingResult` left this header for its own (§62.18), and the
 *     bare constructor is still the F13.16 addition it was — but a match with no declared kind was a match a
 *     caller could not classify, which is now fixed;
 *   * NAMED GROUPS, which POSIX ERE cannot spell. `(?<name>…)` is TRANSLATED to `(` before regcomp — the same
 *     capture group to the engine — and the names are kept, in declaration order, on the object that owns the
 *     pattern (see `-indexOfCaptureGroupNamed:` below). The translation is a SCAN, not a substitution: it
 *     tracks backslash escapes and character classes, because `\(` and `[(]` are literals that must not be
 *     touched;
 *   * `-enumerateMatchesInString:options:range:usingBlock:`, Apple's general way to walk matches and the one
 *     its `NSDataDetector` page uses as its example.
 */

#ifndef FOUNDATION_NSREGULAREXPRESSION_H
#define FOUNDATION_NSREGULAREXPRESSION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSTextCheckingResult.h>	/* a match IS a text-checking result */

@class NSArray;
@class NSDictionary;
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

/* ONE MATCH — `NSTextCheckingResult`, whose own header is where the class lives: its thirteen kinds, its
 * payloads, and the keys of its component dictionaries. It is IMPORTED (at the top of this file) rather than
 * declared here because every match this class answers with IS a text-checking result of type
 * `NSTextCheckingTypeRegularExpression`, so a caller that matches and then reads the result has one class to
 * learn rather than two. */

@interface NSRegularExpression : NSObject <NSCopying>
{
	void *_compiled;		/* a regex_t, owned here so POSIX stays out of this header */
	NSString *_pattern;
	NSRegularExpressionOptions _options;
	NSUInteger _captures;
	NSDictionary *_groupNames;	/* OUR OWN: capture-group name -> index, in declaration order */
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

/* AN ADDITION, and it is the one seam named groups needed. Apple has no such method because Apple's engine has
 * named groups natively; this library compiles with POSIX ERE (the decision §10 recorded), which has no
 * `(?<name>…)` construct at all - so the pattern is TRANSLATED on the way to the engine (the `(?<name>` prefix
 * becomes a plain `(`, which is the same group) and the names are remembered here. `-rangeWithName:` on a
 * result comes through this method, which is why the map lives on the pattern's own object: the name is a
 * property of the pattern, and a property of the pattern does not belong on a match. */
- (NSUInteger)indexOfCaptureGroupNamed:(NSString *)name;

/* THE BLOCK ENUMERATOR, which Apple's page for `NSDataDetector` uses as its example of the general way to walk
 * matches. `stop` is the caller's, and setting it stops the walk after the current block invocation. */
- (void)enumerateMatchesInString:(NSString *)string
			 options:(NSMatchingOptions)options
			   range:(NSRange)range
		      usingBlock:(void (^)(NSTextCheckingResult *result, NSMatchingFlags flags,
					   BOOL *stop))block;

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
