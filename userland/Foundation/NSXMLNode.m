/*
 * NSXMLNode.m — THE TREE (XML slice XML-b): the node kinds and their factories, the accessors, the
 * navigation, the children and attributes, the namespace helpers and the XML serialization. See the header
 * for what is shipped, what is registered (XPath, XSLT, validation, HTML tidying, canonicalization) and for
 * the two storage readings this file states: ATTRIBUTES LIVE IN THE ELEMENT'S OWN STORE, and a NAMESPACE IS
 * A CHILD of the element it is declared on.
 */

#import <Foundation/NSXMLNode.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSString.h>

/* ---- THE ESCAPING, ONE PLACE PER CONTEXT ---------------------------------------------------------
 * Text and attribute values are escaped by DIFFERENT rules (a quote is fatal in one and ordinary in the
 * other), which is why there are two functions and not one - the mistake a single escaper makes is exactly
 * the one that breaks documents. */
/* SPACES, BUILT BY HAND: this library's formatters do not implement every C99 specifier - "%C" came back
 * literal when the parser tried it, and a WIDTH FROM AN ARGUMENT ("%*s") came back literal here, which the
 * probe showed in its own readable output. A width is a loop, not a format. */
static NSString *fn_xml_indent(NSUInteger depth)
{
	NSMutableString *answer = [NSMutableString stringWithCapacity:depth * 2];
	NSUInteger i;

	for (i = 0; i < depth * 2; i++) {
		[answer appendString:@" "];
	}
	return answer;
}

static NSString *fn_xml_escape_text(NSString *text)
{
	NSMutableString *answer;
	NSUInteger i, length = [text length];

	if ([text rangeOfString:@"&"].location == NSNotFound &&
	    [text rangeOfString:@"<"].location == NSNotFound &&
	    [text rangeOfString:@">"].location == NSNotFound) {
		return text;
	}
	answer = [NSMutableString stringWithCapacity:length + 8];
	for (i = 0; i < length; i++) {
		unichar c = [text characterAtIndex:i];

		if (c == '&') [answer appendString:@"&amp;"];
		else if (c == '<') [answer appendString:@"&lt;"];
		else if (c == '>') [answer appendString:@"&gt;"];
		else [answer appendString:[NSString stringWithCharacters:&c length:1]];
	}
	return answer;
}

static NSString *fn_xml_escape_attribute(NSString *text, BOOL singleQuoted)
{
	NSMutableString *answer;
	NSUInteger i, length = [text length];

	answer = [NSMutableString stringWithCapacity:length + 8];
	for (i = 0; i < length; i++) {
		unichar c = [text characterAtIndex:i];

		if (c == '&') [answer appendString:@"&amp;"];
		else if (c == '<') [answer appendString:@"&lt;"];
		else if (c == '"' && !singleQuoted) [answer appendString:@"&quot;"];
		else if (c == '\'' && singleQuoted) [answer appendString:@"&apos;"];
		else [answer appendString:[NSString stringWithCharacters:&c length:1]];
	}
	return answer;
}

@implementation NSXMLNode

/* A NODE IS MADE PRIVATELY AND ANSWERED THROUGH THE FACTORIES, which is what keeps the kinds consistent
 * with the fields they use. */
- (id)initWithKind:(NSXMLNodeKind)kind name:(NSString *)name value:(NSString *)value
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_kind = kind;
	_name = [name copy];
	_stringValue = [value copy];
	_children = (kind == NSXMLDocumentKind || kind == NSXMLElementKind) ?
			[[NSMutableArray alloc] init] : nil;
	return self;
}

+ (id)document
{
	return [[[NSXMLNode alloc] initWithKind:NSXMLDocumentKind name:nil value:nil] autorelease];
}

+ (id)documentWithRootElement:(id)element
{
	NSXMLNode *document = [self document];

	[document addChild:element];
	return document;
}

+ (id)elementWithName:(NSString *)name
{
	return [[[NSXMLElement alloc] initWithName:name] autorelease];
}

+ (id)elementWithName:(NSString *)name stringValue:(NSString *)stringValue
{
	return [[[NSXMLElement alloc] initWithName:name stringValue:stringValue] autorelease];
}

+ (id)elementWithName:(NSString *)name children:(NSArray *)children attributes:(NSArray *)attributes
{
	return [[[NSXMLElement alloc] initWithName:name children:children attributes:attributes] autorelease];
}

