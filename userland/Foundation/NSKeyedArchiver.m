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
#import <Foundation/NSOrderedSet.h>
#import <Foundation/NSMutableOrderedSet.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSValue.h>	/* the geometry doors box their struct in one of these */
#import <Foundation/NSNull.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/FNKeyedWire.h>	/* the collection payload keys, shared with the collections (§63.10) */
#import <objc/runtime.h>

static NSString *const kRef = @"$ref";
static NSString *const kClass = @"$class";
static NSString *const kClassname = @"$classname";
static NSString *const kClasses = @"$classes";
/* THE TWO COLLECTION KEYS ARE SHARED, NOT SPELLED TWICE (§63.10): the collections' own NSCoding doors write
 * and read these same two names, and the header is where the single definition lives. The `#define` keeps
 * every use below unchanged rather than renaming eleven call sites for no gain. */
#define kObjects FNKeyedObjectsKey
#define kKeys FNKeyedKeysKey
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

/* THE ROOT'S ACTUAL KEY, which is a LITERAL and not `NSKeyedArchiveRootObjectKey` above: the class-method
 * flow writes its root under "root" (`+archivedDataWithRootObject:`), and the reader's root door must
 * read the same one. The fixed constant and this key do not agree, which is Cocoa's own inconsistency
 * and not a mistake to paper over here. */
static NSString *const kRootKey = @"root";

/* An NSError for the top-level doors, in the domain Apple uses for coder failures. `description` is the
 * human half; the code is one of this library's NSCoder* codes. */
static NSError *fn_decode_error(NSInteger code, NSString *description)
{
	return [NSError errorWithDomain:NSCocoaErrorDomain
				   code:code
			       userInfo:description != nil
				      ? @{ NSLocalizedDescriptionKey : description } : nil];
}

/* IS THE VALUE ONE OF THESE CLASSES? `classes == nil` is "no list was given", which this library reads
 * as no restriction — a caller who wanted one named one. The list is walked rather than asked with
 * `-containsObject:`, because a Class object is not equal to its instances. */
