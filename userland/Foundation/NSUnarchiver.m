/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUnarchiver.m — the READING half of the classic pair (§62.86). MANUAL OWNERSHIP.
 *
 * EVERY READ IS BOUNDS-CHECKED AND A SHORT READ IS `NSInconsistentArchiveException`, not a crash and not a
 * zero: a truncated archive is a fact about the DATA, and it is reported as the exception Apple names for
 * exactly that class of failure.
 *
 * THE TABLE IS FILLED BEFORE A CONTAINER'S ELEMENTS ARE READ, which is the whole reason a self-containing
 * array survives the round trip: the array is registered while it is still empty, so an element that refers
 * to it resolves to the same instance instead of recursing forever. The same trick is used for a decoded
 * object: it is registered between `alloc` and the object's own reader, so a class whose reader refers to
 * itself gets the instance being built. A CLASS CLUSTER whose reader returns a DIFFERENT object is the one
 * case that trick cannot cover, and the header says so rather than pretending otherwise.
 *
 * A DECODED OBJECT IS A BORROWED REFERENCE: the unarchiver owns the graph it decoded (that is what makes
 * shared and cyclic references possible at all), so a caller that wants to keep a value past the
 * unarchiver's life keeps its own reference. The class methods below do exactly that — which is why they
 * retain the root before letting the unarchiver go.
 */

#import <Foundation/NSArchiver.h>
#import <Foundation/FNArchiverWire.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>
#include <stdlib.h>

@implementation NSUnarchiver

/* --- the byte reader: EVERY read is bounded, and the failure is named --------------------------------- */

- (void)fnRaiseShort:(const char *)what
{
	[NSException raise:NSInconsistentArchiveException
		    format:@"the archive ends in the middle of %s", what];
}

- (uint8_t)fnReadByte
{
	const unsigned char *bytes = [_data bytes];

	if (_pos + 1 > [_data length]) {
		[self fnRaiseShort:"a value"];
	}
	return fnar_get_u8(bytes + _pos++);
}

- (uint32_t)fnReadU32
{
	const unsigned char *bytes = [_data bytes];
	uint32_t v;

	if (_pos + 4 > [_data length]) {
		[self fnRaiseShort:"a 32-bit length"];
	}
	v = fnar_get_u32(bytes + _pos);
	_pos += 4;
	return v;
}

- (uint64_t)fnReadU64
{
	const unsigned char *bytes = [_data bytes];
	uint64_t v;

	if (_pos + 8 > [_data length]) {
		[self fnRaiseShort:"a 64-bit value"];
	}
	v = fnar_get_u64(bytes + _pos);
	_pos += 8;
	return v;
}

/* The payload of a length-prefixed value, as a BORROWED pointer into the archive (no copy: the data is
 * alive for as long as this unarchiver is, which is what the class's contract says). */
- (const void *)fnReadPayloadOfLength:(NSUInteger *)lengthp
{
	uint32_t n = [self fnReadU32];
	const unsigned char *bytes = [_data bytes];

	if (_pos + n > [_data length]) {
		[self fnRaiseShort:"a length-prefixed value"];
	}
	*lengthp = n;
	{
		const void *p = bytes + _pos;

		_pos += n;
		return p;
	}
}

/* --- substitution (the READING side) ----------------------------------------------------------------- */

/* THE READING SIDE MATCHES BY EQUALITY, AND THE WRITING SIDE MATCHES BY IDENTITY — a difference the probe
 * found and that is inherent, not a choice: on the way OUT the caller holds the very instance it is
 * substituting, so identity is the natural and exact link; on the way IN the object does not exist until it
 * has been decoded, so a caller can only name what it is EQUAL TO. The doc's words are "whenever it is found
 * decoded", which is exactly this. */
- (BOOL)fnReplacementFor:(id)object at:(NSUInteger *)index
{
	NSUInteger i, n = [_replaced count];

	for (i = 0; i < n; i++) {
		id source = [_replaced objectAtIndex:i];

		if (source != nil && [source isEqual:object]) {
			*index = i;
			return YES;
		}
	}
	return NO;
}

