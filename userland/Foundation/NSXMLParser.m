/*
 * NSXMLParser.m — THE EVENT-DRIVEN PARSER, HAND-WRITTEN (W8 slice XML-a): this system has no libxml2, so the
 * single pass over the document's bytes, the entity decoder, the attribute reader and the namespace
 * expansion all live here. See the header for what is measured from Apple and what is ours.
 */

#import <Foundation/NSXMLParser.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <string.h>

NSString *const NSXMLParserErrorDomain = @"NSXMLParserErrorDomain";

/* ---- THE CURSOR OVER THE DOCUMENT'S BYTES ---------------------------------------------------------
 * One struct and a handful of operations on it, so "where the parser is" is a fact of the DATA rather than a
 * set of indices kept in step by hand - and so LINE and COLUMN (Apple publishes both through -lineNumber
 * and -columnNumber, and a caller can ask from inside a callback) are counted as the cursor moves instead
 * of reconstructed afterwards. */
struct fn_xml {
	const char *bytes;
	NSUInteger length;
	NSUInteger at;
	NSInteger line;
	NSInteger column;
};

static void fn_xml_advance(struct fn_xml *x)
{
	if (x->at >= x->length) {
		return;
	}
	if (x->bytes[x->at] == '\n') {
		x->line++;
		x->column = 1;
	} else {
		x->column++;
	}
	x->at++;
}

static int fn_xml_peek(struct fn_xml *x)
{
	return x->at < x->length ? (unsigned char)x->bytes[x->at] : -1;
}

static int fn_xml_peek_at(struct fn_xml *x, NSUInteger offset)
{
	return x->at + offset < x->length ? (unsigned char)x->bytes[x->at + offset] : -1;
}

static BOOL fn_xml_skip(struct fn_xml *x, const char *literal)
{
	size_t n = strlen(literal);
	NSUInteger i;

	if (x->at + n > x->length || memcmp(x->bytes + x->at, literal, n) != 0) {
		return NO;
	}
	for (i = 0; i < n; i++) {
		fn_xml_advance(x);
	}
	return YES;
}

/* A STRING FROM A RANGE THE CURSOR HAS ALREADY WALKED: the consuming helper advances, this one does not -
 * which is what a reader that advanced WHILE it scanned needs. */
static NSString *fn_xml_build(struct fn_xml *x, NSUInteger start, NSUInteger end)
{
	NSData *slice;
	NSString *answer;

	if (end > x->length) {
		end = x->length;
	}
	if (end <= start) {
		return @"";
	}
	/* THROUGH NSData, BECAUSE THAT IS THE CONSTRUCTOR THIS LIBRARY HAS: -[NSString
	 * initWithBytes:length:encoding:] is declared and NOT implemented here, and a call to it raises - which
	 * is how the probe found this out (the guest said so by name, and the parse aborted rather than
	 * misreading the document). */
	slice = [NSData dataWithBytes:(const void *)(x->bytes + start) length:end - start];
	answer = [[NSString alloc] initWithData:slice encoding:NSUTF8StringEncoding];
	return [answer autorelease];
}

static NSString *fn_xml_take(struct fn_xml *x, NSUInteger count)
{
	NSUInteger start = x->at;

	while (count-- > 0) {
		fn_xml_advance(x);
	}
	return fn_xml_build(x, start, x->at);
}

