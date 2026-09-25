/*
 * NSXMLDTD.m — THE DTD AS A DATA MODEL (XML slice XML-d): one declaration parsed out of its own text, the
 * container that holds them, and the five entities XML itself defines. See the header for the boundary: the
 * PARSER-SIDE half (firing the six DTD events while reading a document) is slice XML-e.
 */

#import <Foundation/NSXMLDTD.h>
#import <Foundation/NSXMLParser.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

/* THE PRIVATE HALF, declared before it is used: the element name an attribute declaration belongs to (which
 * -attributeDeclarationForName:elementName: looks up by), and the fact that this class OVERRIDES the
 * serializer its parent declares in its own private category. */
@interface NSXMLDTDNode (FNPrivate)
- (void)fnSetElementName:(NSString *)name;
- (NSString *)fnElementName;
@end

/* ---- THE DECLARATION READER -----------------------------------------------------------------------
 * One function over a string, because every form XML declares is `<!KEYWORD ...>` with quoted parts that may
 * contain the very characters the scanner looks for: the reader walks the text and RESPECTS QUOTES, which is
 * the only way `<!ENTITY e "a > b">` is read as one declaration. */
typedef struct {
	NSString *text;
	NSUInteger at;
	NSUInteger length;
} fn_dtd_reader;

static BOOL fn_dtd_ws(fn_dtd_reader *r)
{
	BOOL skipped = NO;

	while (r->at < r->length) {
		unichar c = [r->text characterAtIndex:r->at];

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			r->at++;
			skipped = YES;
			continue;
		}
		break;
	}
	return skipped;
}

static NSString *fn_dtd_word(fn_dtd_reader *r)
{
	NSUInteger start;

	fn_dtd_ws(r);
	start = r->at;
	while (r->at < r->length) {
		unichar c = [r->text characterAtIndex:r->at];

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '>' || c == '(' ||
		    c == ')' || c == '|' || c == ',') {
			break;
		}
		r->at++;
	}
	return r->at > start ? [r->text substringWithRange:NSMakeRange(start, r->at - start)] : nil;
}

/* A QUOTED LITERAL, with the quote it was written in - which is what makes "a > b" one value. */
static NSString *fn_dtd_quoted(fn_dtd_reader *r)
{
	unichar quote;
	NSUInteger start;

	fn_dtd_ws(r);
	if (r->at >= r->length) {
		return nil;
	}
	quote = [r->text characterAtIndex:r->at];
	if (quote != '"' && quote != '\'') {
		return nil;
	}
	r->at++;
	start = r->at;
	while (r->at < r->length && [r->text characterAtIndex:r->at] != quote) {
		r->at++;
	}
	if (r->at >= r->length) {
		return nil;
	}
	{
		NSString *literal = [r->text substringWithRange:NSMakeRange(start, r->at - start)];

		/* AND THE CURSOR CROSSES THE CLOSING QUOTE, which is the bug that made TWO checks fail at once:
		 * without this step the NEXT quoted literal started AT the previous closing quote, so a NOTATION's
		 * second value came back as the spaces between them and an `<!ENTITY … SYSTEM "uri" NDATA gif>`
		 * marker was blocked by a stray quote character. ONE reader rule, two symptoms. */
		r->at++;
		return literal;
	}
}

/* WHATEVER IS LEFT UP TO A '>' THAT IS NOT INSIDE A QUOTE: a content model, a default clause, a URI. */
static NSString *fn_dtd_rest(fn_dtd_reader *r)
{
	NSUInteger start = r->at;
	unichar quote = 0;

	while (r->at < r->length) {
		unichar c = [r->text characterAtIndex:r->at];

		if (quote != 0) {
			if (c == quote) {
				quote = 0;
			}
		} else if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '>') {
			break;
		}
		r->at++;
	}
	{
		NSString *rest = [r->text substringWithRange:NSMakeRange(start, r->at - start)];

		return [rest stringByTrimmingCharactersInSet:
				[NSCharacterSet whitespaceAndNewlineCharacterSet]];
	}
}