- (id)fnApplyReplacement:(id)object
{
	NSUInteger i;

	if (object != nil && [self fnReplacementFor:object at:&i]) {
		return [[_targets objectAtIndex:i] pointerValue];	/* a NULL pointer means "decode as nil" */
	}
	return object;
}

/* --- the one method that decides what a value is ------------------------------------------------------ */

/* THE SUBSTITUTION IS A WRAPPER, NOT A BRANCH INSIDE THE READER, and the probe is what taught that: Apple's
 * contract is that a replacement applies "whenever it is found decoded", so it must cover EVERY value a
 * caller can decode — a string included, which never reaches the object table and therefore never reached
 * the substitution when this was an inner branch. The first run of the probe caught exactly that. */
- (id)fnReadValue
{
	return [self fnApplyReplacement:[self fnReadRawValue]];
}

- (id)fnReadRawValue
{
	uint8_t tag = [self fnReadByte];

	switch (tag) {
	case FNARTagNil:
		return nil;
	case FNARTagNull:
		return [NSNull null];
	case FNARTagBool: {
		unsigned char b = [self fnReadByte];

		return [NSNumber numberWithBool:(b != 0)];
	}
	case FNARTagInt32:
		return [NSNumber numberWithInt:(int)(int32_t)[self fnReadU32]];
	case FNARTagInt64:
		return [NSNumber numberWithLongLong:(long long)[self fnReadU64]];
	case FNARTagFloat: {
		uint32_t bits = (uint32_t)[self fnReadU32];
		float f;

		memcpy(&f, &bits, 4);
		return [NSNumber numberWithFloat:f];
	}
	case FNARTagDouble: {
		uint64_t bits;
		double d;

		bits = [self fnReadU64];
		memcpy(&d, &bits, 8);
		return [NSNumber numberWithDouble:d];
	}
	case FNARTagString: {
		NSUInteger n;
		const void *p = [self fnReadPayloadOfLength:&n];
		char *buffer = (char *)malloc(n + 1);
		id string;

		if (buffer == NULL) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"no memory for a %lu-byte string", (unsigned long)n];
		}
		memcpy(buffer, p, n);
		buffer[n] = '\0';
		string = [NSString stringWithUTF8String:buffer];
		free(buffer);
		return string;
	}
	case FNARTagData: {
		NSUInteger n;
		const void *p = [self fnReadPayloadOfLength:&n];

		return [NSData dataWithBytes:p length:n];
	}
	case FNARTagArray: {
		uint32_t i, n = [self fnReadU32];
		NSMutableArray *array = [NSMutableArray array];

		[_objects addObject:array];	/* BEFORE the elements: an element may be this array */
		for (i = 0; i < n; i++) {
			[array addObject:[self fnReadValue]];
		}
		return array;
	}
	case FNARTagDict: {
		uint32_t i, n = [self fnReadU32];
		NSMutableDictionary *dict = [NSMutableDictionary dictionary];

		[_objects addObject:dict];
		for (i = 0; i < n; i++) {
			id key = [self fnReadValue];
			id value = [self fnReadValue];

			[value retain];	/* the dictionary's setObject: would retain it too, but a key that IS the
					 * dictionary (a cycle through a key) needs it alive NOW */
			[dict setObject:value forKey:key];
			[value release];
		}
		return dict;
	}
	case FNARTagSet: {
		uint32_t i, n = [self fnReadU32];
		NSMutableSet *set = [NSMutableSet set];

		[_objects addObject:set];
		for (i = 0; i < n; i++) {
			[set addObject:[self fnReadValue]];
		}
		return set;
	}
	case FNARTagObjectRef: {
		uint32_t index = [self fnReadU32];

		if (index >= [_objects count]) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive refers to object %u, which it never wrote",
					   (unsigned)index];
		}
		return [_objects objectAtIndex:index];
	}
	case FNARTagObject: {
		NSUInteger n;
		const void *p = [self fnReadPayloadOfLength:&n];
		char *buffer = (char *)malloc(n + 1);
		NSString *name;
		Class cls;
		id object, decoded;
		NSUInteger index;

		if (buffer == NULL) {
			[NSException raise:NSInconsistentArchiveException format:@"no memory for a class name"];
		}
		memcpy(buffer, p, n);
		buffer[n] = '\0';
		name = [NSString stringWithUTF8String:buffer];
		free(buffer);

		cls = NSClassFromString(name);
		if (cls == Nil) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive names the class %@, which this system does not have", name];
		}
		if (![cls conformsToProtocol:@protocol(NSCoding)]) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"%@ is in the archive but does not conform to NSCoding", name];
		}
		object = [cls alloc];
		index = [_objects count];
		[_objects addObject:object];	/* registered BEFORE the reader: a self-reference resolves to it */
		decoded = [object initWithCoder:self];
		if (decoded != object) {
			/* A cluster that answered with its own instance: the table follows the ANSWER. */
			[_objects replaceObjectAtIndex:index withObject:decoded];
		}
		return [_objects objectAtIndex:index];
	}
	case FNARTagBytes:
		/* A raw-bytes value has no object spelling: the bytes door is the one that reads it, and saying so
		 * is better than handing back an NSData the writer never wrote as one. */
		[NSException raise:NSInconsistentArchiveException
			    format:@"a raw-bytes value cannot be read as an object — use "
				   @"-decodeBytesWithReturnedLength:"];
		/* NOT REACHED */
	}
	[NSException raise:NSInconsistentArchiveException format:@"tag %u is not a value", (unsigned)tag];
	return nil;
}

