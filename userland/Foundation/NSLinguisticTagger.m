/*
 * NSLinguisticTagger.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * The design, the refusals and their grounds are in NSLinguisticTagger.h. What is here is the vocabulary, the
 * break-iterator bridge's two questions turned into answers, and the two schemes that are properties of the text.
 *
 * THE TAG VALUES ARE OURS UNDER §11.6.1 D2, AND THEY ARE ALSO THE OBVIOUS SPELLINGS: Apple publishes the NAMES
 * ("Word", "LexicalClass", "PersonalName") and a tag is compared BY EQUALITY, so what matters is that this header
 * and this implementation agree — which is why the probe PINS EVERY ONE OF THEM rather than sampling.
 */
#import <Foundation/NSLinguisticTagger.h>
#import <Foundation/FNTextBreaking.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#import <Foundation/NSOrthography.h>
#import <Foundation/NSGeometry.h>

#include <stdlib.h>

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utypes.h>

/* --- THE SCHEMES ------------------------------------------------------------------------------------ */

NSLinguisticTagScheme const NSLinguisticTagSchemeTokenType = @"TokenType";
NSLinguisticTagScheme const NSLinguisticTagSchemeLexicalClass = @"LexicalClass";
NSLinguisticTagScheme const NSLinguisticTagSchemeNameType = @"NameType";
NSLinguisticTagScheme const NSLinguisticTagSchemeNameTypeOrLexicalClass = @"NameTypeOrLexicalClass";
NSLinguisticTagScheme const NSLinguisticTagSchemeLemma = @"Lemma";
NSLinguisticTagScheme const NSLinguisticTagSchemeLanguage = @"Language";
NSLinguisticTagScheme const NSLinguisticTagSchemeScript = @"Script";

/* --- THE TOKEN TYPES, AND THEIR REFINEMENTS --------------------------------------------------------- */

NSLinguisticTag const NSLinguisticTagWord = @"Word";
NSLinguisticTag const NSLinguisticTagWhitespace = @"Whitespace";
NSLinguisticTag const NSLinguisticTagPunctuation = @"Punctuation";
NSLinguisticTag const NSLinguisticTagOther = @"Other";

NSLinguisticTag const NSLinguisticTagSentenceTerminator = @"SentenceTerminator";
NSLinguisticTag const NSLinguisticTagOpenQuote = @"OpenQuote";
NSLinguisticTag const NSLinguisticTagCloseQuote = @"CloseQuote";
NSLinguisticTag const NSLinguisticTagOpenParenthesis = @"OpenParenthesis";
NSLinguisticTag const NSLinguisticTagCloseParenthesis = @"CloseParenthesis";
NSLinguisticTag const NSLinguisticTagWordJoiner = @"WordJoiner";
NSLinguisticTag const NSLinguisticTagDash = @"Dash";
NSLinguisticTag const NSLinguisticTagOtherPunctuation = @"OtherPunctuation";
NSLinguisticTag const NSLinguisticTagParagraphBreak = @"ParagraphBreak";
NSLinguisticTag const NSLinguisticTagOtherWhitespace = @"OtherWhitespace";
NSLinguisticTag const NSLinguisticTagOtherWord = @"OtherWord";

/* --- THE LEXICAL CLASSES AND THE NAME TYPES: NAMED, NEVER PRODUCED HERE ------------------------------ */

NSLinguisticTag const NSLinguisticTagNoun = @"Noun";
NSLinguisticTag const NSLinguisticTagVerb = @"Verb";
NSLinguisticTag const NSLinguisticTagAdjective = @"Adjective";
NSLinguisticTag const NSLinguisticTagAdverb = @"Adverb";
NSLinguisticTag const NSLinguisticTagPronoun = @"Pronoun";
NSLinguisticTag const NSLinguisticTagDeterminer = @"Determiner";
NSLinguisticTag const NSLinguisticTagParticle = @"Particle";
NSLinguisticTag const NSLinguisticTagPreposition = @"Preposition";
NSLinguisticTag const NSLinguisticTagNumber = @"Number";
NSLinguisticTag const NSLinguisticTagConjunction = @"Conjunction";
NSLinguisticTag const NSLinguisticTagInterjection = @"Interjection";
NSLinguisticTag const NSLinguisticTagClassifier = @"Classifier";
NSLinguisticTag const NSLinguisticTagIdiom = @"Idiom";

NSLinguisticTag const NSLinguisticTagPersonalName = @"PersonalName";
NSLinguisticTag const NSLinguisticTagPlaceName = @"PlaceName";
NSLinguisticTag const NSLinguisticTagOrganizationName = @"OrganizationName";

