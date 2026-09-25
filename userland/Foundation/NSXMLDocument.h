/*
 * NSXMLDocument.h — XML slice XML-c: THE DOCUMENT, AND THE BRIDGE FROM THE PARSER INTO THE TREE.
 * docs/design/foundation-plan.md §60.
 *
 * APPLE'S ABSTRACT: "An XML document as internalized into a logical tree structure" - and that sentence is
 * the slice: XML-a produced EVENTS, XML-b produced a TREE, and this class is the thing that turns one into
 * the other (`-initWithData:options:error:` runs XML-a's parser over a delegate that BUILDS the tree) and
 * then writes it back out.
 *
 * WHAT IS SHIPPED is the measured surface that does not need an engine: the four initializers, the document
 * attributes (version, characterEncoding, isStandalone, MIMEType, documentContentKind), the root element,
 * and the two writing doors. `+replacementClassForClass:` ships as Apple describes it: the default
 * implementation answers the class it is given.
 *
 * WHAT IS REGISTERED, AT THE DOORS IT BELONGS TO: `-dtd`/`-setDTD:` (the DTD class is slice XML-d),
 * `-validate` (this system has no DTD validation), the three XSLT doors (there is no XSLT processor), and
 * FOUR OF THE FIVE OPTIONS, which describe a pipeline this system does not have: NSXMLDocumentValidate (DTD
 * validation), NSXMLDocumentTidyHTML and NSXMLDocumentTidyXML (libxml2's HTML tidier), and
 * NSXMLDocumentXInclude (XInclude processing). Passing one of those to a PARSING door is a NAMED REFUSAL -
 * nil and an error that says which one - rather than a document that quietly came out wrong.
 * NSXMLDocumentIncludeContentTypeDeclaration is the fifth: it describes the HTML/XHTML output, so the
 * WRITING door ignores it and the header says so, because that door has no error to refuse with.
 *
 * TWO BOUNDARIES ARE STATED RATHER THAN LEFT TO BE DISCOVERED: the XML DECLARATION is not read back (XML-a
 * reports a document's `<?xml ...?>` as nothing at all, so a parsed document keeps this class's DEFAULTS -
 * "1.0", "UTF-8", not standalone - and writing it out EMITS a declaration from those), and a CDATA block
 * arrives as a TEXT child whose CDATA marking is not recorded, so writing it back escapes what was raw.
 */

#ifndef FOUNDATION_NSXMLDOCUMENT_H
#define FOUNDATION_NSXMLDOCUMENT_H

#import <Foundation/NSXMLNode.h>

@class NSData;
@class NSDictionary;
@class NSError;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* "The kind of document content" - Apple's four kinds, ours in value (§11.6.1 D2). Only the XML kind is
 * PARSED here; the others are what the Tidy options would act on, and those options are refused. */
typedef enum {
	NSXMLDocumentXMLKind = 0,
	NSXMLDocumentXHTMLKind = 1,
	NSXMLDocumentHTMLKind = 2,
	NSXMLDocumentTextKind = 3,
} NSXMLDocumentContentKind;

/* THE FIVE DOCUMENT OPTIONS, whose members are BITS (an option set: the first version of the NODE options
 * numbered them and made the first one invisible - see NSXMLNode.h). They are declared WITHOUT a type name
 * because Apple's ledger names no type for them. */
enum {
	NSXMLDocumentValidate = 1 << 0,
	NSXMLDocumentTidyHTML = 1 << 1,
	NSXMLDocumentTidyXML = 1 << 2,
	NSXMLDocumentXInclude = 1 << 3,
	NSXMLDocumentIncludeContentTypeDeclaration = 1 << 4,
};

@interface NSXMLDocument : NSXMLNode
{
	NSString *_version;
	NSString *_characterEncoding;
	NSString *_mimeType;
	BOOL _standalone;
	NSXMLDocumentContentKind _contentKind;
}

/* FOUR DOORS TO A DOCUMENT: from bytes, from a file URL, from a string, and from a root element a caller
 * has already built. The first three PARSE - the bridge is inside this file - and each answers nil with an
 * error when the document cannot be read or when an option this system cannot honour was asked for. */
- (nullable id)initWithData:(NSData *)data
		    options:(NSUInteger)mask
		      error:(NSError ** _Nullable)error;
- (nullable id)initWithContentsOfURL:(NSURL *)url
			     options:(NSUInteger)mask
			       error:(NSError ** _Nullable)error;
- (nullable id)initWithXMLString:(NSString *)string
			 options:(NSUInteger)mask
			   error:(NSError ** _Nullable)error;
- (id)initWithRootElement:(nullable id)element;

/* "Returns the class to use for a node" - Apple's hook for subclasses, whose default answers the class it
 * was given, which is what this does. */
+ (Class)replacementClassForClass:(Class)cls;

- (nullable NSString *)version;
- (void)setVersion:(nullable NSString *)version;
- (nullable NSString *)characterEncoding;
- (void)setCharacterEncoding:(nullable NSString *)encoding;
- (BOOL)isStandalone;
- (void)setStandalone:(BOOL)standalone;
- (nullable NSString *)MIMEType;
- (void)setMIMEType:(nullable NSString *)MIMEType;
- (NSXMLDocumentContentKind)documentContentKind;
- (void)setDocumentContentKind:(NSXMLDocumentContentKind)kind;

- (nullable id)rootElement;
- (void)setRootElement:(nullable id)root;

/* THE TWO WRITING DOORS: the document's children as XML, with a declaration built from this document's own
 * attributes. -XMLDataWithOptions: takes the NODE options from NSXMLNode.h (pretty printing, quoting, the
 * empty-element form); the DOCUMENT options are ignored here and refused at the parsing doors - see the
 * header's note. */
- (NSData *)XMLData;
- (NSData *)XMLDataWithOptions:(NSUInteger)options;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSXMLDOCUMENT_H */
