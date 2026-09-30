/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSKeyedArchiver.m — the archive writer and reader (F13.12). MANUAL OWNERSHIP.
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

#import <Foundation/NSKeyedArchiver.h>
#import <Foundation/NSCoding.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSData.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSCountedSet.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSException.h>
#import <objc/runtime.h>

static NSString *const kRef = @"$ref";
static NSString *const kClass = @"$class";
static NSString *const kClassname = @"$classname";
static NSString *const kClasses = @"$classes";
static NSString *const kObjects = @"NS.objects";
static NSString *const kKeys = @"NS.keys";
/* THE COUNTED SET'S SECOND PAYLOAD. `-allObjects` on an NSCountedSet deliberately answers each distinct
 * member ONCE — that is the class's own documented reading of itself, and it agrees with `-count` — so the
 * multiplicities live beside the members rather than inside them, and this is the key they live under. It
 * is OURS: Apple publishes no key name for the counts, so there is no value to match (§11.6.1 D2's
 * ground), and writing the members alone would silently lose the one thing the class exists to carry. */
static NSString *const kCounts = @"NS.counts";

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

/* THE KEY THE ROOT OBJECT IS STORED UNDER (§62.103). */
NSString * const NSKeyedArchiveRootObjectKey = @"NSKeyedArchiveRootObjectKey";

@implementation NSKeyedArchiver

+ (nullable NSData *)archivedDataWithRootObject:(id)rootObject
{
	/* THE CLASS METHOD *IS* THE INSTANCE FLOW, with the root under Cocoa's own key. That is what makes a
	 * delegate reachable at all: a delegate belongs to an INSTANCE, and before this the class method built
	 * its root through an internal path that no delegate could observe. */
	NSMutableData *data = [[NSMutableData alloc] init];
	NSKeyedArchiver *archiver = [[self alloc] initForWritingWithMutableData:data];

	[archiver encodeObject:rootObject forKey:@"root"];
	[archiver finishEncoding];
	return [data autorelease];
}

+ (BOOL)archiveRootObject:(id)rootObject toFile:(NSString *)path
{
	NSData *data = [self archivedDataWithRootObject:rootObject];

	return data != nil && [data writeToFile:path atomically:YES];
}

- (instancetype)initForWritingWithMutableData:(NSMutableData *)data
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_objects = [[NSMutableArray alloc] init];
	_memo = [[NSMutableArray alloc] init];
	_stack = [[NSMutableArray alloc] init];
	_top = [[NSMutableDictionary alloc] init];
	_data = [data retain];	/* THE CALLER'S BUFFER, which -finishEncoding fills */
	/* INDEX 0 IS `$null`, so a nil and an NSNull are the same reference and need no type. */
	[_objects addObject:@"$null"];
	return self;
}

- (void)dealloc
{
	[_objects release];
	[_memo release];
	[_stack release];
	[_top release];
	[_data release];
	[super dealloc];
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
		[entry setObject:fn_reference([self fnClassIndexOf:[object classForCoder]]) forKey:kClass];
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
		} else if ([object isKindOfClass:[NSSet class]]) {
			/* A SET, AND IT IS A COLLECTION THE ARCHIVER ITSELF KNOWS — like an array or a
			 * dictionary, and for the same reason: the members go under `NS.objects`, which is the
			 * key a reader already knows, and the class entry names the PUBLIC class because
			 * `-classForCoder` answers it (§C.3 item 4). `-allObjects` is the door that answers the
			 * members, which is what keeps this branch off a private concrete class's storage.
			 *
			 * AN NSCountedSet CARRIES A SECOND PAYLOAD (see kCounts): its members are written once
			 * each, exactly as `-allObjects` and `-count` describe them, and the multiplicities ride
			 * beside them. */
			NSSet *members = (NSSet *)object;
			NSArray *all = [members allObjects];
			NSMutableArray *slots = [NSMutableArray array];
			NSUInteger j;

			for (j = 0; j < [all count]; j++) {
				[slots addObject:[self fnSlotFor:[all objectAtIndex:j]]];
			}
			[entry setObject:slots forKey:kObjects];
			if ([object isKindOfClass:[NSCountedSet class]]) {
				NSMutableArray *counts = [NSMutableArray array];

				for (j = 0; j < [all count]; j++) {
					[counts addObject:[NSNumber numberWithUnsignedInteger:
						[(NSCountedSet *)object
							countForObject:[all objectAtIndex:j]]]];
				}
				[entry setObject:counts forKey:kCounts];
			}
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
	id slot;
	id encoded;

	/* THE DELEGATE'S SUBSTITUTING DOOR, AND IT IS ASKED BEFORE ANY WORK: `-archiver:willEncodeObject:`
	 * answers THE OBJECT TO ENCODE, so whatever it returns is what the rest of this method writes. It is
	 * asked for a reference only — the nil and NSNull cases below keep their own meaning, and a delegate
	 * that answered nil for everything would otherwise silently empty an archive. */
	encoded = object;
	if (encoded != nil && _delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(archiver:willEncodeObject:)]) {
		encoded = [(id)_delegate archiver:self willEncodeObject:encoded];
	}
	slot = [self fnSlotForEncoded:encoded];
	if (_delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(archiver:didEncodeObject:)]) {
		[(id)_delegate archiver:self didEncodeObject:encoded];
	}
	return slot;
}