/* --- THE TWO SCHEMES THIS LIBRARY ANSWERS ----------------------------------------------------------- */

/* THE COARSE CLASSIFICATION, PLUS THE REFINEMENTS THE VOCABULARY ALREADY CARRIES. A token that is entirely
 * whitespace is Whitespace (or OtherWhitespace when it is not a plain space or tab, which is what makes
 * `ParagraphBreak` reachable at all); a letter or a mark makes a Word; punctuation and symbols are refined by
 * their first character, and EVERYTHING ELSE IS Other — including a number, which Apple's TokenType scheme also
 * calls Other rather than giving it a class of its own. */
static NSLinguisticTag fn_token_type(const UChar *buffer, int32_t start, int32_t end, int32_t length)
{
	int32_t i;
	int32_t kinds = 0;
	UChar first;

	if (end <= start) {
		return NSLinguisticTagOther;
	}
	first = buffer[start];
	for (i = start; i < end; i++) {
		if (!u_isUWhiteSpace(buffer[i]) && !u_iscntrl(buffer[i])) {
			kinds++;
		}
	}
	if (kinds == 0) {
		for (i = start; i < end; i++) {
			if (buffer[i] == 0x000a || buffer[i] == 0x000d) {
				return NSLinguisticTagParagraphBreak;
			}
		}
		return NSLinguisticTagWhitespace;
	}
	if (u_isalpha(first) || u_charType(first) == U_NON_SPACING_MARK ||
	    u_charType(first) == U_COMBINING_SPACING_MARK) {
		return NSLinguisticTagWord;
	}
	switch (first) {
	case '.': case '!': case '?': case 0x3002: case 0xff01: case 0xff1f:
		return NSLinguisticTagSentenceTerminator;
	/* THE STRAIGHT QUOTE HAS NO DIRECTION OF ITS OWN, SO THE RULE IS CONTEXTUAL: a quote preceded by whitespace
	 * (or by nothing) OPENS, and one preceded by anything else CLOSES. That is the rule Apple's own documentation
	 * describes for the typographic pair, applied where a character cannot decide for itself - and the first
	 * version of this classifier answered OpenQuote for both, which a probe caught by printing the sequence it
	 * saw ("OpenQuote Word OpenQuote") rather than merely reporting that something was wrong. */
	case '"': case '\'':
		if (start == 0 || u_isUWhiteSpace(buffer[start - 1])) {
			return NSLinguisticTagOpenQuote;
		}
		return NSLinguisticTagCloseQuote;
	case 0x201c: case 0x2018: case 0xab:
		return NSLinguisticTagOpenQuote;
	case 0x201d: case 0x2019: case 0xbb:
		return NSLinguisticTagCloseQuote;
	case '(': case '[': case '{': case 0x300c:
		return NSLinguisticTagOpenParenthesis;
	case ')': case ']': case '}': case 0x300d:
		return NSLinguisticTagCloseParenthesis;
	case '-': case 0x2010: case 0x2013: case 0x2014:
		return NSLinguisticTagDash;
	case 0x200b: case 0x2060: case 0xfeff:
		return NSLinguisticTagWordJoiner;
	default:
		break;
	}
	if (u_ispunct(first) || u_charType(first) == U_MATH_SYMBOL ||
	    u_charType(first) == U_CURRENCY_SYMBOL || u_charType(first) == U_MODIFIER_SYMBOL) {
		return NSLinguisticTagPunctuation;
	}
	return NSLinguisticTagOther;
}

/* THE SCRIPT, AS ICU NAMES IT: `uscript_getScript` for the token's first character and `uscript_getShortName` for
 * the tag ("Latn", "Cyrl", "Han"). THE SHORT NAME IS A CHOICE, NOT A FIDELITY CLAIM: Apple does not publish the
 * spellings its Script tags take, so under D2 this is ours — and a caller comparing against a literal has a
 * published vocabulary of exactly these seven tags in the header rather than a guess about ours. */
static NSLinguisticTag fn_token_script(const UChar *buffer, int32_t start, int32_t end)
{
	UErrorCode status = U_ZERO_ERROR;
	UScriptCode code;
	int32_t i;

	if (end <= start) {
		return nil;
	}
	for (i = start; i < end; i++) {
		if (u_isUWhiteSpace(buffer[i])) {
			continue;
		}
		code = uscript_getScript((UChar32)buffer[i], &status);
		if (U_FAILURE(status)) {
			return nil;
		}
		return [NSString stringWithUTF8String:uscript_getShortName(code)];
	}
	return nil;
}

