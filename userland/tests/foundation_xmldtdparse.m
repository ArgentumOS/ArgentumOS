/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_xmldtdparse, unit of 1 — XML slice XML-e's acceptance: THE PARSER'S SIX DTD EVENTS, AND THE
 * DOCUMENT THAT KEEPS WHAT THEY SAID. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE: a subset is a string, and the file that is
 * never opened is named rather than created - which is itself one of the checks.
 *
 *   dtdparse-the-six-events-fire   a subset declaring one of each thing fires all six, FROM THE SAME READER
 *                                 the DTD objects use (one understanding of a declaration in this library);
 *   dtdparse-the-element-declaration-carries-its-model  the model arrives as TEXT, and the node built from
 *                                 it is classified by the same rule that classifies a model;
 *   dtdparse-the-attribute-declaration-carries-element-type-and-default  name, ELEMENT, type and default;
 *   dtdparse-the-entity-events-are-the-right-three  internal, external and unparsed are three different
 *                                 events, which is what the protocol publishes;
 *   dtdparse-a-document-without-a-subset-fires-none  a document is not a DTD;
 *   dtdparse-the-document-keeps-its-dtd-and-writes-it-back  and the DOCTYPE it writes can be read again,
 *                                 which is the round trip that matters;
 *   dtdparse-an-external-subset-is-named-and-never-fetched  a DOCTYPE naming a file that does not exist
 *                                 still parses, and builds no DTD - the boundary stated where it happens;
 *   dtdparse-an-unclosed-subset-is-a-parse-error  rather than a silent end of document.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-XMLDTDPARSE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-XMLDTDPARSE %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

static id fn_document_data(NSString *xml)
{
	return [xml dataUsingEncoding:NSUTF8StringEncoding];
}

@interface DTDDelegate : NSObject <NSXMLParserDelegate>
{
	NSMutableArray *_log;
}
- (NSArray *)log;
- (BOOL)hasPrefix:(NSString *)prefix;
- (NSString *)firstWithPrefix:(NSString *)prefix;
@end

@implementation DTDDelegate

- (id)init
{
	self = [super init];
	if (self != nil) {
		_log = [[NSMutableArray alloc] init];
	}
	return self;
}

- (NSArray *)log { return _log; }

- (BOOL)hasPrefix:(NSString *)prefix
{
	NSUInteger i;

	for (i = 0; i < [_log count]; i++) {
		if ([[_log objectAtIndex:i] hasPrefix:prefix]) {
			return YES;
		}
	}
	return NO;
}

- (NSString *)firstWithPrefix:(NSString *)prefix
{
	NSUInteger i;

	for (i = 0; i < [_log count]; i++) {
		if ([[_log objectAtIndex:i] hasPrefix:prefix]) {
			return [_log objectAtIndex:i];
		}
	}
	return nil;
}

- (void)parser:(NSXMLParser *)parser foundElementDeclarationWithName:(NSString *)name model:(NSString *)model
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"element:%@:%@", name, model]];
}

- (void)parser:(NSXMLParser *)parser
    foundAttributeDeclarationWithName:(NSString *)attributeName
			    forElement:(NSString *)elementName
				  type:(NSString *)type
			  defaultValue:(NSString *)defaultValue
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"attribute:%@:%@:%@:%@", attributeName, elementName, type,
		defaultValue]];
}

- (void)parser:(NSXMLParser *)parser
    foundInternalEntityDeclarationWithName:(NSString *)name
				     value:(NSString *)value
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"internal:%@:%@", name, value]];
}

- (void)parser:(NSXMLParser *)parser
    foundExternalEntityDeclarationWithName:(NSString *)name
				 publicID:(NSString *)publicID
				 systemID:(NSString *)systemID
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"external:%@:%@", name, systemID]];
}

- (void)parser:(NSXMLParser *)parser
    foundUnparsedEntityDeclarationWithName:(NSString *)name
				 publicID:(NSString *)publicID
				 systemID:(NSString *)systemID
			     notationName:(NSString *)notationName
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"unparsed:%@:%@:%@", name, systemID, notationName]];
}

- (void)parser:(NSXMLParser *)parser
    foundNotationDeclarationWithName:(NSString *)name
			     publicID:(NSString *)publicID
			     systemID:(NSString *)systemID
{
	(void)parser;
	[_log addObject:[NSString stringWithFormat:@"notation:%@:%@", name, systemID]];
}

@end

static NSString *fn_subset(void)
{
	return @"<!DOCTYPE root [\n"
	       @"<!ELEMENT root (child)>\n"
	       @"<!ATTLIST root id ID #REQUIRED>\n"
	       @"<!ENTITY greeting \"hello\">\n"
	       @"<!ENTITY img SYSTEM \"img.gif\">\n"
	       @"<!ENTITY logo SYSTEM \"logo.gif\" NDATA gif>\n"
	       @"<!NOTATION gif SYSTEM \"image/gif\">\n"
	       @"]>\n<root id=\"1\"><child/></root>";
}