static BOOL fn_xml_is_ws(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void fn_xml_whitespace(struct fn_xml *x)
{
	while (fn_xml_is_ws(fn_xml_peek(x))) {
		fn_xml_advance(x);
	}
}

/* XML's NameStartChar and NameChar, as far as a document this parser accepts needs them: a letter, an
 * underscore or a colon starts a name; digits, dots and hyphens continue it. */
static BOOL fn_xml_is_name_start(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' || c > 0x7f;
}

static BOOL fn_xml_is_name_char(int c)
{
	return fn_xml_is_name_start(c) || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

static NSString *fn_xml_name(struct fn_xml *x)
{
	NSUInteger start = x->at;

	if (!fn_xml_is_name_start(fn_xml_peek(x))) {
		return nil;
	}
	while (fn_xml_is_name_char(fn_xml_peek(x))) {
		fn_xml_advance(x);
	}
	return fn_xml_build(x, start, x->at);
}

/* ---- ENTITIES ------------------------------------------------------------------------------------
 * The five XML declares, plus character references in decimal and hexadecimal. Anything else is REFUSED
 * with the code Apple publishes for it, which is the difference between a parser and a text munger: an
 * undeclared entity is a document that cannot be read, not a word to leave alone. */
#define FN_XML_BAD_REF 1
#define FN_XML_MISSING_SEMI 2
#define FN_XML_UNDECLARED 3

static NSString *fn_xml_decode(NSString *raw, int *outFailure)
{
	NSMutableString *answer = [[NSMutableString alloc] init];
	NSUInteger i, length = [raw length];

	*outFailure = 0;
	for (i = 0; i < length; i++) {
		unichar c = [raw characterAtIndex:i];

		if (c != '&') {
			[answer appendString:[NSString stringWithCharacters:&c length:1]];
			continue;
		}
		{
			NSUInteger semi = i + 1;

			while (semi < length && [raw characterAtIndex:semi] != ';') {
				semi++;
			}
			if (semi >= length) {
				*outFailure = FN_XML_MISSING_SEMI;
				[answer release];
				return nil;
			}
			{
				NSString *name = [raw substringWithRange:NSMakeRange(i + 1, semi - i - 1)];

				if ([name isEqual:@"amp"]) {
					[answer appendString:@"&"];
				} else if ([name isEqual:@"lt"]) {
					[answer appendString:@"<"];
				} else if ([name isEqual:@"gt"]) {
					[answer appendString:@">"];
				} else if ([name isEqual:@"quot"]) {
					[answer appendString:@"\""];
				} else if ([name isEqual:@"apos"]) {
					[answer appendString:@"'"];
				} else if ([name hasPrefix:@"#"]) {
					NSString *digits = [name substringFromIndex:1];
					long value = -1;

					if ([digits length] > 1 &&
					    ([digits hasPrefix:@"x"] || [digits hasPrefix:@"X"])) {
						NSUInteger k;
						value = 0;
						for (k = 1; k < [digits length]; k++) {
							unichar d = [digits characterAtIndex:k];

							if (d >= '0' && d <= '9') value = value * 16 + (d - '0');
							else if (d >= 'a' && d <= 'f') value = value * 16 + (d - 'a' + 10);
							else if (d >= 'A' && d <= 'F') value = value * 16 + (d - 'A' + 10);
							else { value = -1; break; }
						}
					} else if ([digits length] > 0) {
						NSUInteger k;

						value = 0;
						for (k = 0; k < [digits length]; k++) {
							unichar d = [digits characterAtIndex:k];

							if (d >= '0' && d <= '9') value = value * 10 + (d - '0');
							else { value = -1; break; }
						}
					}
					if (value <= 0 || value > 0x10FFFF) {
						*outFailure = FN_XML_BAD_REF;
						[answer release];
						return nil;
					}
					{
						unichar decoded = (unichar)value;

						[answer appendString:[NSString stringWithCharacters:&decoded length:1]];
					}
				} else {
					*outFailure = FN_XML_UNDECLARED;
					[answer release];
					return nil;
				}
			}
			i = semi;
		}
	}
	return [answer autorelease];
}

/* ---- THE PARSER ----------------------------------------------------------------------------------- */
@interface NSXMLParser (FNPrivate)
- (void)fnFail:(NSInteger)code message:(NSString *)message;
- (BOOL)fnAborted;
@end

@implementation NSXMLParser

- (nullable instancetype)initWithData:(NSData *)data
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (data == nil) {
		[self release];
		return nil;
	}
	_data = [data retain];
	_at = 0;
	_line = 1;
	_column = 1;
	return self;
}

- (nullable instancetype)initWithContentsOfURL:(NSURL *)url
{
	NSData *data;

	if (url == nil || ![url isFileURL]) {
		[self release];
		return nil;
	}
	data = [NSData dataWithContentsOfFile:[url path]];
	if (data == nil) {
		[self release];
		return nil;
	}
	return [self initWithData:data];
}

- (nullable id <NSXMLParserDelegate>)delegate
{
	return _delegate;
}

- (void)setDelegate:(nullable id <NSXMLParserDelegate>)delegate
{
	/* NOT RETAINED: Apple's page says a parser does not retain its delegate, and a delegate holding the
	 * parser would otherwise make a cycle. */
	_delegate = delegate;
}

- (BOOL)shouldProcessNamespaces { return _processNamespaces; }
- (void)setShouldProcessNamespaces:(BOOL)processNamespaces { _processNamespaces = processNamespaces; }
- (BOOL)shouldReportNamespacePrefixes { return _reportNamespacePrefixes; }
- (void)setShouldReportNamespacePrefixes:(BOOL)reportNamespacePrefixes { _reportNamespacePrefixes = reportNamespacePrefixes; }
- (BOOL)shouldResolveExternalEntities { return _resolveExternalEntities; }
- (void)setShouldResolveExternalEntities:(BOOL)resolveExternalEntities { _resolveExternalEntities = resolveExternalEntities; }

- (nullable NSError *)parserError
{
	return _parserError;
}

- (NSInteger)lineNumber { return _line; }
- (NSInteger)columnNumber { return _column; }
- (nullable NSString *)publicID { return nil; }
- (nullable NSString *)systemID { return nil; }

- (BOOL)fnAborted
{
	return _aborted;
}

/* ONE PLACE WHERE AN ERROR IS MADE, so every failure path reports the same way: the domain Apple names, the
 * code from the enum, and a description that says WHERE - because "not well balanced" without a line is a
 * message a caller cannot act on. */
- (void)fnFail:(NSInteger)code message:(NSString *)message
{
	NSDictionary *userInfo;

	if (_parserError != nil) {
		return;
	}
	userInfo = [NSDictionary dictionaryWithObject:
			[NSString stringWithFormat:@"line %ld, column %ld: %@",
				(long)_line, (long)_column, message]
						    forKey:NSLocalizedDescriptionKey];
	_parserError = [[NSError alloc] initWithDomain:NSXMLParserErrorDomain code:code userInfo:userInfo];
	_aborted = YES;
}

- (void)abortParsing
{
	_aborted = YES;
	_abortedByDelegate = YES;
}

/* A HELPER FOR EVERY EVENT, so the delegate is asked once and the parser never sends a message to an object
 * that does not implement it. */
#define FN_XML_EVENT(sel)	((_delegate != nil) && [_delegate respondsToSelector:@selector(sel)])

- (BOOL)parse
{
	struct fn_xml x;
	NSMutableArray *stack = [[NSMutableArray alloc] init];	/* one frame per OPEN element */
	BOOL ok = YES;

	if (_data == nil || [_data length] == 0) {
		[stack release];
		[self fnFail:NSXMLParserEmptyDocumentError message:@"the document is empty"];
		return NO;
	}
	/* THE BYTES MUST BE UTF-8, and this is checked ONCE rather than per token: every other reading in this
	 * file would answer nil for a document in an encoding this system cannot decode, and a parse that
	 * quietly reports nothing is worse than one that refuses. */
	if ([[NSString alloc] initWithData:_data encoding:NSUTF8StringEncoding] == nil) {
		[stack release];
		[self fnFail:NSXMLParserEncodingNotSupportedError
		     message:@"the document's bytes are not UTF-8"];
		return NO;
	}
	x.bytes = (const char *)[_data bytes];
	x.length = [_data length];
	x.at = 0;
	x.line = 1;
	x.column = 1;
	/* THE MACRO TAKES THE SELECTOR AS WRITTEN, COLON INCLUDED - which is why the first version of this file
	 * never fired the two document events: it passed "parserDidStartDocument" and asked the delegate whether
	 * it responded to a selector with no argument. The probe's event log is what made that visible (the
	 * first entry was an element, not a document). */
	if (FN_XML_EVENT(parserDidStartDocument:)) {
		[_delegate parserDidStartDocument:self];
	}
	while (!_aborted && fn_xml_peek(&x) != -1) {
		if (fn_xml_peek(&x) != '<') {
			/* TEXT: everything up to the next '<', with its entities decoded. Apple's own pages allow the
			 * characters to arrive in pieces; one call per run is the shape this parser uses. */
			NSUInteger start = x.at;
			NSString *raw;
			NSString *decoded;
			int failure = 0;

			while (fn_xml_peek(&x) != -1 && fn_xml_peek(&x) != '<') {
				fn_xml_advance(&x);
			}
			raw = fn_xml_build(&x, start, x.at);
			decoded = fn_xml_decode(raw, &failure);
			if (decoded == nil) {
				[self fnFail:(failure == FN_XML_MISSING_SEMI ?
						NSXMLParserEntityReferenceMissingSemiError :
						failure == FN_XML_BAD_REF ?
						NSXMLParserInvalidCharacterRefError :
						NSXMLParserUndeclaredEntityError)
				     message:[NSString stringWithFormat:@"in the text %@", raw]];
				break;
			}
			if ([decoded length] > 0 && FN_XML_EVENT(parser:foundCharacters:)) {
				[_delegate parser:self foundCharacters:decoded];
			}
			continue;
		}
		/* '<' - WHICH OF THE FIVE THINGS IT STARTS. */
		fn_xml_advance(&x);
		if (fn_xml_skip(&x, "!--")) {
			/* A COMMENT, and "--" inside one is the one thing XML forbids there. */
			NSMutableString *text = [[NSMutableString alloc] init];
			BOOL closed = NO;
			BOOL doubled = NO;

			while (fn_xml_peek(&x) != -1) {
				if (fn_xml_skip(&x, "-->")) {
					closed = YES;
					break;
				}
				if (fn_xml_peek(&x) == '-' && fn_xml_peek_at(&x, 1) == '-') {
					doubled = YES;
				}
				[text appendString:fn_xml_take(&x, 1)];
			}
			if (!closed) {
				[self fnFail:NSXMLParserCommentNotFinishedError message:@"a comment is not closed"];
			} else if (doubled) {
				[self fnFail:NSXMLParserCommentContainsDoubleHyphenError
				     message:@"a comment contains a double hyphen"];
			} else if (FN_XML_EVENT(parser:foundComment:)) {
				[_delegate parser:self foundComment:text];
			}
			[text release];
			continue;
		}
		if (fn_xml_skip(&x, "![CDATA[")) {
			NSUInteger start = x.at;
			BOOL closed = NO;

			while (fn_xml_peek(&x) != -1) {
				if (fn_xml_skip(&x, "]]>")) {
					closed = YES;
					break;
				}
				fn_xml_advance(&x);
			}
			if (!closed) {
				[self fnFail:NSXMLParserCDATANotFinishedError message:@"a CDATA section is not closed"];
			} else if (FN_XML_EVENT(parser:foundCDATA:)) {
				NSData *block = [NSData dataWithBytes:(const void *)(x.bytes + start)
							       length:(x.at - 3 - start)];

				[_delegate parser:self foundCDATA:block];
			}
			continue;
		}
		if (fn_xml_skip(&x, "!DOCTYPE")) {
			/* THE DTD IS DECLARED AND NOT INTERPRETED (slice XML-c): the internal subset is skipped as a
			 * balanced bracket region, so a document with one parses and its DTD DECLARATIONS produce no
			 * events here rather than producing wrong ones. */
			int depth = 0;

			while (fn_xml_peek(&x) != -1) {
				int c = fn_xml_peek(&x);

				if (c == '[') depth++;
				if (c == ']') depth--;
				if (c == '>' && depth <= 0) { fn_xml_advance(&x); break; }
				fn_xml_advance(&x);
			}
			continue;
		}
		if (fn_xml_skip(&x, "!")) {
			[self fnFail:NSXMLParserInvalidCharacterError
			     message:@"a declaration this parser does not know"];
			continue;
		}
		if (fn_xml_peek(&x) == '?') {
			NSString *target;
			NSString *payload = nil;
			BOOL closed = NO;
			NSUInteger start;

			fn_xml_advance(&x);
			target = fn_xml_name(&x);
			if (target == nil) {
				[self fnFail:NSXMLParserInvalidCharacterError
				     message:@"a processing instruction has no target"];
				continue;
			}
			fn_xml_whitespace(&x);
			start = x.at;
			while (fn_xml_peek(&x) != -1) {
				if (fn_xml_peek(&x) == '?' && fn_xml_peek_at(&x, 1) == '>') {
					payload = fn_xml_build(&x, start, x.at);
					fn_xml_advance(&x);
					fn_xml_advance(&x);
					closed = YES;
					break;
				}
				fn_xml_advance(&x);
			}
			if (!closed) {
				[self fnFail:NSXMLParserUnfinishedTagError
				     message:@"a processing instruction is not closed"];
			} else if (![target isEqual:@"xml"] && FN_XML_EVENT(parser:foundProcessingInstructionWithTarget:data:)) {
				/* THE XML DECLARATION IS NOT REPORTED AS A PROCESSING INSTRUCTION, which is what a caller
				 * expects: it declares the document's version and encoding rather than its content. */
				[_delegate parser:self
				    foundProcessingInstructionWithTarget:target
								    data:([payload length] > 0 ? payload : nil)];
			}
			continue;
		}
		if (fn_xml_peek(&x) == '/') {
			/* AN END TAG: the name must match the open element, which is what well-formedness IS. */
			NSString *name;

			fn_xml_advance(&x);
			name = fn_xml_name(&x);
			fn_xml_whitespace(&x);
			if (name == nil) {
				[self fnFail:NSXMLParserInvalidCharacterError message:@"an end tag has no name"];
				continue;
			}
			if (!fn_xml_skip(&x, ">")) {
				[self fnFail:NSXMLParserGTRequiredError
				     message:[NSString stringWithFormat:@"the end tag </%@ is not closed", name]];
				continue;
			}
			if ([stack count] == 0) {
				[self fnFail:NSXMLParserNotWellBalancedError
				     message:[NSString stringWithFormat:@"</%@> closes nothing", name]];
				continue;
			}
			{
				NSArray *frame = [stack lastObject];
				NSString *open = [frame objectAtIndex:0];

				if (![open isEqual:name]) {
					[self fnFail:NSXMLParserTagNameMismatchError
					     message:[NSString stringWithFormat:@"</%@> closes <%@>", name, open]];
					continue;
				}
				if (FN_XML_EVENT(parser:didEndElement:namespaceURI:qualifiedName:)) {
					[_delegate parser:self
					    didEndElement:name
					     namespaceURI:[frame objectAtIndex:1]
					    qualifiedName:[frame objectAtIndex:2]];
				}
				if (_reportNamespacePrefixes) {
					NSArray *prefixes = [frame objectAtIndex:3];
					NSUInteger p;

					for (p = 0; p < [prefixes count]; p++) {
						if (FN_XML_EVENT(parser:didEndMappingPrefix:)) {
							[_delegate parser:self
							    didEndMappingPrefix:[prefixes objectAtIndex:p]];
						}
					}
				}
				[stack removeLastObject];
			}
			continue;
		}
		{
			/* A START TAG: its name, then its attributes, then either '>' or '/>'. The NAMESPACE work
			 * happens after the attributes are read, because the declarations ARE attributes. */
			NSString *qualified = fn_xml_name(&x);
			NSMutableDictionary *attributes;
			NSMutableArray *mapped;
			BOOL selfClosing = NO;
			BOOL closed = NO;
			NSString *elementName;
			NSString *namespaceURI = nil;

			if (qualified == nil) {
				[self fnFail:NSXMLParserInvalidCharacterError message:@"a tag with no name"];
				continue;
			}
			attributes = [[NSMutableDictionary alloc] init];
			for (;;) {
				NSString *attribute;

				fn_xml_whitespace(&x);
				if (fn_xml_skip(&x, "/>")) { selfClosing = YES; closed = YES; break; }
				if (fn_xml_skip(&x, ">")) { closed = YES; break; }
				if (fn_xml_peek(&x) == -1) {
					break;
				}
				attribute = fn_xml_name(&x);
				if (attribute == nil) {
					[self fnFail:NSXMLParserInvalidCharacterError
					     message:[NSString stringWithFormat:@"inside <%@>", qualified]];
					break;
				}
				fn_xml_whitespace(&x);
				if (!fn_xml_skip(&x, "=")) {
					[self fnFail:NSXMLParserEqualExpectedError
					     message:[NSString stringWithFormat:@"after the attribute %@", attribute]];
					break;
				}
				fn_xml_whitespace(&x);
				{
					int quote = fn_xml_peek(&x);
					NSUInteger start;
					NSString *value;
					int failure = 0;

					if (quote != '"' && quote != '\'') {
						[self fnFail:NSXMLParserAttributeHasNoValueError
						     message:[NSString stringWithFormat:@"the attribute %@", attribute]];
						break;
					}
					fn_xml_advance(&x);
					start = x.at;
					while (fn_xml_peek(&x) != -1 && fn_xml_peek(&x) != quote) {
						fn_xml_advance(&x);
					}
					if (fn_xml_peek(&x) == -1) {
						[self fnFail:NSXMLParserUnfinishedTagError
						     message:[NSString stringWithFormat:@"the value of %@", attribute]];
						break;
					}
					value = fn_xml_decode(fn_xml_build(&x, start, x.at), &failure);
					fn_xml_advance(&x);
					if (value == nil) {
						[self fnFail:(failure == FN_XML_BAD_REF ?
								NSXMLParserInvalidCharacterRefError :
								failure == FN_XML_MISSING_SEMI ?
								NSXMLParserEntityReferenceMissingSemiError :
								NSXMLParserUndeclaredEntityError)
						     message:[NSString stringWithFormat:@"the value of %@", attribute]];
						break;
					}
					[attributes setObject:value forKey:attribute];
				}
			}
			if (_aborted || !closed) {
				if (!_aborted) {
					[self fnFail:NSXMLParserGTRequiredError
					     message:[NSString stringWithFormat:@"<%@ is not closed", qualified]];
				}
				[attributes release];
				continue;
			}
			/* THE NAMESPACE EXPANSION, which only runs when the caller asked for it: the element name is
			 * then the LOCAL part, the URI comes from the matching declaration, and the qualified name is
			 * what the document wrote. */
			mapped = [[NSMutableArray alloc] init];
			elementName = qualified;
			if (_processNamespaces) {
				NSRange colon = [qualified rangeOfString:@":"];
				NSString *prefix = colon.location == NSNotFound ? nil :
						   [qualified substringToIndex:colon.location];
				NSString *local = colon.location == NSNotFound ? qualified :
						  [qualified substringFromIndex:colon.location + 1];
				NSString *declared = [attributes objectForKey:
						prefix == nil ? @"xmlns" :
						[NSString stringWithFormat:@"xmlns:%@", prefix]];

				elementName = local;
				namespaceURI = declared;
				if (_reportNamespacePrefixes && declared != nil) {
					if (FN_XML_EVENT(parser:didStartMappingPrefix:toURI:)) {
						[_delegate parser:self
						    didStartMappingPrefix:(prefix != nil ? prefix : @"")
								    toURI:declared];
					}
					[mapped addObject:(prefix != nil ? prefix : @"")];
				}
			} else if (_reportNamespacePrefixes) {
				/* EVEN WITHOUT NAMESPACE PROCESSING, the declarations are announced when the caller asked
				 * to hear about prefixes - which is what "report" means. */
				NSArray *names = [attributes allKeys];
				NSUInteger k;

				for (k = 0; k < [names count]; k++) {
					NSString *name = [names objectAtIndex:k];

					if ([name isEqual:@"xmlns"] || [name hasPrefix:@"xmlns:"]) {
						NSString *prefix = [name isEqual:@"xmlns"] ? @"" :
								   [name substringFromIndex:6];

						if (FN_XML_EVENT(parser:didStartMappingPrefix:toURI:)) {
							[_delegate parser:self
							    didStartMappingPrefix:prefix
									    toURI:[attributes objectForKey:name]];
						}
						[mapped addObject:prefix];
					}
				}
			}
			[stack addObject:[NSArray arrayWithObjects:qualified, namespaceURI != nil ? namespaceURI : (id)[NSNull null],
						qualified, mapped, nil]];
			if (FN_XML_EVENT(parser:didStartElement:namespaceURI:qualifiedName:attributes:)) {
				[_delegate parser:self
				    didStartElement:elementName
				      namespaceURI:namespaceURI
				     qualifiedName:(_processNamespaces ? qualified : nil)
					attributes:attributes];
			}
			[mapped release];
			[attributes release];
			if (selfClosing) {
				/* A SELF-CLOSING TAG IS A START AND AN END, which is what a caller writing a handler per
				 * element expects and what the document means. */
				NSArray *frame = [stack lastObject];

				if (FN_XML_EVENT(parser:didEndElement:namespaceURI:qualifiedName:)) {
					[_delegate parser:self
					    didEndElement:elementName
					     namespaceURI:[frame objectAtIndex:1] == [NSNull null] ? nil :
							[frame objectAtIndex:1]
					    qualifiedName:(_processNamespaces ? [frame objectAtIndex:2] : nil)];
				}
				if (_reportNamespacePrefixes) {
					NSArray *prefixes = [frame objectAtIndex:3];
					NSUInteger p;

					for (p = 0; p < [prefixes count]; p++) {
						if (FN_XML_EVENT(parser:didEndMappingPrefix:)) {
							[_delegate parser:self
							    didEndMappingPrefix:[prefixes objectAtIndex:p]];
						}
					}
				}
				[stack removeLastObject];
			}
			_line = x.line;
			_column = x.column;
			continue;
		}
	}
	_line = x.line;
	_column = x.column;
	if (_aborted) {
		if (_abortedByDelegate && _parserError == nil) {
			ok = NO;
			[self fnFail:NSXMLParserDelegateAbortedParseError
			     message:@"the delegate aborted the parse"];
		} else {
			ok = NO;
		}
	} else if ([stack count] > 0) {
		ok = NO;
		[self fnFail:NSXMLParserUnfinishedTagError
		     message:[NSString stringWithFormat:@"<%@ is never closed",
				[[stack lastObject] objectAtIndex:0]]];
	}
	if (!ok) {
		if (FN_XML_EVENT(parser:parseErrorOccurred:)) {
			[_delegate parser:self parseErrorOccurred:_parserError];
		}
		[stack release];
		return NO;
	}
	if (FN_XML_EVENT(parserDidEndDocument:)) {
		[_delegate parserDidEndDocument:self];
	}
	[stack release];
	return YES;
}

- (void)dealloc
{
	[_data release];
	[_parserError release];
	[super dealloc];
}

@end