@implementation NSLinguisticTagger

+ (NSArray *)availableTagSchemesForUnit:(NSLinguisticTaggerUnit)unit
						       language:(NSString *)language
{
	(void)language;
	/* APPLE'S DOOR, USED FOR THE THING IT IS FOR: what this tagger can answer, before anything is built. THE TWO
	 * SCHEMES ARE PROPERTIES OF A TOKEN, so the WORD unit — and only it — has them; a sentence has no token type
	 * and no script, and saying so here is better than answering nil from every later call. The morphological
	 * schemes are absent from every list, which is where their refusal becomes readable. */
	if (unit == NSLinguisticTaggerUnitWord) {
		return [NSArray arrayWithObjects:NSLinguisticTagSchemeTokenType,
						NSLinguisticTagSchemeScript, nil];
	}
	return [NSArray array];
}

- (instancetype)initWithTagSchemes:(NSArray *)tagSchemes options:(NSUInteger)options
{
	self = [super init];
	if (self != nil) {
		_tagSchemes = [tagSchemes copy];
		_options = options;
		_orthographyRange = NSMakeRange(0, 0);
	}
	return self;
}

- (NSArray *)tagSchemes { return _tagSchemes; }
- (NSString *)string { return _string; }

- (void)setString:(NSString *)string
{
	NSString *old = _string;

	_string = [string copy];
	[old release];
}

- (void)setOrthography:(NSOrthography *)orthography range:(NSRange)range
{
	NSOrthography *old = _orthography;

	_orthography = [orthography copy];
	[old release];
	_orthographyRange = range;
}

/* THE EFFECTIVE RANGE IS THE ONE THE ORTHOGRAPHY WAS SET FOR, not a recomputed one: this slice holds ONE
 * orthography, and saying that plainly beats inventing a second stored range to hand back. */
- (NSOrthography *)orthographyAtIndex:(NSUInteger)charIndex effectiveRange:(NSRangePointer)effectiveRange
{
	if (effectiveRange != NULL) {
		*effectiveRange = _orthographyRange;
	}
	if (charIndex < _orthographyRange.location ||
	    charIndex >= (_orthographyRange.location + _orthographyRange.length)) {
		return nil;
	}
	return _orthography;
}

/* --- THE TAGS --------------------------------------------------------------------------------------- */

- (NSLinguisticTag)fnTagForScheme:(NSLinguisticTagScheme)scheme
			    string:(NSString *)string
			   tokenStart:(NSUInteger)tokenStart
			     tokenEnd:(NSUInteger)tokenEnd
{
	NSUInteger length = [string length];
	UChar *buffer;
	NSLinguisticTag tag = nil;

	if (tokenEnd > length || tokenEnd <= tokenStart) {
		return nil;
	}
	buffer = (UChar *)malloc(sizeof(UChar) * length);
	if (buffer == NULL) {
		return nil;
	}
	[string getCharacters:buffer range:NSMakeRange(0, length)];
	if ([scheme isEqualToString:NSLinguisticTagSchemeTokenType]) {
		tag = fn_token_type(buffer, (int32_t)tokenStart, (int32_t)tokenEnd, (int32_t)length);
	} else if ([scheme isEqualToString:NSLinguisticTagSchemeScript]) {
		tag = fn_token_script(buffer, (int32_t)tokenStart, (int32_t)tokenEnd);
	}
	/* EVERY OTHER SCHEME ANSWERS NIL, WHICH IS THE REFUSAL IN ITS ACTIVE FORM: the morphological schemes have no
	 * tagger behind them and the language scheme has no identifier behind it (see the header). */
	free(buffer);
	return tag;
}

- (NSLinguisticTag)tagAtIndex:(NSUInteger)charIndex
			scheme:(NSLinguisticTagScheme)scheme
		    tokenRange:(NSRangePointer)tokenRange
		 sentenceRange:(NSRangePointer)sentenceRange
{
	NSRange token = NSMakeRange(NSNotFound, 0);

	if (_string == nil || charIndex >= [_string length]) {
		return nil;
	}
	token = [FNTextBreaking fnUnitContaining:FNTextUnitWord inString:_string atIndex:charIndex];
	if (tokenRange != NULL) {
		*tokenRange = token;
	}
	if (sentenceRange != NULL) {
		*sentenceRange = [self sentenceRangeForRange:NSMakeRange(charIndex, 0)];
	}
	if (token.location == NSNotFound) {
		return nil;
	}
	return [self fnTagForScheme:scheme
			     string:_string
			 tokenStart:token.location
			   tokenEnd:(token.location + token.length)];
}