/* The work fnSlotFor: used to do inline, with the delegate's substitution already applied. */
- (id)fnSlotForEncoded:(id)object
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

	if (key == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedArchiver: -encodeObject:forKey: needs a key"];
	}
	/* OUTSIDE -encodeWithCoder: THE KEYS ARE THE ARCHIVE'S TOP-LEVEL ONES. That is Cocoa's instance flow:
	 * an archiver built over a mutable buffer is handed its root (and anything else top-level) directly, and
	 * those entries live in `$top`, where a reader's `-decodeObjectForKey:` finds them. */
	if (entry == nil) {
		[_top setObject:[self fnSlotFor:object] forKey:key];
		return;
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
	/* THE TWO HALVES OF THE END: the delegate is told BEFORE the archive is closed — which is the window in
	 * which it may still add an entry — and again after. */
	if (_delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(archiverWillFinish:)]) {
		[(id)_delegate archiverWillFinish:self];
	}
	/* THE ARCHIVE IS PRODUCED HERE, and that is what makes the caller's buffer THE archive. Writing it
	 * between the two finish doors is deliberate: `willFinish` is the window in which a delegate may still
	 * add an entry, so the bytes must not be cut before it, and `didFinish` reports a finished archive. */
	if (_data != nil && [_top count] > 0) {
		NSData *bytes = [self fnArchiveBytes];

		if (bytes != nil) {
			[_data setData:bytes];
		}
	}
	if (_delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(archiverDidFinish:)]) {
		[(id)_delegate archiverDidFinish:self];
	}
}

- (nullable id <NSKeyedArchiverDelegate>)delegate
{
	return _delegate;
}

- (void)setDelegate:(nullable id <NSKeyedArchiverDelegate>)delegate
{
	_delegate = delegate;	/* NOT retained: see the header */
}