+ (id)attributeWithName:(NSString *)name stringValue:(NSString *)stringValue
{
	return [[[NSXMLNode alloc] initWithKind:NSXMLAttributeKind name:name value:stringValue] autorelease];
}

+ (id)namespaceWithName:(NSString *)name stringValue:(NSString *)stringValue
{
	return [[[NSXMLNode alloc] initWithKind:NSXMLNamespaceKind name:name value:stringValue] autorelease];
}

+ (id)processingInstructionWithName:(NSString *)name stringValue:(NSString *)stringValue
{
	return [[[NSXMLNode alloc] initWithKind:NSXMLProcessingInstructionKind
					  name:name
					 value:stringValue] autorelease];
}

+ (id)commentWithStringValue:(NSString *)stringValue
{
	return [[[NSXMLNode alloc] initWithKind:NSXMLCommentKind name:nil value:stringValue] autorelease];
}

+ (id)textWithStringValue:(NSString *)stringValue
{
	return [[[NSXMLNode alloc] initWithKind:NSXMLTextKind name:nil value:stringValue] autorelease];
}

+ (id)DTDNodeWithXMLString:(NSString *)string
{
	/* XML-d LANDED, SO THIS DOOR IS OPEN - AND IT OPENS THROUGH THE RUNTIME rather than through an import:
	 * the declaration reader lives where the kinds it decides between live (NSXMLDTDNode's own file), and a
	 * header import here would make the tree depend on the DTD it holds. The class is asked for by name,
	 * which is what a forward declaration cannot do and what keeps the two families one-way. */
	Class nodeClass = NSClassFromString(@"NSXMLDTDNode");

	if (nodeClass == Nil) {
		return nil;
	}
	return [[nodeClass alloc] initWithXMLString:string];
}

+ (id)localNameForName:(NSString *)name
{
	NSRange colon = [name rangeOfString:@":"];

	if (name == nil) {
		return nil;
	}
	return colon.location == NSNotFound ? name : [name substringFromIndex:colon.location + 1];
}

+ (id)prefixForName:(NSString *)name
{
	NSRange colon = [name rangeOfString:@":"];

	if (name == nil || colon.location == NSNotFound) {
		return nil;
	}
	return [name substringToIndex:colon.location];
}

+ (id)predefinedNamespaceForPrefix:(NSString *)name
{
	/* THE THREE PREFIXES XML ITSELF BINDS (the XML namespace, the namespace namespace, and the empty prefix
	 * which means "no namespace"), from the W3C's own table rather than from Apple's page - Apple publishes
	 * the method and a WIDER list whose values are the W3C's anyway. */
	if (name == nil || [name length] == 0) {
		return nil;
	}
	if ([name isEqual:@"xml"]) {
		return @"http://www.w3.org/XML/1998/namespace";
	}
	if ([name isEqual:@"xmlns"]) {
		return @"http://www.w3.org/2000/xmlns/";
	}
	return nil;
}

- (NSXMLNodeKind)kind { return (NSXMLNodeKind)_kind; }
- (nullable NSString *)name { return _name; }
- (void)setName:(nullable NSString *)name { [_name release]; _name = [name copy]; }
- (nullable NSString *)stringValue { return _stringValue; }
- (void)setStringValue:(nullable NSString *)stringValue
{
	[_stringValue release];
	_stringValue = [stringValue copy];
}
- (nullable id)objectValue { return self; }
- (nullable NSString *)URI { return _uri; }
- (void)setURI:(nullable NSString *)URI { [_uri release]; _uri = [URI copy]; }

- (NSUInteger)index
{
	NSXMLNode *parent = _parent;

	if (parent == nil) {
		return 0;
	}
	return [[parent children] indexOfObjectIdenticalTo:self];
}

- (NSUInteger)level
{
	NSUInteger level = 0;
	id node = _parent;

	while (node != nil) {
		level++;
		node = [node parent];
	}
	return level;
}

- (nullable id)rootDocument
{
	id node = self;

	while ([node parent] != nil) {
		node = [node parent];
	}
	return [node kind] == NSXMLDocumentKind ? node : nil;
}

- (nullable id)parent { return _parent; }

- (nullable id)childAtIndex:(NSUInteger)index
{
	if (_children == nil || index >= [_children count]) {
		return nil;
	}
	return [_children objectAtIndex:index];
}

