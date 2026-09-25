/*
 * NSXMLDocument.m — THE DOCUMENT AND THE PARSE BRIDGE (XML slice XML-c). See the header for what is shipped,
 * what is registered, and the two stated boundaries (the declaration is not read back; a CDATA block is not
 * marked as such).
 */

#import <Foundation/NSXMLDocument.h>
#import <Foundation/NSXMLParser.h>
#import <Foundation/NSXMLDTD.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

/* ---- THE BRIDGE: A DELEGATE THAT BUILDS THE TREE -------------------------------------------------
 * XML-a produces EVENTS and XML-b holds a TREE, so this is the whole of what turns one into the other - and
 * it is a delegate rather than a second parser, which is what keeps the XML reading rules in ONE place.
 * THREE THINGS IT DECIDES, each stated because each is a choice:
 *  - an element is named with the QUALIFIED name, which is what the document wrote (XML-a hands the local
 *    name and the qualified name separately when namespaces are processed);
 *  - an `xmlns`/`xmlns:p` attribute becomes a NAMESPACE CHILD and not an attribute, which is XML-b's stated
 *    reading of where a declaration lives;
 *  - a CDATA block becomes a TEXT child (its marking is not recorded - see the header). */
@interface FNXMLTreeBuilder : NSObject <NSXMLParserDelegate>
{
	NSXMLNode *_document;
	NSXMLDTD *_dtd;			/* the DTD the declarations built, or nil */
	NSMutableArray *_stack;		/* the elements being built, innermost last */
	NSError *_error;
}
- (id)initWithDocument:(NSXMLNode *)document;
- (nullable NSError *)error;
@end

@implementation FNXMLTreeBuilder

- (id)initWithDocument:(NSXMLNode *)document
{
	self = [super init];
	if (self != nil) {
		_document = document;
		_stack = [[NSMutableArray alloc] init];
	}
	return self;
}

- (nullable NSError *)error
{
	return _error;
}

/* WHERE A NEW ELEMENT GOES: inside the innermost open element, or as a child of the DOCUMENT when nothing is
 * open - which is the root element, and a second one would be a document with two roots. */
- (void)fnAdd:(id)node
{
	if ([_stack count] > 0) {
		[[_stack lastObject] addChild:node];
	} else {
		[_document addChild:node];
	}
}

- (void)parser:(NSXMLParser *)parser
    didStartElement:(NSString *)elementName
      namespaceURI:(NSString *)namespaceURI
     qualifiedName:(NSString *)qualifiedName
	attributes:(NSDictionary *)attributeDict
{
	NSXMLElement *element = [NSXMLNode elementWithName:(qualifiedName != nil ? qualifiedName : elementName)];
	NSArray *names = [attributeDict allKeys];
	NSUInteger i;

	(void)parser;
	(void)namespaceURI;
	for (i = 0; i < [names count]; i++) {
		NSString *attribute = [names objectAtIndex:i];
		NSString *value = [attributeDict objectForKey:attribute];

		if ([attribute isEqual:@"xmlns"]) {
			[element addNamespace:[NSXMLNode namespaceWithName:@"" stringValue:value]];
		} else if ([attribute hasPrefix:@"xmlns:"]) {
			[element addNamespace:[NSXMLNode namespaceWithName:
						[attribute substringFromIndex:6] stringValue:value]];
		} else {
			[element addAttribute:[NSXMLNode attributeWithName:attribute stringValue:value]];
		}
	}
	[self fnAdd:element];
	[_stack addObject:element];
}

- (void)parser:(NSXMLParser *)parser
      didEndElement:(NSString *)elementName
       namespaceURI:(NSString *)namespaceURI
      qualifiedName:(NSString *)qualifiedName
{
	(void)parser;
	(void)elementName;
	(void)namespaceURI;
	(void)qualifiedName;
	if ([_stack count] > 0) {
		[_stack removeLastObject];
	}
}

- (void)parser:(NSXMLParser *)parser foundCharacters:(NSString *)string
{
	(void)parser;
	if ([string length] > 0) {
		[self fnAdd:[NSXMLNode textWithStringValue:string]];
	}
}

