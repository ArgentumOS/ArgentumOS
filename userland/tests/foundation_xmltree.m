/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_xmltree, unit of 1 — XML slice XML-b's acceptance: THE TREE. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE AND NO FILE SYSTEM: this slice is the
 * logical tree and its serialization, and a tree is built by hand rather than read - NSXMLDocument, which
 * PARSES into one, is slice XML-c.
 *
 *   tree-the-factories-make-the-kinds     each factory answers the kind it names, and the DTD factory is
 *                                          REFUSED (nil) because the DTD kinds are slice XML-c's;
 *   tree-an-element-serializes-with-its-attributes  the XML a caller gets back, with escaping done by the
 *                                          rules of each CONTEXT (a quote is fatal in an attribute and
 *                                          ordinary in text);
 *   tree-an-empty-element-follows-the-option  <a></a> by default, <a/> when asked to be compact;
 *   tree-the-quote-option-decides-the-attribute-quotes  double by default, single when asked;
 *   tree-pretty-print-indents-element-children  the option that makes output readable;
 *   tree-the-children-are-the-tree-and-a-node-has-one-parent  adding a child ADOPTS it (taking it from
 *                                          wherever it was) and -detach gives it back;
 *   tree-navigation-is-document-order      nextNode/previousNode are TREE order, and the siblings, index
 *                                          and level agree with it;
 *   tree-attributes-live-in-the-elements-own-store  the stated storage reading: -attributes in insertion
 *                                          order, replacing by name, and NOT counted among -children;
 *   tree-namespaces-are-children-and-resolve-upward  a declaration is a child AND appears in the start tag,
 *                                          and resolution walks up the tree;
 *   tree-elements-for-name-and-for-local-name  the two child-element selectors;
 *   tree-the-name-helpers-split-and-bind-prefixes  +localNameForName:, +prefixForName: and the two
 *                                          prefixes XML ITSELF binds.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-XMLTREE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-XMLTREE %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}


/* the children's names, in order, joined - the only shape in which "the order is right" can be asserted */
static NSString *fn_names(id element)
{
	NSArray *children = [element children];
	NSMutableArray *names = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [children count]; i++) {
		[names addObject:[[children objectAtIndex:i] name]];
	}
	return [names componentsJoinedByString:@","];
}