int main(void)
{
	{
		DTDDelegate *delegate = [[DTDDelegate alloc] init];
		NSXMLParser *parser = [[NSXMLParser alloc] initWithData:fn_document_data(fn_subset())];

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];
			NSArray *log = [delegate log];

			check("dtdparse-the-six-events-fire",
			      ok && [delegate hasPrefix:@"element:root"] && [delegate hasPrefix:@"attribute:id"] &&
			      [delegate hasPrefix:@"internal:greeting"] && [delegate hasPrefix:@"external:img"] &&
			      [delegate hasPrefix:@"unparsed:logo"] && [delegate hasPrefix:@"notation:gif"],
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, log]);

			check("dtdparse-the-element-declaration-carries-its-model",
			      [[delegate firstWithPrefix:@"element:root"] isEqual:@"element:root:(child)"],
			      [delegate firstWithPrefix:@"element:root"]);

			check("dtdparse-the-attribute-declaration-carries-element-type-and-default",
			      [[delegate firstWithPrefix:@"attribute:"] isEqual:@"attribute:id:root:ID:#REQUIRED"],
			      [delegate firstWithPrefix:@"attribute:"]);

			check("dtdparse-the-entity-events-are-the-right-three",
			      [[delegate firstWithPrefix:@"internal:"] isEqual:@"internal:greeting:hello"] &&
			      [[delegate firstWithPrefix:@"external:"] isEqual:@"external:img:img.gif"] &&
			      [[delegate firstWithPrefix:@"unparsed:"] isEqual:@"unparsed:logo:logo.gif:gif"],
			      [NSString stringWithFormat:@"internal=%@ external=%@ unparsed=%@",
				[delegate firstWithPrefix:@"internal:"], [delegate firstWithPrefix:@"external:"],
				[delegate firstWithPrefix:@"unparsed:"]]);
		}
	}

	{
		DTDDelegate *delegate = [[DTDDelegate alloc] init];
		NSXMLParser *parser = [[NSXMLParser alloc] initWithData:
					fn_document_data(@"<plain><child/></plain>")];

		[parser setDelegate:delegate];
		{
			BOOL ok = [parser parse];

			check("dtdparse-a-document-without-a-subset-fires-none",
			      ok && [[delegate log] count] == 0,
			      [NSString stringWithFormat:@"ok=%d log=%@", (int)ok, [delegate log]]);
		}
	}

	{
		NSXMLDocument *document = [[NSXMLDocument alloc] initWithData:fn_document_data(fn_subset())
								      options:0
									error:NULL];
		NSData *written = [document XMLData];
		NSString *text = [[NSString alloc] initWithData:written encoding:NSUTF8StringEncoding];
		NSXMLDTD *dtd = [document dtd];
		NSXMLDocument *again = [[NSXMLDocument alloc] initWithData:written options:0 error:NULL];

		check("dtdparse-the-document-keeps-its-dtd-and-writes-it-back",
		      document != nil && dtd != nil && [dtd childCount] == 6 &&
		      [dtd elementDeclarationForName:@"root"] != nil &&
		      [dtd attributeDeclarationForName:@"id" elementName:@"root"] != nil &&
		      [dtd entityDeclarationForName:@"logo"] != nil &&
		      [dtd notationDeclarationForName:@"gif"] != nil &&
		      [text rangeOfString:@"<!DOCTYPE root"].location != NSNotFound &&
		      [text rangeOfString:@"<!ELEMENT root (child)>"].location != NSNotFound &&
		      again != nil && [[again dtd] childCount] == 6,
		      [NSString stringWithFormat:@"children=%lu again-dtd=%@ again-children=%lu written=%@",
			(unsigned long)(dtd != nil ? [dtd childCount] : 0),
			(again != nil ? [again dtd] : (id)@"no document"),
			(unsigned long)(again != nil && [again dtd] != nil ? [[again dtd] childCount] : 0),
			[text length] > 220 ? [text substringToIndex:220] : text]);
	}

	{
		NSXMLDocument *document = [[NSXMLDocument alloc] initWithData:
						fn_document_data(@"<!DOCTYPE root SYSTEM \"nowhere.dtd\">"
								 @"<root/>")
								      options:0
									error:NULL];
		NSXMLNode *root = (NSXMLNode *)[document rootElement];

		check("dtdparse-an-external-subset-is-named-and-never-fetched",
		      document != nil && [document dtd] == nil && root != nil &&
		      [[root name] isEqual:@"root"],
		      [NSString stringWithFormat:@"document=%@ dtd=%@ root=%@", document, [document dtd],
			[root name]]);
	}

	{
		NSError *error = nil;
		NSXMLDocument *document = [[NSXMLDocument alloc] initWithData:
						fn_document_data(@"<!DOCTYPE root [<!ELEMENT root EMPTY>")
								      options:0
									error:&error];

		check("dtdparse-an-unclosed-subset-is-a-parse-error",
		      document == nil && error != nil &&
		      [[error domain] isEqual:NSXMLParserErrorDomain],
		      [NSString stringWithFormat:@"document=%@ error=%@", document,
			[error localizedDescription]]);
	}

	printf("FOUNDATION-XMLDTDPARSE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-XMLDTDPARSE DONE\n");
	return failc == 0 ? 0 : 1;
}