/* THE ATTRIBUTE TYPES, in the order XML names them: what an <!ATTLIST> declaration's third word means. */
static NSXMLDTDNodeKind fn_dtd_attribute_kind(NSString *type)
{
	if (type == nil) return NSXMLAttributeCDATAKind;
	if ([type isEqual:@"CDATA"]) return NSXMLAttributeCDATAKind;
	if ([type isEqual:@"ID"]) return NSXMLAttributeIDKind;
	if ([type isEqual:@"IDREF"]) return NSXMLAttributeIDRefKind;
	if ([type isEqual:@"IDREFS"]) return NSXMLAttributeIDRefsKind;
	if ([type isEqual:@"ENTITY"]) return NSXMLAttributeEntityKind;
	if ([type isEqual:@"ENTITIES"]) return NSXMLAttributeEntitiesKind;
	if ([type isEqual:@"NMTOKEN"]) return NSXMLAttributeNMTokenKind;
	if ([type isEqual:@"NMTOKENS"]) return NSXMLAttributeNMTokensKind;
	if ([type isEqual:@"NOTATION"]) return NSXMLAttributeNotationKind;
	if ([type hasPrefix:@"("]) return NSXMLAttributeEnumerationKind;
	return NSXMLAttributeCDATAKind;
}

static NSString *fn_dtd_attribute_type(NSXMLDTDNodeKind kind)
{
	switch (kind) {
	case NSXMLAttributeIDKind: return @"ID";
	case NSXMLAttributeIDRefKind: return @"IDREF";
	case NSXMLAttributeIDRefsKind: return @"IDREFS";
	case NSXMLAttributeEntityKind: return @"ENTITY";
	case NSXMLAttributeEntitiesKind: return @"ENTITIES";
	case NSXMLAttributeNMTokenKind: return @"NMTOKEN";
	case NSXMLAttributeNMTokensKind: return @"NMTOKENS";
	case NSXMLAttributeNotationKind: return @"NOTATION";
	case NSXMLAttributeEnumerationKind: return @"(enumeration)";
	default: return @"CDATA";
	}
}

/* THE CONTENT MODEL'S FORM, which is the element declaration's DTD kind. */
static NSXMLDTDNodeKind fn_dtd_element_kind(NSString *model)
{
	if (model == nil) return NSXMLElementDeclarationUndefinedKind;
	if ([model isEqual:@"EMPTY"]) return NSXMLElementDeclarationEmptyKind;
	if ([model isEqual:@"ANY"]) return NSXMLElementDeclarationAnyKind;
	if ([model hasPrefix:@"(#PCDATA"]) return NSXMLElementDeclarationMixedKind;
	if ([model hasPrefix:@"("]) return NSXMLElementDeclarationElementKind;
	return NSXMLElementDeclarationUndefinedKind;
}

/* ONE DECLARATION, PARSED INTO THE NODES IT DECLARES - a LIST, because ONE <!ATTLIST> may declare several
 * attributes and Apple's own event fires once per attribute. */
