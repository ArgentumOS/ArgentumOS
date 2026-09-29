/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSArchiver.m — the WRITING half of the classic pair (§62.86). MANUAL OWNERSHIP.
 *
 * ONE METHOD DECIDES WHAT A VALUE IS (`fnWriteValue:`), and every door lands there: the type-less object
 * door, the type-CODE door, the data door and the raw-bytes door. A second copy of that decision is how
 * a reader and a writer drift apart, so there is exactly one.
 *
 * IDENTITY IS LINEAR ON PURPOSE. The object table is an array searched by POINTER, not by `-isEqual:`,
 * because identity is what the wire preserves: two equal strings are two values, while one array
 * referenced twice is one object. A table of the size an archive holds is small, and a hash table keyed
 * by hashing would answer the OTHER question ("is this value equal") which is exactly the wrong one.
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

@implementation NSArchiver

/* --- the table ------------------------------------------------------------------------------------ */

static NSUInteger fn_index_of(NSArray *table, id object)
{
	NSUInteger i, n = [table count];

	for (i = 0; i < n; i++) {
		if ([table objectAtIndex:i] == object) {
			return i;
		}
	}
	return NSNotFound;
}

- (BOOL)fnReplacementFor:(id)object at:(NSUInteger *)index
{
	NSUInteger i, n = [_replaced count];

	for (i = 0; i < n; i++) {
		if ([[_replaced objectAtIndex:i] pointerValue] == object) {
			*index = i;
			return YES;
		}
	}
	return NO;
}

/* --- writing -------------------------------------------------------------------------------------- */

- (void)fnWriteByte:(uint8_t)byte
{
	unsigned char b = byte;

	[_data appendBytes:&b length:1];
}

- (void)fnWriteBytes:(const void *)bytes length:(NSUInteger)length
{
	unsigned char header[4];

	fnar_put_u32(header, (uint32_t)length);
	[_data appendBytes:header length:4];
	[_data appendBytes:bytes length:length];
}

- (void)fnWriteObjectOfClass:(id)object
{
	NSString *name = NSStringFromClass([object classForArchiver]);

	[self fnWriteByte:FNARTagObject];
	/* THE LENGTH IS BYTES, NOT CHARACTERS — the wire counts the same unit on both sides, and a class name is
	 * ASCII in practice but must not depend on being so. */
	[self fnWriteBytes:[name UTF8String] length:strlen([name UTF8String])];
	[object encodeWithCoder:self];
}