- (void)parser:(NSXMLParser *)parser foundCDATA:(NSData *)block
{
	NSString *text = [[NSString alloc] initWithData:block encoding:NSUTF8StringEncoding];

	(void)parser;
	if (text != nil) {
		[self fnAdd:[NSXMLNode textWithStringValue:text]];
	}
}

- (void)parser:(NSXMLParser *)parser foundComment:(NSString *)comment
{
	(void)parser;
	[self fnAdd:[NSXMLNode commentWithStringValue:comment]];
}

- (void)parser:(NSXMLParser *)parser
    foundProcessingInstructionWithTarget:(NSString *)target
				    data:(NSString *)data
{
	(void)parser;
	[self fnAdd:[NSXMLNode processingInstructionWithName:target stringValue:data]];
}

/* THE SIX DTD EVENTS BUILD THE DOCUMENT'S DTD - from what the parser reports, which is what keeps the
 * document's DTD and the parser's reading in agreement. The DTD is made on the first declaration and named
 * for the ROOT element (see the header). */
- (nullable NSXMLDTD *)fnDTD
{
	if (_dtd == nil) {
		_dtd = [[NSXMLDTD alloc] init];

		[_dtd setName:[[_document rootElement] name]];
		[_document setDTD:_dtd];
	}
	return _dtd;
}

- (void)parser:(NSXMLParser *)parser foundElementDeclarationWithName:(NSString *)name model:(NSString *)model
{
	NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithKind:FNDTDNodeKindForModel(model)];

	[node setName:name];
	[node setStringValue:model];

	(void)parser;
	[[self fnDTD] addChild:node];
}

- (void)parser:(NSXMLParser *)parser
    foundAttributeDeclarationWithName:(NSString *)attributeName
			    forElement:(NSString *)elementName
				  type:(NSString *)type
			  defaultValue:(NSString *)defaultValue
{
	NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithKind:NSXMLAttributeKind];

	(void)parser;
	[node setName:attributeName];
	[node setStringValue:defaultValue];
	[node fnSetElementName:elementName];
	[[self fnDTD] addChild:node];
	[node release];
}

- (void)parser:(NSXMLParser *)parser
    foundInternalEntityDeclarationWithName:(NSString *)name
				     value:(NSString *)value
{
	NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithKind:NSXMLEntityGeneralKind];

	(void)parser;
	[node setName:name];
	[node setStringValue:value];
	[[self fnDTD] addChild:node];
	[node release];
}

- (void)parser:(NSXMLParser *)parser
    foundExternalEntityDeclarationWithName:(NSString *)name
				 publicID:(NSString *)publicID
				 systemID:(NSString *)systemID
{
	NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithKind:NSXMLEntityParsedKind];

	(void)parser;
	[node setName:name];
	[node setPublicID:publicID];
	[node setSystemID:systemID];
	[[self fnDTD] addChild:node];
	[node release];
}

- (void)parser:(NSXMLParser *)parser
    foundUnparsedEntityDeclarationWithName:(NSString *)name
				 publicID:(NSString *)publicID
				 systemID:(NSString *)systemID
			     notationName:(NSString *)notationName
{
	NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithKind:NSXMLEntityUnparsedKind];

	(void)parser;
	[node setName:name];
	[node setPublicID:publicID];
	[node setSystemID:systemID];
	[node setNotationName:notationName];
	[[self fnDTD] addChild:node];
	[node release];
}

- (void)parser:(NSXMLParser *)parser
    foundNotationDeclarationWithName:(NSString *)name
			     publicID:(NSString *)publicID
			     systemID:(NSString *)systemID
{
	NSXMLDTDNode *node = [[NSXMLDTDNode alloc] initWithKind:NSXMLNotationDeclarationKind];

	(void)parser;
	[node setName:name];
	[node setPublicID:publicID];
	[node setSystemID:systemID];
	[[self fnDTD] addChild:node];
	[node release];
}

- (void)parser:(NSXMLParser *)parser parseErrorOccurred:(NSError *)parseError
{
	(void)parser;
	[_error release];
	_error = [parseError retain];
}