- (NSUInteger)childCount { return _children != nil ? [_children count] : 0; }
- (nullable NSArray *)children
{
	if (_children == nil) {
		return nil;
	}
	return [NSArray arrayWithArray:_children];
}

/* THE TREE ORDER, which is what -nextNode/-previousNode mean: the next node in DOCUMENT order is the first
 * child, else the next sibling, else the next sibling of the nearest ancestor that has one. */
- (nullable id)nextNode
{
	id node = self;
	id sibling;

	if ([self childCount] > 0) {
		return [self childAtIndex:0];
	}
	for (;;) {
		sibling = [node nextSibling];
		if (sibling != nil) {
			return sibling;
		}
		node = [node parent];
		if (node == nil) {
			return nil;
		}
	}
}

- (nullable id)previousNode
{
	id parent = _parent;
	NSUInteger at;

	if (parent == nil) {
		return nil;
	}
	at = [parent index];
	if (at == 0) {
		return parent;
	}
	{
		id before = [parent childAtIndex:at - 1];

		while ([before childCount] > 0) {
			before = [before childAtIndex:[before childCount] - 1];
		}
		return before;
	}
}

- (nullable id)nextSibling
{
	id parent = _parent;
	NSUInteger at;

	if (parent == nil) {
		return nil;
	}
	at = [parent index];
	return [parent childAtIndex:at + 1];
}

- (nullable id)previousSibling
{
	id parent = _parent;
	NSUInteger at;

	if (parent == nil) {
		return nil;
	}
	at = [parent index];
	return at == 0 ? nil : [parent childAtIndex:at - 1];
}

- (void)detach
{
	id parent = _parent;

	if (parent != nil && [parent respondsToSelector:@selector(fnRemoveChild:)]) {
		[parent fnRemoveChild:self];
	}
}

/* ---- ADDING AND REMOVING, WHICH IS WHAT KEEPS PARENT AND CHILD IN STEP ---------------------------- */
- (void)addChild:(id)child
{
	if (child == nil) {
		return;
	}
	if (_children == nil) {
		_children = [[NSMutableArray alloc] init];
	}
	if ([child parent] != nil) {
		[child detach];		/* A NODE HAS ONE PARENT: taking it here takes it away from there */
	}
	[_children addObject:child];
	[child fnAdopt:self];
}

- (void)fnRemoveChild:(id)child
{
	if (_children == nil) {
		return;
	}
	[_children removeObjectIdenticalTo:child];
	[child fnAdopt:nil];
}

- (void)insertChild:(id)child atIndex:(NSUInteger)index
{
	if (child == nil) {
		return;
	}
	if (_children == nil) {
		_children = [[NSMutableArray alloc] init];
	}
	if (index > [_children count]) {
		index = [_children count];
	}
	if ([child parent] != nil) {
		[child detach];
	}
	[_children insertObject:child atIndex:index];
	[child fnAdopt:self];
}

/* THE FOUR THAT WERE DECLARED AND MISSING, each built on the three primitives above rather than beside
 * them: a list insert that keeps its ORDER, a removal by index, a replacement that ADOPTS before it
 * detaches, and a whole-list set that heals the back edges. Apple's pages define what each does; what is
 * chosen here (and asserted by the probe) is the out-of-range shape, which follows THIS class's own
 * -childAtIndex: - a no-op rather than a raise, because a tree edit that cannot happen should not take the
 * caller's process with it. */
- (void)insertChildren:(NSArray *)children atIndex:(NSUInteger)index
{
	NSUInteger i;

	if (children == nil) {
		return;
	}
	/* THE INDEX ADVANCES: inserting [a, b] at 2 of [x, y] must give [x, y, a, b]. Inserting both at the
	 * SAME index would reverse them, which is the bug this loop exists to not have. */
	for (i = 0; i < [children count]; i++) {
		[self insertChild:[children objectAtIndex:i] atIndex:index + i];
	}
}

- (void)removeChildAtIndex:(NSUInteger)index
{
	if (_children == nil || index >= [_children count]) {
		return;
	}
	[self fnRemoveChild:[_children objectAtIndex:index]];
}

- (void)replaceChildAtIndex:(NSUInteger)index withNode:(id)node
{
	id old;

	if (_children == nil || index >= [_children count] || node == nil) {
		return;
	}
	/* INSERT FIRST, THEN DETACH THE OLD ONE: inserting at `index` pushes the old node to index+1 and gives
	 * the replacement its parent, and removing the old node by identity then leaves it parentless without
	 * ever leaving the array in a state where the replacement is not there. */
	old = [_children objectAtIndex:index];
	[self insertChild:node atIndex:index];
	[self fnRemoveChild:old];
}

