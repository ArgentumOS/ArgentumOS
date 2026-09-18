/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nskeyedarchiver.m — the archive writer and reader (F13.12). ARC file.
 *
 * THE SHAPE IS IN THE HEADER. What this file is really about is TWO RULES that make a graph
 * survive a round trip:
 *
 *   1. AN INDEX IS RESERVED BEFORE ITS CONTENTS ARE WRITTEN. The entry goes into `$objects` and the
 *      memo records which object owns it, and only THEN is the object asked to encode itself. That
 *      ordering is what makes a CYCLE terminate: an object that reaches itself finds the memo
 *      already carrying its index and is written as a reference to the entry still being filled.
 *      Reverse the two lines and a self-referential object recurses until the stack ends.
 *
 *   2. A SLOT IS EITHER A REFERENCE OR A VALUE. `{"$ref": n}` means "the object at n"; anything else
 *      in a slot IS the value (a string, number, date or data). That is why the archive is readable
 *      without a type table: `NSNull` and nil are the reference to index 0, `$null`, which is
 *      Cocoa's own trick.
 *
 * VALUE TYPES ARE WRITTEN INLINE and therefore have no identity in the archive — two equal strings
 * are two strings after a round trip, which is the same promise Cocoa makes. OBJECTS (including
 * collections) go in the table, so an object reached twice comes back as ONE object.
 */

#import <foundation/NSKeyedArchiver.h>
#import <foundation/NSCoding.h>
#import <foundation/NSPropertyListSerialization.h>
#import <foundation/NSData.h>
#import <foundation/NSString.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSDate.h>
#import <foundation/NSNumber.h>
#import <foundation/NSNull.h>
#import <foundation/NSException.h>
#import <objc/runtime.h>

static NSString *const kRef = @"$ref";
static NSString *const kClass = @"$class";
static NSString *const kClassname = @"$classname";
static NSString *const kClasses = @"$classes";
static NSString *const kObjects = @"NS.objects";
static NSString *const kKeys = @"NS.keys";

/* ONE MEMO ENTRY, matched BY IDENTITY: two distinct but equal objects are two objects. */
@interface FNMemo : NSObject
{
@public
	__unsafe_unretained id _object;
	NSUInteger _index;
}
@end

@implementation FNMemo
@end

static NSDictionary *fn_reference(NSUInteger index)
{
	return @{ kRef : [NSNumber numberWithUnsignedInteger:index] };
}

/* IS THIS SLOT A REFERENCE? Exactly one key, named `$ref` — a collection entry also carries keys. */
static BOOL fn_slot_is_reference(id slot, NSUInteger *outIndex)
{
	id index;

	if (![slot isKindOfClass:[NSDictionary class]] || [(NSDictionary *)slot count] != 1) {
		return NO;
	}
	index = [(NSDictionary *)slot objectForKey:kRef];
	if (![index isKindOfClass:[NSNumber class]]) {
		return NO;
	}
	if (outIndex != NULL) {
		*outIndex = (NSUInteger)[index unsignedIntegerValue];
	}
	return YES;
}

/* THE CLASS NAME, as text: this library has no NSStringFromClass, and the runtime has the name. */
static NSString *fn_class_name(Class cls)
{
	return [NSString stringWithUTF8String:class_getName(cls)];
}

/* AN OBJECT THAT IS WRITTEN INLINE, or nil for one that goes in the table. */
static BOOL fn_is_value_type(id object)
{
	return [object isKindOfClass:[NSString class]] || [object isKindOfClass:[NSNumber class]] ||
	       [object isKindOfClass:[NSDate class]] || [object isKindOfClass:[NSData class]];
}

@implementation NSKeyedArchiver

+ (nullable NSData *)archivedDataWithRootObject:(id)rootObject
{
	NSKeyedArchiver *archiver = [[self alloc] initForWritingWithMutableData:
					[[NSMutableData alloc] init]];
	NSUInteger rootIndex = [archiver fnIndexOfObject:rootObject];

	return [archiver fnArchivedDataWithRootIndex:rootIndex];
}

+ (BOOL)archiveRootObject:(id)rootObject toFile:(NSString *)path
{
	NSData *data = [self archivedDataWithRootObject:rootObject];

	return data != nil && [data writeToFile:path atomically:YES];
}

- (instancetype)initForWritingWithMutableData:(NSMutableData *)data
{
	(void)data;	/* see the class comment: the archive is produced by the class method, and the
			 * mutable-data flow Cocoa offers is accepted and ignored */
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_objects = [[NSMutableArray alloc] init];
	_memo = [[NSMutableArray alloc] init];
	_stack = [[NSMutableArray alloc] init];
	/* INDEX 0 IS `$null`, so a nil and an NSNull are the same reference and need no type. */
	[_objects addObject:@"$null"];
	return self;
}