int main(void)
{
	{
		NSXMLNode *document = [NSXMLNode document];
		NSXMLNode *element = [NSXMLNode elementWithName:@"a"];
		NSXMLNode *attribute = [NSXMLNode attributeWithName:@"id" stringValue:@"1"];
		NSXMLNode *namespaceNode = [NSXMLNode namespaceWithName:@"p" stringValue:@"urn:x"];
		NSXMLNode *comment = [NSXMLNode commentWithStringValue:@" c "];
		NSXMLNode *text = [NSXMLNode textWithStringValue:@"t"];
		NSXMLNode *pi = [NSXMLNode processingInstructionWithName:@"target" stringValue:@"data"];

		/* THE DTD CLAUSE USED TO ASSERT AN ABSENCE ("the DTD kinds are slice XML-c's") AND WENT STALE THE
		 * DAY SLICE XML-d SHIPPED THEM - the second instance of that bug class today, after
		 * foundation_filecoordinator. It now asserts what IS: the factory answers a DTD node, and its kind
		 * is in the DTD family, which this library numbers from 100 so the two families cannot be confused
		 * in one switch. */
		NSXMLNode *dtd = [NSXMLNode DTDNodeWithXMLString:@"<!ELEMENT a (#PCDATA)>"];

		check("tree-the-factories-make-the-kinds",
		      [document kind] == NSXMLDocumentKind && [element kind] == NSXMLElementKind &&
		      [attribute kind] == NSXMLAttributeKind && [namespaceNode kind] == NSXMLNamespaceKind &&
		      [comment kind] == NSXMLCommentKind && [text kind] == NSXMLTextKind &&
		      [pi kind] == NSXMLProcessingInstructionKind &&
		      dtd != nil && [dtd kind] >= 100,
		      [NSString stringWithFormat:@"kinds=%lu/%lu/%lu/%lu/%lu/%lu/%lu",
			(unsigned long)[document kind], (unsigned long)[element kind],
			(unsigned long)[attribute kind], (unsigned long)[namespaceNode kind],
			(unsigned long)[comment kind], (unsigned long)[text kind], (unsigned long)[pi kind]]);
	}

	{
		NSXMLElement *element = [NSXMLNode elementWithName:@"a"];
		NSXMLNode *text = [NSXMLNode textWithStringValue:@"x & y < z"];

		[element addAttribute:[NSXMLNode attributeWithName:@"q" stringValue:@"say \"hi\" & bye"]];
		[element addAttribute:[NSXMLNode attributeWithName:@"id" stringValue:@"1"]];
		[element addChild:text];
		check("tree-an-element-serializes-with-its-attributes",
		      [[element XMLString] isEqual:
			@"<a q=\"say &quot;hi&quot; &amp; bye\" id=\"1\">x &amp; y &lt; z</a>"],
		      [element XMLString]);
	}

	{
		NSXMLElement *element = [NSXMLNode elementWithName:@"empty"];

		check("tree-an-empty-element-follows-the-option",
		      [[element XMLString] isEqual:@"<empty></empty>"] &&
		      [[element XMLStringWithOptions:NSXMLNodeCompactEmptyElement] isEqual:@"<empty/>"],
		      [NSString stringWithFormat:@"default=%@ compact=%@", [element XMLString],
			[element XMLStringWithOptions:NSXMLNodeCompactEmptyElement]]);
	}

	{
		NSXMLElement *element = [NSXMLNode elementWithName:@"a"];

		[element addAttribute:[NSXMLNode attributeWithName:@"id" stringValue:@"1"]];
		check("tree-the-quote-option-decides-the-attribute-quotes",
		      [[element XMLStringWithOptions:NSXMLNodeUseSingleQuotes] isEqual:@"<a id='1'></a>"],
		      [element XMLStringWithOptions:NSXMLNodeUseSingleQuotes]);
	}

	{
		NSXMLElement *root = [NSXMLNode elementWithName:@"r"];
		NSXMLElement *child = [NSXMLNode elementWithName:@"c"];
		NSXMLElement *grandchild = [NSXMLNode elementWithName:@"g"];

		[child addChild:grandchild];
		[root addChild:child];
		{
			/* THE PRETTY OUTPUT CONTAINS NEWLINES, and a console line does not: the detail shows them
			 * escaped, because a failure whose value is cut at the first newline cannot be read. */
			NSString *pretty = [root XMLStringWithOptions:NSXMLNodePrettyPrint];
			NSMutableString *readable = [NSMutableString string];
			NSUInteger i;

			for (i = 0; i < [pretty length]; i++) {
				unichar c = [pretty characterAtIndex:i];

				if (c == '\n') {
					[readable appendString:@"\\n"];
				} else {
					[readable appendString:[NSString stringWithCharacters:&c length:1]];
				}
			}
			check("tree-pretty-print-indents-element-children",
			      [pretty isEqual:@"<r>\n  <c>\n    <g></g>\n  </c>\n</r>"],
			      readable);
		}
	}

	{
		NSXMLNode *first = [NSXMLNode elementWithName:@"first"];
		NSXMLNode *second = [NSXMLNode elementWithName:@"second"];
		NSXMLElement *parent = [NSXMLNode elementWithName:@"p"];
		NSXMLElement *other = [NSXMLNode elementWithName:@"o"];

		[parent addChild:first];
		[other addChild:second];
		[parent addChild:second];		/* ADOPTS: second leaves `other` */
		{
			BOOL adopted = [second parent] == parent && [other childCount] == 0 &&
				       [parent childCount] == 2;

			[second detach];
			check("tree-the-children-are-the-tree-and-a-node-has-one-parent",
			      adopted && [parent childCount] == 1 && [second parent] == nil &&
			      [parent childAtIndex:0] == first,
			      [NSString stringWithFormat:@"adopted=%d after detach parent has %lu",
				(int)adopted, (unsigned long)[parent childCount]]);
		}
	}

	{
		NSXMLElement *root = [NSXMLNode elementWithName:@"r"];
		NSXMLElement *child = [NSXMLNode elementWithName:@"c"];
		NSXMLNode *text = [NSXMLNode textWithStringValue:@"t"];

		[child addChild:text];
		[root addChild:child];
		check("tree-navigation-is-document-order",
		      [root nextNode] == child && [child nextNode] == text &&
		      [text nextNode] == nil && [text previousNode] == child &&
		      [child previousNode] == root && [root previousNode] == nil &&
		      [child nextSibling] == nil && [child index] == 0 && [text index] == 0 &&
		      [root level] == 0 && [child level] == 1 && [text level] == 2,
		      [NSString stringWithFormat:@"levels=%lu/%lu/%lu", (unsigned long)[root level],
			(unsigned long)[child level], (unsigned long)[text level]]);
	}

	{
		NSXMLElement *element = [NSXMLNode elementWithName:@"a"];
		NSDictionary *dictionary = [NSDictionary dictionaryWithObjectsAndKeys:
						@"1", @"id", @"x", @"name", nil];

		[element addAttribute:[NSXMLNode attributeWithName:@"id" stringValue:@"0"]];
		[element addAttribute:[NSXMLNode attributeWithName:@"id" stringValue:@"1"]];	/* replaces */
		[element setAttributesWithDictionary:dictionary];
		{
			NSArray *attributes = [element attributes];
			BOOL ordered = [attributes count] == 2 &&
				       [[[attributes objectAtIndex:0] name] isEqual:@"id"];

			[element removeAttributeForName:@"id"];
			check("tree-attributes-live-in-the-elements-own-store",
			      ordered && [element childCount] == 0 &&
			      [element attributeForName:@"id"] == nil &&
			      [[[element attributeForName:@"name"] stringValue] isEqual:@"x"],
			      [NSString stringWithFormat:@"attributes=%@ childCount=%lu", attributes,
				(unsigned long)[element childCount]]);
		}
	}

	{
		NSXMLElement *root = [NSXMLNode elementWithName:@"r"];
		NSXMLElement *child = [NSXMLNode elementWithName:@"p:c"];

		[root addNamespace:[NSXMLNode namespaceWithName:@"p" stringValue:@"urn:example"]];
		[root addChild:child];
		check("tree-namespaces-are-children-and-resolve-upward",
		      [[root XMLString] isEqual:
			@"<r xmlns:p=\"urn:example\"><p:c></p:c></r>"] &&
		      [[root namespaces] count] == 1 &&
		      [[child resolveNamespaceForName:@"p:c"] isEqual:@"urn:example"] &&
		      [[child resolvePrefixForNamespaceURI:@"urn:example"] isEqual:@"p"],
		      [root XMLString]);
	}

	{
		NSXMLElement *root = [NSXMLNode elementWithName:@"r"];
		NSXMLElement *plain = [NSXMLNode elementWithName:@"item"];
		NSXMLElement *prefixed = [NSXMLNode elementWithName:@"p:item"];

		[root addNamespace:[NSXMLNode namespaceWithName:@"p" stringValue:@"urn:example"]];
		[root addChild:plain];
		[root addChild:prefixed];
		check("tree-elements-for-name-and-for-local-name",
		      [[root elementsForName:@"item"] count] == 1 &&
		      [[root elementsForName:@"p:item"] count] == 1 &&
		      [[root elementsForLocalName:@"item" URI:@"urn:example"] count] == 1 &&
		      [[root elementsForLocalName:@"item" URI:nil] count] == 1 &&
		      [[root elementsForLocalName:@"item" URI:@"urn:other"] count] == 0,
		      [NSString stringWithFormat:@"byName=%lu byLocal=%lu",
			(unsigned long)[[root elementsForLocalName:@"item" URI:@"urn:example"] count],
			(unsigned long)[[root elementsForName:@"item"] count]]);
	}

	{
		check("tree-the-name-helpers-split-and-bind-prefixes",
		      [[NSXMLNode localNameForName:@"p:item"] isEqual:@"item"] &&
		      [[NSXMLNode localNameForName:@"item"] isEqual:@"item"] &&
		      [[NSXMLNode prefixForName:@"p:item"] isEqual:@"p"] &&
		      [NSXMLNode prefixForName:@"item"] == nil &&
		      [[NSXMLNode predefinedNamespaceForPrefix:@"xml"] isEqual:
			@"http://www.w3.org/XML/1998/namespace"] &&
		      [NSXMLNode predefinedNamespaceForPrefix:@"not-a-prefix"] == nil,
		      @"the split helpers, and the two prefixes XML itself binds");
	}

	{
		/* THE FOUR EDIT DOORS THAT WERE DECLARED AND MISSING UNTIL §62's REPORT NAMED THEM, AND THE ORDER
		 * RULE THAT IS EASY TO GET WRONG: inserting [b, c] at 1 of [a, d] must give [a, b, c, d] - an
		 * implementation that inserted both at the SAME index would reverse them, and only an ORDERED
		 * assertion can see the difference. */
		id root = [[NSXMLElement alloc] initWithName:@"root"];
		id a = [[NSXMLElement alloc] initWithName:@"a"];
		id b = [[NSXMLElement alloc] initWithName:@"b"];
		id c = [[NSXMLElement alloc] initWithName:@"c"];
		id d = [[NSXMLElement alloc] initWithName:@"d"];
		id e = [[NSXMLElement alloc] initWithName:@"e"];
		NSString *afterInsert, *afterRemove, *afterReplace, *afterSet;
		BOOL parentsRight;

		check("the-four-edit-doors-ship",
		      [root respondsToSelector:@selector(insertChildren:atIndex:)] &&
		      [root respondsToSelector:@selector(removeChildAtIndex:)] &&
		      [root respondsToSelector:@selector(replaceChildAtIndex:withNode:)] &&
		      [root respondsToSelector:@selector(setChildren:)],
		      @"insertChildren:atIndex:, removeChildAtIndex:, replaceChildAtIndex:withNode:, setChildren:");

		[root addChild:a];
		[root addChild:d];
		[root insertChildren:[NSArray arrayWithObjects:b, c, nil] atIndex:1];
		afterInsert = fn_names(root);
		parentsRight = [a parent] == root && [b parent] == root && [c parent] == root && [d parent] == root;
		[root removeChildAtIndex:1];
		afterRemove = fn_names(root);
		[root replaceChildAtIndex:1 withNode:e];
		afterReplace = fn_names(root);
		[root setChildren:[NSArray arrayWithObject:d]];
		afterSet = fn_names(root);
		check("edit-doors-keep-order-and-parentage",
		      [afterInsert isEqual:@"a,b,c,d"] && [afterRemove isEqual:@"a,c,d"] &&
		      [afterReplace isEqual:@"a,e,d"] && [afterSet isEqual:@"d"] && parentsRight &&
		      [c parent] == nil && [e parent] == nil && [a parent] == nil,
		      [NSString stringWithFormat:@"insert=%@ remove=%@ replace=%@ set=%@ parents=%d c-parent=%@ "
			@"e-parent=%@", afterInsert, afterRemove, afterReplace, afterSet, (int)parentsRight,
			[c parent], [e parent]]);
	}

	printf("FOUNDATION-XMLTREE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-XMLTREE DONE\n");
	return failc == 0 ? 0 : 1;
}
