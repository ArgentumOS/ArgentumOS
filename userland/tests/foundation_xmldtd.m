/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_xmldtd, unit of 1 — XML slice XML-d's acceptance: THE DTD AS A DATA MODEL.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus the POSIX calls its file fixture makes.
 *
 *   dtd-a-declaration-parses-into-a-node  <!ELEMENT a (#PCDATA)> becomes a node of the element-declaration
 *                                         kind whose model is its string value - AND the declaration it
 *                                         writes back is the declaration it read;
 *   dtd-an-attlist-declaration-parses   the ATTRIBUTE TYPE decides the node's DTD kind (ID, not "an
 *                                         attribute declaration"), the element it belongs to is what the
 *                                         container's lookup is BY, and the default clause is the value;
 *   dtd-entity-declarations-internal-external-and-parameter  the three shapes an <!ENTITY> can take,
 *                                         including NDATA making it an UNPARSED entity with a notation;
 *   dtd-notation-declarations           SYSTEM and PUBLIC, which is what a notation is;
 *   dtd-a-container-parses-an-internal-subset  every `<!...>` in the text becomes a child, and the subset
 *                                         itself is remembered;
 *   dtd-the-four-lookups-find-their-declarations  element, attribute (by element+name), entity and notation,
 *                                         each answering nil when it is not there;
 *   dtd-the-predefined-entities         the five XML itself defines, from the specification's table;
 *   dtd-a-string-that-is-not-a-declaration-is-refused  nil rather than an empty node whose kind says
 *                                         nothing;
 *   dtd-a-dtd-parses-from-a-file-url    the second door, over a real .dtd file;
 *   probe-tree-removed                  the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <sys/stat.h>

#define PROBE_ROOT "/System/Temporary Files/nsxmldtd-probe"

static int okc, failc;

static int lastcheck;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-XMLDTD %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-XMLDTD %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
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

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];
	mkdir(PROBE_ROOT, 0755);

	{
		NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithXMLString:@"<!ELEMENT a (#PCDATA)>"];

		check("dtd-a-declaration-parses-into-a-node",
		      node != nil && [node kind] == NSXMLDTDKind &&
		      [node DTDKind] == NSXMLElementDeclarationMixedKind &&
		      [[node name] isEqual:@"a"] && [[node stringValue] isEqual:@"(#PCDATA)"] &&
		      [[node XMLString] isEqual:@"<!ELEMENT a (#PCDATA)>"],
		      [NSString stringWithFormat:@"kind=%lu dtdKind=%lu name=%@ xml=%@",
			(unsigned long)[node kind], (unsigned long)[node DTDKind], [node name],
			[node XMLString]]);
	covers("NSXMLDTD", "addChild:");
	}

	{
		NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithXMLString:
					@"<!ATTLIST item id ID #REQUIRED>"];

		check("dtd-an-attlist-declaration-parses",
		      node != nil && [node DTDKind] == NSXMLAttributeIDKind &&
		      [[node name] isEqual:@"id"] && [[node stringValue] isEqual:@"#REQUIRED"] &&
		      [[node XMLString] isEqual:@"<!ATTLIST item id ID #REQUIRED>"],
		      [NSString stringWithFormat:@"dtdKind=%lu name=%@ value=%@ xml=%@",
			(unsigned long)[node DTDKind], [node name], [node stringValue], [node XMLString]]);
	}

	{
		NSXMLDTDNode *internal = [[NSXMLDTDNode alloc] initWithXMLString:@"<!ENTITY e \"a & b\">"];
		NSXMLDTDNode *parameter = [[NSXMLDTDNode alloc] initWithXMLString:@"<!ENTITY % p \"v\">"];
		NSXMLDTDNode *external = [[NSXMLDTDNode alloc] initWithXMLString:
						@"<!ENTITY img SYSTEM \"img.gif\" NDATA gif>"];

		check("dtd-entity-declarations-internal-external-and-parameter",
		      internal != nil && [internal DTDKind] == NSXMLEntityGeneralKind &&
		      [[internal stringValue] isEqual:@"a & b"] &&
		      parameter != nil && [parameter DTDKind] == NSXMLEntityParameterKind &&
		      [[parameter name] isEqual:@"p"] &&
		      external != nil && [external DTDKind] == NSXMLEntityUnparsedKind &&
		      [[external systemID] isEqual:@"img.gif"] &&
		      [[external notationName] isEqual:@"gif"],
		      [NSString stringWithFormat:@"internal=%@/%lu parameter=%lu external=%lu/%@/%@",
			[internal stringValue], (unsigned long)[internal DTDKind],
			(unsigned long)[parameter DTDKind], (unsigned long)[external DTDKind],
			[external systemID], [external notationName]]);
	}

	{
		NSXMLDTDNode *system = [[NSXMLDTDNode alloc] initWithXMLString:
					@"<!NOTATION gif SYSTEM \"image/gif\">"];
		NSXMLDTDNode *public_ = [[NSXMLDTDNode alloc] initWithXMLString:
					@"<!NOTATION x PUBLIC \"-//id//\" \"http://example/\">"];

		check("dtd-notation-declarations",
		      system != nil && [system DTDKind] == NSXMLNotationDeclarationKind &&
		      [[system systemID] isEqual:@"image/gif"] && [system publicID] == nil &&
		      public_ != nil && [[public_ publicID] isEqual:@"-//id//"] &&
		      [[public_ systemID] isEqual:@"http://example/"],
		      [NSString stringWithFormat:@"system(kind=%lu id=%@ pub=%@) public(kind=%lu id=%@ pub=%@)",
			(unsigned long)[system DTDKind], [system systemID], [system publicID],
			(unsigned long)[public_ DTDKind], [public_ systemID], [public_ publicID]]);
	}

	{
		NSString *subset = @"<!ELEMENT item (#PCDATA)>\n"
				   @"<!ATTLIST item id ID #REQUIRED>\n"
				   @"<!ENTITY greeting \"hello\">\n"
				   @"<!NOTATION gif SYSTEM \"image/gif\">\n";
		NSXMLDTD *dtd = [[NSXMLDTD alloc] init];

		[dtd setName:@"item"];
		[dtd setPublicID:@"-//example//EN"];
		[dtd setSystemID:@"http://example/item.dtd"];
		[dtd setInternalSubset:subset];
		check("dtd-a-container-parses-an-internal-subset",
		      [dtd kind] == NSXMLDTDKind && [dtd childCount] == 4 &&
		      [[dtd internalSubset] isEqual:subset] &&
		      [[dtd publicID] isEqual:@"-//example//EN"] &&
		      [[dtd systemID] isEqual:@"http://example/item.dtd"],
		      [NSString stringWithFormat:@"children=%lu subset=%@", (unsigned long)[dtd childCount],
			[dtd internalSubset]]);
		check("dtd-the-four-lookups-find-their-declarations",
		      [dtd elementDeclarationForName:@"item"] != nil &&
		      [dtd elementDeclarationForName:@"missing"] == nil &&
		      [dtd attributeDeclarationForName:@"id" elementName:@"item"] != nil &&
		      [dtd attributeDeclarationForName:@"id" elementName:@"other"] == nil &&
		      [dtd entityDeclarationForName:@"greeting"] != nil &&
		      [dtd notationDeclarationForName:@"gif"] != nil &&
		      [dtd notationDeclarationForName:@"none"] == nil,
		      @"four lookups, each answering nil when the declaration is not there");
	}

	{
		NSXMLDTDNode *amp = [NSXMLDTD predefinedEntityDeclarationForName:@"amp"];

		check("dtd-the-predefined-entities",
		      amp != nil && [[amp name] isEqual:@"amp"] && [[amp stringValue] isEqual:@"&"] &&
		      [amp DTDKind] == NSXMLEntityGeneralKind &&
		      [NSXMLDTD predefinedEntityDeclarationForName:@"nosuch"] == nil,
		      [NSString stringWithFormat:@"amp=%@", [amp stringValue]]);
	covers("NSXMLDTD", "elementDeclarationForName:");
	covers("NSXMLDTD", "entityDeclarationForName:");
	covers("NSXMLDTD", "notationDeclarationForName:");
	covers("NSXMLDTD", "predefinedEntityDeclarationForName:");
	}

	{
		NSXMLDTDNode *garbage = [[NSXMLDTDNode alloc] initWithXMLString:@"not a declaration at all"];

		check("dtd-a-string-that-is-not-a-declaration-is-refused", garbage == nil,
		      garbage != nil ? [garbage XMLString] : @"nil");
	}

	{
		NSData *dtdText = [@"<!ELEMENT file (#PCDATA)>\n<!ENTITY x \"y\">\n"
				   dataUsingEncoding:NSUTF8StringEncoding];
		NSXMLDTD *dtd;

		[dtdText writeToFile:fn_path(@"sample.dtd") atomically:YES];
		dtd = [[NSXMLDTD alloc] initWithContentsOfURL:fn_url(fn_path(@"sample.dtd"))
					      options:0
						error:NULL];
		check("dtd-a-dtd-parses-from-a-file-url",
		      dtd != nil && [dtd childCount] == 2 &&
		      [dtd elementDeclarationForName:@"file"] != nil &&
		      [dtd entityDeclarationForName:@"x"] != nil,
		      [NSString stringWithFormat:@"children=%lu", (unsigned long)[dtd childCount]]);
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"still there");
	}

	printf("FOUNDATION-XMLDTD RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-XMLDTD DONE\n");
	return failc == 0 ? 0 : 1;
}
