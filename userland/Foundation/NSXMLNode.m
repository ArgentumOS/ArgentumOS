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

@interface NSXMLNode (FNCanonical)
- (NSString *)fnCanonicalStringDepth:(NSUInteger)depth preservingComments:(BOOL)preserve;
@end

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

+ (id)attributeWithName:(NSString *)name URI:(NSString *)URI stringValue:(NSString *)stringValue
{
	id node = [[[NSXMLNode alloc] initWithKind:NSXMLAttributeKind name:name value:stringValue] autorelease];

	[node setURI:URI];
	return node;
}

+ (id)elementWithName:(NSString *)name URI:(NSString *)URI
{
	id node = [[[NSXMLElement alloc] initWithName:name] autorelease];

	[node setURI:URI];
	return node;
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

- (NSString *)localName
{
	return (NSString *)[[self class] localNameForName:[self name]];
}

- (NSString *)prefix
{
	return (NSString *)[[self class] prefixForName:[self name]];
}

- (NSString *)XPath
{
	NSMutableString *path = [NSMutableString string];
	NSXMLNode *node = self;

	while (node != nil && [node kind] != NSXMLDocumentKind) {	/* §63.196: the topmost ELEMENT is a step; the document is not */
		NSXMLNodeKind kind = [node kind];
		NSString *name = [node name] != nil ? [node name] : @"";

		if (kind == NSXMLAttributeKind) {
			[path insertString:[NSString stringWithFormat:@"/@%@" , name] atIndex:0];
		} else if (kind == NSXMLNamespaceKind) {
			[path insertString:[NSString stringWithFormat:@"/namespace::%@" , name] atIndex:0];
		} else if (kind == NSXMLElementKind) {
			NSUInteger index = 1;
			NSXMLNode *sibling = [node previousSibling];

			while (sibling != nil) {
				if ([sibling kind] == NSXMLElementKind && [[sibling name] isEqual:name]) {
					index++;
				}
				sibling = [sibling previousSibling];
			}
			[path insertString:[NSString stringWithFormat:@"/%@[%lu]", name, (unsigned long)index] atIndex:0];
		} else {
			[path insertString:[NSString stringWithFormat:@"/%@" , name] atIndex:0];
		}
		node = [node parent];
	}
	if ([path length] == 0) {
		return ([self kind] == NSXMLDocumentKind) ? @"/" : nil;
	}
	return path;
}

- (void)setStringValue:(NSString *)string resolvingEntities:(BOOL)resolve
{
	if (resolve == NO) {
		[self setStringValue:string];
		return;
	}
	/* ONE FORWARD PASS, which is the whole point: a resolver that re-scans its own output turns "&amp;"
	 * into an ampersand and then finds an ampersand. Entity references here are the five XML 1.0
	 * predefines plus the numeric forms, which is the set that needs no DTD. */
	{
		NSUInteger i, n = [string length];
		NSMutableString *answer = [NSMutableString stringWithCapacity:n];

		for (i = 0; i < n; i++) {
			unichar c = [string characterAtIndex:i];

			if (c != '&') {
				[answer appendFormat:@"%C", c];
				continue;
			}
			{
				NSRange semi = [string rangeOfString:@";" options:0
							       range:NSMakeRange(i, n - i)];
				NSString *replacement = nil;

				if (semi.location != NSNotFound && semi.location > i + 1) {
					NSString *ref = [string substringWithRange:
						NSMakeRange(i + 1, semi.location - i - 1)];

					if ([ref isEqual:@"amp"]) replacement = @"&";
					else if ([ref isEqual:@"lt"]) replacement = @"<";
					else if ([ref isEqual:@"gt"]) replacement = @">";
					else if ([ref isEqual:@"quot"]) replacement = @"\"";
					else if ([ref isEqual:@"apos"]) replacement = @"'";
					else if ([ref length] > 1 && [ref characterAtIndex:0] == '#') {
						unsigned long scalar = 0;
						NSUInteger j = 1;
						BOOL hex = NO;
						BOOL ok = YES;

						if ([ref characterAtIndex:1] == 'x' || [ref characterAtIndex:1] == 'X') {
							hex = YES;
							j = 2;
						}
						for (; j < [ref length]; j++) {
							unichar d = [ref characterAtIndex:j];
							unsigned long v;

							if (d >= '0' && d <= '9') v = d - '0';
							else if (hex && d >= 'a' && d <= 'f') v = 10 + d - 'a';
							else if (hex && d >= 'A' && d <= 'F') v = 10 + d - 'A';
							else { ok = NO; break; }
							scalar = scalar * (hex ? 16 : 10) + v;
							if (scalar > 0x10FFFF) { ok = NO; break; }
						}
						if (ok && scalar > 0 && j > (hex ? 2 : 1)) {
							unichar buffer[2];
							NSUInteger used = 0;

							if (scalar <= 0xFFFF) {
								buffer[used++] = (unichar)scalar;
							} else {
								unsigned long v = scalar - 0x10000;
								buffer[used++] = (unichar)(0xD800 + (v >> 10));
								buffer[used++] = (unichar)(0xDC00 + (v & 0x3FF));
							}
							replacement = [NSString stringWithCharacters:buffer length:used];
						}
					}
					if (replacement != nil) {
						[answer appendString:replacement];
						i = semi.location;
						continue;
					}
				}
			}
			[answer appendString:@"&"];	/* an unresolvable reference is LEFT AS WRITTEN */
		}
		[self setStringValue:answer];
	}
}
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
	/* THE NODE'S OWN OPTIONS ARE PART OF THIS DOOR TOO: -XMLString used to reach the internal serializer
	 * directly, so a node built with NSXMLNodeCompactEmptyElement serialized as if it had none. */
	return [self XMLStringWithOptions:0];
}