/* THE ARCHIVE'S BYTES, from the table and the top-level keys as they now stand. */
- (nullable NSData *)fnArchiveBytes
{
	NSDictionary *archive = @{
		@"$version" : @1,
		@"$archiver" : @"NSKeyedArchiver",
		@"$objects" : _objects,
		@"$top" : _top
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
	/* THE COLLECTION CLASSES, AND **BOTH SPELLINGS OF EACH**: the writer records the class it was handed, so
	 * an NSMutableArray is written as `NSMutableArray` — a spelling this reader did not know, which made an
	 * archive holding one UNREADABLE ("NSMutableArray does not implement -initWithCoder:"). The mutable and
	 * immutable names are one case here because a decoded collection is built MUTABLE either way, which is
	 * what lets an archive be filled in as it is read. */
	{
		BOOL isArray = [className isEqualToString:@"NSArray"] ||
			       [className isEqualToString:@"NSMutableArray"];
		BOOL isDictionary = [className isEqualToString:@"NSDictionary"] ||
				    [className isEqualToString:@"NSMutableDictionary"];
		/* THE SET FAMILY, ALL THREE SPELLINGS — a plain set, a mutable one, and the COUNTED set
		 * whose multiplicities arrive beside its members under kCounts. */
		BOOL isCountedSet = [className isEqualToString:@"NSCountedSet"];
		BOOL isSet = isCountedSet ||
			     [className isEqualToString:@"NSSet"] ||
			     [className isEqualToString:@"NSMutableSet"];

		if (isArray || isDictionary || isSet) {
			id collection = isArray ? (id)[NSMutableArray array]
				      : isDictionary ? (id)[NSMutableDictionary dictionary]
				      : isCountedSet ? (id)[NSCountedSet set]
				      : (id)[NSMutableSet set];
			NSArray *slots;

			/* REGISTERED BEFORE ITS MEMBERS ARE DECODED, so a collection that contains itself — or
			 * that two parents share — is built once. */
			[_memo replaceObjectAtIndex:index withObject:collection];
			if (isArray || isSet) {
				slots = [(NSDictionary *)entry objectForKey:kObjects];
				if (isCountedSet) {
					/* THE COUNTS GO BACK AS ADDITIONS, because that is what a count IS: a
					 * member's multiplicity rises by one every time `-addObject:` sees it again.
					 * A member ADDED ONCE is what a plain set means, which is why the same loop
					 * serves both — and a missing counts array falls back to one rather than
					 * dropping the member. */
					NSArray *counts = [(NSDictionary *)entry objectForKey:kCounts];

					for (NSUInteger i = 0; slots != nil && i < [slots count]; i++) {
						id member = [self fnDecodeSlot:[slots objectAtIndex:i]];
						NSUInteger n = counts != nil && i < [counts count]
							     ? [[counts objectAtIndex:i] unsignedIntegerValue] : 1;

						for (NSUInteger c = 0; c < n; c++) {
							[collection addObject:member];
						}
					}
				} else {
					for (NSUInteger i = 0; slots != nil && i < [slots count]; i++) {
						[collection addObject:[self fnDecodeSlot:
							[slots objectAtIndex:i]]];
					}
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
	}
	cls = objc_getClass([className UTF8String]);
	if (cls == Nil) {
		/* THE DELEGATE'S CLASS DOOR, AND THIS IS THE POINT OF HAVING IT: an archive NAMES its classes as
		 * strings, so a reader may be handed a name it does not have. The delegate's answer is the class
		 * to decode INSTEAD — and nil is the refusal, which the exception below then reports. */
		if (_delegate != nil && [(id)_delegate respondsToSelector:
				@selector(unarchiver:cannotDecodeObjectOfClassName:originalClasses:)]) {
			cls = [(id)_delegate unarchiver:self
				  cannotDecodeObjectOfClassName:className
				       originalClasses:[self fnClassNamesAt:classIndex fallback:className]];
		}
	}
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
	/* THE DELEGATE'S SUBSTITUTING DOOR: it receives each object as it is built and may answer a
	 * replacement, which the memo learns as well — so a SECOND reference to this index gets the
	 * substitute rather than a fresh decode of the original. A NIL ANSWER MEANS "NO SUBSTITUTE" rather
	 * than "replace it with nothing": an optional door that could empty an object graph would be a trap,
	 * and that is also the only reading under which the answer is genuinely optional. */
	if (_delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(unarchiver:didDecodeObject:)]) {
		id replacement = [(id)_delegate unarchiver:self didDecodeObject:object];

		if (replacement != nil && replacement != object) {
			object = replacement;
			[_memo replaceObjectAtIndex:index withObject:object];
		}
	}
	return object;
}

/* THE NAMES THE ARCHIVE GAVE FOR A CLASS — the `originalClasses` the delegate's class door reports. The
 * archive carries a `$classes` chain for a class it had to describe; an archive that carries none is
 * described by the name the object itself used, which keeps the argument from ever being empty. */
- (NSArray *)fnClassNamesAt:(NSUInteger)classIndex fallback:(NSString *)className
{
	id chain = [(NSDictionary *)[_table objectAtIndex:classIndex] objectForKey:kClasses];

	if ([chain isKindOfClass:[NSArray class]] && [(NSArray *)chain count] > 0) {
		return chain;
	}
	return [NSArray arrayWithObject:className];
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
	id slot = [(NSDictionary *)_top objectForKey:@"root"];
	NSUInteger rootIndex = 0;

	if (_root != nil) {
		return _root;
	}
	if (slot == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: the archive's $top names no root"];
	}
	/* THE ROOT MAY BE INLINE: a VALUE type (a string, a number, a date, bytes) is written where it stands
	 * rather than into the table, so a root of one of those is its own answer. Requiring a reference here
	 * would fail for exactly the archives the WRITER produces for such a root. */
	_root = fn_slot_is_reference(slot, &rootIndex)
	      ? [self fnObjectAtIndex:rootIndex] : [self fnDecodeSlot:slot];
	return _root;
}

- (nullable id)decodeObjectForKey:(NSString *)key
{
	id entry;

	if (key == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: -decodeObjectForKey: needs a key"];
	}
	/* OUTSIDE -initWithCoder: THE KEYS ARE THE ARCHIVE'S TOP-LEVEL ONES — the reader's half of Cocoa's
	 * instance flow, and the ONLY way a caller who built an unarchiver can read its root. Without it an
	 * unarchiver's delegate (and its whole class door) is unreachable, exactly as an archiver's is
	 * without the writer's half. */
	if ([_stack count] == 0) {
		id slot = [(NSDictionary *)_top objectForKey:key];

		if (slot == nil) {
			[NSException raise:NSInvalidArgumentException
				    format:@"NSKeyedUnarchiver: the archive's $top names nothing for "
					   "the key \"%@\"", key];
		}
		return [self fnDecodeSlot:slot];
	}
	entry = [self fnCurrentEntry];

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
	/* THE TWO HALVES OF THE END, split so that a delegate has a window while the graph is still open (to
	 * ask for one more object) and another after it is closed. */
	if (_delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(unarchiverWillFinish:)]) {
		[(id)_delegate unarchiverWillFinish:self];
	}
	/* NOTHING TO RELEASE: the table is the archive's, and nothing here holds a stream. */
	if (_delegate != nil &&
	    [(id)_delegate respondsToSelector:@selector(unarchiverDidFinish:)]) {
		[(id)_delegate unarchiverDidFinish:self];
	}
}

- (nullable id <NSKeyedUnarchiverDelegate>)delegate
{
	return _delegate;
}

- (void)setDelegate:(nullable id <NSKeyedUnarchiverDelegate>)delegate
{
	_delegate = delegate;	/* NOT retained: see the header */
}

@end
