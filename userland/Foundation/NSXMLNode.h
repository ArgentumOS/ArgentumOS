/*
 * NSXMLNode.h — XML slice XML-b: THE TREE. docs/design/foundation-plan.md §60.
 *
 * APPLE'S ABSTRACTS: NSXMLNode is "the nodes in the abstract, logical tree structure that represents an XML
 * document" and NSXMLElement is "the element nodes in an XML tree structure". Between them the pages publish
 * about seventy members; THIS SLICE SHIPS THE PART A CALLER CAN BUILD AND READ - the node kinds and their
 * factories, the accessors, the TREE NAVIGATION, the children and attributes, the namespace helpers and the
 * SERIALIZATION - and REGISTERS THE PART THAT NEEDS AN ENGINE THIS SYSTEM DOES NOT HAVE:
 *
 *   XPATH (NSXMLNode's `-nodesForXPath:error:`, `-objectsForXQuery:...` and the query family) needs an XPath
 *   engine; XSLT (`NSXMLDocument`'s `-objectByApplyingXSLT...`) needs an XSLT processor; `-validate` needs
 *   DTD validation, which XML-a already recorded as absent; the `NSXMLDocumentTidy*` options need libxml2's
 *   HTML parser; and `-canonicalXMLStringPreservingComments:` is C14N, a specification of its own. Each is a
 *   boundary with a ground, not a half-built door.
 *
 * AND NSXMLDocument ITSELF IS SLICE XML-c: it is the class that PARSES into this tree (a delegate over
 * XML-a's parser), and it is worth its own slice rather than being smuggled in here. What this slice proves
 * is that a tree can be BUILT, walked and written back out - which is what everything else in the family
 * presumes.
 *
 * THE VALUES OF THE TWO ENUMS ARE OURS (§11.6.1 D2 - Apple publishes the names); the DECLARATION is
 * generated from the ledger so it cannot drift from it.
 */

#ifndef FOUNDATION_NSXMLNODE_H
#define FOUNDATION_NSXMLNODE_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSDictionary;
@class NSError;
@class NSMutableArray;
@class NSMutableDictionary;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* "The type of a node" - Apple's thirteen kinds, whose VALUES are ours (D2). */
typedef enum {
	NSXMLInvalidKind = 0,
	NSXMLDocumentKind = 1,
	NSXMLElementKind = 2,
	NSXMLAttributeKind = 3,
	NSXMLNamespaceKind = 4,
	NSXMLProcessingInstructionKind = 5,
	NSXMLCommentKind = 6,
	NSXMLTextKind = 7,
	NSXMLDTDKind = 8,
	NSXMLEntityDeclarationKind = 9,
	NSXMLAttributeDeclarationKind = 10,
	NSXMLElementDeclarationKind = 11,
	NSXMLNotationDeclarationKind = 12,
} NSXMLNodeKind;
typedef enum {
	NSXMLNodeOptionsNone = 0,
	NSXMLNodeCompactEmptyElement = 1 << 0,
	NSXMLNodeExpandEmptyElement = 1 << 1,
	NSXMLNodeIsCDATA = 1 << 2,
	NSXMLNodeUseSingleQuotes = 1 << 3,
	NSXMLNodeUseDoubleQuotes = 1 << 4,
	NSXMLNodeNeverEscapeContents = 1 << 5,
	NSXMLNodePreserveWhitespace = 1 << 6,
	NSXMLNodePreserveCDATA = 1 << 7,
	NSXMLNodePromoteSignificantWhitespace = 1 << 8,
	NSXMLNodePreserveNamespaceOrder = 1 << 9,
	NSXMLNodePreserveAttributeOrder = 1 << 10,
	NSXMLNodePreserveEntities = 1 << 11,
	NSXMLNodePreservePrefixes = 1 << 12,
	NSXMLNodePreserveQuotes = 1 << 13,
	NSXMLNodePreserveDTD = 1 << 14,
	NSXMLNodePreserveCharacterReferences = 1 << 15,
	NSXMLNodePreserveEmptyElements = 1 << 16,
	NSXMLNodePreserveAll = 1 << 17,
	NSXMLNodeLoadExternalEntitiesAlways = 1 << 18,
	NSXMLNodeLoadExternalEntitiesNever = 1 << 19,
	NSXMLNodeLoadExternalEntitiesSameOriginOnly = 1 << 20,
	NSXMLNodePrettyPrint = 1 << 21,
} NSXMLNodeOptions;


@interface NSXMLNode : NSObject
{
	NSUInteger _kind;			/* one of the kinds above */
	NSString *_name;			/* retained */
	NSString *_stringValue;			/* retained; nil for a node that has none */
	NSString *_uri;				/* retained */
	id _objectValue;			/* retained */
	id _parent;				/* WEAK BY AGREEMENT: a child does not own its parent */
	NSMutableArray *_children;		/* retained, always present (empty for a leaf) */
}
/* THE DOCUMENT NODE AND THE THREE SIMPLE NODES, as Apple's own class methods declare them. */
+ (id)document;
+ (id)documentWithRootElement:(id)element;
+ (id)elementWithName:(NSString *)name;
+ (id)elementWithName:(NSString *)name stringValue:(nullable NSString *)stringValue;
+ (id)elementWithName:(NSString *)name children:(nullable NSArray *)children
	   attributes:(nullable NSArray *)attributes;