static NSArray *fn_dtd_parse_declaration(NSString *declaration)
{
	NSMutableArray *answer = [NSMutableArray array];
	fn_dtd_reader r;
	NSString *keyword;
	NSString *first;

	r.text = declaration;
	r.at = 0;
	r.length = [declaration length];
	fn_dtd_ws(&r);
	if (![declaration hasPrefix:@"<!"]) {
		return answer;
	}
	r.at = 2;
	keyword = fn_dtd_word(&r);
	if ([keyword isEqual:@"ELEMENT"]) {
		NSString *name = fn_dtd_word(&r);
		NSString *model = fn_dtd_rest(&r);
		NSXMLDTDNode *node;

		if (name == nil) {
			return answer;
		}
		node = [[NSXMLDTDNode alloc] initWithKind:fn_dtd_element_kind(model)];
		[node setName:name];
		[node setStringValue:model];
		[answer addObject:node];
		[node release];
		return answer;
	}
	if ([keyword isEqual:@"ATTLIST"]) {
		NSString *element = fn_dtd_word(&r);

		if (element == nil) {
			return answer;
		}
		for (;;) {
			NSString *attribute = fn_dtd_word(&r);

			if (attribute == nil) {
				break;
			}
			{
				NSString *type = fn_dtd_word(&r);
				NSString *clause = fn_dtd_rest(&r);
				NSXMLDTDNode *node = [[NSXMLDTDNode alloc]
							initWithKind:fn_dtd_attribute_kind(type)];

				[node setName:attribute];
				[node setStringValue:clause];
				[node fnSetElementName:element];
				[answer addObject:node];
				[node release];
			}
			break;		/* ONE attribute per node: see the note above */
		}
		return answer;
	}
	if ([keyword isEqual:@"ENTITY"]) {
		BOOL parameter = NO;
		NSString *name;
		NSString *value;
		NSXMLDTDNode *node;

		fn_dtd_ws(&r);
		if (r.at < r.length && [r.text characterAtIndex:r.at] == '%') {
			parameter = YES;
			r.at++;
		}
		name = fn_dtd_word(&r);
		if (name == nil) {
			return answer;
		}
		value = fn_dtd_quoted(&r);
		node = [[NSXMLDTDNode alloc] initWithKind:parameter ? NSXMLEntityParameterKind
								     : NSXMLEntityGeneralKind];
		[node setName:name];
		if (value != nil) {
			[node setStringValue:value];
		} else {
			NSString *word = fn_dtd_word(&r);

			if ([word isEqual:@"SYSTEM"]) {
				[node setSystemID:fn_dtd_quoted(&r)];
			} else if ([word isEqual:@"PUBLIC"]) {
				[node setPublicID:fn_dtd_quoted(&r)];
				[node setSystemID:fn_dtd_quoted(&r)];
			}
			/* `NDATA notation` IS READ AS A WORD AND NOT AS A PREFIX OF WHAT IS LEFT, because "what is
			 * left" needed a trim the tree's NSString may not implement - and a dependency that answers
			 * nil silently is exactly how a marker goes missing while everything else looks right. */
			fn_dtd_ws(&r);
			if (r.at < r.length) {
				NSString *marker = fn_dtd_word(&r);

				if ([marker isEqual:@"NDATA"]) {
					[node setDTDKind:NSXMLEntityUnparsedKind];
					[node setNotationName:fn_dtd_word(&r)];
				}
			}
		}
		[answer addObject:node];
		[node release];
		return answer;
	}
	if ([keyword isEqual:@"NOTATION"]) {
		NSString *name = fn_dtd_word(&r);
		NSString *word;
		NSXMLDTDNode *node;

		if (name == nil) {
			return answer;
		}
		node = [[NSXMLDTDNode alloc] initWithKind:NSXMLNotationDeclarationKind];
		[node setName:name];
		word = fn_dtd_word(&r);
		if ([word isEqual:@"PUBLIC"]) {
			[node setPublicID:fn_dtd_quoted(&r)];
			[node setSystemID:fn_dtd_quoted(&r)];
		} else if ([word isEqual:@"SYSTEM"]) {
			[node setSystemID:fn_dtd_quoted(&r)];
		}
		[answer addObject:node];
		[node release];
		return answer;
	}
	(void)first;
	return answer;
}

@implementation NSXMLDTDNode

- (id)initWithKind:(NSXMLDTDNodeKind)kind
{
	self = [super initWithKind:NSXMLDTDKind name:nil value:nil];
	if (self != nil) {
		_dtdKind = kind;
	}
	return self;
}

- (nullable id)initWithXMLString:(NSString *)string
{
	NSArray *nodes;

	if (string == nil) {
		[self release];
		return nil;
	}
	nodes = fn_dtd_parse_declaration(string);
	if ([nodes count] == 0) {
		/* A STRING THAT IS NOT A DECLARATION IS REFUSED rather than kept as an empty node: a DTD node with
		 * no kind to say what it declares is a node a caller cannot use. */
		[self release];
		return nil;
	}
	{
		NSXMLDTDNode *first = [nodes objectAtIndex:0];

		[self release];
		return [first retain];
	}
}

- (void)fnSetElementName:(NSString *)name
{
	[_elementName release];
	_elementName = [name copy];
}

- (NSString *)fnElementName
{
	return _elementName;
}

- (NSXMLDTDNodeKind)DTDKind { return _dtdKind; }
- (void)setDTDKind:(NSXMLDTDNodeKind)kind { _dtdKind = kind; }
- (nullable NSString *)publicID { return _publicID; }
- (void)setPublicID:(nullable NSString *)publicID { [_publicID release]; _publicID = [publicID copy]; }
- (nullable NSString *)systemID { return _systemID; }
- (void)setSystemID:(nullable NSString *)systemID { [_systemID release]; _systemID = [systemID copy]; }
- (nullable NSString *)notationName { return _notationName; }
- (void)setNotationName:(nullable NSString *)notationName
{
	[_notationName release];
	_notationName = [notationName copy];
}

/* THE DECLARATION WRITTEN BACK OUT, which is the shape a DTD is read in - and the one place this class
 * differs from its parent's serializer, because a declaration is not an element. */