static BOOL fn_value_is_allowed(id value, NSSet *classes)
{
	NSArray *list;
	NSUInteger i;

	if (classes == nil) {
		return YES;
	}
	list = [classes allObjects];
	for (i = 0; i < [list count]; i++) {
		if ([value isKindOfClass:(Class)[list objectAtIndex:i]]) {
			return YES;
		}
	}
	return NO;
}

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
		} else if ([object isKindOfClass:[NSOrderedSet class]]) {
			/* AN ORDERED SET IS A COLLECTION THE ARCHIVER KNOWS BY KIND TOO, and its ORDER IS PART
			 * OF ITS STATE — the whole difference from the `NSSet` branch above, whose members have
			 * no order to lose. `-array` is the door that answers the members IN ORDER (it walks the
			 * primitives rather than handing out storage), and the reader re-adds them in exactly
			 * that order, so there is nothing extra to write: `NS.objects` IS the order. */
			NSArray *all = [(NSOrderedSet *)object array];
			NSMutableArray *slots = [NSMutableArray array];
			NSUInteger j;

			for (j = 0; j < [all count]; j++) {
				[slots addObject:[self fnSlotFor:[all objectAtIndex:j]]];
			}
			[entry setObject:slots forKey:kObjects];
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

- (BOOL)allowsKeyedCoding
{
	return YES;	/* this IS the keyed family */
}

- (void)encodeInt32:(int32_t)value forKey:(NSString *)key
{
	/* THE WIDTH TRAVELS AS THE NSNumber'S TYPE, which is what lets -decodeInt32ForKey: keep it. */
	[self encodeObject:[NSNumber numberWithInt:(int)value] forKey:key];
}

- (void)encodeInt64:(int64_t)value forKey:(NSString *)key
{
	[self encodeObject:[NSNumber numberWithLongLong:(long long)value] forKey:key];
}

- (void)encodeConditionalObject:(nullable id)object forKey:(NSString *)key
{
	NSUInteger i;

	/* CONDITIONAL MEANS "ONLY IF IT IS ALREADY HERE": the object is written as a reference when the memo
	 * already holds it (someone else reached it first) and as nil when it does not — so a class may point
	 * at its owner or delegate WITHOUT dragging it into the archive. A nil object is the ordinary nil,
	 * which is a value here; a VALUE type has no identity to condition on and is written inline anyway. */
	if (key == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedArchiver: -encodeConditionalObject:forKey: needs a key"];
	}
	if (object == nil || fn_is_value_type(object)) {
		[self encodeObject:object forKey:key];
		return;
	}
	for (i = 0; i < [_memo count]; i++) {
		FNMemo *held = [_memo objectAtIndex:i];

		if (held->_object == object) {
			[self encodeObject:object forKey:key];	/* a reference to the entry that exists */
			return;
		}
	}
	[self encodeObject:nil forKey:key];	/* not yet encoded: nothing is written */
}

/* ⚠ THE `CG`-SPELLED KEYED GEOMETRY DOORS ARE NOT FOUNDATION'S ANY MORE (§63.54): they are the CoreGraphics
 * tier's — `userland/CoreGraphics/NSCoderCGGeometry.{h,m}`, a category on `NSCoder` over the PUBLIC keyed
 * doors. The `NS`-spelled pair (`-encodePoint:forKey:`) stays, because macOS's `NSGeometry.h` declares it.
 *
 * ⚠⚠ AND THIS FILE IS WHERE A SILENT BREAK LIVED FOR ONE COMMIT: these ten implementations built their boxes
 * with the `CG`-spelled `NSValue` doors, so moving THOSE out (§63.53) left ten call sites here pointing at
 * selectors that no longer existed — and **an Objective-C message send emits NO UNDEFINED SYMBOL, so the linker
 * cannot see it, and the build passed only because this file's object was STALE** (its dependency is its own
 * `.m`, never a header). A forced rebuild is what showed it: five warnings for the class-method boxes and five
 * ERRORS for the readers ("returning 'id' from a function with incompatible result type 'CGPoint'"). The ten
 * implementations and their ten call sites left Foundation TOGETHER, which is why the fix and the move are one
 * change. */

/* The Foundation spellings (NSPoint/NSSize/NSRect) are the SAME boxes as the CG ones above, because this
 * tree's typedefs make the types identical (NSGeometry.h) — the door differs only in the name a caller
 * speaks, and both names are doors Apple declares. */
- (void)encodePoint:(NSPoint)point forKey:(NSString *)key
{
	[self encodeObject:[NSValue valueWithPoint:point] forKey:key];
}

- (void)encodeSize:(NSSize)size forKey:(NSString *)key
{
	[self encodeObject:[NSValue valueWithSize:size] forKey:key];
}

- (void)encodeRect:(NSRect)rect forKey:(NSString *)key
{
	[self encodeObject:[NSValue valueWithRect:rect] forKey:key];
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
		/* AND THE ORDERED SET, WHOSE MEMBERS ARRIVE IN ORDER — an array-shaped entry, decoded into a
		 * MUTABLE ordered set, because a decoded collection is built mutable either way and that is
		 * what lets the archive be filled in as it is read. */
		BOOL isOrderedSet = [className isEqualToString:@"NSOrderedSet"] ||
				    [className isEqualToString:@"NSMutableOrderedSet"];

		if (isArray || isDictionary || isSet || isOrderedSet) {
			id collection = isArray ? (id)[NSMutableArray array]
				      : isDictionary ? (id)[NSMutableDictionary dictionary]
				      : isCountedSet ? (id)[NSCountedSet set]
				      : isOrderedSet ? (id)[NSMutableOrderedSet orderedSet]
				      : (id)[NSMutableSet set];
			NSArray *slots;

			/* REGISTERED BEFORE ITS MEMBERS ARE DECODED, so a collection that contains itself — or
			 * that two parents share — is built once. */
			[_memo replaceObjectAtIndex:index withObject:collection];
			if (isArray || isSet || isOrderedSet) {
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
	/* SECURE CODING, WHEN IT WAS ASKED FOR. A class that does not claim `NSSecureCoding` — or, when an
	 * allow-list was given, is not on it — is REFUSED before it is ever instantiated. Off by default
	 * (`-requiresSecureCoding` is NO unless a caller turned it on), so an archive of this library's own
	 * classes still reads; a reader of FOREIGN data is the caller who turns it on. This is the
	 * enforcement `NSCoding.h` used to say was missing. */
	if (_requiresSecureCoding &&
	    (![cls conformsToProtocol:@protocol(NSSecureCoding)] ||
	     (_allowedClasses != nil && ![_allowedClasses containsObject:cls]))) {
		[self failWithError:fn_decode_error(NSCoderInvalidValueError,
			[NSString stringWithFormat:@"NSKeyedUnarchiver: %@ is not allowed under secure coding",
			 className])];
		return nil;
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

- (BOOL)allowsKeyedCoding
{
	return YES;	/* this IS the keyed family */
}

- (int32_t)decodeInt32ForKey:(NSString *)key
{
	return (int32_t)[[self decodeObjectForKey:key] intValue];
}

- (int64_t)decodeInt64ForKey:(NSString *)key
{
	return (int64_t)[[self decodeObjectForKey:key] longLongValue];
}

/* THE DECODER'S FAILURE DOOR, AND THE POLICY IS THE WHOLE OF IT: under SetErrorAndReturn the error is
 * RECORDED (and `-error` answers it), under RaiseException it is raised. The recorded error is retained,
 * because `-error` may be read long after `-failWithError:` returned. */
- (void)failWithError:(NSError *)error
{
	if (_decodingFailurePolicy == NSDecodingFailurePolicySetErrorAndReturn) {
		if (_error != error) {
			[_error release];
			_error = [error retain];
		}
		return;
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"NSKeyedUnarchiver: %@", [error localizedDescription]];
}

/* --- THE CLASS GATES: `NSSecureCoding`'s READING HALF (NSCoder.h says why) --------------------------
 *
 * Each decodes normally and then REFUSES a value of the wrong class through `-failWithError:`, so the
 * refusal is the decoder's policy decision (raise, or nil + `-error`) rather than a second rule. A nil
 * value stays nil: an ABSENT key is not the wrong class. */
- (nullable id)decodeObjectOfClass:(Class)aClass forKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];

	if (value != nil && ![value isKindOfClass:aClass]) {
		[self failWithError:fn_decode_error(NSCoderInvalidValueError,
			[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, not a %@",
			 key, [value class], aClass])];
		return nil;
	}
	return value;
}

- (nullable id)decodeObjectOfClasses:(nullable NSSet *)classes forKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];

	if (value != nil && !fn_value_is_allowed(value, classes)) {
		[self failWithError:fn_decode_error(NSCoderInvalidValueError,
			[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, which is not an "
			 "allowed class", key, [value class]])];
		return nil;
	}
	return value;
}

- (nullable NSArray *)decodeArrayOfObjectsOfClass:(Class)cls forKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];
	NSUInteger i;

	if (value == nil) {
		return nil;
	}
	if (![value isKindOfClass:[NSArray class]]) {
		[self failWithError:fn_decode_error(NSCoderInvalidValueError,
			[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, not an array",
			 key, [value class]])];
		return nil;
	}
	for (i = 0; i < [(NSArray *)value count]; i++) {
		id element = [(NSArray *)value objectAtIndex:i];

		if (element != nil && ![element isKindOfClass:cls]) {
			[self failWithError:fn_decode_error(NSCoderInvalidValueError,
				[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" holds a %@, not a %@",
				 key, [element class], cls])];
			return nil;
		}
	}
	return value;
}