+ (id)attributeWithName:(NSString *)name stringValue:(NSString *)stringValue;
+ (id)namespaceWithName:(NSString *)name stringValue:(NSString *)stringValue;
+ (id)processingInstructionWithName:(NSString *)name stringValue:(nullable NSString *)stringValue;
+ (id)commentWithStringValue:(NSString *)stringValue;
+ (id)textWithStringValue:(NSString *)stringValue;
+ (nullable id)DTDNodeWithXMLString:(NSString *)string;
+ (nullable id)localNameForName:(NSString *)name;
+ (nullable id)prefixForName:(NSString *)name;
+ (nullable id)predefinedNamespaceForPrefix:(NSString *)name;

/* "Managing XML Node Objects": what a node IS. */
- (NSXMLNodeKind)kind;
- (nullable NSString *)name;
- (void)setName:(nullable NSString *)name;
- (nullable NSString *)stringValue;
- (void)setStringValue:(nullable NSString *)stringValue;
/* APPLE'S TYPE FOR THIS DOOR IS `id`, NOT NSString, and the difference is this class's answer: a node's object
 * value IS the node when the node has no Cocoa object of its own - which is every node here, since this tree's
 * XML classes hold strings. Declaring a narrow type made the return a lie the compiler reported. */
- (nullable id)objectValue;
- (nullable NSString *)URI;
- (void)setURI:(nullable NSString *)URI;
- (NSUInteger)index;
- (NSUInteger)level;

/* "Navigating the Tree of Nodes". A CHILD DOES NOT OWN ITS PARENT (Apple's own object graph is a tree with
 * upward pointers), so -parent answers without retaining - and -detach is what removes a node. */
- (nullable id)rootDocument;
- (nullable id)parent;
- (nullable id)childAtIndex:(NSUInteger)index;
- (NSUInteger)childCount;
- (nullable NSArray *)children;
- (nullable id)nextNode;
- (nullable id)nextSibling;
- (nullable id)previousNode;
- (nullable id)previousSibling;
- (void)detach;

/* "Emitting Node Content": the node written back out as XML, with the options that decide its shape. */
- (NSString *)XMLString;
- (NSString *)XMLStringWithOptions:(NSXMLNodeOptions)options;
- (NSString *)description;

@end

/* "The element nodes in an XML tree structure" - children and attributes, which is what an element ADDS. */
@interface NSXMLElement : NSXMLNode
{
	NSMutableDictionary *_attributesByName;		/* retained: name -> NSXMLNode, insertion ordered the Cocoa way */
	NSMutableArray *_attributeOrder;		/* retained: the names in the order they were added */
}

- (id)initWithName:(NSString *)name;
- (id)initWithName:(NSString *)name stringValue:(nullable NSString *)stringValue;
- (id)initWithName:(NSString *)name URI:(nullable NSString *)URI;
- (id)initWithName:(NSString *)name children:(nullable NSArray *)children
	   attributes:(nullable NSArray *)attributes;

- (NSArray *)elementsForName:(NSString *)name;
- (NSArray *)elementsForLocalName:(NSString *)localName URI:(nullable NSString *)URI;

- (void)addChild:(id)child;
- (void)insertChild:(id)child atIndex:(NSUInteger)index;
- (void)insertChildren:(NSArray *)children atIndex:(NSUInteger)index;
- (void)removeChildAtIndex:(NSUInteger)index;
- (void)replaceChildAtIndex:(NSUInteger)index withNode:(id)node;
- (void)setChildren:(nullable NSArray *)children;
- (void)normalizeAdjacentTextNodesPreservingCDATA:(BOOL)preserve;

- (void)addAttribute:(id)attribute;
- (void)removeAttributeForName:(NSString *)name;
- (void)setAttributes:(nullable NSArray *)attributes;
- (void)setAttributesWithDictionary:(NSDictionary *)attributes;
- (nullable NSArray *)attributes;
- (nullable id)attributeForName:(NSString *)name;

- (void)addNamespace:(id)aNamespace;
- (void)removeNamespaceForPrefix:(NSString *)name;
- (nullable NSArray *)namespaces;
- (nullable id)namespaceForPrefix:(NSString *)name;
- (nullable id)resolveNamespaceForName:(NSString *)name;
- (nullable id)resolvePrefixForNamespaceURI:(NSString *)namespaceURI;

@end

/* OURS: THE SERIALIZER AS A METHOD RATHER THAN A C FUNCTION, because a SUBCLASS has to be able to override
 * it - which is exactly what NSXMLDTDNode does, since a declaration is not an element. */
@interface NSXMLNode (FNPrivate)
- (NSString *)fnXMLStringWithOptions:(NSXMLNodeOptions)options depth:(NSUInteger)depth;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSXMLNODE_H */