- (NSUInteger)fnClassIndexOf:(Class)cls
{
	NSUInteger i;

	for (i = 0; i < [_memo count]; i++) {
		FNMemo *held = [_memo objectAtIndex:i];

		if (held->_object == (id)cls) {
			return held->_index;
		}
	}
	{
		NSUInteger index = [_objects count];
		NSMutableDictionary *entry = [NSMutableDictionary dictionary];
		NSMutableArray *chain = [NSMutableArray array];
		FNMemo *memo = [[FNMemo alloc] init];
		Class walk;

		for (walk = cls; walk != Nil; walk = class_getSuperclass(walk)) {
			[chain addObject:fn_class_name(walk)];
		}
		[entry setObject:fn_class_name(cls) forKey:kClassname];
		[entry setObject:chain forKey:kClasses];
		memo->_object = (id)cls;
		memo->_index = index;
		[_memo addObject:memo];
		[_objects addObject:entry];
		return index;
	}
}

- (NSUInteger)fnIndexOfObject:(id)object
{
	NSUInteger i;

	/* NIL IS `$null` AND NSNull IS AN OBJECT OF ITS OWN. Both used to be index 0, which meant a nil
	 * came back as an NSNull and the difference between "nothing was here" and "an empty place was
	 * here" was lost. Cocoa collapses them into `$null`; keeping them apart costs one table entry
	 * and preserves both — measured, because the probe asserts each. */
	if (object == nil) {
		return 0;
	}
	for (i = 0; i < [_memo count]; i++) {
		FNMemo *held = [_memo objectAtIndex:i];

		if (held->_object == object) {
			return held->_index;
		}
	}
	{
		NSUInteger index = [_objects count];
		NSMutableDictionary *entry = [NSMutableDictionary dictionary];
		FNMemo *memo = [[FNMemo alloc] init];

		memo->_object = object;
		memo->_index = index;
		[_memo addObject:memo];
		/* RESERVED BEFORE ITS CONTENTS — rule 1 in the file comment. */
		[_objects addObject:entry];
		[entry setObject:fn_reference([self fnClassIndexOf:[object class]]) forKey:kClass];
		if ([object isKindOfClass:[NSArray class]]) {
			NSArray *members = (NSArray *)object;
			NSMutableArray *slots = [NSMutableArray array];
			NSUInteger j;

			for (j = 0; j < [members count]; j++) {
				[slots addObject:[self fnSlotFor:[members objectAtIndex:j]]];
			}
			[entry setObject:slots forKey:kObjects];
		} else if ([object isKindOfClass:[NSDictionary class]]) {
			NSDictionary *pairs = (NSDictionary *)object;
			NSArray *keys = [pairs allKeys];
			NSMutableArray *keySlots = [NSMutableArray array];
			NSMutableArray *valueSlots = [NSMutableArray array];
			NSUInteger j;

			for (j = 0; j < [keys count]; j++) {
				id key = [keys objectAtIndex:j];

				[keySlots addObject:[self fnSlotFor:key]];
				[valueSlots addObject:[self fnSlotFor:[pairs objectForKey:key]]];
			}
			[entry setObject:keySlots forKey:kKeys];
			[entry setObject:valueSlots forKey:kObjects];
		} else if ([object isKindOfClass:[NSNull class]]) {
			/* AN EMPTY OBJECT ENTRY: NSNull has no state to write, and its CLASS is the whole
			 * message. */
		} else {
			/* AN NSCoding OBJECT, and the two doors are the protocol's. */
			if (![object respondsToSelector:@selector(encodeWithCoder:)]) {
				[NSException raise:NSInvalidArgumentException
					    format:@"NSKeyedArchiver: %@ is neither a value type nor an "
						   "NSCoding object", [object class]];
			}
			[_stack addObject:entry];
			[object encodeWithCoder:self];
			[_stack removeLastObject];
		}
		return index;
	}
}

- (id)fnSlotFor:(id)object
{
	if (object == nil) {
		return fn_reference(0);	/* $null */
	}
	if ([object isKindOfClass:[NSNull class]]) {
		/* AN NSNull IS AN OBJECT HERE, not an absent value: it gets its own entry so that a nil and
		 * an NSNull can be told apart after the round trip. */
		return fn_reference([self fnIndexOfObject:object]);
	}
	if (fn_is_value_type(object)) {
		return object;
	}
	return fn_reference([self fnIndexOfObject:object]);
}

- (void)encodeObject:(nullable id)object forKey:(NSString *)key
{
	NSMutableDictionary *entry = [_stack lastObject];

	if (entry == nil || key == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedArchiver: -encodeObject:forKey: is only meaningful inside "
				   "-encodeWithCoder:"];
	}
	[entry setObject:[self fnSlotFor:object] forKey:key];
}