- (nullable NSArray *)decodeArrayOfObjectsOfClasses:(nullable NSSet *)classes forKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];
	NSUInteger i;

	if (value == nil) {
		return nil;
	}
	if (![value isKindOfClass:[NSArray class]]) {
		[self failWithError:fn_decode_error(NSCoderInvalidValueError,
			[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, not an array",
			 key, [value class]])];
		return nil;
	}
	for (i = 0; i < [(NSArray *)value count]; i++) {
		id element = [(NSArray *)value objectAtIndex:i];

		if (element != nil && !fn_value_is_allowed(element, classes)) {
			[self failWithError:fn_decode_error(NSCoderInvalidValueError,
				[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" holds a %@, which is not "
				 "an allowed class", key, [element class]])];
			return nil;
		}
	}
	return value;
}

- (nullable NSDictionary *)decodeDictionaryWithKeysOfClass:(Class)keyClass
						    objectsOfClass:(Class)objectClass
							    forKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];
	NSArray *keys;
	NSUInteger i;

	if (value == nil) {
		return nil;
	}
	if (![value isKindOfClass:[NSDictionary class]]) {
		[self failWithError:fn_decode_error(NSCoderInvalidValueError,
			[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, not a dictionary",
			 key, [value class]])];
		return nil;
	}
	keys = [(NSDictionary *)value allKeys];
	for (i = 0; i < [keys count]; i++) {
		id k = [keys objectAtIndex:i];
		id v = [(NSDictionary *)value objectForKey:k];

		if (![k isKindOfClass:keyClass] || (v != nil && ![v isKindOfClass:objectClass])) {
			[self failWithError:fn_decode_error(NSCoderInvalidValueError,
				[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" has a %@/%@ pair that "
				 "is not a %@/%@ pair", key, [k class], [v class], keyClass, objectClass])];
			return nil;
		}
	}
	return value;
}