/* --- the doors ---------------------------------------------------------------------------------------- */

- (nullable id)decodeObject { return [self fnReadValue]; }

- (nullable NSData *)decodeDataObject
{
	if ([self fnReadByte] != FNARTagData) {
		[NSException raise:NSInconsistentArchiveException
			    format:@"-decodeDataObject: found a value that is not data"];
	}
	{
		NSUInteger n;
		const void *p = [self fnReadPayloadOfLength:&n];

		return [NSData dataWithBytes:p length:n];
	}
}

- (nullable const void *)decodeBytesWithReturnedLength:(NSUInteger *)lengthp
{
	if ([self fnReadByte] != FNARTagBytes) {
		[NSException raise:NSInconsistentArchiveException
			    format:@"-decodeBytesWithReturnedLength: found a value that is not raw bytes"];
	}
	return [self fnReadPayloadOfLength:lengthp];
}

- (void)decodeValueOfObjCType:(const char *)valueType at:(void *)data
{
	uint8_t tag;

	if (valueType == NULL) {
		[NSException raise:NSInconsistentArchiveException format:@"a nil type code has no type"];
	}
	tag = [self fnReadByte];
	switch (valueType[0]) {
	case '@':
		*(id *)data = [self fnReadValue];
		return;
	case '*': {
		NSUInteger n;
		const void *p = [self fnReadPayloadOfLength:&n];

		if (tag != FNARTagString) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive holds %u where a string was asked for", (unsigned)tag];
		}
		*(const char **)data = (const char *)p;	/* BORROWED: valid while the unarchiver is */
		return;
	}
	case 'c': case 'C': case 'B':
		if (tag != FNARTagBool) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive holds %u where a BOOL was asked for", (unsigned)tag];
		}
		*(unsigned char *)data = [self fnReadByte];
		return;
	case 's': case 'S': case 'i': case 'I': case 'l': case 'L':
		if (tag != FNARTagInt32) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive holds %u where a 32-bit integer was asked for", (unsigned)tag];
		}
		*(int *)data = (int)(int32_t)[self fnReadU32];
		return;
	case 'q': case 'Q':
		if (tag != FNARTagInt64) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive holds %u where a 64-bit integer was asked for", (unsigned)tag];
		}
		*(long long *)data = (long long)[self fnReadU64];
		return;
	case 'f':
		if (tag != FNARTagFloat) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive holds %u where a float was asked for", (unsigned)tag];
		}
		{
			uint32_t bits = (uint32_t)[self fnReadU32];

			memcpy(data, &bits, 4);
		}
		return;
	case 'd':
		if (tag != FNARTagDouble) {
			[NSException raise:NSInconsistentArchiveException
				    format:@"the archive holds %u where a double was asked for", (unsigned)tag];
		}
		{
			uint64_t bits = [self fnReadU64];

			memcpy(data, &bits, 8);
		}
		return;
	default:
		[NSException raise:NSInconsistentArchiveException
			    format:@"type code '%c' has no sequential spelling in this wire", valueType[0]];
	}
}