- (void)encodeBool:(BOOL)value forKey:(NSString *)key
{
	[self encodeObject:[NSNumber numberWithBool:value] forKey:key];
}

- (void)encodeInt:(int)value forKey:(NSString *)key
{
	[self encodeObject:[NSNumber numberWithInt:value] forKey:key];
}

- (void)encodeInteger:(NSInteger)value forKey:(NSString *)key
{
	[self encodeObject:[NSNumber numberWithInteger:value] forKey:key];
}

- (void)encodeDouble:(double)value forKey:(NSString *)key
{
	[self encodeObject:[NSNumber numberWithDouble:value] forKey:key];
}

- (void)encodeFloat:(float)value forKey:(NSString *)key
{
	[self encodeObject:[NSNumber numberWithFloat:value] forKey:key];
}

- (void)encodeBytes:(const void *)bytes length:(NSUInteger)length forKey:(NSString *)key
{
	NSData *data = bytes != NULL ? [NSData dataWithBytes:bytes length:length] : nil;

	[self encodeObject:data forKey:key];
}

- (void)finishEncoding
{
	/* NOTHING TO FLUSH: every write went straight into the table, and the plist is produced by
	 * -fnArchivedDataWithRootIndex:. It exists because Cocoa's flow calls it. */
}

- (nullable NSData *)fnArchivedDataWithRootIndex:(NSUInteger)rootIndex
{
	NSDictionary *archive = @{
		@"$version" : @1,
		@"$archiver" : @"NSKeyedArchiver",
		@"$objects" : _objects,
		@"$top" : @{ @"root" : fn_reference(rootIndex) }
	};

	return [NSPropertyListSerialization dataWithPropertyList:archive
							  format:NSPropertyListXMLFormat_v1_0
							 options:0
							   error:NULL];
}

@end

@implementation NSKeyedUnarchiver

+ (nullable id)unarchiveObjectWithData:(NSData *)data
{
	NSKeyedUnarchiver *unarchiver = [[self alloc] initForReadingWithData:data];

	return [unarchiver fnDecodeRoot];
}

+ (nullable id)unarchiveObjectWithFile:(NSString *)path
{
	NSData *data = [NSData dataWithContentsOfFile:path];

	return data != nil ? [self unarchiveObjectWithData:data] : nil;
}