- (void)dealloc
{
	[_stack release];
	[_dtd release];
	[_error release];
	[super dealloc];
}

@end

/* ---- THE DOCUMENT --------------------------------------------------------------------------------- */
@implementation NSXMLDocument

- (id)init
{
	self = [super initWithKind:NSXMLDocumentKind name:nil value:nil];
	if (self != nil) {
		/* APPLE'S DEFAULTS, which are what a document parsed WITHOUT a readable declaration keeps (XML-a
		 * reports the declaration as nothing - see the header). */
		_version = [@"1.0" copy];
		_characterEncoding = [@"UTF-8" copy];
		_mimeType = [@"text/xml" copy];
		_contentKind = NSXMLDocumentXMLKind;
	}
	return self;
}

- (id)initWithRootElement:(id)element
{
	self = [self init];
	if (self != nil && element != nil) {
		[self addChild:element];
	}
	return self;
}

/* THE OPTIONS THAT DESCRIBE A PIPELINE THIS SYSTEM DOES NOT HAVE ARE REFUSED BY NAME, which is the
 * difference between a document that could not be read the way the caller asked and one that was quietly
 * read a different way. */
+ (nullable NSError *)fnRefusedOption:(NSUInteger)mask
{
	NSString *which = nil;

	if ((mask & NSXMLDocumentValidate) != 0) {
		which = @"NSXMLDocumentValidate (DTD validation)";
	} else if ((mask & NSXMLDocumentTidyHTML) != 0) {
		which = @"NSXMLDocumentTidyHTML (libxml2's HTML parser)";
	} else if ((mask & NSXMLDocumentTidyXML) != 0) {
		which = @"NSXMLDocumentTidyXML (libxml2's tidier)";
	} else if ((mask & NSXMLDocumentXInclude) != 0) {
		which = @"NSXMLDocumentXInclude (XInclude processing)";
	}
	if (which == nil) {
		return nil;
	}
	return [NSError errorWithDomain:NSXMLParserErrorDomain
				   code:NSXMLParserInternalError
			       userInfo:[NSDictionary dictionaryWithObject:
					[NSString stringWithFormat:
						@"this system cannot honour %@, so the document was not read",
						which]
								    forKey:NSLocalizedDescriptionKey]];
}

- (nullable id)initWithData:(NSData *)data options:(NSUInteger)mask error:(NSError **)error
{
	NSError *refused = [NSXMLDocument fnRefusedOption:mask];
	NSXMLParser *parser;
	FNXMLTreeBuilder *builder;
	BOOL ok;

	if (error != NULL) {
		*error = nil;
	}
	if (refused != nil) {
		if (error != NULL) {
			*error = refused;
		}
		[self release];
		return nil;
	}
	self = [self init];
	if (self == nil) {
		return nil;
	}
	parser = [[NSXMLParser alloc] initWithData:data];
	if (parser == nil) {
		[self release];
		return nil;
	}
	builder = [[FNXMLTreeBuilder alloc] initWithDocument:self];
	[parser setDelegate:builder];
	ok = [parser parse];
	if (!ok) {
		if (error != NULL) {
			*error = [builder error] != nil ? [builder error] : [parser parserError];
		}
		[builder release];
		[parser release];
		[self release];
		return nil;
	}
	[builder release];
	[parser release];
	return self;
}

- (nullable id)initWithContentsOfURL:(NSURL *)url options:(NSUInteger)mask error:(NSError **)error
{
	NSData *data;

	if (url == nil || ![url isFileURL]) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:NSXMLParserErrorDomain
						     code:NSXMLParserInternalError
						 userInfo:[NSDictionary dictionaryWithObject:
							@"a document URL must be a file URL"
									    forKey:NSLocalizedDescriptionKey]];
		}
		[self release];
		return nil;
	}
	data = [NSData dataWithContentsOfFile:[url path]];
	if (data == nil) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:NSXMLParserErrorDomain
						     code:NSXMLParserInternalError
						 userInfo:[NSDictionary dictionaryWithObject:
							[NSString stringWithFormat:@"cannot read %@", [url path]]
									    forKey:NSLocalizedDescriptionKey]];
		}
		[self release];
		return nil;
	}
	return [self initWithData:data options:mask error:error];
}

