/*
 * NSXMLDTD.h — XML slice XML-d: THE DTD FAMILY, AS A DATA MODEL. docs/design/foundation-plan.md §60.
 *
 * WHAT A DTD IS HERE, SAID FIRST: XML-a's parser SKIPS a document's internal subset as a balanced bracket
 * region and reports nothing about it, and the six DTD events of NSXMLParserDelegate are declared and never
 * fired. So this slice builds the DTD's OBJECTS - the nineteen node kinds Apple publishes, the declaration
 * nodes parsed out of their own XML text, and the container with the four lookups Apple describes - and the
 * PARSER-SIDE half (firing the six events while reading a document, and handing the document its DTD) is
 * slice XML-e, stated there rather than half-built here.
 *
 *   NSXMLDTDNode  "A node in a DTD" - one declaration: an element declaration, an attribute declaration, an
 *                 entity declaration or a notation declaration, with its DTD KIND (the nineteen kinds: the
 *                 ten attribute TYPES, the five element-declaration forms, and the four entity kinds).
 *                 +DTDNodeWithXMLString: parses ONE declaration out of its own text, which is also how
 *                 NSXMLNode's factory of the same name is answered (it was refusing until now - see
 *                 NSXMLNode.h).
 *   NSXMLDTD      "A DTD node" - the CONTAINER: an internal subset parses into declaration children, the
 *                 four lookups find them by name, and the two identifiers and the internal subset itself are
 *                 readable and settable.
 *
 * THE ONE THING AN EXTERNAL SUBSET DOES HERE IS BE REMEMBERED: a declaration naming a SYSTEM or PUBLIC
 * identifier keeps it (that is what -systemID/-publicID are for), and nothing FETCHES it - which is the same
 * boundary XML-a states for external entities, and which the probe asserts as a fact about a declaration
 * rather than a silence about a fetch.
 */

#ifndef FOUNDATION_NSXMLDTD_H
#define FOUNDATION_NSXMLDTD_H

#import <Foundation/NSXMLNode.h>

@class NSArray;
@class NSData;
@class NSError;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* "The DTD node kinds" - Apple's nineteen, ours in value (§11.6.1 D2). The ten NSXMLAttribute… kinds are
 * the ATTRIBUTE TYPES of an <!ATTLIST> declaration; the five ElementDeclaration… kinds are what an
 * <!ELEMENT> declaration's content model can be; the four Entity… kinds say what an <!ENTITY> declares. */
typedef enum {
	NSXMLAttributeCDATAKind = 100,
	NSXMLAttributeEntitiesKind = 101,
	NSXMLAttributeEntityKind = 102,
	NSXMLAttributeEnumerationKind = 103,
	NSXMLAttributeIDKind = 104,
	NSXMLAttributeIDRefKind = 105,
	NSXMLAttributeIDRefsKind = 106,
	NSXMLAttributeNMTokenKind = 107,
	NSXMLAttributeNMTokensKind = 108,
	NSXMLAttributeNotationKind = 109,
	NSXMLElementDeclarationAnyKind = 110,
	NSXMLElementDeclarationElementKind = 111,
	NSXMLElementDeclarationEmptyKind = 112,
	NSXMLElementDeclarationMixedKind = 113,
	NSXMLElementDeclarationUndefinedKind = 114,
	NSXMLEntityGeneralKind = 115,
	NSXMLEntityParameterKind = 116,
	NSXMLEntityParsedKind = 117,
	NSXMLEntityUnparsedKind = 118,
} NSXMLDTDNodeKind;

/* "A node in a DTD": one declaration, of one of the kinds above. */
@interface NSXMLDTDNode : NSXMLNode
{
@public
	NSString *_fnElementName;	/* OURS: see the note below on the ivar this replaces */
	NSXMLDTDNodeKind _dtdKind;
	NSString *_publicID;
	NSString *_systemID;
	NSString *_notationName;
	NSString *_elementName;		/* OURS: the ELEMENT an attribute declaration belongs to, which Apple's
					 * -attributeDeclarationForName:elementName: looks up by and does not otherwise expose */
}
- (nullable id)initWithXMLString:(NSString *)string;
- (id)initWithKind:(NSXMLDTDNodeKind)kind;

