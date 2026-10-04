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
 *   HTML parser. Each is a boundary with a ground, not a half-built door.
 *
 * ⚠ AND §63.243 REMOVED ONE NAME FROM THAT LIST: `-canonicalXMLStringPreservingComments:` **SHIPS** (§63.233) —
 * this note kept calling it a C14N boundary long after the slice that built it, which is the same stale-note
 * class §63.236 caught in NSLocale.h and §63.237 in a row's blocker. The door's OWN note below states the
 * reading it implements; what stays unbuilt is namespace-DECLARATION synthesis.
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
	NSXMLNodeOptions _options;		/* §63.196: this node's own serialization options, merged over the caller's */
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
+ (id)attributeWithName:(NSString *)name URI:(NSString *)URI stringValue:(NSString *)stringValue;
+ (id)elementWithName:(NSString *)name URI:(NSString *)URI;
+ (id)namespaceWithName:(NSString *)name stringValue:(NSString *)stringValue;
+ (id)processingInstructionWithName:(NSString *)name stringValue:(nullable NSString *)stringValue;
+ (id)commentWithStringValue:(NSString *)stringValue;
+ (id)textWithStringValue:(NSString *)stringValue;
+ (nullable id)DTDNodeWithXMLString:(NSString *)string;
+ (nullable id)localNameForName:(NSString *)name;
+ (nullable id)prefixForName:(NSString *)name;
+ (nullable id)predefinedNamespaceForPrefix:(NSString *)name;

/* "Managing XML Node Objects": what a node IS. */
- (instancetype)initWithKind:(NSXMLNodeKind)kind options:(NSXMLNodeOptions)options;

- (NSXMLNodeKind)kind;
- (nullable NSString *)name;
- (void)setName:(nullable NSString *)name;
- (nullable NSString *)stringValue;
- (void)setStringValue:(nullable NSString *)stringValue;
- (void)setStringValue:(NSString *)string resolvingEntities:(BOOL)resolve;
/* APPLE'S TYPE FOR THIS DOOR IS `id`, NOT NSString, and the difference is this class's answer: a node's object
 * value IS the node when the node has no Cocoa object of its own - which is every node here, since this tree's
 * XML classes hold strings. Declaring a narrow type made the return a lie the compiler reported. */
- (nullable id)objectValue;
- (nullable NSString *)URI;

/* THE NAME'S PARTS AND THE NODE'S PATH. `localName` and `prefix` are the class-side helpers applied to
 * this node's own name; `XPath` is the path this tree can PROMISE — the element steps carry a positional
 * predicate ALWAYS ([name[1]] even when unambiguous), which is a stated reading of a door Apple leaves
 * under-specified. */
@property (readonly, nullable) NSString *localName;
@property (readonly, nullable) NSString *prefix;
@property (readonly, nullable) NSString *XPath;
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

/* THE CHILD-MUTATION DOORS, DECLARED HERE BECAUSE THIS IS WHERE THEY ARE IMPLEMENTED - and this is a PLACEMENT
 * DEVIATION from Apple, stated rather than hidden (§11.6). Apple declares them on the three classes that MUTATE -
 * NSXMLElement, NSXMLDocument and NSXMLDTD - while this tree has ONE implementation, on the node all three
 * inherit from; NSXMLElement's copy of the declarations was satisfied by a pure `[super addChild:]` forwarder and
 * the other five simply did not resolve, which is why the compiler reported them as missing here and as calls it
 * could not check in NSXMLDocument and NSXMLDTD (the -Wobjc-method-access class that hid §62.69's selector typo).
 * Declaring them on the base changes NO behaviour: it is the same method the same objects already reached. */
- (void)addChild:(id)child;
- (void)insertChild:(id)child atIndex:(NSUInteger)index;
- (void)insertChildren:(NSArray *)children atIndex:(NSUInteger)index;
- (void)removeChildAtIndex:(NSUInteger)index;
- (void)replaceChildAtIndex:(NSUInteger)index withNode:(id)node;
- (void)setChildren:(nullable NSArray *)children;

/* "Emitting Node Content": the node written back out as XML, with the options that decide its shape. */
- (NSString *)XMLString;
- (NSString *)XMLStringWithOptions:(NSXMLNodeOptions)options;
- (NSString *)description;


/* §63.233: CANONICAL XML, AS A STATED READING. Canonical XML is a specification with its own rules, and the ones
 * this door implements are written here because the others are visible in its output: UTF-8 with no XML
 * declaration, attributes ordered by (URI, local name), empty elements EXPANDED (never `<a/>`), the minimal
 * escapes (`&`, `<`, `>` in text; `&`, `<`, `"`, TAB/LF/CR in attribute values), and comments CONTROLLED BY
 * THE FLAG — `comments:YES` preserves them, `NO` drops them, and THAT IS THE ONLY DIFFERENCE BETWEEN THE TWO
 * ANSWERS (§63.243 measured it, and the check builds the same tree twice to say so). An earlier version of
 * this note justified preserving them ALWAYS by "the sibling that drops them" — a sibling this tree does not
 * have and does not need, because the parameter already carries the distinction.
 * THE ONE RULE IT DOES NOT IMPLEMENT: namespace DECLARATIONS are not synthesised; a namespace must already be
 * written as an ordinary attribute, so a caller relying on inherited prefix resolution gets the attributes as
 * they stand rather than a rewritten set. */
- (NSString *)canonicalXMLStringPreservingComments:(BOOL)comments;
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

/* THE CHILD-MUTATION DOORS ARE INHERITED, not redeclared: see the note in NSXMLNode's interface for why this
 * tree declares them once, on the node that implements them. */
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


/* §63.213: THE ELEMENT'S THREE. `-attributeForLocalName:URI:` reads the SAME order array the serializer walks,
 * so the two cannot disagree about which attribute is which; `-setAttributesAsDictionary:` REPLACES the set
 * (Apple's door takes the whole of it); `-initWithXMLString:error:` is the failing form of the door the class
 * already has. */
- (nullable id)attributeForLocalName:(NSString *)localName URI:(nullable NSString *)URI;
- (void)setAttributesAsDictionary:(NSDictionary *)attributes;
- (nullable id)initWithXMLString:(NSString *)string error:(NSError **)errorPtr;
@end

/* OURS: THE SERIALIZER AS A METHOD RATHER THAN A C FUNCTION, because a SUBCLASS has to be able to override
 * it - which is exactly what NSXMLDTDNode does, since a declaration is not an element. */
@interface NSXMLNode (FNPrivate)
- (NSString *)fnXMLStringWithOptions:(NSXMLNodeOptions)options depth:(NSUInteger)depth;

/* THE DESIGNATED INITIALIZER AND THE XML-STRING DOOR, declared here so the compiler can CHECK them: the
 * implementations are in this file, and NSXMLDocument and NSXMLDTD reach the initializer through
 * `[super initWithKind:name:value:]` - calls that were UNCHECKED because no declaration existed, which is the
 * -Wobjc-method-access class that hid §62.69's selector typo. A mistyped argument would have compiled silently. */
- (id)initWithKind:(NSXMLNodeKind)kind
	      name:(nullable NSString *)name
	     value:(nullable NSString *)value;
- (nullable id)initWithXMLString:(NSString *)string;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSXMLNODE_H */