- (nullable NSDictionary *)decodeDictionaryWithKeysOfClasses:(nullable NSSet *)keyClasses
						     objectsOfClasses:(nullable NSSet *)objectClasses
							      forKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];
	NSArray *keys;
	NSUInteger i;

	if (value == nil) {
		return nil;
	}
	if (![value isKindOfClass:[NSDictionary class]]) {
		[self failWithError:fn_decode_error(NSCoderInvalidValueError,
			[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, not a dictionary",
			 key, [value class]])];
		return nil;
	}
	keys = [(NSDictionary *)value allKeys];
	for (i = 0; i < [keys count]; i++) {
		id k = [keys objectAtIndex:i];
		id v = [(NSDictionary *)value objectForKey:k];

		if (!fn_value_is_allowed(k, keyClasses) ||
		    (v != nil && !fn_value_is_allowed(v, objectClasses))) {
			[self failWithError:fn_decode_error(NSCoderInvalidValueError,
				[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" has a %@/%@ pair that "
				 "is not allowed", key, [k class], [v class]])];
			return nil;
		}
	}
	return value;
}

- (nullable const void *)decodeBytesForKey:(NSString *)key minimumLength:(NSUInteger)minimumLength
{
	id value = [self decodeObjectForKey:key];

	if (value == nil) {
		return NULL;
	}
	if (![value isKindOfClass:[NSData class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: \"%@\" is not bytes but %@", key, [value class]];
	}
	if ([(NSData *)value length] < minimumLength) {
		/* A SHORT RUN IS A CORRUPT ARCHIVE, not a shorter value: the writer promised a floor and the
		 * bytes do not meet it. */
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: \"%@\" holds %lu bytes, fewer than the %lu required",
				   key, (unsigned long)[(NSData *)value length],
				   (unsigned long)minimumLength];
	}
	return [(NSData *)value bytes];
}

- (nullable id)decodePropertyListForKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];

	/* A PROPERTY LIST IS ONE OF THE PLIST'S OWN TYPES — the same set the serialiser carries. This is a
	 * CHECK, not a coercion: an object that is not one of them is refused rather than described. */
	if (value == nil || [value isKindOfClass:[NSString class]] ||
	    [value isKindOfClass:[NSNumber class]] || [value isKindOfClass:[NSData class]] ||
	    [value isKindOfClass:[NSDate class]] || [value isKindOfClass:[NSArray class]] ||
	    [value isKindOfClass:[NSDictionary class]] || [value isKindOfClass:[NSNull class]]) {
		return value;
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"NSKeyedUnarchiver: \"%@\" is not a property list but %@", key, [value class]];
	return nil;
}