- (instancetype)initForReadingWithData:(NSData *)data
{
	id plist;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	plist = [NSPropertyListSerialization propertyListWithData:data
							  options:0
							   format:NULL
							    error:NULL];
	if (![plist isKindOfClass:[NSDictionary class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: the data is not a keyed archive"];
	}
	_table = [(NSDictionary *)plist objectForKey:@"$objects"];
	_top = [(NSDictionary *)plist objectForKey:@"$top"];
	if (![_table isKindOfClass:[NSArray class]] || ![_top isKindOfClass:[NSDictionary class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: the archive has no $objects table or no $top"];
	}
	_memo = [[NSMutableArray alloc] init];
	for (NSUInteger i = 0; i < [_table count]; i++) {
		[_memo addObject:[NSNull null]];	/* "nothing built for this index yet" */
	}
	_stack = [[NSMutableArray alloc] init];
	return self;
}

- (id)fnDecodeSlot:(id)slot
{
	NSUInteger index = 0;

	if (fn_slot_is_reference(slot, &index)) {
		return [self fnObjectAtIndex:index];
	}
	return slot;			/* a VALUE, written inline */
}

- (id)fnObjectAtIndex:(NSUInteger)index
{
	id entry;
	id classSlot;
	NSString *className;
	Class cls;
	id object;
	NSUInteger classIndex = 0;

	if (index >= [_table count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: the archive refers to object %lu of %lu",
				   (unsigned long)index, (unsigned long)[_table count]];
	}
	if (index == 0) {
		return nil;		/* $null: the reference that means "nothing was here" */
	}
	{
		id built = [_memo objectAtIndex:index];

		if (![built isKindOfClass:[NSNull class]]) {
			return built;
		}
	}
	entry = [_table objectAtIndex:index];
	if (![entry isKindOfClass:[NSDictionary class]]) {
		/* A PLAIN VALUE IN THE TABLE, which the writer does not produce but a hand-written
		 * archive may carry. It is its own answer. */
		return entry;
	}
	classSlot = [(NSDictionary *)entry objectForKey:kClass];
	if (!fn_slot_is_reference(classSlot, &classIndex) || classIndex >= [_table count]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: object %lu has no usable $class",
				   (unsigned long)index];
	}
	className = [(NSDictionary *)[_table objectAtIndex:classIndex] objectForKey:kClassname];
	if (![className isKindOfClass:[NSString class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: object %lu names no class",
				   (unsigned long)index];
	}
	if ([className isEqualToString:@"NSNull"]) {
		/* THE EMPTY OBJECT: its class IS its state. */
		[_memo replaceObjectAtIndex:index withObject:[NSNull null]];
		return [NSNull null];
	}
	if ([className isEqualToString:@"NSArray"] || [className isEqualToString:@"NSDictionary"]) {
		id collection = [className isEqualToString:@"NSArray"]
			      ? (id)[NSMutableArray array] : (id)[NSMutableDictionary dictionary];
		NSArray *slots;

		/* REGISTERED BEFORE ITS MEMBERS ARE DECODED, so a collection that contains itself — or
		 * that two parents share — is built once. */
		[_memo replaceObjectAtIndex:index withObject:collection];
		if ([className isEqualToString:@"NSArray"]) {
			slots = [(NSDictionary *)entry objectForKey:kObjects];
			for (NSUInteger i = 0; slots != nil && i < [slots count]; i++) {
				[collection addObject:[self fnDecodeSlot:[slots objectAtIndex:i]]];
			}
		} else {
			NSArray *keys = [(NSDictionary *)entry objectForKey:kKeys];

			slots = [(NSDictionary *)entry objectForKey:kObjects];
			for (NSUInteger i = 0; keys != nil && i < [keys count]; i++) {
				id key = [self fnDecodeSlot:[keys objectAtIndex:i]];
				id value = slots != nil && i < [slots count]
					 ? [self fnDecodeSlot:[slots objectAtIndex:i]] : [NSNull null];

				if (key != nil && ![key isKindOfClass:[NSNull class]]) {
					[collection setObject:value forKey:key];
				}
			}
		}
		return collection;
	}
	cls = objc_getClass([className UTF8String]);
	if (cls == Nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: the archive names a class this process does not "
				   "have (%@)", className];
	}
	object = [cls alloc];
	if (![object respondsToSelector:@selector(initWithCoder:)]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: %@ does not implement -initWithCoder:", cls];
	}
	[_memo replaceObjectAtIndex:index withObject:object];	/* reserved: a cycle sees it in time */
	[_stack addObject:entry];
	object = [object initWithCoder:self];
	[_stack removeLastObject];
	[_memo replaceObjectAtIndex:index withObject:object];
	return object;
}

- (id)fnCurrentEntry
{
	id entry = [_stack lastObject];

	if (entry == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: -decodeObjectForKey: is only meaningful inside "
				   "-initWithCoder:"];
	}
	return entry;
}

- (id)fnDecodeRoot
{
	NSUInteger rootIndex = 0;

	if (_root != nil) {
		return _root;
	}
	if (!fn_slot_is_reference([(NSDictionary *)_top objectForKey:@"root"], &rootIndex)) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: the archive's $top names no root"];
	}
	_root = [self fnObjectAtIndex:rootIndex];
	return _root;
}

- (nullable id)decodeObjectForKey:(NSString *)key
{
	id entry = [self fnCurrentEntry];

	if (key == nil || [(NSDictionary *)entry objectForKey:key] == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: nothing was written for the key \"%@\"", key];
	}
	return [self fnDecodeSlot:[(NSDictionary *)entry objectForKey:key]];
}

- (BOOL)containsValueForKey:(NSString *)key
{
	return key != nil && [(NSDictionary *)[self fnCurrentEntry] objectForKey:key] != nil;
}

- (BOOL)decodeBoolForKey:(NSString *)key
{
	return [[self decodeObjectForKey:key] boolValue];
}

- (int)decodeIntForKey:(NSString *)key
{
	return [[self decodeObjectForKey:key] intValue];
}

- (NSInteger)decodeIntegerForKey:(NSString *)key
{
	return [[self decodeObjectForKey:key] integerValue];
}

- (double)decodeDoubleForKey:(NSString *)key
{
	return [[self decodeObjectForKey:key] doubleValue];
}

- (float)decodeFloatForKey:(NSString *)key
{
	return [[self decodeObjectForKey:key] floatValue];
}

- (nullable const void *)decodeBytesForKey:(NSString *)key
			    returnedLength:(nullable NSUInteger *)lengthp
{
	id value = [self decodeObjectForKey:key];

	if (value == nil) {
		if (lengthp != NULL) {
			*lengthp = 0;
		}
		return NULL;
	}
	if (![value isKindOfClass:[NSData class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: \"%@\" is not bytes but %@", key, [value class]];
	}
	if (lengthp != NULL) {
		*lengthp = [(NSData *)value length];
	}
	return [(NSData *)value bytes];
}

- (void)finishDecoding
{
	/* NOTHING TO RELEASE: the table is the archive's, and nothing here holds a stream. */
}

@end