- (void)setChildren:(NSArray *)children
{
	NSUInteger i;

	while (_children != nil && [_children count] > 0) {
		[self fnRemoveChild:[_children objectAtIndex:0]];
	}
	for (i = 0; i < [children count]; i++) {
		[self addChild:[children objectAtIndex:i]];
	}
}

- (void)fnAdopt:(id)parent
{
	_parent = parent;	/* NOT RETAINED: the tree owns downward, and the upward pointer is a back edge */
}

/* ---- THE SERIALIZATION ---------------------------------------------------------------------------
 * One recursive function, with the node's kind deciding the shell - and an ELEMENT's namespace children
 * emitted INSIDE ITS START TAG, which is where a declaration belongs in the output even though it is a
 * child in the tree. */
- (NSString *)fnXMLStringWithOptions:(NSXMLNodeOptions)options depth:(NSUInteger)depth
{
	BOOL singleQuoted = (options & NSXMLNodeUseSingleQuotes) != 0;
	BOOL compact = (options & NSXMLNodeCompactEmptyElement) != 0;
	BOOL pretty = (options & NSXMLNodePrettyPrint) != 0;
	BOOL escape = (options & NSXMLNodeNeverEscapeContents) == 0;
	NSMutableString *answer = [NSMutableString string];
	NSString *quote = singleQuoted ? @"'" : @"\"";
	NSUInteger i;

	switch (_kind) {
	case NSXMLDocumentKind:
		for (i = 0; i < [_children count]; i++) {
			if (pretty && i > 0) {
				[answer appendString:@"\n"];
			}
			[answer appendString:[[_children objectAtIndex:i] fnXMLStringWithOptions:options
											depth:depth]];
		}
		return answer;
	case NSXMLElementKind: {
		[answer appendFormat:@"<%@", _name != nil ? _name : @""];
		/* THE NAMESPACE DECLARATIONS FIRST, as the start tag's own attributes. */
		for (i = 0; i < [_children count]; i++) {
			id child = [_children objectAtIndex:i];

			if ([child kind] == NSXMLNamespaceKind) {
				NSString *prefix = [child name];

				[answer appendFormat:@" xmlns%@=%@%@%@",
					(prefix != nil && [prefix length] > 0) ?
						[NSString stringWithFormat:@":%@", prefix] : @"",
					quote, escape ? fn_xml_escape_attribute([child stringValue], singleQuoted) :
							[child stringValue], quote];
			}
		}
		{
			NSArray *attributes = [(NSXMLElement *)self attributes];

			for (i = 0; i < [attributes count]; i++) {
				id attribute = [attributes objectAtIndex:i];

				[answer appendFormat:@" %@=%@%@%@", [attribute name], quote,
					escape ? fn_xml_escape_attribute([attribute stringValue], singleQuoted) :
						[attribute stringValue], quote];
			}
		}
		{
			NSMutableArray *content = [NSMutableArray array];
			BOOL hasElementChild = NO;

			for (i = 0; i < [_children count]; i++) {
				id child = [_children objectAtIndex:i];

				if ([child kind] == NSXMLNamespaceKind) {
					continue;	/* already in the start tag */
				}
				[content addObject:child];
				if ([child kind] == NSXMLElementKind) {
					hasElementChild = YES;
				}
			}
			if ([content count] == 0) {
				/* AN EMPTY ELEMENT IS EXPANDED UNLESS ASKED TO BE COMPACT: the default output is a start
				 * tag AND an end tag, and NSXMLNodeCompactEmptyElement is what selects "<a/>". The first
				 * version appended a bare '>' - and when that was corrected it still wrote the DEFAULT as
				 * if it were the compact form, which four checks said at once. */
				return compact ? [answer stringByAppendingString:@"/>"] :
						 [answer stringByAppendingFormat:@"></%@>", _name != nil ? _name : @""];
			}
			[answer appendString:@">"];
			for (i = 0; i < [content count]; i++) {
				if (pretty && hasElementChild) {
					[answer appendString:@"\n"];
					[answer appendString:fn_xml_indent(depth + 1)];
				}
				[answer appendString:[[content objectAtIndex:i] fnXMLStringWithOptions:options
												depth:depth + 1]];
			}
			if (pretty && hasElementChild) {
				[answer appendString:@"\n"];
				[answer appendString:fn_xml_indent(depth)];
			}
			[answer appendFormat:@"</%@>", _name != nil ? _name : @""];
		}
		return answer;
	}
	case NSXMLAttributeKind:
		return [NSString stringWithFormat:@"%@=%@%@%@", _name != nil ? _name : @"", quote,
			escape ? fn_xml_escape_attribute(_stringValue, singleQuoted) : _stringValue, quote];
	case NSXMLNamespaceKind:
		return [NSString stringWithFormat:@"xmlns%@=%@%@%@",
			(_name != nil && [_name length] > 0) ? [NSString stringWithFormat:@":%@", _name] : @"",
			quote, escape ? fn_xml_escape_attribute(_stringValue, singleQuoted) : _stringValue, quote];
	case NSXMLCommentKind:
		return [NSString stringWithFormat:@"<!--%@-->", _stringValue != nil ? _stringValue : @""];
	case NSXMLProcessingInstructionKind:
		return [NSString stringWithFormat:@"<?%@%@?>", _name != nil ? _name : @"",
			(_stringValue != nil && [_stringValue length] > 0) ?
				[NSString stringWithFormat:@" %@", _stringValue] : @""];
	case NSXMLTextKind:
		return escape ? fn_xml_escape_text(_stringValue != nil ? _stringValue : @"") :
				(_stringValue != nil ? _stringValue : @"");
	default:
		return @"";
	}
}