- (void)fnWriteValue:(id _Nullable)object
{
	NSUInteger index;

	if (object == nil) {
		[self fnWriteByte:FNARTagNil];
		return;
	}
	/* SUBSTITUTION COMES FIRST: a replaced object is never examined as itself. */
	{
		NSUInteger ri;

		if ([self fnReplacementFor:object at:&ri]) {
			/* A NULL POINTER in the target slot means "write this as nil"; NSNull as a target stays a
			 * VALUE, which is why the target is boxed rather than stored raw. */
			object = [[_targets objectAtIndex:ri] pointerValue];
			if (object == nil) {
				[self fnWriteByte:FNARTagNil];
				return;
			}
		}
	}
	/* THE OBJECT TABLE, for the values whose IDENTITY the wire preserves: containers (which is what makes
	 * a self-containing collection survive) and NSCoding objects. Strings and data are values. */
	if ([object isKindOfClass:[NSArray class]] || [object isKindOfClass:[NSDictionary class]] ||
	    [object isKindOfClass:[NSSet class]] ||
	    (![object isKindOfClass:[NSString class]] && ![object isKindOfClass:[NSData class]] &&
	     ![object isKindOfClass:[NSNumber class]] && ![object isKindOfClass:[NSNull class]])) {
		index = fn_index_of(_objects, object);
		if (index != NSNotFound) {
			unsigned char buf[4];

			[self fnWriteByte:FNARTagObjectRef];
			fnar_put_u32(buf, (uint32_t)index);
			[_data appendBytes:buf length:4];
			return;
		}
		[_objects addObject:object];
	}
	if ([object isKindOfClass:[NSNull class]]) {
		[self fnWriteByte:FNARTagNull];
		return;
	}
	if ([object isKindOfClass:[NSString class]]) {
		[self fnWriteByte:FNARTagString];
		[self fnWriteBytes:[object UTF8String] length:strlen([object UTF8String])];
		return;
	}
	if ([object isKindOfClass:[NSData class]]) {
		[self fnWriteByte:FNARTagData];
		[self fnWriteBytes:[object bytes] length:[object length]];
		return;
	}
	if ([object isKindOfClass:[NSNumber class]]) {
		const char *type = [object objCType];

		if (type != NULL && (type[0] == 'c' || type[0] == 'B')) {
			unsigned char b = (unsigned char)[object boolValue];

			[self fnWriteByte:FNARTagBool];
			[_data appendBytes:&b length:1];
		} else if (type != NULL && (type[0] == 'f')) {
			unsigned char buf[4];
			float f = [object floatValue];
			uint32_t bits;

			memcpy(&bits, &f, 4);
			fnar_put_u32(buf, bits);
			[self fnWriteByte:FNARTagFloat];
			[_data appendBytes:buf length:4];
		} else if (type != NULL && (type[0] == 'd')) {
			unsigned char buf[8];
			double d = [object doubleValue];
			uint64_t bits;

			memcpy(&bits, &d, 8);
			fnar_put_u64(buf, bits);
			[self fnWriteByte:FNARTagDouble];
			[_data appendBytes:buf length:8];
		} else if (type != NULL && (type[0] == 'i' || type[0] == 'l' || type[0] == 's')) {
			unsigned char buf[4];

			fnar_put_u32(buf, (uint32_t)[object intValue]);
			[self fnWriteByte:FNARTagInt32];
			[_data appendBytes:buf length:4];
		} else {
			unsigned char buf[8];

			fnar_put_u64(buf, (uint64_t)[object longLongValue]);
			[self fnWriteByte:FNARTagInt64];
			[_data appendBytes:buf length:8];
		}
		return;
	}
	if ([object isKindOfClass:[NSArray class]]) {
		NSUInteger i, n = [object count];
		unsigned char buf[4];

		[self fnWriteByte:FNARTagArray];
		fnar_put_u32(buf, (uint32_t)n);
		[_data appendBytes:buf length:4];
		for (i = 0; i < n; i++) {
			[self fnWriteValue:[object objectAtIndex:i]];
		}
		return;
	}
	if ([object isKindOfClass:[NSDictionary class]]) {
		NSArray *keys = [object allKeys];
		NSUInteger i, n = [keys count];
		unsigned char buf[4];

		[self fnWriteByte:FNARTagDict];
		fnar_put_u32(buf, (uint32_t)n);
		[_data appendBytes:buf length:4];
		for (i = 0; i < n; i++) {
			[self fnWriteValue:[keys objectAtIndex:i]];
			[self fnWriteValue:[object objectForKey:[keys objectAtIndex:i]]];
		}
		return;
	}
	if ([object isKindOfClass:[NSSet class]]) {
		NSArray *members = [object allObjects];
		NSUInteger i, n = [members count];
		unsigned char buf[4];

		[self fnWriteByte:FNARTagSet];
		fnar_put_u32(buf, (uint32_t)n);
		[_data appendBytes:buf length:4];
		for (i = 0; i < n; i++) {
			[self fnWriteValue:[members objectAtIndex:i]];
		}
		return;
	}
	/* ANYTHING ELSE MUST SPEAK THE SEQUENTIAL PROTOCOL. A class that only implements the keyed doors will
	 * reach a keyed door from inside its own writer and raise there, naming the family it needs — which is
	 * the correct answer, because a class written against keys has no order to fall back on. */
	if (![object conformsToProtocol:@protocol(NSCoding)]) {
		[NSException raise:NSInconsistentArchiveException
			    format:@"%@ cannot be archived sequentially: it does not conform to NSCoding",
				   [object class]];
	}
	[self fnWriteObjectOfClass:object];
}

/* --- the doors ------------------------------------------------------------------------------------ */

- (void)encodeObject:(nullable id)object { [self fnWriteValue:object]; }

- (void)encodeDataObject:(NSData *)data
{
	[self fnWriteByte:FNARTagData];
	[self fnWriteBytes:[data bytes] length:[data length]];
}