/* --- THE KEYED GEOMETRY DOORS' READING HALF (NSCoder.h says why they box an `NSValue`) --------------------
 *
 * The shared reading: the key names a boxed structure, and anything that is not an `NSValue` is a corrupt
 * archive — refused the same way `-decodeBytesForKey:minimumLength:` refuses a non-bytes value rather than
 * answering a zeroed structure that looks like data. A missing key still raises through
 * `-decodeObjectForKey:`, exactly as the scalar doors do. */
- (NSValue *)fnDecodeGeometryValueForKey:(NSString *)key
{
	id value = [self decodeObjectForKey:key];

	if (![value isKindOfClass:[NSValue class]]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSKeyedUnarchiver: \"%@\" is not a geometry value but %@",
				   key, [value class]];
	}
	return value;
}

- (NSPoint)decodePointForKey:(NSString *)key
{
	return [[self fnDecodeGeometryValueForKey:key] pointValue];
}

- (NSSize)decodeSizeForKey:(NSString *)key
{
	return [[self fnDecodeGeometryValueForKey:key] sizeValue];
}

- (NSRect)decodeRectForKey:(NSString *)key
{
	return [[self fnDecodeGeometryValueForKey:key] rectValue];
}

- (nullable id)decodeTopLevelObjectAndReturnError:(NSError * _Nullable * _Nullable)error
{
	/* THE ROOT, UNDER THE KEY THE CLASS-METHOD FLOW WRITES IT WITH (kRootKey). */
	return [self decodeTopLevelObjectForKey:kRootKey error:error];
}

- (nullable id)decodeTopLevelObjectForKey:(NSString *)key
				    error:(NSError * _Nullable * _Nullable)error
{
	id slot;

	if (error != NULL) {
		*error = nil;
	}
	if (key == nil) {
		if (error != NULL) {
			*error = fn_decode_error(NSCoderValueNotFoundError,
				@"NSKeyedUnarchiver: a nil key names nothing");
		}
		return nil;
	}
	slot = [(NSDictionary *)_top objectForKey:key];
	if (slot == nil) {
		if (error != NULL) {
			*error = fn_decode_error(NSCoderValueNotFoundError,
				[NSString stringWithFormat:@"NSKeyedUnarchiver: the archive's $top names nothing "
				 "for \"%@\"", key]);
		}
		return nil;
	}
	/* ANYTHING the decode raises becomes the ERROR VALUE instead of an exception — that is the whole
	 * difference between this door family and -decodeObjectForKey: (and the reason a caller reading
	 * someone else's archive reaches for these). */
	@try {
		return [self fnDecodeSlot:slot];
	} @catch (NSException *exception) {
		if (error != NULL) {
			*error = fn_decode_error(NSCoderReadCorruptError, [exception reason]);
		}
		return nil;
	}
}

- (nullable id)decodeTopLevelObjectOfClass:(Class)cls
				    forKey:(NSString *)key
				     error:(NSError * _Nullable * _Nullable)error
{
	NSError *inner = nil;
	id value = [self decodeTopLevelObjectForKey:key error:&inner];

	if (value == nil) {
		if (error != NULL) {
			*error = inner;
		}
		return nil;
	}
	if (![value isKindOfClass:cls]) {
		if (error != NULL) {
			*error = fn_decode_error(NSCoderInvalidValueError,
				[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, not a %@",
				 key, [value class], cls]);
		}
		return nil;
	}
	return value;
}

- (nullable id)decodeTopLevelObjectOfClasses:(nullable NSSet *)classes
				      forKey:(NSString *)key
				       error:(NSError * _Nullable * _Nullable)error
{
	NSError *inner = nil;
	id value = [self decodeTopLevelObjectForKey:key error:&inner];

	if (value == nil) {
		if (error != NULL) {
			*error = inner;
		}
		return nil;
	}
	if (!fn_value_is_allowed(value, classes)) {
		if (error != NULL) {
			*error = fn_decode_error(NSCoderInvalidValueError,
				[NSString stringWithFormat:@"NSKeyedUnarchiver: \"%@\" is a %@, which is not an "
				 "allowed class", key, [value class]]);
		}
		return nil;
	}
	return value;
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