- (NSString *)XMLString
{
	return [self fnXMLStringWithOptions:0 depth:0];
}

- (NSString *)XMLStringWithOptions:(NSXMLNodeOptions)options
{
	return [self fnXMLStringWithOptions:options depth:0];
}

- (NSString *)description
{
	return [self XMLString];
}

- (void)dealloc
{
	[_name release];
	[_stringValue release];
	[_uri release];
	[_objectValue release];
	[_children release];
	[super dealloc];
}

@end

@implementation NSXMLElement

- (id)initWithName:(NSString *)name
{
	return [self initWithName:name stringValue:nil];
}

- (id)initWithName:(NSString *)name stringValue:(NSString *)stringValue
{
	self = [super initWithKind:NSXMLElementKind name:name value:stringValue];
	if (self != nil) {
		_attributesByName = [[NSMutableDictionary alloc] init];
		_attributeOrder = [[NSMutableArray alloc] init];
	}
	return self;
}

- (id)initWithName:(NSString *)name URI:(NSString *)URI
{
	self = [self initWithName:name];
	if (self != nil) {
		[self setURI:URI];
	}
	return self;
}

- (id)initWithName:(NSString *)name children:(NSArray *)children attributes:(NSArray *)attributes
{
	self = [self initWithName:name];
	if (self != nil) {
		NSUInteger i;

		for (i = 0; i < [children count]; i++) {
			[self addChild:[children objectAtIndex:i]];
		}
		for (i = 0; i < [attributes count]; i++) {
			[self addAttribute:[attributes objectAtIndex:i]];
		}
	}
	return self;
}

- (NSArray *)elementsForName:(NSString *)name
{
	NSMutableArray *answer = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];

		if ([child kind] == NSXMLElementKind && [[child name] isEqual:name]) {
			[answer addObject:child];
		}
	}
	return answer;
}

- (NSArray *)elementsForLocalName:(NSString *)localName URI:(NSString *)URI
{
	NSMutableArray *answer = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];
		NSString *childLocal;
		NSString *childURI;

		if ([child kind] != NSXMLElementKind) {
			continue;
		}
		childLocal = [NSXMLNode localNameForName:[child name]];
		childURI = [self resolveNamespaceForName:[child name]];
		if ([childLocal isEqual:localName] &&
		    (URI == nil ? childURI == nil : [childURI isEqual:URI])) {
			[answer addObject:child];
		}
	}
	return answer;
}

- (void)normalizeAdjacentTextNodesPreservingCDATA:(BOOL)preserve
{
	NSUInteger i = 0;

	(void)preserve;		/* the CDATA marking is an option bit on a text node; nothing sets it here */
	while (i + 1 < [self childCount]) {
		id first = [self childAtIndex:i];
		id second = [self childAtIndex:i + 1];

		if ([first kind] == NSXMLTextKind && [second kind] == NSXMLTextKind) {
			[first setStringValue:[NSString stringWithFormat:@"%@%@", [first stringValue],
						[second stringValue]]];
			[self removeChildAtIndex:i + 1];
			continue;
		}
		i++;
	}
}