- (NSInteger)versionForClassName:(NSString *)className
{
	/* NO NUMBER IS RECORDED IN THIS WIRE, so none is answered: `-versionForClassName:` on an archive that
	 * carries no version would be the one door in this pair that fabricates. */
	[NSException raise:NSInconsistentArchiveException
		    format:@"this wire records no version for %@ (Apple's classic stream did)", className];
	return 0;
}

/* --- lifetime, substitution and the class doors -------------------------------------------------------- */

- (nullable instancetype)initForReadingWithData:(NSData *)data
{
	const unsigned char *bytes;

	if (data == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-initForReadingWithData: needs data to read"];
	}
	self = [super init];
	if (self != nil) {
		_data = [data retain];
		_objects = [[NSMutableArray alloc] init];
		_replaced = [[NSMutableArray alloc] init];
		_targets = [[NSMutableArray alloc] init];
		_pos = 0;
		/* AN ARCHIVE THAT IS NOT OURS IS A nil, NOT AN EXCEPTION: "this is not one of my archives" is a
		 * fact a caller can act on, and it is also how a KEYED archive is refused (Apple's own rule). */
		bytes = [_data bytes];
		if ([_data length] < 5 || memcmp(bytes, fn_archive_magic, 4) != 0 ||
		    fnar_get_u8(bytes + 4) != FNAR_VERSION) {
			[self release];
			return nil;
		}
		_pos = 5;
	}
	return self;
}

- (void)dealloc
{
	[_data release];
	[_objects release];
	[_replaced release];
	[_targets release];
	[super dealloc];
}

- (BOOL)isAtEnd { return _pos >= [_data length]; }

- (void)replaceObject:(nullable id)object withObject:(nullable id)newObject
{
	if (object == nil) {
		return;
	}
	/* THE SOURCE IS KEPT AS ITSELF (the comparison is by equality), and the TARGET is boxed so a NULL
	 * pointer can mean "decode as nil" while NSNull stays a value. */
	[_replaced addObject:object];
	[_targets addObject:[NSValue valueWithPointer:newObject]];
}

+ (nullable id)unarchiveObjectWithData:(NSData *)data
{
	NSUnarchiver *unarchiver = [[NSUnarchiver alloc] initForReadingWithData:data];
	id root = nil;

	if (unarchiver != nil) {
		/* THE ROOT IS RETAINED ACROSS THE UNARCHIVER'S LIFE, because the graph belongs to the unarchiver
		 * (see the file's note) and the caller is entitled to keep what it gets. */
		root = [[unarchiver decodeObject] retain];
		[unarchiver release];
		[root autorelease];
	}
	return root;
}

+ (nullable id)unarchiveObjectWithFile:(NSString *)path
{
	NSData *data = [NSData dataWithContentsOfFile:path];

	if (data == nil) {
		return nil;
	}
	return [self unarchiveObjectWithData:data];
}

@end

id _Nullable NXReadNSObjectFromCoder(NSCoder *coder)
{
	/* ONE LINE, AND NO MORE IS HONEST: the legacy free function is the old spelling of the object door, and
	 * a "legacy" function that quietly did something extra would be a second behaviour to keep in step. */
	return [coder decodeObject];
}