- (void)encodeBytes:(nullable const void *)bytesp length:(NSUInteger)length
{
	[self fnWriteByte:FNARTagBytes];
	[self fnWriteBytes:bytesp length:length];
}

- (void)encodeValueOfObjCType:(const char *)valueType at:(const void *)address
{
	unsigned char buf[8];

	if (valueType == NULL) {
		[NSException raise:NSInconsistentArchiveException format:@"a nil type code has no type"];
	}
	switch (valueType[0]) {
	case '@':
		[self fnWriteValue:*(id const *)address];
		return;
	case '*': {
		const char *c = *(const char * const *)address;

		[self fnWriteByte:FNARTagString];
		[self fnWriteBytes:(c != NULL ? c : "") length:(c != NULL ? strlen(c) : 0)];
		return;
	}
	case 'c': case 'C': case 'B':
		[self fnWriteByte:FNARTagBool];
		[_data appendBytes:address length:1];
		return;
	case 's': case 'S': case 'i': case 'I': case 'l': case 'L':
		fnar_put_u32(buf, (uint32_t)*(const int *)address);
		[self fnWriteByte:FNARTagInt32];
		[_data appendBytes:buf length:4];
		return;
	case 'q': case 'Q': {
		uint64_t v = (uint64_t)*(const long long *)address;

		fnar_put_u64(buf, v);
		[self fnWriteByte:FNARTagInt64];
		[_data appendBytes:buf length:8];
		return;
	}
	case 'f': {
		uint32_t bits;

		memcpy(&bits, address, 4);
		fnar_put_u32(buf, bits);
		[self fnWriteByte:FNARTagFloat];
		[_data appendBytes:buf length:4];
		return;
	}
	case 'd': {
		uint64_t bits;

		memcpy(&bits, address, 8);
		fnar_put_u64(buf, bits);
		[self fnWriteByte:FNARTagDouble];
		[_data appendBytes:buf length:8];
		return;
	}
	default:
		/* NAMED RATHER THAN GUESSED: a structure, a C array or a pointer has no sequential spelling here,
		 * and inventing one would be a wire no reader could be written against. */
		[NSException raise:NSInconsistentArchiveException
			    format:@"type code '%c' has no sequential spelling in this wire", valueType[0]];
	}
}

/* --- lifetime and the root ------------------------------------------------------------------------ */

- (instancetype)initForWritingWithMutableData:(NSMutableData *)data
{
	unsigned char header[5];

	if (data == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-initForWritingWithMutableData: needs data to write into"];
	}
	self = [super init];
	if (self != nil) {
		_data = [data retain];
		_objects = [[NSMutableArray alloc] init];
		_replaced = [[NSMutableArray alloc] init];
		_targets = [[NSMutableArray alloc] init];
		memcpy(header, fn_archive_magic, 4);
		header[4] = FNAR_VERSION;
		[_data appendBytes:header length:5];
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

- (void)encodeRootObject:(id)rootObject
{
	if (_rootEncoded) {
		[NSException raise:NSInvalidArgumentException
			    format:@"this NSArchiver has already encoded its root object — one archive, one root"];
	}
	_rootEncoded = YES;
	[self fnWriteValue:rootObject];
}

- (NSMutableData *)archiverData { return _data; }

- (void)replaceObject:(nullable id)object withObject:(nullable id)newObject
{
	if (object == nil) {
		return;
	}
	[_replaced addObject:[NSValue valueWithPointer:object]];
	[_targets addObject:[NSValue valueWithPointer:newObject]];	/* NULL pointer means "as nil" */
}

+ (NSData *)archivedDataWithRootObject:(id)rootObject
{
	NSMutableData *data = [[NSMutableData alloc] init];
	NSArchiver *archiver = [[NSArchiver alloc] initForWritingWithMutableData:data];
	id out;

	[archiver encodeRootObject:rootObject];
	out = [[NSData alloc] initWithData:data];
	[archiver release];
	[data release];
	return [out autorelease];
}

+ (BOOL)archiveRootObject:(id)rootObject toFile:(NSString *)path
{
	NSData *data = [self archivedDataWithRootObject:rootObject];

	return [data writeToFile:path atomically:YES];
}

@end
