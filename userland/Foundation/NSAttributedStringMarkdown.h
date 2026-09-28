/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE MARKDOWN HALF OF "STRINGS WITH METADATA" (2026-09-26, plan §62.64): the options an importer would be
 * configured with, and the SOURCE POSITION that says where in the markdown a piece of text came from. Both are
 * VALUE OBJECTS and neither needs a parser, which is why they ship while the importer does not — §12.6 lists "a
 * markdown parser" as a dependency this tree still has to ADD, and the plan's own vocabulary rule has shipped a
 * family's nouns before its engine (W10 shipped the attributes and the two enums).
 *
 * ONE HEADER FOR BOTH CLASSES AND THE TWO ENUMS, which Cocoa splits across three headers: they are one family in
 * Apple's own grouping ("Strings with Metadata"), the enums belong to the options class that takes them, and
 * reading them together is worth more than matching a file count — the same choice NSLock.h records for the four
 * locking classes.
 *
 * WHAT THIS FILE DOES NOT DO, named rather than implied: it does not PARSE markdown and it does not apply the
 * options. Their whole meaning is what an importer would consult, and the importer is the dependency above; the
 * classes answer their own questions and nothing else.
 */

#import <Foundation/NSAttributedString.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@class NSData;
@class NSError;
@class NSString;
@class NSURL;

/* HOW MUCH OF THE MARKDOWN A PARSE WOULD TURN INTO ATTRIBUTES. Apple publishes the case names and their
 * meanings ("Full" = block structure and inline formatting, "InlineOnly" = the inline half without the block
 * structure, "InlineOnlyPreservingWhitespace" = the same with layout-preserving whitespace handling) and no
 * VALUES: the numbers are ours (§11.6.1 D2), which is the standing rule for every enum in this library. */
typedef enum {
	NSAttributedStringMarkdownInterpretedSyntaxFull = 0,
	NSAttributedStringMarkdownInterpretedSyntaxInlineOnly = 1,
	NSAttributedStringMarkdownInterpretedSyntaxInlineOnlyPreservingWhitespace = 2
} NSAttributedStringMarkdownInterpretedSyntax;

/* WHAT TO DO WHEN THE SOURCE IS NOT VALID MARKDOWN: fail with an error, or return what could be read. */
typedef enum {
	NSAttributedStringMarkdownParsingFailureReturnError = 0,
	NSAttributedStringMarkdownParsingFailureReturnPartiallyParsedIfPossible = 1
} NSAttributedStringMarkdownParsingFailurePolicy;

/* THE OPTIONS AN IMPORTER WOULD BE GIVEN, as a mutable value object: Apple's own page shows `-init` and five
 * PROPERTIES, and Swift's spelling of the same type declares them `var`, so they are readwrite here too. */
@interface NSAttributedStringMarkdownParsingOptions : NSObject <NSCopying>
{
@protected
	BOOL _allowsExtendedAttributes;
	BOOL _appliesSourcePositionAttributes;
	NSAttributedStringMarkdownInterpretedSyntax _interpretedSyntax;
	NSAttributedStringMarkdownParsingFailurePolicy _failurePolicy;
	NSString *_languageCode;
}

/* "Whether to parse extended attributes" — Apple's own default is NO, and the initialiser takes no arguments
 * for it: the defaults ARE the shape of an object made with -init, and the setters are how a caller changes it. */
@property BOOL allowsExtendedAttributes;
/* "Whether to apply the source position attributes" — our starting value is NO (Apple publishes the property
 * and, unlike the one above, no default). */
@property BOOL appliesSourcePositionAttributes;
/* WHICH SYNTAX. Apple publishes no starting value for this one either; ours is Full. */
@property NSAttributedStringMarkdownInterpretedSyntax interpretedSyntax;
/* WHAT A FAILURE DOES. Ours is the forgiving policy, ReturnPartiallyParsedIfPossible. */
@property NSAttributedStringMarkdownParsingFailurePolicy failurePolicy;
/* The natural language of the source, as a BCP-47 code — and this one IS Apple's: "The default is `nil`, which
 * applies no attributes." */
@property (nullable, copy) NSString *languageCode;

@end

/* WHERE A PIECE OF MARKDOWN CAME FROM, as two positions: the line and column where it begins and where it ends.
 * APPLE'S OWN RULE FOR THE NUMBERS, quoted where they are read: lines and columns are 1-BASED, and "columns
 * represent UTF-8 indices; for multi-byte characters, the column indicates the first byte" — so a column is a
 * BYTE offset into the line and not a character index, which is the one thing about this class a caller can get
 * wrong without noticing. */
@interface NSAttributedStringMarkdownSourcePosition : NSObject <NSCopying>
{
@protected
	NSInteger _startLine;
	NSInteger _startColumn;
	NSInteger _endLine;
	NSInteger _endColumn;
}

- (instancetype)initWithStartLine:(NSInteger)startLine
		      startColumn:(NSInteger)startColumn
			  endLine:(NSInteger)endLine
			endColumn:(NSInteger)endColumn;

@property (readonly) NSInteger startLine;
@property (readonly) NSInteger startColumn;
@property (readonly) NSInteger endLine;
@property (readonly) NSInteger endColumn;

/* THE POSITION AS A RANGE IN THE STRING IT DESCRIBES — "use this method to access the marked-up region of
 * `string` with an NSRange, rather than making manual calculations based on row and column values", which is
 * exactly what it does: the two positions become UTF-16 indices (the unit every range in this library uses) and
 * the answer spans them.
 *
 * THE END IS EXCLUSIVE, AND THAT IS OURS RATHER THAN APPLE'S: the page states what the method is FOR and not
 * whether the end position is included, so the range runs from the start position up to (and not through) the
 * end position — which composes with the NSRange convention (length = end - start) and makes a whole line
 * `(line, 1)` to `(line, bytes + 1)`. A position past the end of the string is CLAMPED rather than refused, and
 * an end before its start answers an empty range at the start. (§11.6.1 D2.) */
- (NSRange)rangeInString:(NSString *)string;

@end

/* THE THREE DOORS (§62.107), declared where the options they take live. Apple's own pages show them as
 * initialisers OF NSAttributedString and publish neither the header they sit in nor the values of the enums
 * above, so the placement is this library's and the reason is the same one the options class was written down
 * with: a caller compiling against this header finds the whole surface of one subsystem in one place.
 *
 * THE CONTRACT, in Apple's words where they exist: an attributed string carrying the source's SEMANTICS —
 * inline intents and block presentation intents — and NOT its styling; nil plus an error when the source is
 * not valid markdown and the options say a failure should be reported (see the conventions at the parser). */
@interface NSAttributedString (NSAttributedStringMarkdown)
- (nullable instancetype)initWithMarkdown:(NSData *)markdown
                                  options:(nullable NSAttributedStringMarkdownParsingOptions *)options
                                  baseURL:(nullable NSURL *)baseURL
                                    error:(NSError * _Nullable * _Nullable)error;
- (nullable instancetype)initWithMarkdownString:(NSString *)markdownString
                                        options:(nullable NSAttributedStringMarkdownParsingOptions *)options
                                        baseURL:(nullable NSURL *)baseURL
                                          error:(NSError * _Nullable * _Nullable)error;
- (nullable instancetype)initWithContentsOfMarkdownFileAtURL:(NSURL *)url
                                                     options:(nullable NSAttributedStringMarkdownParsingOptions *)options
                                                       error:(NSError * _Nullable * _Nullable)error;
@end

NS_ASSUME_NONNULL_END