- (void)addAttribute:(id)attribute
{
	NSString *name;

	if (attribute == nil) {
		return;
	}
	name = [attribute name];
	if (name == nil) {
		return;
	}
	if ([_attributesByName objectForKey:name] == nil) {
		[_attributeOrder addObject:name];
	}
	[_attributesByName setObject:attribute forKey:name];
}

- (void)removeAttributeForName:(NSString *)name
{
	if (name == nil) {
		return;
	}
	[_attributesByName removeObjectForKey:name];
	[_attributeOrder removeObject:name];
}

- (nullable NSArray *)attributes
{
	NSMutableArray *answer = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [_attributeOrder count]; i++) {
		id attribute = [_attributesByName objectForKey:[_attributeOrder objectAtIndex:i]];

		if (attribute != nil) {
			[answer addObject:attribute];
		}
	}
	return answer;
}

- (nullable id)attributeForName:(NSString *)name
{
	return name != nil ? [_attributesByName objectForKey:name] : nil;
}

- (void)setAttributes:(NSArray *)attributes
{
	NSUInteger i;

	[_attributesByName removeAllObjects];
	[_attributeOrder removeAllObjects];
	for (i = 0; i < [attributes count]; i++) {
		[self addAttribute:[attributes objectAtIndex:i]];
	}
}

- (void)setAttributesWithDictionary:(NSDictionary *)attributes
{
	NSArray *names = [attributes allKeys];
	NSUInteger i;

	[self setAttributes:nil];
	for (i = 0; i < [names count]; i++) {
		NSString *name = [names objectAtIndex:i];

		[self addAttribute:[NSXMLNode attributeWithName:name
						    stringValue:[attributes objectForKey:name]]];
	}
}

- (void)addNamespace:(id)aNamespace
{
	if (aNamespace != nil) {
		[self addChild:aNamespace];
	}
}

- (void)removeNamespaceForPrefix:(NSString *)name
{
	NSUInteger i = 0;

	while (i < [self childCount]) {
		id child = [self childAtIndex:i];

		if ([child kind] == NSXMLNamespaceKind && [[child name] isEqual:name]) {
			[self removeChildAtIndex:i];
			continue;
		}
		i++;
	}
}

- (nullable NSArray *)namespaces
{
	NSMutableArray *answer = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [self childCount]; i++) {
		id child = [self childAtIndex:i];

		if ([child kind] == NSXMLNamespaceKind) {
			[answer addObject:child];
		}
	}
	return [answer count] > 0 ? answer : nil;
}

- (nullable id)namespaceForPrefix:(NSString *)name
{
	NSArray *namespaces = [self namespaces];
	NSUInteger i;

	for (i = 0; i < [namespaces count]; i++) {
		id ns = [namespaces objectAtIndex:i];

		if ([[ns name] isEqual:name] || ([ns name] == nil && [name length] == 0)) {
			return ns;
		}
	}
	return nil;
}

/* RESOLUTION WALKS UP THE TREE, which is what a namespace's SCOPE is: an element's own declarations first,
 * then its ancestors'. */
- (nullable id)resolveNamespaceForName:(NSString *)name
{
	NSString *prefix = [NSXMLNode prefixForName:name];
	id node = self;

	if (prefix == nil) {
		return nil;
	}
	while (node != nil) {
		id found = [node namespaceForPrefix:prefix];

		if (found != nil) {
			return [found stringValue];
		}
		node = [node parent];
	}
	return [NSXMLNode predefinedNamespaceForPrefix:prefix];
}

- (nullable id)resolvePrefixForNamespaceURI:(NSString *)namespaceURI
{
	id node = self;

	while (node != nil) {
		NSArray *namespaces = [node namespaces];
		NSUInteger i;

		for (i = 0; i < [namespaces count]; i++) {
			id ns = [namespaces objectAtIndex:i];

			if ([[ns stringValue] isEqual:namespaceURI]) {
				return [ns name];
			}
		}
		node = [node parent];
	}
	return nil;
}

- (void)dealloc
{
	[_attributesByName release];
	[_attributeOrder release];
	[super dealloc];
}

@end
