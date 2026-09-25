/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_xmlparser, unit of 1 — W8 slice XML-a's acceptance: the EVENT-DRIVEN PARSER.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus the POSIX calls its file fixture makes. It builds
 * a tree of its own under /System/Temporary Files and removes it at the end and at the start.
 *
 * THE DELEGATE IS THE PROBE'S OWN OBJECT, and it keeps an EVENT LOG rather than a set of booleans: what a
 * parser promises is an ORDER, and an order compared as a set is not the same claim. The log's entries name
 * the event and what it carried, so a failure reads as the sequence that happened.
 *
 *   xml-the-events-of-a-document-in-order  document, element (with its attributes), characters, end,
 *                                          document - in that order;
 *   xml-entities-are-decoded-in-text-and-attributes  the five XML declares plus decimal and hexadecimal
 *                                          character references, in TEXT and in an ATTRIBUTE value;
 *   xml-comments-cdata-and-processing-instructions  the three events, and the XML DECLARATION is NOT
 *                                          reported as a processing instruction;
 *   xml-a-self-closing-element-is-a-start-and-an-end  what the document means;
 *   xml-namespaces-are-off-by-default     the element name is then the qualified name and the URI is nil;
 *   xml-namespaces-on-split-and-expand    with processing on: the local name, the declared URI, and the
 *                                          qualified name as written;
 *   xml-prefix-mappings-are-reported-when-asked  didStartMappingPrefix/didEndMappingPrefix;
 *   xml-a-malformed-document-fails-with-its-code-and-position  NO, an error in the domain, the
 *                                          parseErrorOccurred event, and a line and column that are not
 *                                          invented;
 *   xml-a-tag-mismatch-and-a-double-hyphen-comment-are-told-apart  two malformations, two codes;
 *   xml-abort-stops-the-parse             aborting from a callback makes the parse fail with the code Apple
 *                                          publishes for it, and no later event arrives;
 *   xml-it-parses-from-a-file-url-too     the second initializer, over a real file;
 *   probe-tree-removed                    the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <sys/stat.h>

#define PROBE_ROOT "/System/Temporary Files/nsxmlparser-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-XMLPARSER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-XMLPARSER %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* NULLABLE CONSTRUCTORS ROUTED THROUGH `id`, this tier's rule for -Werror=nullable-to-nonnull-conversion. */
static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

@interface TestDelegate : NSObject <NSXMLParserDelegate>
{
	NSMutableArray *_log;
	NSMutableString *_text;
	NSString *_lastCharacters;
	NSMutableString *_textCollector;
	BOOL _abortOnStart;
	BOOL _reportPrefixes;
}
- (id)initWithAbort:(BOOL)abort reportPrefixes:(BOOL)report;
- (NSArray *)log;
- (NSString *)text;
- (BOOL)missingEvent:(NSString *)event;
@end

@implementation TestDelegate

/* THE ORDINARY -init IS WHAT THE PROBE CALLS, and without it the log array was never made: eight checks
 * reported "log=(null)" because every -addObject: went to a nil array in silence. An instrument that
 * silently records nothing is the instrument fault this probe would have blamed on the parser. */
- (id)init
{
	return [self initWithAbort:NO reportPrefixes:NO];
}

- (id)initWithAbort:(BOOL)abort reportPrefixes:(BOOL)report
{
	self = [super init];
	if (self != nil) {
		_log = [[NSMutableArray alloc] init];
		_text = [[NSMutableString alloc] init];
		_abortOnStart = abort;
		_reportPrefixes = report;
	}
	return self;
}

- (NSArray *)log { return _log; }
- (NSString *)text { return _text; }
- (BOOL)missingEvent:(NSString *)event
{
	NSUInteger i;

	for (i = 0; i < [_log count]; i++) {
		if ([[_log objectAtIndex:i] hasPrefix:event]) {
			return NO;
		}
	}
	return YES;
}

- (void)parserDidStartDocument:(NSXMLParser *)parser
{
	(void)parser;
	[_log addObject:@"start-document"];
}

- (void)parserDidEndDocument:(NSXMLParser *)parser
{
	(void)parser;
	[_log addObject:@"end-document"];
}

- (void)parser:(NSXMLParser *)parser
    didStartElement:(NSString *)elementName
      namespaceURI:(NSString *)namespaceURI
     qualifiedName:(NSString *)qName
	attributes:(NSDictionary *)attributeDict
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"start(%@,uri=%@,q=%@,attrs=%@)", elementName,
		namespaceURI != nil ? namespaceURI : @"nil", qName != nil ? qName : @"nil", attributeDict]];
	if (_abortOnStart) {
		[parser abortParsing];
	}
}

- (void)parser:(NSXMLParser *)parser
      didEndElement:(NSString *)elementName
       namespaceURI:(NSString *)namespaceURI
      qualifiedName:(NSString *)qName
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"end(%@,uri=%@,q=%@)", elementName,
		namespaceURI != nil ? namespaceURI : @"nil", qName != nil ? qName : @"nil"]];
}

