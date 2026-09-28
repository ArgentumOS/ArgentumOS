/*
 * NSXMLParser.h — W8's XML family, slice XML-a: THE EVENT-DRIVEN PARSER. docs/design/foundation-plan.md §60.
 *
 * APPLE'S ABSTRACT: "An event driven parser of XML documents (including DTD declarations)" - and its
 * published surface is measured from the class's page: three initializers (data, contents-of-URL, stream),
 * the delegate, three BEHAVIOUR flags, -parse/-abortParsing/-parserError, the four pieces of parser STATE
 * (columnNumber, lineNumber, publicID, systemID), and its constants. Its delegate protocol publishes 20
 * members, of which the 14 under "Handling XML" are the events a parser without a DTD validator can
 * actually produce; the six DTD events and the tree classes (NSXMLNode and its family) are THEIR OWN
 * slices, because a DTD is a grammar language and a tree is a second parser over the same text.
 *
 * WHAT IS OURS HERE, SAID ONCE: the VALUES of the error enum and of the policy enum are ours (§11.6.1 D2 -
 * Apple publishes the names and not the numbers, which are libxml2's internal codes), the parser is
 * HAND-WRITTEN against this library's own string handling (this system has no libxml2), and the events it
 * produces are the delegate's own documented ones.
 *
 * THE PARSE IS ONE PASS AND IT DOES NOT TREAT WHITESPACE SPECIALLY WITHOUT A DTD: -foundIgnorableWhitespace:
 * exists to report text a DTD declared ignorable, and this slice has no DTD, so whitespace arrives through
 * -foundCharacters: like every other character. That is a boundary rather than an oversight, and the probe
 * states it.
 */

#ifndef FOUNDATION_NSXMLPARSER_H
#define FOUNDATION_NSXMLPARSER_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSData;
@class NSDictionary;
@class NSError;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* THE ERROR DOMAIN, whose name Apple publishes and whose codes are the enum below. */
extern NSString *const NSXMLParserErrorDomain;

/* "The following error codes are defined" - ALL 93 NAMES APPLE PUBLISHES, with values that are OURS
 * (§11.6.1 D2). A caller compares an error's code to one of these and never to a number. */