/* THE OMISSIONS, APPLIED TO A TAG: three of them drop TOKENS by their type, and `OmitWords` drops the tags for
 * word tokens while keeping the tokens themselves (Apple's arrangement, and the reason it is a separate bit). */
- (BOOL)fnShouldOmitTag:(NSLinguisticTag)tag options:(NSLinguisticTaggerOptions)options
{
	if (tag == nil) {
		return NO;
	}
	if ((options & NSLinguisticTaggerOmitWhitespace) &&
	    ([tag isEqualToString:NSLinguisticTagWhitespace] ||
	     [tag isEqualToString:NSLinguisticTagOtherWhitespace] ||
	     [tag isEqualToString:NSLinguisticTagParagraphBreak])) {
		return YES;
	}
	if ((options & NSLinguisticTaggerOmitPunctuation) &&
	    ([tag isEqualToString:NSLinguisticTagPunctuation] ||
	     [tag isEqualToString:NSLinguisticTagSentenceTerminator] ||
	     [tag isEqualToString:NSLinguisticTagOpenQuote] ||
	     [tag isEqualToString:NSLinguisticTagCloseQuote] ||
	     [tag isEqualToString:NSLinguisticTagOpenParenthesis] ||
	     [tag isEqualToString:NSLinguisticTagCloseParenthesis] ||
	     [tag isEqualToString:NSLinguisticTagDash] ||
	     [tag isEqualToString:NSLinguisticTagOtherPunctuation])) {
		return YES;
	}
	if ((options & NSLinguisticTaggerOmitOther) && [tag isEqualToString:NSLinguisticTagOther]) {
		return YES;
	}
	return NO;
}

- (void)enumerateTagsInRange:(NSRange)range
			unit:(NSLinguisticTaggerUnit)unit
		      scheme:(NSLinguisticTagScheme)scheme
		     options:(NSLinguisticTaggerOptions)options
		  usingBlock:(void (^)(NSLinguisticTag, NSRange, BOOL *))block
{
	FNTextUnit bridgingUnit;
	NSString *string = _string;

	if (block == nil || string == nil) {
		return;
	}
	switch (unit) {
	case NSLinguisticTaggerUnitSentence:
		bridgingUnit = FNTextUnitSentence;
		break;
	case NSLinguisticTaggerUnitParagraph:
		bridgingUnit = FNTextUnitParagraph;
		break;
	case NSLinguisticTaggerUnitDocument:
		/* A DOCUMENT IS THE WHOLE STRING: one unit, and the bridge is asked for nothing. */
		if (range.length > 0) {
			BOOL stop = NO;

			block([self fnTagForScheme:scheme string:string tokenStart:range.location
					 tokenEnd:(range.location + range.length)], range, &stop);
		}
		return;
	case NSLinguisticTaggerUnitWord:
	default:
		bridgingUnit = FNTextUnitWord;
		break;
	}
	[FNTextBreaking fnEnumerate:bridgingUnit
			   inString:string
			      range:range
			 usingBlock:^(NSRange unitRange, BOOL *stop) {
		NSLinguisticTag tag = [self fnTagForScheme:scheme
						    string:string
						tokenStart:unitRange.location
						  tokenEnd:(unitRange.location + unitRange.length)];

		if ((options & NSLinguisticTaggerOmitWords) &&
		    [tag isEqualToString:NSLinguisticTagWord]) {
			tag = nil;
		}
		if ([self fnShouldOmitTag:tag options:options]) {
			return;
		}
		block(tag, unitRange, stop);
	}];
}

- (void)enumerateTagsInRange:(NSRange)range
		       scheme:(NSLinguisticTagScheme)scheme
		      options:(NSLinguisticTaggerOptions)options
		   usingBlock:(void (^)(NSLinguisticTag, NSRange, BOOL *))block
{
	[self enumerateTagsInRange:range
			      unit:NSLinguisticTaggerUnitWord
			    scheme:scheme
			   options:options
			usingBlock:block];
}

- (NSRange)sentenceRangeForRange:(NSRange)range
{
	if (_string == nil || range.location >= [_string length]) {
		return NSMakeRange(NSNotFound, 0);
	}
	return [FNTextBreaking fnUnitContaining:FNTextUnitSentence
				       inString:_string
					atIndex:range.location];
}

- (void)dealloc
{
	[_tagSchemes release];
	[_string release];
	[_orthography release];
	[super dealloc];
}

@end