/* THE ELEMENT AN ATTRIBUTE DECLARATION BELONGS TO, which Apple's -attributeDeclarationForName:elementName: looks
 * up by and does not otherwise expose. Declared in the HEADER's internal category rather than in NSXMLDTD.m
 * because it has callers in other files - NSXMLDocument sets it, NSXMLParser reads it - and a declaration only
 * one file could see made both calls invisible to the compiler. */
- (void)fnSetElementName:(NSString *)name;
- (NSString *)fnElementName;

- (NSXMLDTDNodeKind)DTDKind;
- (void)setDTDKind:(NSXMLDTDNodeKind)kind;
- (nullable NSString *)publicID;
- (void)setPublicID:(nullable NSString *)publicID;
- (nullable NSString *)systemID;
- (void)setSystemID:(nullable NSString *)systemID;
- (nullable NSString *)notationName;
- (void)setNotationName:(nullable NSString *)notationName;

@end

/* "A DTD node" - the container, whose CHILDREN are the declarations. */
@interface NSXMLDTD : NSXMLNode
{
	NSString *_publicID;
	NSString *_systemID;
	NSString *_internalSubset;
}

/* The two doors to a DTD: from a file URL and from bytes - "options" is carried as Apple's signature has
 * it, and the THIRD door, an internal subset as a string, is -setInternalSubset:. */
- (nullable id)initWithContentsOfURL:(NSURL *)url options:(NSUInteger)mask error:(NSError ** _Nullable)error;
- (nullable id)initWithData:(NSData *)data options:(NSUInteger)mask error:(NSError ** _Nullable)error;

- (nullable NSString *)publicID;
- (void)setPublicID:(nullable NSString *)publicID;
- (nullable NSString *)systemID;
- (void)setSystemID:(nullable NSString *)systemID;
/* SETTING THE INTERNAL SUBSET PARSES IT, which is what makes this a DTD rather than a string: each
 * declaration in the text becomes a child of this node. */
- (nullable NSString *)internalSubset;
- (void)setInternalSubset:(nullable NSString *)internalSubset;

- (nullable NSXMLDTDNode *)elementDeclarationForName:(NSString *)name;
- (nullable NSXMLDTDNode *)attributeDeclarationForName:(NSString *)name elementName:(NSString *)elementName;
- (nullable NSXMLDTDNode *)entityDeclarationForName:(NSString *)name;
- (nullable NSXMLDTDNode *)notationDeclarationForName:(NSString *)name;

/* "Returns the entity declaration for a PREDEFINED entity" - the five XML itself defines, answered from the
 * specification rather than from any document. */
+ (nullable NSXMLDTDNode *)predefinedEntityDeclarationForName:(NSString *)name;

@end

/* THE DECLARATION READER, OURS AND SHARED WITH THE PARSER (slice XML-e): XML-a's parser fires the six DTD
 * events FROM THIS READING rather than from a second one, so a declaration is understood in exactly one place
 * in this library. It answers an ARRAY, because one <!ATTLIST> declares several attributes. */
NSArray *FNDTDDeclarationNodes(NSString *declaration);
/* AND THE SUBSET SPLITTER, for the same reason: one top-level scan of `<!...>` regions, quote-aware. */
NSArray *FNDTDDeclarationNodesFromSubset(NSString *subset);
/* THE TYPE SPELLING OF AN ATTRIBUTE DECLARATION, which is what the parser's event reports as a STRING. */
NSString *FNDTDNodeTypeName(NSXMLDTDNode *node);
/* AND THE ELEMENT FORM A MODEL TEXT MEANS, exported for the same reason: an <!ELEMENT> event hands over the
 * MODEL AS TEXT, and the node built from it must be classified by the ONE rule that classifies a model -
 * not by a second one written in the class that consumes the event. */
NSXMLDTDNodeKind FNDTDNodeKindForModel(NSString *model);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSXMLDTD_H */