- (nullable id)initWithXMLString:(NSString *)string options:(NSUInteger)mask error:(NSError **)error
{
	id data = [string dataUsingEncoding:NSUTF8StringEncoding];

	return [self initWithData:data options:mask error:error];
}

+ (Class)replacementClassForClass:(Class)cls
{
	/* APPLE'S HOOK FOR SUBCLASSES, whose default answers the class it was given - which is what this does,
	 * and the tree this library builds uses the classes it has. */
	return cls;
}

- (nullable NSString *)version { return _version; }
- (void)setVersion:(nullable NSString *)version { [_version release]; _version = [version copy]; }
- (nullable NSString *)characterEncoding { return _characterEncoding; }
- (void)setCharacterEncoding:(nullable NSString *)encoding
{
	[_characterEncoding release];
	_characterEncoding = [encoding copy];
}
- (BOOL)isStandalone { return _standalone; }
- (void)setStandalone:(BOOL)standalone { _standalone = standalone; }
- (nullable NSString *)MIMEType { return _mimeType; }
- (void)setMIMEType:(nullable NSString *)MIMEType { [_mimeType release]; _mimeType = [MIMEType copy]; }
- (NSXMLDocumentContentKind)documentContentKind { return _contentKind; }
- (void)setDocumentContentKind:(NSXMLDocumentContentKind)kind { _contentKind = kind; }

- (nullable id)rootElement
{
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];

		if ([child kind] == NSXMLElementKind) {
			return child;
		}
	}
	return nil;
}

- (nullable NSXMLDTD *)dtd
{
	return _dtd;
}

- (void)setDTD:(nullable NSXMLDTD *)dtd
{
	[dtd retain];
	[_dtd release];
	_dtd = dtd;
}

- (void)setRootElement:(id)root
{
	id existing = [self rootElement];

	if (existing != nil) {
		[existing detach];
	}
	if (root != nil) {
		[self insertChild:root atIndex:0];
	}
}

- (NSData *)XMLData
{
	return [self XMLDataWithOptions:0];
}

- (NSData *)XMLDataWithOptions:(NSUInteger)options
{
	/* THE DECLARATION IS BUILT FROM THIS DOCUMENT'S OWN ATTRIBUTES, which is the other half of the stated
	 * boundary: a parsed document keeps the defaults rather than the values its bytes carried, and what is
	 * written out is what this object says about itself. */
	NSMutableString *text = [NSMutableString string];
	NSString *encoding = _characterEncoding != nil ? _characterEncoding : @"UTF-8";

	[text appendFormat:@"<?xml version=\"%@\" encoding=\"%@\"%@?>",
		_version != nil ? _version : @"1.0", encoding,
		_standalone ? @" standalone=\"yes\"" : @""];
	if (_dtd != nil) {
		/* THE DTD IS WRITTEN BACK WHERE IT WAS READ FROM: the DOCTYPE, with its identifiers and its
		 * internal subset, in the one spelling that can be read again. */
		NSString *rootName = [[self rootElement] name];

		(void)rootName;
		[text appendFormat:@"\n<!DOCTYPE %@", rootName != nil ? rootName : @""];
		if ([_dtd publicID] != nil) {
			[text appendFormat:@" PUBLIC \"%@\"", [_dtd publicID]];
			if ([_dtd systemID] != nil) {
				[text appendFormat:@" \"%@\"", [_dtd systemID]];
			}
		} else if ([_dtd systemID] != nil) {
			[text appendFormat:@" SYSTEM \"%@\"", [_dtd systemID]];
		}
		if ([[_dtd internalSubset] length] > 0) {
			[text appendFormat:@" [%@]", [_dtd internalSubset]];
		}
		[text appendString:@">"];
	}
	[text appendString:[self XMLStringWithOptions:(NSXMLNodeOptions)options]];
	return [text dataUsingEncoding:NSUTF8StringEncoding];
}

- (void)dealloc
{
	[_dtd release];
	[_version release];
	[_characterEncoding release];
	[_mimeType release];
	[super dealloc];
}

@end