- (NSString *)fnXMLStringWithOptions:(NSXMLNodeOptions)options depth:(NSUInteger)depth
{
	NSString *name = [self name] != nil ? [self name] : @"";
	NSString *value = [self stringValue];

	(void)options;
	(void)depth;
	if ([self kind] != NSXMLDTDKind) {
		return [super fnXMLStringWithOptions:options depth:depth];
	}
	switch (_dtdKind) {
	case NSXMLElementDeclarationAnyKind:
	case NSXMLElementDeclarationEmptyKind:
	case NSXMLElementDeclarationElementKind:
	case NSXMLElementDeclarationMixedKind:
	case NSXMLElementDeclarationUndefinedKind:
		return [NSString stringWithFormat:@"<!ELEMENT %@ %@>", name,
			[value length] > 0 ? value : @"ANY"];
	case NSXMLNotationDeclarationKind:
		if (_publicID != nil) {
			return [NSString stringWithFormat:@"<!NOTATION %@ PUBLIC \"%@\" \"%@\">", name, _publicID,
				_systemID != nil ? _systemID : @""];
		}
		return [NSString stringWithFormat:@"<!NOTATION %@ SYSTEM \"%@\">", name,
			_systemID != nil ? _systemID : @""];
	default: {
		NSString *kindText = name;

		if (_dtdKind == NSXMLEntityParameterKind || _dtdKind == NSXMLEntityGeneralKind ||
		    _dtdKind == NSXMLEntityParsedKind || _dtdKind == NSXMLEntityUnparsedKind) {
			NSString *external = _systemID != nil ?
				[NSString stringWithFormat:@" SYSTEM \"%@\"", _systemID] : nil;

			return [NSString stringWithFormat:@"<!ENTITY %@%@%@%@>",
				_dtdKind == NSXMLEntityParameterKind ? @"% " : @"", kindText,
				external != nil ? external :
					[NSString stringWithFormat:@" \"%@\"", value != nil ? value : @""],
				_notationName != nil ?
					[NSString stringWithFormat:@" NDATA %@", _notationName] : @""];
		}
		/* AN ATTRIBUTE DECLARATION, whose element this node remembers (see the header). */
		return [NSString stringWithFormat:@"<!ATTLIST %@ %@ %@ %@>",
			_elementName != nil ? _elementName : @"", name, fn_dtd_attribute_type(_dtdKind),
			value != nil ? value : @"#IMPLIED"];
	}
	}
}

- (void)dealloc
{
	[_publicID release];
	[_systemID release];
	[_notationName release];
	[_elementName release];
	[super dealloc];
}

@end

@implementation NSXMLDTD

- (id)init
{
	return [super initWithKind:NSXMLDTDKind name:nil value:nil];
}

- (nullable id)initWithData:(NSData *)data options:(NSUInteger)mask error:(NSError **)error
{
	NSString *text;

	(void)mask;
	if (error != NULL) {
		*error = nil;
	}
	text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
	if (text == nil) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:@"NSXMLParserErrorDomain"
						     code:NSXMLParserEncodingNotSupportedError
						 userInfo:[NSDictionary dictionaryWithObject:
							@"the DTD's bytes are not UTF-8"
									    forKey:NSLocalizedDescriptionKey]];
		}
		[self release];
		return nil;
	}
	self = [self init];
	if (self != nil) {
		[self setInternalSubset:text];
	}
	[text release];
	return self;
}

- (nullable id)initWithContentsOfURL:(NSURL *)url options:(NSUInteger)mask error:(NSError **)error
{
	NSData *data;

	if (url == nil || ![url isFileURL]) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:@"NSXMLParserErrorDomain"
						     code:NSXMLParserInternalError
						 userInfo:[NSDictionary dictionaryWithObject:
							@"a DTD URL must be a file URL"
									    forKey:NSLocalizedDescriptionKey]];
		}
		[self release];
		return nil;
	}
	data = [NSData dataWithContentsOfFile:[url path]];
	if (data == nil) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:@"NSXMLParserErrorDomain"
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

- (nullable NSString *)publicID { return _publicID; }
- (void)setPublicID:(nullable NSString *)publicID { [_publicID release]; _publicID = [publicID copy]; }
- (nullable NSString *)systemID { return _systemID; }
- (void)setSystemID:(nullable NSString *)systemID { [_systemID release]; _systemID = [systemID copy]; }
- (nullable NSString *)internalSubset { return _internalSubset; }

/* SETTING THE SUBSET PARSES IT: every `<!...>` at the top level becomes a child, quotes respected so that a
 * `>` inside a literal does not end a declaration early. */