typedef enum {
	NSXMLParserAttributeHasNoValueError = 0,
	NSXMLParserAttributeListNotFinishedError = 1,
	NSXMLParserAttributeListNotStartedError = 2,
	NSXMLParserAttributeNotFinishedError = 3,
	NSXMLParserAttributeNotStartedError = 4,
	NSXMLParserAttributeRedefinedError = 5,
	NSXMLParserCDATANotFinishedError = 6,
	NSXMLParserCharacterRefAtEOFError = 7,
	NSXMLParserCharacterRefInDTDError = 8,
	NSXMLParserCharacterRefInEpilogError = 9,
	NSXMLParserCharacterRefInPrologError = 10,
	NSXMLParserCommentContainsDoubleHyphenError = 11,
	NSXMLParserCommentNotFinishedError = 12,
	NSXMLParserConditionalSectionNotFinishedError = 13,
	NSXMLParserConditionalSectionNotStartedError = 14,
	NSXMLParserDOCTYPEDeclNotFinishedError = 15,
	NSXMLParserDelegateAbortedParseError = 16,
	NSXMLParserDocumentStartError = 17,
	NSXMLParserElementContentDeclNotFinishedError = 18,
	NSXMLParserElementContentDeclNotStartedError = 19,
	NSXMLParserEmptyDocumentError = 20,
	NSXMLParserEncodingNotSupportedError = 21,
	NSXMLParserEntityBoundaryError = 22,
	NSXMLParserEntityIsExternalError = 23,
	NSXMLParserEntityIsParameterError = 24,
	NSXMLParserEntityNotFinishedError = 25,
	NSXMLParserEntityNotStartedError = 26,
	NSXMLParserEntityRefAtEOFError = 27,
	NSXMLParserEntityRefInDTDError = 28,
	NSXMLParserEntityRefInEpilogError = 29,
	NSXMLParserEntityRefInPrologError = 30,
	NSXMLParserEntityRefLoopError = 31,
	NSXMLParserEntityReferenceMissingSemiError = 32,
	NSXMLParserEntityReferenceWithoutNameError = 33,
	NSXMLParserEntityValueRequiredError = 34,
	NSXMLParserEqualExpectedError = 35,
	NSXMLParserExternalStandaloneEntityError = 36,
	NSXMLParserExternalSubsetNotFinishedError = 37,
	NSXMLParserExtraContentError = 38,
	NSXMLParserGTRequiredError = 39,
	NSXMLParserInternalError = 40,
	NSXMLParserInvalidCharacterError = 41,
	NSXMLParserInvalidCharacterInEntityError = 42,
	NSXMLParserInvalidCharacterRefError = 43,
	NSXMLParserInvalidConditionalSectionError = 44,
	NSXMLParserInvalidDecimalCharacterRefError = 45,
	NSXMLParserInvalidEncodingError = 46,
	NSXMLParserInvalidEncodingNameError = 47,
	NSXMLParserInvalidHexCharacterRefError = 48,
	NSXMLParserInvalidURIError = 49,
	NSXMLParserLTRequiredError = 50,
	NSXMLParserLTSlashRequiredError = 51,
	NSXMLParserLessThanSymbolInAttributeError = 52,
	NSXMLParserLiteralNotFinishedError = 53,
	NSXMLParserLiteralNotStartedError = 54,
	NSXMLParserMisplacedCDATAEndStringError = 55,
	NSXMLParserMisplacedXMLDeclarationError = 56,
	NSXMLParserMixedContentDeclNotFinishedError = 57,
	NSXMLParserMixedContentDeclNotStartedError = 58,
	NSXMLParserNAMERequiredError = 59,
	NSXMLParserNMTOKENRequiredError = 60,
	NSXMLParserNamespaceDeclarationError = 61,
	NSXMLParserNoDTDError = 62,
	NSXMLParserNotWellBalancedError = 63,
	NSXMLParserNotationNotFinishedError = 64,
	NSXMLParserNotationNotStartedError = 65,
	NSXMLParserOutOfMemoryError = 66,
	NSXMLParserPCDATARequiredError = 67,
	NSXMLParserParsedEntityRefAtEOFError = 68,
	NSXMLParserParsedEntityRefInEpilogError = 69,
	NSXMLParserParsedEntityRefInInternalError = 70,
	NSXMLParserParsedEntityRefInInternalSubsetError = 71,
	NSXMLParserParsedEntityRefInPrologError = 72,
	NSXMLParserParsedEntityRefMissingSemiError = 73,
	NSXMLParserParsedEntityRefNoNameError = 74,
	NSXMLParserPrematureDocumentEndError = 75,
	NSXMLParserProcessingInstructionNotFinishedError = 76,
	NSXMLParserProcessingInstructionNotStartedError = 77,
	NSXMLParserPublicIdentifierRequiredError = 78,
	NSXMLParserSeparatorRequiredError = 79,
	NSXMLParserSpaceRequiredError = 80,
	NSXMLParserStandaloneValueError = 81,
	NSXMLParserStringNotClosedError = 82,
	NSXMLParserStringNotStartedError = 83,
	NSXMLParserTagNameMismatchError = 84,
	NSXMLParserURIFragmentError = 85,
	NSXMLParserURIRequiredError = 86,
	NSXMLParserUndeclaredEntityError = 87,
	NSXMLParserUnfinishedTagError = 88,
	NSXMLParserUnknownEncodingError = 89,
	NSXMLParserUnparsedEntityError = 90,
	NSXMLParserXMLDeclNotFinishedError = 91,
	NSXMLParserXMLDeclNotStartedError = 92,
} NSXMLParserError;

/* "The policy to use when resolving external entities" - ours in value, Apple's in name. */
typedef enum {
	NSXMLParserResolveExternalEntitiesAlways = 0,
	NSXMLParserResolveExternalEntitiesNever = 1,
	NSXMLParserResolveExternalEntitiesNoNetwork = 2,
	NSXMLParserResolveExternalEntitiesSameOriginOnly = 3,
} NSXMLParserExternalEntityResolvingPolicy;

@protocol NSXMLParserDelegate;

@interface NSXMLParser : NSObject
{
	NSData *_data;			/* the document's bytes (retained) */
	NSUInteger _at;			/* how far the single pass has got */
	NSUInteger _line;		/* 1-based, for -lineNumber */
	NSUInteger _column;		/* 1-based, for -columnNumber */
	BOOL _processNamespaces;
	BOOL _reportNamespacePrefixes;
	BOOL _resolveExternalEntities;
	BOOL _aborted;
	BOOL _abortedByDelegate;
	BOOL _sawDocumentStart;
	NSError *_parserError;		/* retained once something has gone wrong */
	id <NSXMLParserDelegate> _delegate;	/* NOT retained: see -setDelegate: */
}

- (nullable instancetype)initWithData:(NSData *)data;
- (nullable instancetype)initWithContentsOfURL:(NSURL *)url;

/* NOT RETAINED: Apple's own page says a parser does not retain its delegate, and a delegate that retained
 * the parser would make a cycle. */
- (nullable id <NSXMLParserDelegate>)delegate;
- (void)setDelegate:(nullable id <NSXMLParserDelegate>)delegate;

/* THE THREE BEHAVIOUR FLAGS, whose defaults are Apple's defaults: namespaces are NOT processed and NOT
 * reported (the element name is then the qualified name and the namespace URI is nil), and external
 * entities are NOT resolved. */