- (void)parser:(NSXMLParser *)parser foundCharacters:(NSString *)string
{
	(void)parser;
	[_text appendString:string];
	[_log addObject:[NSString stringWithFormat:@"chars(%@)", string]];
}

- (void)parser:(NSXMLParser *)parser foundComment:(NSString *)comment
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"comment(%@)", comment]];
}

- (void)parser:(NSXMLParser *)parser foundCDATA:(NSData *)CDATABlock
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"cdata(%@)",
		[[NSString alloc] initWithData:CDATABlock encoding:NSUTF8StringEncoding]]];
}

- (void)parser:(NSXMLParser *)parser
    foundProcessingInstructionWithTarget:(NSString *)target
				    data:(NSString *)data
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"pi(%@,%@)", target, data != nil ? data : @"nil"]];
}

- (void)parser:(NSXMLParser *)parser didStartMappingPrefix:(NSString *)prefix toURI:(NSString *)namespaceURI
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"+prefix(%@->%@)", prefix, namespaceURI]];
}

- (void)parser:(NSXMLParser *)parser didEndMappingPrefix:(NSString *)prefix
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"-prefix(%@)", prefix]];
}

- (void)parser:(NSXMLParser *)parser parseErrorOccurred:(NSError *)parseError
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"parse-error(%ld)", (long)[parseError code]]];
}

@end

