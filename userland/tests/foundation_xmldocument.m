/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_xmldocument, unit of 1 — XML slice XML-c's acceptance. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus the POSIX calls its file fixture makes. It builds
 * a tree of its own under /System/Temporary Files and removes it at the end and at the start.
 *
 *   document-parses-into-a-tree   XML-a's events become XML-b's nodes: the root element, its children, its
 *                                 attributes - and the document writes them back out;
 *   document-parses-from-a-string-and-a-file-url  the other two doors, over a real file;
 *   document-namespace-declarations-become-children  an `xmlns:p` ATTRIBUTE is a namespace CHILD in the
 *                                 tree (XML-b's stated reading), and it comes back out in the start tag;
 *   document-a-malformed-document-answers-nil-and-its-error  the parser's error, reported through the door;
 *   document-the-declaration-is-written-from-the-attributes  the emitted `<?xml ...?>` comes from THIS
 *                                 document's attributes - the stated boundary, since a parsed declaration is
 *                                 not read back;
 *   document-the-attributes-default-and-round-trip  version, encoding, MIME type, standalone, content kind;
 *   document-the-root-element-can-be-replaced  -setRootElement: replaces the one child that is an element;
 *   document-a-refused-option-says-which-one  FOUR of the five document options describe a pipeline this
 *                                 system does not have, and each is refused WITH AN ERROR THAT NAMES IT;
 *   document-the-content-type-option-is-ignored-at-the-writing-door  the fifth describes HTML/XHTML output,
 *                                 and that door has no error to refuse with - so it is ignored, and said so;
 *   document-replacement-class-answers-its-argument  Apple's subclassing hook, whose default is the class;
 *   probe-tree-removed            the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <sys/stat.h>

#define PROBE_ROOT "/System/Temporary Files/nsxmldocument-probe"

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-XMLDOCUMENT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-XMLDOCUMENT %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

/* covers("NSXMLElement", "addChild:") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

static id fn_document(NSString *xml)
{
	id data = [xml dataUsingEncoding:NSUTF8StringEncoding];

	return [[NSXMLDocument alloc] initWithData:data options:0 error:NULL];
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];
	mkdir(PROBE_ROOT, 0755);

	{
		NSXMLDocument *document = fn_document(@"<root id=\"1\"><child>text</child></root>");
		NSXMLElement *root = (NSXMLElement *)[document rootElement];
		NSXMLNode *child;

		child = [root childAtIndex:0];
		check("document-parses-into-a-tree",
		      document != nil && [document kind] == NSXMLDocumentKind && root != nil &&
		      [root kind] == NSXMLElementKind && [[root name] isEqual:@"root"] &&
		      [[[root attributeForName:@"id"] stringValue] isEqual:@"1"] &&
		      [root childCount] == 1 && [[child name] isEqual:@"child"] &&
		      [[[child childAtIndex:0] stringValue] isEqual:@"text"] &&
		      [[root XMLString] isEqual:@"<root id=\"1\"><child>text</child></root>"],
		      [NSString stringWithFormat:@"root=%@ xml=%@", [root name], [root XMLString]]);
	}

	{
		NSXMLDocument *fromString = [[NSXMLDocument alloc] initWithXMLString:@"<a><b/></a>"
									    options:0
									      error:NULL];
		NSData *document = [@"<file><item>ok</item></file>" dataUsingEncoding:NSUTF8StringEncoding];
		NSXMLDocument *fromFile;

		[document writeToFile:fn_path(@"doc.xml") atomically:YES];
		fromFile = [[NSXMLDocument alloc] initWithContentsOfURL:fn_url(fn_path(@"doc.xml"))
							       options:0
								 error:NULL];
		check("document-parses-from-a-string-and-a-file-url",
		      fromString != nil && [[[fromString rootElement] name] isEqual:@"a"] &&
		      fromFile != nil && [[[fromFile rootElement] name] isEqual:@"file"] &&
		      [[[[[fromFile rootElement] childAtIndex:0] childAtIndex:0] stringValue] isEqual:@"ok"],
		      [NSString stringWithFormat:@"string=%@ file=%@",
			[[fromString rootElement] name], [[fromFile rootElement] name]]);
	covers("NSXMLElement", "attributeForName:");
	covers("NSXMLNode", "childAtIndex:");
	}

	{
		NSXMLDocument *document = fn_document(@"<r xmlns:p=\"urn:example\"><p:c/></r>");
		NSXMLElement *root = (NSXMLElement *)[document rootElement];

		NSXMLNode *declared = [[root namespaces] objectAtIndex:0];

		check("document-namespace-declarations-become-children",
		      [[root namespaces] count] == 1 &&
		      [declared kind] == NSXMLNamespaceKind &&
		      [root attributeForName:@"xmlns:p"] == nil &&
		      [[root XMLString] isEqual:@"<r xmlns:p=\"urn:example\"><p:c></p:c></r>"],
		      [NSString stringWithFormat:@"namespaces=%lu xml=%@", (unsigned long)[[root namespaces] count],
			[root XMLString]]);
	}

	{
		NSError *error = nil;
		id data = [@"<a><b></a>" dataUsingEncoding:NSUTF8StringEncoding];
		NSXMLDocument *document = [[NSXMLDocument alloc] initWithData:data options:0 error:&error];

		check("document-a-malformed-document-answers-nil-and-its-error",
		      document == nil && error != nil &&
		      [[error domain] isEqual:NSXMLParserErrorDomain],
		      [NSString stringWithFormat:@"document=%@ error=%@", document, error]);
	}

	{
		NSXMLDocument *document = fn_document(@"<root/>");
		id defaults = [document XMLData];
		NSString *expected = @"<?xml version=\"1.0\" encoding=\"UTF-8\"?><root></root>";

		check("document-the-declaration-is-written-from-the-attributes",
		      [[[NSString alloc] initWithData:defaults encoding:NSUTF8StringEncoding] isEqual:expected],
		      [[NSString alloc] initWithData:defaults encoding:NSUTF8StringEncoding]);
		[document setVersion:@"1.1"];
		[document setCharacterEncoding:@"UTF-16"];
		[document setStandalone:YES];
		{
			NSString *withDeclaration = [[NSString alloc] initWithData:[document XMLData]
									 encoding:NSUTF8StringEncoding];

			check("document-the-attributes-default-and-round-trip",
			      [[document version] isEqual:@"1.1"] &&
			      [[document characterEncoding] isEqual:@"UTF-16"] && [document isStandalone] &&
			      [withDeclaration isEqual:
				@"<?xml version=\"1.1\" encoding=\"UTF-16\" standalone=\"yes\"?><root></root>"],
			      withDeclaration);
		}
	}

	{
		NSXMLDocument *document = fn_document(@"<root><old/></root>");
		NSXMLElement *replacement = [NSXMLNode elementWithName:@"new"];

		[document setRootElement:replacement];
		check("document-the-root-element-can-be-replaced",
		      [document rootElement] == replacement && [document childCount] == 1 &&
		      [[document XMLData] length] > 0,
		      [NSString stringWithFormat:@"root=%@ children=%lu", [[document rootElement] name],
			(unsigned long)[document childCount]]);
	covers("NSXMLNode", "elementWithName:");
	}

	{
		NSUInteger options[4] = { NSXMLDocumentValidate, NSXMLDocumentTidyHTML,
					  NSXMLDocumentTidyXML, NSXMLDocumentXInclude };
		NSMutableArray *refused = [NSMutableArray array];
		id data = [@"<a/>" dataUsingEncoding:NSUTF8StringEncoding];
		NSUInteger i;

		for (i = 0; i < 4; i++) {
			NSError *error = nil;
			NSXMLDocument *document = [[NSXMLDocument alloc] initWithData:data
									     options:options[i]
									       error:&error];

			if (document == nil && error != nil) {
				[refused addObject:[error localizedDescription]];
			}
		}
		check("document-a-refused-option-says-which-one",
		      [refused count] == 4 && [[refused objectAtIndex:0] rangeOfString:@"Validate"].location != NSNotFound &&
		      [[refused objectAtIndex:1] rangeOfString:@"TidyHTML"].location != NSNotFound &&
		      [[refused objectAtIndex:2] rangeOfString:@"TidyXML"].location != NSNotFound &&
		      [[refused objectAtIndex:3] rangeOfString:@"XInclude"].location != NSNotFound,
		      [refused componentsJoinedByString:@" | "]);
	}

	{
		NSXMLDocument *document = fn_document(@"<root/>");
		id without = [document XMLDataWithOptions:0];
		id with = [document XMLDataWithOptions:NSXMLDocumentIncludeContentTypeDeclaration];

		check("document-the-content-type-option-is-ignored-at-the-writing-door",
		      [without isEqual:with],
		      [NSString stringWithFormat:@"with=%lu bytes without=%lu bytes",
			(unsigned long)[with length], (unsigned long)[without length]]);
	}

	check("document-replacement-class-answers-its-argument",
	      [NSXMLDocument replacementClassForClass:[NSXMLElement class]] == [NSXMLElement class],
	      @"the default hook answers the class it was given");

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"still there");
	}

	printf("FOUNDATION-XMLDOCUMENT RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-XMLDOCUMENT DONE\n");
	return failc == 0 ? 0 : 1;
}