- (BOOL)shouldProcessNamespaces;
- (void)setShouldProcessNamespaces:(BOOL)processNamespaces;
- (BOOL)shouldReportNamespacePrefixes;
- (void)setShouldReportNamespacePrefixes:(BOOL)reportNamespacePrefixes;
- (BOOL)shouldResolveExternalEntities;
- (void)setShouldResolveExternalEntities:(BOOL)resolveExternalEntities;

/* "Starts the event-driven parsing operation" - YES when the document parsed, NO when it did not, in which
 * case -parserError says why. */
- (BOOL)parse;

/* "Stops the parser object" - and it is the DELEGATE that is told, which is why the parse then fails with
 * NSXMLParserDelegateAbortedParseError rather than succeeding. */
- (void)abortParsing;

- (nullable NSError *)parserError;

/* WHERE THE PARSER IS, which is state a caller can ask about at any point - including from inside a
 * delegate callback. */
- (NSInteger)lineNumber;
- (NSInteger)columnNumber;
- (nullable NSString *)publicID;
- (nullable NSString *)systemID;

@end

/* "The interface an XML parser uses to inform its delegate about the content of the parsed document" - the
 * fourteen members under "Handling XML" from the protocol's page, every one of them OPTIONAL, because a
 * delegate implements the events it cares about. */
@protocol NSXMLParserDelegate <NSObject>
@optional

- (void)parserDidStartDocument:(NSXMLParser *)parser;
- (void)parserDidEndDocument:(NSXMLParser *)parser;

- (void)parser:(NSXMLParser *)parser
    didStartElement:(NSString *)elementName
      namespaceURI:(nullable NSString *)namespaceURI
     qualifiedName:(nullable NSString *)qName
	attributes:(NSDictionary *)attributeDict;

- (void)parser:(NSXMLParser *)parser
      didEndElement:(NSString *)elementName
       namespaceURI:(nullable NSString *)namespaceURI
      qualifiedName:(nullable NSString *)qName;

- (void)parser:(NSXMLParser *)parser foundCharacters:(NSString *)string;
- (void)parser:(NSXMLParser *)parser foundIgnorableWhitespace:(NSString *)whitespaceString;
- (void)parser:(NSXMLParser *)parser foundComment:(NSString *)comment;
- (void)parser:(NSXMLParser *)parser foundCDATA:(NSData *)CDATABlock;
- (void)parser:(NSXMLParser *)parser
    foundProcessingInstructionWithTarget:(NSString *)target
				    data:(nullable NSString *)data;

- (void)parser:(NSXMLParser *)parser didStartMappingPrefix:(NSString *)prefix toURI:(NSString *)namespaceURI;
- (void)parser:(NSXMLParser *)parser didEndMappingPrefix:(NSString *)prefix;
- (nullable NSData *)parser:(NSXMLParser *)parser
    resolveExternalEntityName:(NSString *)name
		     systemID:(nullable NSString *)systemID;
/* APPLE'S DTD/ENTITY DECLARATION DOORS, WHICH THIS PROTOCOL DID NOT DECLARE WHILE THE PARSER CALLED ALL SIX of
 * them: -FN_XML_EVENT consults -respondsToSelector: on an `id <NSXMLParserDelegate>` whose protocol knew nothing
 * about them, so the compiler could not check a single one. They are optional in Apple's protocol and optional
 * here. */
- (void)parser:(NSXMLParser *)parser
    foundElementDeclarationWithName:(NSString *)elementName
			       model:(NSString *)model;
- (void)parser:(NSXMLParser *)parser
    foundAttributeDeclarationWithName:(NSString *)attributeName
			    forElement:(NSString *)elementName
				type:(nullable NSString *)type
			defaultValue:(nullable NSString *)defaultValue;
- (void)parser:(NSXMLParser *)parser
    foundNotationDeclarationWithName:(NSString *)name
			    publicID:(nullable NSString *)publicID
			    systemID:(nullable NSString *)systemID;
- (void)parser:(NSXMLParser *)parser
    foundUnparsedEntityDeclarationWithName:(NSString *)name
				  publicID:(nullable NSString *)publicID
				  systemID:(nullable NSString *)systemID
			      notationName:(nullable NSString *)notationName;
- (void)parser:(NSXMLParser *)parser
    foundExternalEntityDeclarationWithName:(NSString *)name
				  publicID:(nullable NSString *)publicID
				  systemID:(nullable NSString *)systemID;
- (void)parser:(NSXMLParser *)parser
    foundInternalEntityDeclarationWithName:(NSString *)name
				     value:(nullable NSString *)value;

- (void)parser:(NSXMLParser *)parser parseErrorOccurred:(NSError *)parseError;
- (void)parser:(NSXMLParser *)parser validationErrorOccurred:(NSError *)validationError;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSXMLPARSER_H */