- (instancetype)initWithKind:(NSXMLNodeKind)kind
{
	/* THE DESIGNATED INITIALIZER THE DECLARATION PROMISED AND NOTHING IMPLEMENTED: this is what -initWithKind:
	 * options: routes through, so without it that door reached doesNotRecognizeSelector: at RUN time while the
	 * ledger and the build were both quiet. */
	return [self initWithKind:kind name:nil value:nil];
}

- (instancetype)initWithKind:(NSXMLNodeKind)kind options:(NSXMLNodeOptions)options
{
	/* THE NODE'S OWN OPTIONS, AND THEY DO SOMETHING: they are merged over whatever the caller passes, which is
	 * how a node created with NSXMLNodeCompactEmptyElement serializes as "<a/>" wherever it is written. */
	self = [self initWithKind:kind];
	if (self != nil) {
		_options = options;
	}
	return self;
}

- (NSString *)XMLStringWithOptions:(NSXMLNodeOptions)options
{
	return [self fnXMLStringWithOptions:(options | _options) depth:0];
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

/* §63.233: THE CANONICAL WALK. The rules are stated at the declaration; what matters here is that it is ONE
 * recursion over the same children every other serializer walks, so the canonical string cannot disagree with the
 * document about what the tree holds. */
static NSString *fn_canonical_escape_text(NSString *text)
{
	NSMutableString *out = [NSMutableString stringWithString:(text != nil ? text : @"")];

	[out replaceOccurrencesOfString:@"&" withString:@"&amp;" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"<" withString:@"&lt;" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@">" withString:@"&gt;" options:0 range:NSMakeRange(0, [out length])];
	return out;
}

static NSString *fn_canonical_escape_attribute(NSString *text)
{
	NSMutableString *out = [NSMutableString stringWithString:(text != nil ? text : @"")];

	[out replaceOccurrencesOfString:@"&" withString:@"&amp;" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"<" withString:@"&lt;" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"\"" withString:@"&quot;" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"\t" withString:@"&#x9;" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"\n" withString:@"&#xA;" options:0 range:NSMakeRange(0, [out length])];
	[out replaceOccurrencesOfString:@"\r" withString:@"&#xD;" options:0 range:NSMakeRange(0, [out length])];
	return out;
}

- (NSString *)canonicalXMLStringPreservingComments:(BOOL)comments
{
	/* §63.243: THE FLAG DECIDES. This door used to ignore `comments` and answer the same string for both
	 * values, justified by a note that said preservation "distinguishes it from the sibling that drops them"
	 * — A SIBLING THIS TREE DOES NOT HAVE (Apple declares ONE canonical door on NSXMLNode, and so does this
	 * header). The distinction therefore has to be carried BY THE FLAG, which is what the parameter is for. */
	return [self fnCanonicalStringDepth:0 preservingComments:comments];
}

- (NSString *)fnCanonicalStringDepth:(NSUInteger)depth preservingComments:(BOOL)preserve
{
	NSXMLNodeKind kind = [self kind];

	(void)depth;
	if (kind == NSXMLDocumentKind) {
		/* A DOCUMENT IS ITS CHILDREN: canonical XML has no XML declaration and no prologue. */
		NSMutableString *out = [NSMutableString string];
		NSArray *kids = [self children];
		NSUInteger i;

		for (i = 0; i < [kids count]; i++) {
			[out appendString:[[kids objectAtIndex:i] fnCanonicalStringDepth:0 preservingComments:preserve]];
		}
		return out;
	}
	if (kind == NSXMLCommentKind) {
		/* §63.243: THE FLAG, AND IT IS THE ONLY THING THAT CAN CARRY THIS DISTINCTION — there is no
		 * sibling door that drops comments (see the door's note in the header). A dropped comment contributes
		 * NOTHING to the output, not even whitespace. */
		if (!preserve) {
			return @"";
		}
		return [NSString stringWithFormat:@"<!--%@-->", _stringValue != nil ? _stringValue : @""];
	}
	if (kind == NSXMLTextKind) {
		return fn_canonical_escape_text(_stringValue);
	}
	if (kind == NSXMLAttributeKind) {
		return [NSString stringWithFormat:@" %@=\"%@\"", _name != nil ? _name : @"",
			fn_canonical_escape_attribute(_stringValue)];
	}
	{
		NSMutableString *out = [NSMutableString stringWithFormat:@"<%@", _name != nil ? _name : @""];
		NSMutableArray *ordered = [NSMutableArray array];
		NSArray *kids = [self children];
		NSUInteger i, j;
		id selfAsElement = self;

		if ([selfAsElement respondsToSelector:@selector(attributes)]) {
			NSArray *attrs = [selfAsElement attributes];

			for (i = 0; attrs != nil && i < [attrs count]; i++) {
				[ordered addObject:[attrs objectAtIndex:i]];
			}
			/* (URI, LOCAL NAME) ORDER — an insertion sort, because these lists are small. */
			for (i = 1; i < [ordered count]; i++) {
				id key = [ordered objectAtIndex:i];
				NSString *keyURI = (NSString *)[key URI];
				NSString *keyLocal = (NSString *)[[key class] localNameForName:[key name]];

				for (j = i; j > 0; j--) {
					id prev = [ordered objectAtIndex:j - 1];
					NSString *prevURI = (NSString *)[prev URI];
					NSString *prevLocal = (NSString *)[[prev class] localNameForName:[prev name]];
					NSComparisonResult byURI = [(prevURI != nil ? prevURI : @"") compare:(keyURI != nil ? keyURI : @"")];
					NSComparisonResult byLocal = [(prevLocal != nil ? prevLocal : @"") compare:(keyLocal != nil ? keyLocal : @"")];

					if (byURI < 0 || (byURI == 0 && byLocal <= 0)) {
						break;
					}
					[ordered replaceObjectAtIndex:j withObject:prev];
				}
				[ordered replaceObjectAtIndex:j withObject:key];
			}
			for (i = 0; i < [ordered count]; i++) {
				[out appendString:[[ordered objectAtIndex:i] fnCanonicalStringDepth:0 preservingComments:preserve]];
			}
		}
		[out appendString:@">"];
		for (i = 0; kids != nil && i < [kids count]; i++) {
			[out appendString:[[kids objectAtIndex:i] fnCanonicalStringDepth:0 preservingComments:preserve]];
		}
		[out appendFormat:@"</%@>", _name != nil ? _name : @""];
		return out;
	}
}
@end

@implementation NSXMLElement

- (instancetype)initWithKind:(NSXMLNodeKind)kind
{
	/* §63.196: an element's own ivars are made by -initWithName:, so the KIND door — declared on this class too
	 * — must route there rather than to the node's plain initializer. */
	return [self initWithName:nil];
}

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


- (id)attributeForLocalName:(NSString *)localName URI:(NSString *)URI
{
	/* THE TABLE IS KEYED BY FULL NAME, so this scans the ORDER array — the one the serializer walks — and
	 * compares the pieces through the class's own name helper. */
	NSUInteger i;

	for (i = 0; i < [_attributeOrder count]; i++) {
		id attribute = [_attributesByName objectForKey:[_attributeOrder objectAtIndex:i]];
		NSString *candidateLocal = (NSString *)[[self class] localNameForName:[attribute name]];
		NSString *candidateURI = [attribute URI];

		if (candidateLocal != nil && [candidateLocal isEqual:localName] &&
		    (URI == nil || (candidateURI != nil && [candidateURI isEqual:URI]))) {
			return attribute;
		}
	}
	return nil;
}

- (void)setAttributesAsDictionary:(NSDictionary *)attributes
{
	/* REPLACE, NOT MERGE: Apple's door takes the whole set, and a nil VALUE is the dictionary's way of saying
	 * the attribute carries no value. */
	NSArray *names = [[_attributeOrder copy] autorelease];
	NSUInteger i;

	for (i = 0; i < [names count]; i++) {
		[self removeAttributeForName:[names objectAtIndex:i]];
	}
	{
		NSEnumerator *keys = [attributes keyEnumerator];
		id key;

		while ((key = [keys nextObject]) != nil) {
			[self addAttribute:[NSXMLNode attributeWithName:(NSString *)key
							    stringValue:[attributes objectForKey:key]]];
		}
	}
}

- (id)initWithXMLString:(NSString *)string error:(NSError **)errorPtr
{
	/* THE FAILING FORM OF THE DOOR THE CLASS ALREADY HAS: the node is the same one, and the error path answers
	 * the parse failure rather than a guess. The key is spelled as the literal it is, so this file needs no
	 * import it does not already have. */
	self = [self initWithXMLString:string];
	if (self == nil && errorPtr != NULL) {
		*errorPtr = [NSError errorWithDomain:@"NSXMLParserErrorDomain"
						code:1
					    userInfo:[NSDictionary dictionaryWithObject:@"the XML fragment could not be parsed"
										 forKey:@"NSLocalizedDescriptionKey"]];
	}
	return self;
}
@end