- (void)setInternalSubset:(nullable NSString *)internalSubset
{
	NSString *text = internalSubset;
	NSUInteger at = 0;
	NSUInteger length = [text length];

	[_internalSubset release];
	_internalSubset = [text copy];
	while (at < length) {
		NSRange open = [text rangeOfString:@"<!" options:0 range:NSMakeRange(at, length - at)];
		unichar quote = 0;
		NSUInteger end;

		if (open.location == NSNotFound) {
			break;
		}
		end = open.location + 2;
		while (end < length) {
			unichar c = [text characterAtIndex:end];

			if (quote != 0) {
				if (c == quote) {
					quote = 0;
				}
			} else if (c == '"' || c == '\'') {
				quote = c;
			} else if (c == '>') {
				break;
			}
			end++;
		}
		if (end >= length) {
			break;
		}
		{
			NSString *declaration = [text substringWithRange:
							NSMakeRange(open.location, end - open.location + 1)];
			NSArray *nodes = fn_dtd_parse_declaration(declaration);
			NSUInteger i;

			for (i = 0; i < [nodes count]; i++) {
				[self addChild:[nodes objectAtIndex:i]];
			}
		}
		at = end + 1;
	}
}

- (nullable NSXMLDTDNode *)fnFirstChildOfKind:(NSXMLDTDNodeKind)kind named:(NSString *)name
{
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];

		if ([child isKindOfClass:[NSXMLDTDNode class]] && [child DTDKind] == kind &&
		    [[child name] isEqual:name]) {
			return child;
		}
	}
	return nil;
}

- (nullable NSXMLDTDNode *)elementDeclarationForName:(NSString *)name
{
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];

		if ([child isKindOfClass:[NSXMLDTDNode class]] && [[child name] isEqual:name] &&
		    [child DTDKind] != NSXMLAttributeCDATAKind &&
		    ([child DTDKind] == NSXMLElementDeclarationAnyKind ||
		     [child DTDKind] == NSXMLElementDeclarationEmptyKind ||
		     [child DTDKind] == NSXMLElementDeclarationElementKind ||
		     [child DTDKind] == NSXMLElementDeclarationMixedKind ||
		     [child DTDKind] == NSXMLElementDeclarationUndefinedKind)) {
			return child;
		}
	}
	return nil;
}

- (nullable NSXMLDTDNode *)attributeDeclarationForName:(NSString *)name elementName:(NSString *)elementName
{
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];

		if (![child isKindOfClass:[NSXMLDTDNode class]] || ![[child name] isEqual:name]) {
			continue;
		}
		if ([[child fnElementName] isEqual:elementName]) {
			return child;
		}
	}
	return nil;
}

- (nullable NSXMLDTDNode *)entityDeclarationForName:(NSString *)name
{
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];

		if (![child isKindOfClass:[NSXMLDTDNode class]] || ![[child name] isEqual:name]) {
			continue;
		}
		if ([child DTDKind] == NSXMLEntityGeneralKind ||
		    [child DTDKind] == NSXMLEntityParameterKind ||
		    [child DTDKind] == NSXMLEntityParsedKind ||
		    [child DTDKind] == NSXMLEntityUnparsedKind) {
			return child;
		}
	}
	return nil;
}

- (nullable NSXMLDTDNode *)notationDeclarationForName:(NSString *)name
{
	return [self fnFirstChildOfKind:NSXMLNotationDeclarationKind named:name];
}

+ (nullable NSXMLDTDNode *)predefinedEntityDeclarationForName:(NSString *)name
{
	NSXMLDTDNode *node;
	NSString *value = nil;

	/* THE FIVE XML ITSELF DEFINES, from the specification's own table rather than from any document - which
	 * is what "predefined" means, and why this door answers even for a DTD that declares nothing. */
	if ([name isEqual:@"lt"]) value = @"<";
	else if ([name isEqual:@"gt"]) value = @">";
	else if ([name isEqual:@"amp"]) value = @"&";
	else if ([name isEqual:@"quot"]) value = @"\"";
	else if ([name isEqual:@"apos"]) value = @"'";
	else return nil;
	node = [[NSXMLDTDNode alloc] initWithKind:NSXMLEntityGeneralKind];
	[node setName:name];
	[node setStringValue:value];
	return [node autorelease];
}

- (void)dealloc
{
	[_publicID release];
	[_systemID release];
	[_internalSubset release];
	[super dealloc];
}

@end