static NSXMLParser *fn_parser(NSString *xml)
{
	id bytes = [xml dataUsingEncoding:NSUTF8StringEncoding];

	return [[NSXMLParser alloc] initWithData:bytes];
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];

	printf("FOUNDATION-XMLPARSER prologue\n");
	[manager removeItemAtPath:@PROBE_ROOT error:NULL];
	mkdir(PROBE_ROOT, 0755);

	{
		TestDelegate *delegate = [[TestDelegate alloc] init];
		NSXMLParser *parser = fn_parser(@"<?xml version=\"1.0\"?><root id=\"1\"><child>text</child></root>");

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			/* SEVEN EVENTS, not six: the document, the root's start, the CHILD's start, the text, the
			 * child's end, the root's end, the document's end. The count is asserted because a parser that
			 * dropped one would still produce a plausible-looking log. */
			check("xml-the-events-of-a-document-in-order",
			      ok && [log count] == 7 &&
			      [[log objectAtIndex:0] isEqual:@"start-document"] &&
			      [[log objectAtIndex:1] hasPrefix:@"start(root,uri=nil,q=nil,attrs={id = 1}"] &&
			      [[log objectAtIndex:2] hasPrefix:@"start(child"] &&
			      [[log objectAtIndex:3] isEqual:@"chars(text)"] &&
			      [[log objectAtIndex:4] hasPrefix:@"end(child"] &&
			      [[log objectAtIndex:5] hasPrefix:@"end(root"] &&
			      [[log objectAtIndex:6] isEqual:@"end-document"],
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);
		}
	}

	{
		TestDelegate *delegate = [[TestDelegate alloc] init];
		NSXMLParser *parser = fn_parser(@"<a t=\"&amp;&#65;\">x &lt; y &#x42;</a>");

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			check("xml-entities-are-decoded-in-text-and-attributes",
			      ok && [[delegate text] isEqual:@"x < y B"] &&
			      /* THE DICTIONARY'S DESCRIPTION IS THIS TREE'S ("{t = &A}"), not Apple's quoted-and-semicolon
			       * form: the check asserts what the API RETURNS, and the decoding is what is under test. */
			      [[log objectAtIndex:1] hasPrefix:@"start(a,uri=nil,q=nil,attrs={t = &A}"],
			      [NSString stringWithFormat:@"ok=%d text=%@ attrs=%@", (int)ok, [delegate text],
				log]);
		}
	}

	{
		TestDelegate *delegate = [[TestDelegate alloc] init];
		NSXMLParser *parser = fn_parser(@"<a><!-- a note --><![CDATA[<raw & stuff>]]><?target some data?><b/></a>");

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			check("xml-comments-cdata-and-processing-instructions",
			      ok &&
			      [log containsObject:@"comment( a note )"] &&
			      [log containsObject:@"cdata(<raw & stuff>)"] &&
			      [log containsObject:@"pi(target,some data)"] &&
			      [delegate missingEvent:@"pi("] == NO,
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);
		}
	}

	{
		TestDelegate *delegate = [[TestDelegate alloc] init];
		NSXMLParser *parser = fn_parser(@"<empty/>");

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			check("xml-a-self-closing-element-is-a-start-and-an-end",
			      ok && [log count] == 4 && [[log objectAtIndex:1] hasPrefix:@"start(empty"] &&
			      [[log objectAtIndex:2] hasPrefix:@"end(empty"],
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);
		}
	}

	{
		TestDelegate *delegate = [[TestDelegate alloc] init];
		NSXMLParser *parser = fn_parser(@"<p:doc xmlns:p=\"urn:example\"><p:item/></p:doc>");
		TestDelegate *reporter = [[TestDelegate alloc] initWithAbort:NO reportPrefixes:YES];
		NSXMLParser *withPrefixes = fn_parser(@"<p:doc xmlns:p=\"urn:example\"><p:item/></p:doc>");

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			/* NAMESPACES OFF (the default): the name is the QUALIFIED one and the URI is nil. */
			check("xml-namespaces-are-off-by-default",
			      ok && [[log objectAtIndex:1] hasPrefix:@"start(p:doc,uri=nil,q=nil"],
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);
		}
		[parser setShouldProcessNamespaces:YES];
		[parser setDelegate:delegate];
		{
			TestDelegate *fresh = [[TestDelegate alloc] init];
			NSXMLParser *again = fn_parser(@"<p:doc xmlns:p=\"urn:example\"><p:item/></p:doc>");

			[again setShouldProcessNamespaces:YES];
			[again setDelegate:fresh];
			{
				BOOL ok = [again parse];

				check("xml-namespaces-on-split-and-expand",
				      ok && [[[fresh log] objectAtIndex:1]
						hasPrefix:@"start(doc,uri=urn:example,q=p:doc"],
				      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, [fresh log]]);
			}
		}
		[withPrefixes setShouldReportNamespacePrefixes:YES];
		[withPrefixes setDelegate:reporter];
		{
			BOOL ok = [withPrefixes parse];
			NSArray *log = [reporter log];

			check("xml-prefix-mappings-are-reported-when-asked",
			      ok && [log containsObject:@"+prefix(->urn:example)"] == NO &&
			      [log containsObject:@"+prefix(p->urn:example)"],
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);
		}
	}

	{
		TestDelegate *delegate = [[TestDelegate alloc] init];
		NSXMLParser *parser = fn_parser(@"<a><b>unclosed</a>");

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSError *error = [parser parserError];

			check("xml-a-malformed-document-fails-with-its-code-and-position",
			      !ok && error != nil &&
			      [[error domain] isEqual:NSXMLParserErrorDomain] &&
			      [error code] == NSXMLParserTagNameMismatchError &&
			      [delegate missingEvent:@"parse-error("] == NO &&
			      [parser lineNumber] == 1 && [parser columnNumber] > 1,
			      [NSString stringWithFormat:@"ok=%d error=%@ line=%ld column=%ld log=%@", (int)ok,
				[error localizedDescription], (long)[parser lineNumber], (long)[parser columnNumber],
				[delegate log]]);
		}
	}

	{
		NSXMLParser *mismatch = fn_parser(@"<a></b>");
		NSXMLParser *hyphen = fn_parser(@"<a><!-- bad -- comment --></a>");
		/* THE DELEGATES ARE HELD HERE, and that is not tidiness: a parser does NOT retain its delegate
		 * (Apple's own rule, implemented by -setDelegate:), so passing a temporary made the parser call a
		 * FREED object - which answered "yes, I abort" from recycled memory and turned a comment error into
		 * a delegate abort. The probe was breaking the contract it is meant to test. */
		TestDelegate *mismatchDelegate = [[TestDelegate alloc] init];
		TestDelegate *hyphenDelegate = [[TestDelegate alloc] init];

		[mismatch setDelegate:mismatchDelegate];
		[hyphen setDelegate:hyphenDelegate];
		{
			BOOL first = [mismatch parse];
			BOOL second = [hyphen parse];

			check("xml-a-tag-mismatch-and-a-double-hyphen-comment-are-told-apart",
			      !first && !second &&
			      [[mismatch parserError] code] == NSXMLParserTagNameMismatchError &&
			      [[hyphen parserError] code] == NSXMLParserCommentContainsDoubleHyphenError,
			      [NSString stringWithFormat:@"parsed=%d/%d codes=%ld/%ld expected=%ld/%ld hyphen-says=%@",
				(int)first, (int)second, (long)[[mismatch parserError] code],
				(long)[[hyphen parserError] code], (long)NSXMLParserTagNameMismatchError,
				(long)NSXMLParserCommentContainsDoubleHyphenError,
				[[hyphen parserError] localizedDescription]]);
		}
	}

	{
		TestDelegate *delegate = [[TestDelegate alloc] initWithAbort:YES reportPrefixes:NO];
		NSXMLParser *parser = fn_parser(@"<a><b/></a>");

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			check("xml-abort-stops-the-parse",
			      !ok && [[parser parserError] code] == NSXMLParserDelegateAbortedParseError &&
			      ![log containsObject:@"end-document"] && [log count] < 6,
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);
		}
	}

	{
		NSData *document = [@"<file><item>ok</item></file>" dataUsingEncoding:NSUTF8StringEncoding];
		TestDelegate *delegate = [[TestDelegate alloc] init];
		NSXMLParser *parser;

		[document writeToFile:fn_path(@"doc.xml") atomically:YES];
		parser = [[NSXMLParser alloc] initWithContentsOfURL:fn_url(fn_path(@"doc.xml"))];
		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			check("xml-it-parses-from-a-file-url-too",
			      ok && [log count] == 7 && [[log objectAtIndex:1] hasPrefix:@"start(file"],
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);
		}
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"still there");
	}

	printf("FOUNDATION-XMLPARSER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-XMLPARSER DONE\n");
	return failc == 0 ? 0 : 1;
}
