/*
 * foundation_clusters.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * M0 OF docs/design/foundation-clusters-plan.md: THE MECHANISM A CLASS CLUSTER NEEDS, PROVED ON A CLUSTER
 * THIS PROBE DEFINES ITSELF. The plan's §C.3 lists the contract the library will match in sixteen
 * families; this unit asserts it where it can be proved WITHOUT changing any shipped class — an
 * ordinary class's `-classForCoder` default, and a front whose concrete classes are private to this file.
 *
 * TWO THINGS HERE ARE THE WHOLE POINT, and the second is the one the plan says everything depends on:
 *
 *  1. `-class` answers the CONCRETE class while `-classForCoder` answers the FRONT, so a caller can be
 *     surprised (that is what a cluster is) and an ARCHIVER never is.
 *  2. THE ARCHIVER'S HINGE IS PROVED BY SEARCHING THE ARCHIVE'S OWN BYTES: the public name must be
 *     there and the private names must NOT be. A check that only asserted `[obj classForCoder]` would
 *     pass even if NSKeyedArchiver went on recording `-class`, which is exactly the bug the plan's
 *     §C.4 table is about (`NSKeyedArchiver.m:205`).
 */
#import <Foundation/Foundation.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

static int okc = 0;
static int failc = 0;

static int lastcheck;

static void check(const char *name, int ok, const char *detail)
{
	lastcheck = ok;	/* read by covers(): a claim can only follow an assertion that held */
	if (ok) {
		okc++;
		printf("  FOUNDATION-CLUSTERS %s ok\n", name);
	} else {
		failc++;
		printf("  FOUNDATION-CLUSTERS %s FAIL %s\n", name, detail);
	}
}

/* covers("NSData", "length") - the behavioural claim, piggybacked on the check above it: no condition of its
 * own, printed only when the last check's result was true. See tools/foundation-cov.py; a claim for a row the
 * ledger does not carry is inert, so every claim is filtered before it is written. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* A BYTE-RUN SEARCH, spelled out rather than using memmem: the probe must not depend on a GNU extension
 * being present in the libc this runs over. */
static int contains(const void *haystack, size_t hayLength, const char *needle)
{
	size_t needleLength = strlen(needle);
	size_t i;

	if (needleLength == 0 || hayLength < needleLength) {
		return 0;
	}
	for (i = 0; i + needleLength <= hayLength; i++) {
		if (memcmp((const char *)haystack + i, needle, needleLength) == 0) {
			return 1;
		}
	}
	return 0;
}

/* ===================================================================================================
 * THE CLUSTER: ONE PUBLIC FRONT, TWO PRIVATE CONCRETE CLASSES.
 * =================================================================================================== */
@interface ProbeCluster : NSObject <NSCoding>
/* THE PRIMITIVE — the one thing a subclass must override (§C.3 item 5), and everything else here is
 * written over it. */
- (NSUInteger)probeCount;
+ (instancetype)clusterWithCount:(NSUInteger)count;
@end

/* PRIVATE IN THE ONLY SENSE THAT MATTERS: declared in this file and in no header anyone imports. */
@interface ProbeClusterEmpty : ProbeCluster
@end

@interface ProbeClusterHolding : ProbeCluster
{
	NSUInteger _count;
}
- (instancetype)initWithCount:(NSUInteger)count;
@end

/* AND ONE ORDINARY CLASS, to assert the other half of the mechanism: for a class that is NOT a cluster,
 * nothing is substituted at all. */
@interface ProbePlain : NSObject
@end


/* §63.191: the no-copy deallocator's accounting, so the probe can assert it ran exactly once, with the
 * caller's own pointer and length. */
static int fn_probe_deallocator_calls = 0;
static void *fn_probe_deallocator_bytes = NULL;
static size_t fn_probe_deallocator_length = 0;

@implementation ProbeCluster

/* THE DOOR IS `+alloc`, because THIS LIBRARY HAS NO `+allocWithZone:` — NSObject.h says so and says why
 * ("THE SINGLETON DOOR IS +alloc", the zone-taking methods were removed) — and §C.3 item 1 wants
 * `[[Front alloc] init]` to answer an EMPTY instance rather than a crash or a class of its own.
 *
 * THE `self != [ProbeCluster class]` TEST IS NOT A SMUGGLE: a concrete class INHERITS this method, and
 * `[super alloc]` in a class method starts the lookup at ProbeCluster's superclass with the receiver
 * still being the class that was asked. So the routing happens exactly once, at the front, and a
 * concrete class asking for an instance gets one. */
+ (id)alloc
{
	if (self != [ProbeCluster class]) {
		return [super alloc];
	}
	return [[ProbeClusterEmpty class] alloc];
}

- (NSUInteger)probeCount
{
	return 0;
}

+ (instancetype)clusterWithCount:(NSUInteger)count
{
	return [[ProbeClusterHolding alloc] initWithCount:count];
}

/* THE FRONT ANSWERS ITSELF TO AN ARCHIVER (plan §C.3 item 4). The concrete classes inherit this, which
 * is the whole reason the archive keeps the public name. */
- (Class)classForCoder
{
	return [ProbeCluster class];
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeInteger:0 forKey:@"count"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		(void)[coder decodeIntegerForKey:@"count"];
	}
	return self;
}
@end

@implementation ProbeClusterEmpty
@end

@implementation ProbeClusterHolding

- (instancetype)initWithCount:(NSUInteger)count
{
	self = [super init];
	if (self != nil) {
		_count = count;
	}
	return self;
}

- (NSUInteger)probeCount
{
	return _count;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeInteger:(NSInteger)_count forKey:@"count"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		_count = (NSUInteger)[coder decodeIntegerForKey:@"count"];
	}
	return self;
}
@end

@implementation ProbePlain
@end

/* THE THIRD-PARTY CASE, AND IT IS THE ONE THAT MAKES THE HEADER'S CLAIM TRUE RATHER THAN ASPIRATIONAL: a
 * class that inherits NSArray and overrides ONLY the two primitives that header names. If any method in
 * the family still reached into the front's own storage, THIS class would be reading whatever it keeps in
 * those ivars - and every check below would fail. Its elements are LITERALS, so the class needs no dealloc
 * and no retain in either memory mode: it is written the way a caller outside this library would. */
@interface ProbePrimitiveArray : NSArray
{
	NSString *_mine[3];
}
@end

@implementation ProbePrimitiveArray

- (id)init
{
	self = [super init];
	if (self != nil) {
		_mine[0] = @"p";
		_mine[1] = @"q";
		_mine[2] = @"r";
	}
	return self;
}

/* THE TWO PRIMITIVES, AND NOTHING ELSE - there is deliberately no -dealloc, no -countByEnumerating… and no
 * -isEqualToArray: in this class. */
- (NSUInteger)count
{
	return 3;
}

- (id)objectAtIndex:(NSUInteger)index
{
	if (index >= 3) {
		[NSException raise:NSRangeException
		            format:@"ProbePrimitiveArray: index %lu beyond bounds", (unsigned long)index];
	}
	return _mine[index];
}

@end

/* THE THIRD-PARTY CASE FOR THE DICTIONARY FAMILY, and it makes the same demand the array family's does: a
 * class that inherits NSDictionary and overrides ONLY the three primitives the header names. If any derived
 * door still read the bucket chains, THIS class would be reading whatever it keeps in those ivars. There is
 * deliberately no -dealloc and no storage: the two keys are literals and the enumerator is a fresh array. */
@interface ProbePrimitiveDictionary : NSDictionary
@end

@implementation ProbePrimitiveDictionary

- (NSUInteger)count
{
	return 2;
}

- (id)objectForKey:(id)key
{
	if ([key isEqual:@"a"]) {
		return @"1";
	}
	if ([key isEqual:@"b"]) {
		return @"2";
	}
	return nil;
}

- (NSEnumerator *)keyEnumerator
{
	return [[NSArray arrayWithObjects:@"a", @"b", nil] objectEnumerator];
}

@end

/* THE THIRD-PARTY CASE FOR THE SET FAMILY: the same demand the array and dictionary families make. It
 * overrides ONLY the three primitives the header names - -count, -member: and -objectEnumerator - with no
 * member array, no snapshot and no -dealloc. */
@interface ProbePrimitiveSet : NSSet
@end

@implementation ProbePrimitiveSet

- (NSUInteger)count
{
	return 2;
}

- (nullable id)member:(id)object
{
	if ([object isEqual:@"p"]) {
		return @"p";
	}
	if ([object isEqual:@"q"]) {
		return @"q";
	}
	return nil;
}

- (NSEnumerator *)objectEnumerator
{
	return [[NSArray arrayWithObjects:@"p", @"q", nil] objectEnumerator];
}

@end

/* THE THIRD-PARTY CASE FOR THE NUMBER FAMILY: a class that overrides ONLY the four primitives the header
 * names - -agKind and the three payload accessors - holding one double of its own. If any derived read
 * still reached for a union member of the front, THIS class could not answer it. */
@interface ProbePrimitiveNumber : NSNumber
{
	double _held;
}
@end

@implementation ProbePrimitiveNumber

- (id)init
{
	self = [super init];
	if (self != nil) {
		_held = 2.5;
	}
	return self;
}

- (unsigned char)agKind
{
	return 'd';
}

- (long long)longLongValue
{
	return 2;
}

- (unsigned long long)unsignedLongLongValue
{
	return 2;
}

- (double)doubleValue
{
	return _held;
}

@end

/* THE THIRD-PARTY CASE FOR THE STRING FAMILY: a class that overrides ONLY the three primitives the header
 * documents - -length, -characterAtIndex: and -UTF8String - over a fixed literal, with NO storage of its
 * own. The byte-level readers the front uses default to a read of -UTF8String, so if any derived door
 * reached for storage instead, THIS class could not answer it. */
@interface ProbePrimitiveString : NSString
@end

@implementation ProbePrimitiveString

- (size_t)length
{
	return 5;			/* "hello", in UTF-16 code units */
}

- (unsigned short)characterAtIndex:(size_t)index
{
	return (unsigned short)"hello"[index];
}

- (const char *)UTF8String
{
	return "hello";
}

@end

/* THE THIRD-PARTY CASE FOR THE ORDERED-SET FAMILY: a class over ONLY the primitives the header names -
 * -count, -objectAtIndex: and -objectEnumerator - with no member array of its own. */
@interface ProbePrimitiveOrderedSet : NSOrderedSet
@end

@implementation ProbePrimitiveOrderedSet

- (NSUInteger)count
{
	return 3;
}

- (nullable id)objectAtIndex:(NSUInteger)index
{
	if (index >= 3) {
		[NSException raise:NSRangeException format:@"ProbePrimitiveOrderedSet: %lu", (unsigned long)index];
	}
	return [NSArray arrayWithObjects:@"x", @"y", @"z", nil][index];
}

- (NSEnumerator *)objectEnumerator
{
	return [[NSArray arrayWithObjects:@"x", @"y", @"z", nil] objectEnumerator];
}

@end

/* THE THIRD-PARTY CASE FOR NSDATA: a class over ONLY the two primitives the header names - -length and
 * -bytes - with no storage of its own. If any derived read still reached for the ivar, THIS class could not
 * answer it. */
@interface ProbePrimitiveData : NSData
@end

@implementation ProbePrimitiveData

- (size_t)length
{
	return 4;
}

- (const void *)bytes
{
	return "wxyz";
}

@end

/* THE THIRD-PARTY CASE FOR NSIndexSet, SCOPED TO THE DOORS THAT ARE MOVED: -count, -firstIndex and
 * -indexGreaterThanIndex: are the primitives, and the four ITERATOR-shaped reads are now written over them.
 * The RANGE-shaped ones (-hash, -getIndexes:…, -countOfIndexesInRange:, -isEqualToIndexSet:,
 * -containsIndexesInRange:, -mutableCopy, -description) are NOT yet, so neither is their check - see the
 * header for why that is a decision about cost rather than an oversight. */
@interface ProbePrimitiveIndexSet : NSIndexSet
@end

@implementation ProbePrimitiveIndexSet

- (NSUInteger)count
{
	return 5;			/* the indexes 1, 2, 3, 7 and 9 */
}

- (NSUInteger)firstIndex
{
	return 1;
}

/* THE RANGE-LEVEL PRIMITIVE, and this is the family's answer to a real problem: the range-shaped doors cannot
 * be derived over an INDEX iterator without turning O(ranges) into O(indexes). Apple's own
 * -enumerateRangesUsingBlock: is the door a third party can implement instead. */
- (void)enumerateRangesUsingBlock:(void (^)(NSRange range, BOOL *stop))block
{
	BOOL stop = NO;

	/*
	 * THE stop PARAMETER IS PART OF THE CONTRACT, and the first version of this probe passed NULL for it -
	 * which crashed the guest, because every derived door dereferences it (`*stop = YES`). The library was
	 * right and the probe was wrong; this is what the signature means.
	 */
	block(NSMakeRange(1, 3), &stop);
	if (!stop) {
		block(NSMakeRange(7, 1), &stop);
	}
	if (!stop) {
		block(NSMakeRange(9, 1), &stop);
	}
}

- (NSUInteger)indexGreaterThanIndex:(NSUInteger)value
{
	if (value < 1) {
		return 1;
	}
	if (value < 2) {
		return 2;
	}
	if (value < 3) {
		return 3;
	}
	if (value < 7) {
		return 7;
	}
	if (value < 9) {
		return 9;
	}
	return NSNotFound;
}

@end

/* THE THIRD-PARTY CASE FOR NSHASHTABLE: a class over ONLY the three primitives the header names -
 * -count, -member: and -objectEnumerator - with no table of its own. */
@interface ProbePrimitiveHashTable : NSHashTable
@end

@implementation ProbePrimitiveHashTable

- (NSUInteger)count
{
	return 2;
}

- (nullable id)member:(nullable id)object
{
	if ([object isEqual:@"m"]) {
		return @"m";
	}
	if ([object isEqual:@"n"]) {
		return @"n";
	}
	return nil;
}

- (NSEnumerator *)objectEnumerator
{
	return [[NSArray arrayWithObjects:@"m", @"n", nil] objectEnumerator];
}

@end

/* THE THIRD-PARTY CASE FOR NSPOINTERARRAY: a class over ONLY the two primitives the header names -
 * -count and -pointerAtIndex: - with no slot array of its own. Its pointers are real OBJECTS, because a fast
 * enumeration readss the slots as objects. */
@interface ProbePrimitivePointerArray : NSPointerArray
@end

@implementation ProbePrimitivePointerArray

- (NSUInteger)count
{
	return 2;
}

- (nullable void *)pointerAtIndex:(NSUInteger)index
{
	if (index >= 2) {
		[NSException raise:NSRangeException format:@"ProbePrimitivePointerArray: %lu", (unsigned long)index];
	}
	return (void *)(index == 0 ? (void *)@"p" : (void *)@"q");
}

@end

/* THE THIRD-PARTY CASE FOR NSATTRIBUTEDSTRING: a class over ONLY the two primitives the header names -
 * -string and -attributesAtIndex:effectiveRange: - with no run store of its own. */
@interface ProbePrimitiveAttributedString : NSAttributedString
@end

@implementation ProbePrimitiveAttributedString

- (NSString *)string
{
	return @"hello";
}

- (NSDictionary *)attributesAtIndex:(NSUInteger)location effectiveRange:(NSRangePointer)range
{
	if (location >= 5) {
		[NSException raise:NSRangeException format:@"ProbePrimitiveAttributedString: %lu",
				   (unsigned long)location];
	}
	if (range != NULL) {
		*range = NSMakeRange(0, 5);
	}
	return [NSDictionary dictionaryWithObject:@"v" forKey:@"k"];
}

@end

/* THE THIRD-PARTY CASE FOR NSMAPTABLE: a class over ONLY the three primitives the header names - -count,
 * -objectForKey: and -keyEnumerator - with no table of its own. */
@interface ProbePrimitiveMapTable : NSMapTable
@end

@implementation ProbePrimitiveMapTable

- (NSUInteger)count
{
	return 2;
}

- (nullable id)objectForKey:(id)key
{
	if ([key isEqual:@"k1"]) {
		return @"v1";
	}
	if ([key isEqual:@"k2"]) {
		return @"v2";
	}
	return nil;
}

- (NSEnumerator *)keyEnumerator
{
	return [[NSArray arrayWithObjects:@"k1", @"k2", nil] objectEnumerator];
}

@end

/* THE THIRD-PARTY CASE FOR NSCHARACTERSET: a class over ONLY the two primitives the header names -
 * -characterIsMember: and -bitmapRepresentation - holding the ASCII digits, with no range array. */
@interface ProbePrimitiveCharacterSet : NSCharacterSet
@end

@implementation ProbePrimitiveCharacterSet

- (BOOL)characterIsMember:(unichar)character
{
	return character >= '0' && character <= '9';
}

- (NSData *)bitmapRepresentation
{
	/* The same layout the family uses: an 8-byte header, then one bit per member, plane 0 only. The header is
	 * this class's own business - the doors that read a bitmap start after it. NSMutableData rather than a C
	 * buffer: it zero-fills, which is what this layout wants, and needs no header of its own. */
	NSMutableData *bitmap = [NSMutableData dataWithLength:8 + 8192];
	unsigned char *bytes = [bitmap mutableBytes];
	unsigned long c;

	if (bytes == NULL) {
		return [NSData data];
	}
	for (c = '0'; c <= '9'; c++) {
		bytes[8 + (c / 8)] |= (unsigned char)(1u << (c % 8));
	}
	return bitmap;
}

@end

/* THE THIRD-PARTY CASE FOR NSVALUE: a class over ONLY the three primitives the header names - -fnBytes,
 * -fnSize and -objCType - holding an int it never copies anywhere. */
static int ag_probe_value_payload = 4242;

@interface ProbePrimitiveValue : NSValue
@end

@implementation ProbePrimitiveValue

- (const void *)fnBytes
{
	return &ag_probe_value_payload;
}

- (NSUInteger)fnSize
{
	return sizeof(int);
}

- (const char *)objCType
{
	return @encode(int);
}

@end

int main(void)
{
	ProbeCluster *empty;
	ProbeCluster *holding;
	ProbePlain *plain = [[ProbePlain alloc] init];
	NSData *archive;
	const void *bytes;
	size_t length;
	id restored;

	printf("FOUNDATION-CLUSTERS begin\n");

	/* §C.3 item 1: the front is allocatable and `-init` on it answers an EMPTY instance. */
	empty = [[ProbeCluster alloc] init];
	check("the-front-is-allocatable-and-inits-empty",
	      empty != nil && [empty probeCount] == 0,
	      "[[Front alloc] init] must answer an empty instance, not nil and not a crash");

	/* §C.3 item 3: `-class` answers the CONCRETE class. */
	check("class-answers-the-concrete-class",
	      [empty class] != [ProbeCluster class],
	      "the instance's class is the private concrete class, not the front");

	/* §C.3 item 6: membership in the front is still YES, and `+class` answers the front. */
	check("an-instance-is-still-kind-of-the-front",
	      [empty isKindOfClass:[ProbeCluster class]],
	      "-isKindOfClass: against the public class must be YES for an instance of the cluster");
	check("plus-class-answers-the-front",
	      [ProbeCluster class] == [ProbeCluster self],
	      "+class on the front answers the front");

	/* §C.3 item 2: a constructor chooses a DIFFERENT concrete class by the data. */
	holding = [ProbeCluster clusterWithCount:3];
	check("a-constructor-chooses-the-concrete-class-by-the-data",
	      [holding class] != [empty class] && [holding probeCount] == 3,
	      "the non-empty constructor must answer a different concrete class, with the data in it");

	/* §C.3 item 4: the archiver's two doors answer the FRONT. */
	check("class-for-coder-answers-the-front",
	      [holding classForCoder] == [ProbeCluster class],
	      "[obj classForCoder] must be the public class even though -class is the concrete one");
	check("class-for-archiver-defaults-to-class-for-coder",
	      [holding classForArchiver] == [ProbeCluster class],
	      "-classForArchiver's default is -classForCoder, and both must answer the public class here");

	/* AND THE OTHER HALF: a class that is NOT a cluster substitutes nothing. */
	check("an-ordinary-class-substitutes-nothing",
	      [plain classForCoder] == [ProbePlain class] &&
	      [plain classForArchiver] == [ProbePlain class],
	      "the defaults answer [self class] for a class that is not a cluster");

	/* THE HINGE, MEASURED ON THE ARCHIVE'S OWN BYTES. §C.4 says NSKeyedArchiver recording
	 * `[object class]` was the one place a cluster would have been expensive; this is the pair that
	 * proves the fix: the public name is in the archive, and NO private name is. */
	archive = [NSKeyedArchiver archivedDataWithRootObject:empty];
	bytes = [archive bytes];
	length = [archive length];
	check("the-archive-is-not-empty",
	      archive != nil && length > 0,
	      "the archiver must produce data at all for this check to mean anything");
	check("the-archive-names-the-public-class",
	      contains(bytes, length, "ProbeCluster"),
	      "the public front's name must appear in the archive");
	check("the-archive-names-no-private-class",
	      !contains(bytes, length, "ProbeClusterEmpty") &&
	      !contains(bytes, length, "ProbeClusterHolding"),
	      "a private concrete class's name must NEVER reach the archive - this is what the hinge buys");

	/* §C.3 item 4's consequence: the archive is loadable BECAUSE it named the front. */
	restored = [NSKeyedUnarchiver unarchiveObjectWithData:archive];
	check("the-archive-round-trips-through-the-front",
	      restored != nil && [restored isKindOfClass:[ProbeCluster class]] &&
	      [(ProbeCluster *)restored probeCount] == 0,
	      "unarchiving must give an instance of the cluster whose data survived");

	/* ===============================================================================================
	 * M1: THE SAME CONTRACT, ON THE SHIPPED FAMILY. Everything above proves the MECHANISM on a cluster
	 * this probe defines itself; this section asserts the same rules on NSArray/NSMutableArray, whose
	 * concrete classes are the library's own, and then on a class written over the primitives ALONE.
	 * =============================================================================================== */
	{
		NSArray *e = [NSArray array];
		NSArray *one = [NSArray arrayWithObject:@"one"];
		NSArray *small = [NSArray arrayWithObjects:@"a", @"b", @"c", nil];
		NSMutableArray *mutable = [NSMutableArray array];
		NSArray *general;
		NSArray *fromInit;
		NSArray *slice;
		NSMutableArray *mutableCopyOfOne;
		ProbePrimitiveArray *handmade;
		NSString *expectedItems[3];
		NSString *seen[4];
		NSUInteger seenCount = 0;
		NSUInteger i;
		NSNumber *want20[20];
		id enumerated;

		for (i = 0; i < 20; i++) {
			want20[i] = [NSNumber numberWithInteger:(NSInteger)i];
		}
		general = [NSArray arrayWithObjects:want20 count:20];

		check("nsarray-class-answers-a-concrete-class",
		      [e class] != [NSArray class] && [[e class] isSubclassOfClass:[NSArray class]],
		      "-class must be a private concrete SUBCLASS of NSArray, never NSArray itself");
		check("nsarray-four-cases-are-distinct-classes",
		      [e class] != [one class] && [e class] != [small class] &&
		      [e class] != [general class] && [one class] != [small class] &&
		      [one class] != [general class] && [small class] != [general class],
		      "the empty, one-element, small and general cases must be FOUR DIFFERENT classes");

		fromInit = [[NSArray alloc] init];
		check("nsarray-alloc-init-is-the-empty-singleton",
		      fromInit != nil && [fromInit count] == 0 && fromInit == e,
		      "[[NSArray alloc] init] is legal, answers an EMPTY instance, and answers the SHARED one");

		check("nsarray-mutable-construction-answers-a-mutable-class",
		      [mutable class] != [NSMutableArray class] &&
		      [[mutable class] isSubclassOfClass:[NSMutableArray class]] &&
		      [mutable isKindOfClass:[NSArray class]],
		      "a mutable constructor answers a private SUBCLASS of NSMutableArray");

		check("nsarray-class-for-coder-answers-the-front",
		      [e classForCoder] == [NSArray class] && [one classForCoder] == [NSArray class] &&
		      [small classForCoder] == [NSArray class] && [general classForCoder] == [NSArray class] &&
		      [mutable classForCoder] == [NSMutableArray class] &&
		      [e classForArchiver] == [NSArray class],
		      "every instance must name the PUBLIC class to an archiver, whatever -class answers");

		mutableCopyOfOne = [one mutableCopy];
		check("nsarray-copy-is-the-receiver-and-mutable-copy-is-mutable",
		      [one copy] == one && mutableCopyOfOne != nil &&
		      [mutableCopyOfOne class] != [NSMutableArray class] &&
		      [[mutableCopyOfOne class] isSubclassOfClass:[NSMutableArray class]] &&
		      [mutableCopyOfOne isEqualToArray:one],
		      "-copy answers the receiver (immutable) and -mutableCopy a MUTABLE concrete class");

		/* §C.3 item 5, AND THIS PAIR IS THE WHOLE REASON THE HEADER DOCUMENTS THE PRIMITIVES: a class that
		 * overrides ONLY -count and -objectAtIndex: must be correct through every derived door. */
		handmade = [[ProbePrimitiveArray alloc] init];
		expectedItems[0] = @"p";
		expectedItems[1] = @"q";
		expectedItems[2] = @"r";
		{
			NSArray *expected = [NSArray arrayWithObjects:expectedItems count:3];

			check("nsarray-primitives-drive-equality-and-hash",
			      [handmade isEqualToArray:expected] && [handmade hash] == [expected hash] &&
			      [[handmade description] isEqual:[expected description]] &&
			      [handmade count] == 3 && [handmade firstObject] == expectedItems[0] &&
			      [handmade lastObject] == expectedItems[2],
			      "equality, hash and description must be written over the primitives, not over storage");

			slice = [handmade subarrayWithRange:NSMakeRange(1, 2)];
			check("nsarray-primitives-drive-slicing-and-search",
			      slice != nil && [slice count] == 2 &&
			      [handmade indexOfObject:expectedItems[2]] == 2 &&
			      [handmade indexOfObjectIdenticalTo:expectedItems[1]] == 1 &&
			      [handmade containsObject:expectedItems[0]],
			      "slicing and the searches must work on a class with NO storage of its own");
		}
		for (enumerated in handmade) {
			if (seenCount < 4) {
				seen[seenCount] = enumerated;
			}
			seenCount++;
		}
		check("nsarray-primitives-drive-fast-enumeration",
		      seenCount == 3 && seen[0] == expectedItems[0] &&
		      seen[1] == expectedItems[1] && seen[2] == expectedItems[2],
		      "fast enumeration must walk the primitives - the buffer is the CALLER's, not the storage");

		/* §C.4's hinge, ON THE SHIPPED FAMILY: the archive carries the public name and no private one. */
		{
			NSData *familyArchive = [NSKeyedArchiver archivedDataWithRootObject:small];
			const void *familyBytes = [familyArchive bytes];
			size_t familyLength = [familyArchive length];

			check("nsarray-archive-names-the-public-class",
			      familyArchive != nil && familyLength > 0 &&
			      contains(familyBytes, familyLength, "NSArray"),
			      "a real array's archive must name the public class");
			check("nsarray-archive-names-no-private-class",
			      !contains(familyBytes, familyLength, "AGArray"),
			      "no private concrete name may reach an archive - what -classForCoder buys");
		}
	}

	/* ===============================================================================================
	 * M2: THE SAME CONTRACT ON THE SHIPPED DICTIONARY FAMILY. M1's section above proves it on NSArray;
	 * this one asserts what the dictionary cluster owes: a concrete class, the SHARED empty instance,
	 * the copy-of-nothing case, the mutable class, and an archiver that only ever sees the front.
	 * =============================================================================================== */
	{
		NSDictionary *emptyDict = [NSDictionary dictionary];
		NSDictionary *oneDict = [NSDictionary dictionaryWithObject:@"v" forKey:@"k"];
		NSDictionary *copyOfNothing = [[NSDictionary alloc] initWithDictionary:[NSDictionary dictionary]];
		NSDictionary *zeroCountA, *zeroCountB;
		NSMutableDictionary *mutableDict = [NSMutableDictionary dictionary];
		NSDictionary *fromInit;
		NSData *dictArchive;
		const void *dictBytes;
		size_t dictLength;

		check("nsdictionary-class-answers-a-concrete-class",
		      [emptyDict class] != [NSDictionary class] &&
		      [[emptyDict class] isSubclassOfClass:[NSDictionary class]],
		      "-class must be a private concrete SUBCLASS of NSDictionary, never NSDictionary itself");
		fromInit = [[NSDictionary alloc] init];
		zeroCountA = [[NSDictionary alloc] initWithObjects:NULL forKeys:NULL count:0];
		zeroCountB = [[NSDictionary alloc] initWithObjects:NULL forKeys:NULL count:0];
		check("nsdictionary-empty-instance-and-the-zero-count-singleton",
		      fromInit != nil && [fromInit count] == 0 && [fromInit class] != [NSDictionary class] &&
		      zeroCountA != nil && zeroCountA == zeroCountB && [zeroCountA count] == 0 &&
		      zeroCountA != fromInit,
		      "[[NSDictionary alloc] init] is EMPTY as a plain instance - this family has "
		      "allocate-then-fill constructors - while the ZERO-COUNT construction answers ONE shared one");
		check("nsdictionary-a-copy-of-nothing-is-the-empty-singleton",
		      copyOfNothing == zeroCountA && [oneDict class] != [zeroCountA class],
		      "copying nothing answers the shared empty instance, and a one-pair dictionary a different class");
		check("nsdictionary-mutable-construction-answers-a-mutable-class",
		      [mutableDict class] != [NSMutableDictionary class] &&
		      [[mutableDict class] isSubclassOfClass:[NSMutableDictionary class]] &&
		      [mutableDict isKindOfClass:[NSDictionary class]],
		      "a mutable constructor answers a private SUBCLASS of NSMutableDictionary");
		check("nsdictionary-class-for-coder-answers-the-front",
		      [emptyDict classForCoder] == [NSDictionary class] &&
		      [oneDict classForCoder] == [NSDictionary class] &&
		      [mutableDict classForCoder] == [NSMutableDictionary class] &&
		      [emptyDict classForArchiver] == [NSDictionary class],
		      "every instance names the PUBLIC class to an archiver, whatever -class answers");
		check("nsdictionary-empty-answers-every-read",
		      [emptyDict count] == 0 && [emptyDict objectForKey:@"k"] == nil &&
		      [[emptyDict allKeys] count] == 0 && [[emptyDict allValues] count] == 0 &&
		      [[emptyDict keyEnumerator] nextObject] == nil &&
		      [emptyDict isEqualToDictionary:[NSDictionary dictionary]] &&
		      [[emptyDict description] length] > 0 &&
		      [emptyDict objectForKeyedSubscript:@"k"] == nil,
		      "the empty concrete class answers the primitives AND every derived read over them");

		/* §C.4's hinge, on the dictionary: the archive carries the public name and no private one. */
		dictArchive = [NSKeyedArchiver archivedDataWithRootObject:oneDict];
		dictBytes = [dictArchive bytes];
		dictLength = [dictArchive length];
		check("nsdictionary-archive-names-the-public-class",
		      dictArchive != nil && dictLength > 0 &&
		      contains(dictBytes, dictLength, "NSDictionary"),
		      "a real dictionary's archive must name the public class");
		check("nsdictionary-archive-names-no-private-class",
		      !contains(dictBytes, dictLength, "AGDictionary"),
		      "no private concrete name may reach an archive - what -classForCoder buys");
		{
			/* THE PRIMITIVES ARE THE CONTRACT (§C.3 item 5), proven the way the array family's is: a class
			 * that overrides ONLY -count, -objectForKey: and -keyEnumerator must be correct through every
			 * door. Its locals are declared HERE, so nothing below depends on the block above. */
			ProbePrimitiveDictionary *handmadeDict = [[ProbePrimitiveDictionary alloc] init];
			NSDictionary *expectedDict =
				[NSDictionary dictionaryWithObjects:[NSArray arrayWithObjects:@"1", @"2", nil]
							    forKeys:[NSArray arrayWithObjects:@"a", @"b", nil]];
			NSUInteger thirdPartySeen = 0;
			id thirdPartyKey;

			check("nsdictionary-primitives-drive-keys-values-and-equality",
			      [[handmadeDict allKeys] count] == 2 && [[handmadeDict allValues] count] == 2 &&
			      [[handmadeDict allKeysForObject:@"1"] count] == 1 &&
			      [handmadeDict isEqualToDictionary:expectedDict] &&
			      [expectedDict isEqualToDictionary:handmadeDict] &&
			      [handmadeDict hash] == [expectedDict hash] &&
			      [[handmadeDict description] length] > 0 &&
			      [handmadeDict objectForKeyedSubscript:@"a"] != nil,
			      "keys, values, BOTH equality directions, hash and description must be written over the "
			      "primitives, on a class that has neither buckets nor a snapshot");

			{
				id __unsafe_unretained seenObjects[2];
				id __unsafe_unretained seenKeys[2];
				NSUInteger seenIndex;
				BOOL paired = YES;

				[handmadeDict getObjects:seenObjects andKeys:seenKeys];
				for (seenIndex = 0; seenIndex < 2; seenIndex++) {
					id seenValue = [handmadeDict objectForKey:seenKeys[seenIndex]];

					/* The value is read FIRST, so a nil one is never handed to a nonnull argument. */
					if (seenValue == nil || ![seenObjects[seenIndex] isEqual:seenValue]) {
						paired = NO;
					}
				}
				check("nsdictionary-primitives-drive-getobjects-andkeys", paired,
				      "-getObjects:andKeys: must keep each key paired with its own value");
			}
			for (thirdPartyKey in handmadeDict) {
				thirdPartySeen++;
			}
			check("nsdictionary-primitives-drive-fast-enumeration", thirdPartySeen == 2,
			      "fast enumeration must walk the primitives through the caller's buffer");
		}

	}

	{
		/*
		 * M3: THE SET LADDER - NSSet, NSMutableSet and NSCountedSet each answer a private concrete class, and
		 * the empty case is ONE shared instance. Every local is declared HERE, so nothing depends on a block
		 * above. NOTE WHAT IS NOT ASSERTED: nothing archives a set. NSKeyedArchiver has no set path at all
		 * (its source names NSSet nowhere), so archiving one RAISES where an array or a dictionary encodes;
		 * -classForCoder below is asserted because that is the contract the archiver will need when it gains
		 * one, and the gap itself is recorded in docs/TODO-before-release.md rather than asserted here.
		 */
		NSSet *emptySet = [NSSet set];
		NSSet *fromInit = [[NSSet alloc] init];
		NSSet *oneSet = [NSSet setWithObject:@"one"];
		NSMutableSet *mutableSet = [NSMutableSet set];
		NSCountedSet *countedSet = [NSCountedSet set];

		check("nsset-class-answers-a-concrete-class",
		      [emptySet class] != [NSSet class] && [[emptySet class] isSubclassOfClass:[NSSet class]] &&
		      [oneSet class] != [emptySet class],
		      "-class must be a private concrete SUBCLASS of NSSet, and the empty case its own");
		check("nsset-alloc-init-is-the-empty-singleton",
		      fromInit != nil && [fromInit count] == 0 && fromInit == emptySet,
		      "[[NSSet alloc] init] is legal and answers the SHARED empty instance");
		check("nsset-mutable-and-counted-answer-their-own-concrete-classes",
		      [mutableSet class] != [NSMutableSet class] &&
		      [[mutableSet class] isSubclassOfClass:[NSMutableSet class]] &&
		      [countedSet class] != [NSCountedSet class] &&
		      [[countedSet class] isSubclassOfClass:[NSCountedSet class]] &&
		      [countedSet isKindOfClass:[NSMutableSet class]],
		      "each rung of the ladder answers its own concrete class");
		check("nsset-class-for-coder-answers-the-front",
		      [emptySet classForCoder] == [NSSet class] && [oneSet classForCoder] == [NSSet class] &&
		      [mutableSet classForCoder] == [NSMutableSet class] &&
		      [countedSet classForCoder] == [NSCountedSet class] &&
		      [emptySet classForArchiver] == [NSSet class],
		      "every instance names the PUBLIC class to an archiver, whatever -class answers");
		check("nsset-empty-answers-every-read",
		      [emptySet count] == 0 && [emptySet member:@"x"] == nil &&
		      ! [emptySet containsObject:@"x"] && [emptySet anyObject] == nil &&
		      [[emptySet allObjects] count] == 0 &&
		      [[emptySet objectEnumerator] nextObject] == nil &&
		      [emptySet isEqualToSet:[NSSet set]] && [emptySet isSubsetOfSet:oneSet] &&
		      ! [emptySet intersectsSet:oneSet] && [[emptySet description] length] > 0,
		      "the empty concrete class answers the primitives AND every derived read over them");
		check("nsset-copy-is-the-receiver-and-mutable-copy-is-mutable",
		      [oneSet copy] == oneSet &&
		      [[oneSet mutableCopy] class] != [NSMutableSet class] &&
		      [[[oneSet mutableCopy] class] isSubclassOfClass:[NSMutableSet class]] &&
		      [[oneSet mutableCopy] isEqualToSet:[NSSet setWithObject:@"one"]],
		      "-copy answers the receiver (immutable) and -mutableCopy a MUTABLE concrete class");
	covers("NSSet", "intersectsSet:");
	covers("NSSet", "isEqualToSet:");
	covers("NSSet", "isSubsetOfSet:");
	}
	{
		/* THE PRIMITIVES ARE THE CONTRACT (§C.3 item 5): a class over the three primitives alone must be
		 * correct through every derived door. */
		ProbePrimitiveSet *handmadeSet = [[ProbePrimitiveSet alloc] init];
		NSSet *expectedSet = [NSSet setWithArray:[NSArray arrayWithObjects:@"p", @"q", nil]];
		NSUInteger thirdPartySeen = 0;
		id thirdPartyObject;

		/* SPLIT INTO THREE, because a check that bundles ten assertions cannot say WHICH one is false -
		 * the per-step named-check rule this project's probes follow. */
		check("nsset-primitives-drive-objects-and-count",
		      [[handmadeSet allObjects] count] == 2 && [handmadeSet count] == 2 &&
		      [handmadeSet member:@"p"] != nil && [handmadeSet containsObject:@"q"] &&
		      [handmadeSet anyObject] != nil,
		      "allObjects, count, member:, containsObject: and anyObject must be over the primitives");
	covers("NSSet", "setWithObject:");
		check("nsset-primitives-drive-equality-subset-and-intersection",
		      [handmadeSet isEqualToSet:expectedSet] && [expectedSet isEqualToSet:handmadeSet] &&
		      [handmadeSet isSubsetOfSet:expectedSet] && [handmadeSet intersectsSet:expectedSet],
		      "equality in BOTH directions, subset and intersection must be over the primitives");
		check("nsset-primitives-drive-hash-and-description",
		      [handmadeSet hash] == [expectedSet hash] && [[handmadeSet description] length] > 0,
		      "hash and description must be over the primitives");
		for (thirdPartyObject in handmadeSet) {
			thirdPartySeen++;
		}
		check("nsset-primitives-drive-fast-enumeration", thirdPartySeen == 2,
		      "fast enumeration must walk the primitives through the caller's buffer");
		check("nsset-primitives-drive-copy-and-mutable-copy",
		      [[handmadeSet mutableCopy] isEqualToSet:expectedSet] &&
		      [[handmadeSet mutableCopy] isKindOfClass:[NSMutableSet class]] &&
		      [[handmadeSet copy] isEqualToSet:expectedSet],
		      "-mutableCopy must carry a THIRD-PARTY class's members; it built from the internal array, "
		      "which that class does not have, so it silently produced an empty set");
	}

	{
		/*
		 * M4: THE NUMBER FAMILY — a concrete class per payload width, chosen BY THE CONSTRUCTOR, and the
		 * front answering the archiver with the PUBLIC class. Locals declared HERE (nothing above is used).
		 */
		NSNumber *one = [NSNumber numberWithInt:1];
		NSNumber *half = [NSNumber numberWithDouble:1.5];
		NSNumber *yes = [NSNumber numberWithBool:YES];
		NSDecimalNumber *decimal = [NSDecimalNumber decimalNumberWithString:@"1.5"];

		check("nsnumber-class-answers-a-concrete-class",
		      [one class] != [NSNumber class] && [[one class] isSubclassOfClass:[NSNumber class]] &&
		      [one class] != [half class] && [half class] != [yes class],
		      "-class must be a private concrete SUBCLASS of NSNumber, and the WIDTHS must be different classes");
		check("nsnumber-class-for-coder-answers-the-front-but-not-for-a-public-subclass",
		      [one classForCoder] == [NSNumber class] && [half classForCoder] == [NSNumber class] &&
		      [yes classForCoder] == [NSNumber class] &&
		      [decimal classForCoder] == [NSDecimalNumber class],
		      "the private classes must name the PUBLIC class, while NSDecimalNumber — a public subclass — "
		      "must still name ITSELF: an unconditional override rewrote its name in every archive");
		check("nsnumber-the-matrix-round-trips",
		      [one intValue] == 1 && [half doubleValue] == 1.5 && [yes boolValue] == YES &&
		      [[NSNumber numberWithLongLong:-7] longLongValue] == -7 &&
		      [[NSNumber numberWithUnsignedLongLong:18446744073709551615ULL] unsignedLongLongValue]
			== 18446744073709551615ULL &&
		      ([one objCType][0] == 'i') && ([half objCType][0] == 'd') &&
		      strcmp([one objCType], "i") == 0,
		      "each creation type must round-trip through its accessor and report itself from -objCType");
	}

	{
		/* THE PRIMITIVES ARE THE CONTRACT (§C.3 item 5): four methods, and every read above them. */
		ProbePrimitiveNumber *handmadeNumber = [[ProbePrimitiveNumber alloc] init];
		NSNumber *expectedNumber = [NSNumber numberWithDouble:2.5];

		check("nsnumber-primitives-drive-conversions-equality-and-hash",
		      [handmadeNumber doubleValue] == 2.5 && [handmadeNumber intValue] == 2 &&
		      [handmadeNumber longLongValue] == 2 && [handmadeNumber boolValue] == YES &&
		      ([handmadeNumber objCType][0] == 'd') &&
		      [[handmadeNumber stringValue] length] > 0 &&
		      [[handmadeNumber description] length] > 0 &&
		      [handmadeNumber isEqualToNumber:expectedNumber] &&
		      [handmadeNumber hash] == [expectedNumber hash] ,
				      "the fifteen conversions, -objCType, -description, -isEqualToNumber: and -hash must all be "
		      "written over the four primitives, on a class that has none of the front's fields");
	}

	{
		/*
		 * M5: THE STRING FAMILY — the front has no storage, the COMPILER creates the literals and the
		 * factories bind to NSOwnedString, so this section asserts the two doors rather than a cluster of
		 * private classes. Locals declared HERE.
		 */
		NSString *literal = @"literal";
		NSString *empty = [[NSString alloc] init];
		NSMutableString *mutableString = [[NSMutableString alloc] init];

		check("nsstring-class-answers-a-concrete-class",
		      [literal class] != [NSString class] && [[literal class] isSubclassOfClass:[NSString class]] &&
		      [empty class] != [NSString class] && [[empty class] isSubclassOfClass:[NSString class]],
		      "a literal and a constructed string must both answer a concrete SUBCLASS, never the storage-less front");
		check("nsstring-alloc-init-is-a-concrete-empty-string",
		      [empty class] != [literal class] && [empty length] == 0 && [empty isEqualToString:@""],
		      "[[NSString alloc] init] is legitimate and answers an EMPTY instance WITH storage - a different "
		      "concrete class from the compiler's literal");
		check("nsstring-class-for-coder-answers-the-front-and-the-public-subclass",
		      [literal classForCoder] == [NSString class] &&
		      [empty classForCoder] == [NSString class] &&
		      [literal classForArchiver] == [NSString class] &&
		      [mutableString classForCoder] == [NSMutableString class],
		      "a LITERAL must name NSString to an archiver (without the override it named the compiler's "
		      "class), while a PUBLIC subclass names itself");
		check("nsstring-empty-answers-every-read",
		      [empty length] == 0 && [empty characterAtIndex:0] == 0 && strcmp([empty UTF8String], "") == 0 &&
		      /* A STRING'S -description IS THE STRING, so an empty one describes itself as "" — the
		       * `length > 0` clause the collection families can assert would be false here. */
		      [[empty description] isEqualToString:@""] && [empty hash] == [@"" hash] &&
		      [[NSMutableString string] length] == 0,
		      "the empty concrete string answers the primitives and the derived doors over them");
	}

	{
		/* THE PRIMITIVES ARE THE CONTRACT (§C.3 item 5): three methods, no storage, and every door above. */
		ProbePrimitiveString *handmadeString = [[ProbePrimitiveString alloc] init];
		NSString *expectedString = @"hello";

		check("nsstring-primitives-drive-equality-hash-and-search",
		      [handmadeString length] == 5 && [handmadeString characterAtIndex:1] == 'e' &&
		      strcmp([handmadeString UTF8String], "hello") == 0 &&
		      [handmadeString isEqualToString:expectedString] &&
		      [handmadeString hash] == [expectedString hash] &&
		      [[handmadeString description] length] > 0 &&
		      [[handmadeString uppercaseString] isEqualToString:@"HELLO"],
		      "equality, hash, -description and a derived transform must all be written over the three "
		      "primitives, on a class that has no storage of its own");
	}

	{
		/*
		 * M6: THE ORDERED SET — a concrete class, ONE shared empty instance chosen by the data, a mutable
		 * concrete class, and the archiver answered with the public class. Locals declared HERE.
		 */
		NSOrderedSet *emptyOrdered = [NSOrderedSet orderedSet];
		NSOrderedSet *fromInit = [[NSOrderedSet alloc] init];
		NSOrderedSet *oneOrdered = [NSOrderedSet orderedSetWithObject:@"one"];
		NSMutableOrderedSet *mutableOrdered = [NSMutableOrderedSet orderedSet];

		check("nsorderedset-class-answers-a-concrete-class",
		      [emptyOrdered class] != [NSOrderedSet class] &&
		      [[emptyOrdered class] isSubclassOfClass:[NSOrderedSet class]] &&
		      [oneOrdered class] != [emptyOrdered class],
		      "-class must be a private concrete SUBCLASS, and the empty case its own");
		check("nsorderedset-alloc-init-is-the-empty-singleton",
		      fromInit != nil && [fromInit count] == 0 && fromInit == emptyOrdered,
		      "[[NSOrderedSet alloc] init] is legal and answers the SHARED empty instance");
		check("nsorderedset-mutable-and-class-for-coder",
		      [mutableOrdered class] != [NSMutableOrderedSet class] &&
		      [[mutableOrdered class] isSubclassOfClass:[NSMutableOrderedSet class]] &&
		      [emptyOrdered classForCoder] == [NSOrderedSet class] &&
		      [oneOrdered classForCoder] == [NSOrderedSet class] &&
		      [mutableOrdered classForCoder] == [NSMutableOrderedSet class] &&
		      [emptyOrdered classForArchiver] == [NSOrderedSet class],
		      "a mutable constructor answers a mutable concrete class, and the archiver always gets the "
		      "PUBLIC class - the mutable one included, since it is a public subclass");
		check("nsorderedset-empty-answers-every-read",
		      [emptyOrdered count] == 0 && [[emptyOrdered array] count] == 0 &&
		      [[emptyOrdered set] count] == 0 && [emptyOrdered firstObject] == nil &&
		      [emptyOrdered lastObject] == nil &&
		      [[emptyOrdered objectEnumerator] nextObject] == nil &&
		      [emptyOrdered indexOfObject:@"x"] == NSNotFound &&
		      ! [emptyOrdered containsObject:@"x"] &&
		      [emptyOrdered isEqualToOrderedSet:[NSOrderedSet orderedSet]] &&
		      [emptyOrdered hash] == [[NSOrderedSet orderedSet] hash] &&
		      [[emptyOrdered description] length] > 0,
		      "the empty concrete class answers the primitives AND every derived read over them");
	}
	{
		/* THE PRIMITIVES ARE THE CONTRACT (§C.3 item 5). */
		ProbePrimitiveOrderedSet *handmadeOrdered = [[ProbePrimitiveOrderedSet alloc] init];
		NSOrderedSet *expectedOrdered = [NSOrderedSet orderedSetWithArray:
			[NSArray arrayWithObjects:@"x", @"y", @"z", nil]];
		NSMutableArray *seen = [NSMutableArray array];
		id enumerated;

		for (enumerated in handmadeOrdered) {
			[seen addObject:enumerated];
		}
		check("nsorderedset-primitives-drive-order-equality-and-hash",
		      [[handmadeOrdered array] count] == 3 && [handmadeOrdered count] == 3 &&
		      [[handmadeOrdered objectAtIndex:1] isEqual:@"y"] &&
		      [handmadeOrdered indexOfObject:@"z"] == 2 && [handmadeOrdered containsObject:@"x"] &&
		      [handmadeOrdered firstObject] != nil && [handmadeOrdered lastObject] != nil &&
		      [seen count] == 3 && [handmadeOrdered isEqualToOrderedSet:expectedOrdered] &&
		      [handmadeOrdered hash] == [expectedOrdered hash] &&
		      [[handmadeOrdered description] length] > 0 &&
		      [handmadeOrdered isSubsetOfOrderedSet:expectedOrdered],
		      "the array view, the index searches, ORDERED equality, hash, -description and fast enumeration "
		      "must be written over the primitives, on a class with no member array");
	}

	{
		/*
		 * M6: NSData — the cluster the other families have, with the dictionary family's shape applied on
		 * purpose: -init does NOT answer the singleton because this family has allocate-then-fill paths, so
		 * the shared instance belongs to the COMPLETE zero-length construction. Locals declared HERE.
		 * NOTE WHAT IS NOT ASSERTED YET: a third-party subclass over -length/-bytes. This family's eighty-four
		 * storage reads have NOT moved onto primitives, so that check is owed rather than skipped.
		 */
		NSData *emptyData = [NSData data];
		NSData *zeroFromInit = [[NSData alloc] initWithBytes:NULL length:0];
		NSData *plainEmpty = [[NSData alloc] init];
		NSData *someData = [NSData dataWithBytes:"ab" length:2];
		NSMutableData *mutableData = [NSMutableData data];

		check("nsdata-class-answers-a-concrete-class",
		      [emptyData class] != [NSData class] && [[emptyData class] isSubclassOfClass:[NSData class]] &&
		      [someData class] != [NSData class] && [someData class] != [emptyData class],
		      "-class must be a private concrete SUBCLASS of NSData, and the empty case its own");
		check("nsdata-empty-is-the-shared-singleton",
		      zeroFromInit == emptyData && [emptyData length] == 0 &&
		      plainEmpty != nil && [plainEmpty length] == 0 &&
		      [plainEmpty isEqualToData:emptyData] && [plainEmpty hash] == [emptyData hash],
		      "the zero-length construction answers ONE shared instance, while [[NSData alloc] init] is a "
		      "plain EMPTY instance - this family has allocate-then-fill paths, so -init must not answer the "
		      "singleton");
	covers("NSData", "length");
	covers("NSData", "isEqualToData:");
		check("nsdata-mutable-and-class-for-coder",
		      [mutableData class] != [NSMutableData class] &&
		      [[mutableData class] isSubclassOfClass:[NSMutableData class]] &&
		      [emptyData classForCoder] == [NSData class] && [someData classForCoder] == [NSData class] &&
		      [mutableData classForCoder] == [NSMutableData class] &&
		      [emptyData classForArchiver] == [NSData class],
		      "a mutable constructor answers a mutable concrete class, and the archiver gets the PUBLIC class "
		      "- the mutable one naming itself, being a public subclass");
		check("nsdata-empty-answers-every-read",
		      [emptyData length] == 0 && [emptyData bytes] == NULL &&
		      [emptyData isEqualToData:[NSData data]] &&
		      [[emptyData description] length] > 0 &&
		      [[NSMutableData data] length] == 0,
		      "the empty concrete class answers the reads a caller makes of it");
	covers("NSData", "length");
	covers("NSData", "bytes");
	covers("NSData", "isEqualToData:");
	covers("NSData", "description");
	covers("NSData", "data");
	}

	{
		/* THE PRIMITIVES ARE THE CONTRACT (§C.3 item 5): -length and -bytes, and every read above them. */
		ProbePrimitiveData *handmadeData = [[ProbePrimitiveData alloc] init];
		NSData *expectedData = [NSData dataWithBytes:"wxyz" length:4];

		check("nsdata-primitives-drive-equality-hash-slicing-and-encoding",
		      [handmadeData length] == 4 &&
		      [handmadeData isEqualToData:expectedData] && [expectedData isEqualToData:handmadeData] &&
		      [handmadeData hash] == [expectedData hash] &&
		      [[handmadeData subdataWithRange:NSMakeRange(1, 2)]
			isEqualToData:[NSData dataWithBytes:"xy" length:2]] &&
		      [[handmadeData description] length] > 0 &&
		      [[handmadeData base64EncodedStringWithOptions:0] length] > 0 &&
		      [[[NSData dataWithData:handmadeData] base64EncodedStringWithOptions:0]
			isEqualToString:[expectedData base64EncodedStringWithOptions:0]],
		      "equality in BOTH directions, hash, slicing, -description and base64 must be written over the "
		      "two primitives, on a class that has no storage of its own");
	covers("NSData", "isEqualToData:");
	covers("NSData", "subdataWithRange:");
	covers("NSData", "description");
	covers("NSData", "base64EncodedStringWithOptions:");
	covers("NSData", "dataWithData:");
	covers("NSData", "dataWithBytes:length:");
	}

	{
		/*
		 * M6: NSIndexSet — the cluster core. -init MAY answer the singleton here (unlike the dictionary and
		 * data families) because this family's constructions are COMPLETE: nothing allocates and then fills.
		 * Locals declared HERE. NOTE: a third-party subclass over the primitives is NOT asserted yet, because
		 * this family's range-based storage reads have not moved onto a primitive set.
		 */
		NSIndexSet *emptyIndexes = [NSIndexSet indexSet];
		NSIndexSet *fromInit = [[NSIndexSet alloc] init];
		NSIndexSet *oneIndex = [NSIndexSet indexSetWithIndex:3];
		NSMutableIndexSet *mutableIndexes = [NSMutableIndexSet indexSet];

		check("nsindexset-class-answers-a-concrete-class",
		      [emptyIndexes class] != [NSIndexSet class] &&
		      [[emptyIndexes class] isSubclassOfClass:[NSIndexSet class]] &&
		      [oneIndex class] != [NSIndexSet class] && [oneIndex class] != [emptyIndexes class],
		      "-class must be a private concrete SUBCLASS of NSIndexSet, and the empty case its own");
		check("nsindexset-alloc-init-is-the-empty-singleton",
		      fromInit != nil && fromInit == emptyIndexes && [fromInit count] == 0,
		      "[[NSIndexSet alloc] init] is legal and answers the SHARED empty instance - safe HERE because "
		      "this family's constructions are complete");
		check("nsindexset-mutable-and-class-for-coder",
		      [mutableIndexes class] != [NSMutableIndexSet class] &&
		      [[mutableIndexes class] isSubclassOfClass:[NSMutableIndexSet class]] &&
		      [emptyIndexes classForCoder] == [NSIndexSet class] &&
		      [oneIndex classForCoder] == [NSIndexSet class] &&
		      [mutableIndexes classForCoder] == [NSMutableIndexSet class] &&
		      [emptyIndexes classForArchiver] == [NSIndexSet class],
		      "a mutable constructor answers a mutable concrete class, and the archiver gets the PUBLIC class "
		      "- the mutable one naming itself");
		check("nsindexset-empty-answers-every-read",
		      [emptyIndexes count] == 0 && [emptyIndexes firstIndex] == NSNotFound &&
		      [emptyIndexes lastIndex] == NSNotFound && ! [emptyIndexes containsIndex:0] &&
		      [emptyIndexes isEqualToIndexSet:[NSIndexSet indexSet]] &&
		      [emptyIndexes hash] == [[NSIndexSet indexSet] hash] &&
		      [[emptyIndexes description] length] > 0,
		      "the empty concrete class answers the reads a caller makes of it");
		[mutableIndexes addIndex:1];
		[mutableIndexes addIndex:4];
		check("nsindexset-mutable-accumulates",
		      [mutableIndexes count] == 2 && [mutableIndexes containsIndex:4] &&
		      ! [mutableIndexes containsIndex:2] && [mutableIndexes firstIndex] == 1 &&
		      [mutableIndexes lastIndex] == 4 &&
		      [emptyIndexes count] == 0,
		      "the mutable concrete class accumulates - and the SHARED empty instance stayed empty, which is "
		      "what the membership guard protects");
	}

	{
		/* THE PRIMITIVES ARE THE CONTRACT (§C.3 item 5), SCOPED TO WHAT HAS MOVED. */
		ProbePrimitiveIndexSet *handmadeIndexes = [[ProbePrimitiveIndexSet alloc] init];
		NSMutableIndexSet *expectedIndexes = [NSMutableIndexSet indexSet];
		NSUInteger fetched[8];

		[expectedIndexes addIndexesInRange:NSMakeRange(1, 3)];
		[expectedIndexes addIndex:7];
		[expectedIndexes addIndex:9];
		check("nsindexset-primitives-drive-the-iterator-doors",
		      [handmadeIndexes count] == 5 && [handmadeIndexes firstIndex] == 1 &&
		      [handmadeIndexes lastIndex] == 9 &&
		      [handmadeIndexes indexGreaterThanIndex:0] == 1 &&
		      [handmadeIndexes indexGreaterThanIndex:3] == 7 &&
		      [handmadeIndexes indexGreaterThanIndex:9] == NSNotFound &&
		      [handmadeIndexes indexLessThanIndex:5] == 3 &&
		      [handmadeIndexes indexLessThanIndex:1] == NSNotFound &&
		      [handmadeIndexes indexGreaterThanOrEqualToIndex:4] == 7 &&
		      [handmadeIndexes indexGreaterThanOrEqualToIndex:9] == 9 &&
		      [handmadeIndexes indexLessThanOrEqualToIndex:8] == 7 &&
		      [handmadeIndexes indexLessThanOrEqualToIndex:0] == NSNotFound &&
		      [handmadeIndexes containsIndex:3] && ! [handmadeIndexes containsIndex:4] &&
		      [handmadeIndexes countOfIndexesInRange:NSMakeRange(0, 5)] == 3 &&
		      [handmadeIndexes hash] == [expectedIndexes hash] &&
		      [handmadeIndexes isEqualToIndexSet:expectedIndexes] &&
		      [expectedIndexes isEqualToIndexSet:handmadeIndexes] &&
		      [[handmadeIndexes description] length] > 0 &&
		      [handmadeIndexes indexGreaterThanIndex:4] == 7,
		      "-lastIndex and the four index search doors must be written over the three primitives, on a "
		      "class whose ONLY storage is the range door: the iterator doors, the containment and count "
		      "doors, equality in BOTH directions, hash and -description must all be written over the four "
		      "primitives");
		fetched[0] = 0;
		check("nsindexset-primitives-drive-the-bulk-door",
		      [handmadeIndexes getIndexes:fetched maxCount:8 inIndexRange:NULL] == 5 &&
		      fetched[0] == 1 && fetched[4] == 9 &&
		      fetched[1] == 2 && [[handmadeIndexes mutableCopy] count] == 5,
		      "-getIndexes:maxCount:inIndexRange: and -mutableCopy must be written over the range primitive");
	}

	{
		/*
		 * M7: NSHashTable — a FRONT rather than a cluster (its one subclass, FNLegacyHashTable, is private and
		 * what the legacy C API builds), so the checks are its contract: the archiver's answer, an empty
		 * instance, and the three primitives carrying every other door. Locals declared HERE.
		 */
		NSHashTable *emptyTable = [[NSHashTable alloc] init];
		NSHashTable *weakTable = [NSHashTable weakObjectsHashTable];
		ProbePrimitiveHashTable *handmadeTable = [[ProbePrimitiveHashTable alloc] init];
		NSUInteger tableSeen = 0;
		id tableObject;

		check("nshashtable-archiver-answer-and-empty-instance",
		      [weakTable classForCoder] == [NSHashTable class] &&
		      [weakTable classForArchiver] == [NSHashTable class] &&
		      emptyTable != nil && [emptyTable count] == 0 && [emptyTable anyObject] == nil &&
		      [[emptyTable allObjects] count] == 0 && [emptyTable member:@"x"] == nil &&
		      ! [emptyTable containsObject:@"x"] &&
		      [[emptyTable objectEnumerator] nextObject] == nil,
		      "[[NSHashTable alloc] init] must answer an usable EMPTY table, and the archiver must be told the "
		      "PUBLIC class - the private legacy subclass's name never reaches an archive");
		check("nshashtable-primitives-drive-every-read",
		      [handmadeTable count] == 2 && [handmadeTable member:@"m"] != nil &&
		      [handmadeTable containsObject:@"n"] && [handmadeTable member:@"z"] == nil &&
		      [[handmadeTable allObjects] count] == 2 && [handmadeTable anyObject] != nil &&
		      [[handmadeTable setRepresentation] count] == 2 &&
		      [handmadeTable isEqualToHashTable:handmadeTable] &&
		      [handmadeTable isSubsetOfHashTable:handmadeTable],
		      "-allObjects, -anyObject, -containsObject:, -setRepresentation and the comparison doors must be "
		      "written over the three primitives, on a class with no table of its own");
		for (tableObject in handmadeTable) {
			tableSeen++;
		}
		check("nshashtable-primitives-drive-fast-enumeration", tableSeen == 2,
		      "fast enumeration must walk the primitives");
	}

	{
		/*
		 * M7: NSPointerArray — a FRONT, mutable by nature, with no cluster and no public subclass. Locals
		 * declared HERE.
		 */
		NSPointerArray *pointers = [NSPointerArray strongObjectsPointerArray];
		ProbePrimitivePointerArray *handmadePointers = [[ProbePrimitivePointerArray alloc] init];
		NSUInteger pointerSeen = 0;
		id pointerObject;

		[pointers addPointer:(void *)@"one"];
		[pointers addPointer:(void *)@"two"];
		check("nspointerarray-archiver-answer-and-reads",
		      [pointers classForCoder] == [NSPointerArray class] &&
		      [pointers classForArchiver] == [NSPointerArray class] &&
		      [pointers count] == 2 && [[pointers allObjects] count] == 2 &&
		      [pointers pointerAtIndex:0] != NULL,
		      "the archiver must be told the PUBLIC class, and the C-level reads must answer");
		for (pointerObject in pointers) {
			pointerSeen++;
		}
		check("nspointerarray-fast-enumeration-walks-the-slots", pointerSeen == 2,
		      "fast enumeration must hand back both slots");
		check("nspointerarray-primitives-drive-allobjects-and-enumeration",
		      [handmadePointers count] == 2 && [[handmadePointers allObjects] count] == 2 &&
		      [handmadePointers pointerAtIndex:1] != NULL &&
		      [[[handmadePointers allObjects] objectAtIndex:1] isEqual:@"q"],
		      "-allObjects must be written over the two primitives, on a class with no slot array of its own");
		pointerSeen = 0;
		for (pointerObject in handmadePointers) {
			pointerSeen++;
		}
		check("nspointerarray-primitives-drive-fast-enumeration", pointerSeen == 2,
		      "fast enumeration must walk the primitives through the caller's buffer");
	}

	{
		/*
		 * M7: NSAttributedString — a FRONT whose mutable subclass is PUBLIC, so the two answer the archiver
		 * differently. Locals declared HERE.
		 */
		NSAttributedString *plainAttr = [[NSAttributedString alloc] initWithString:@"hello"];
		NSMutableAttributedString *mutableAttr = [[NSMutableAttributedString alloc] initWithString:@"hello"];
		ProbePrimitiveAttributedString *handmadeAttr = [[ProbePrimitiveAttributedString alloc] init];

		check("nsattributedstring-archiver-answer-for-front-and-public-subclass",
		      [plainAttr classForCoder] == [NSAttributedString class] &&
		      [plainAttr classForArchiver] == [NSAttributedString class] &&
		      [mutableAttr classForCoder] == [NSMutableAttributedString class] &&
		      [handmadeAttr classForCoder] == [NSAttributedString class],
		      "the front must be named to an archiver while NSMutableAttributedString - a PUBLIC subclass - names "
		      "itself");
		check("nsattributedstring-primitives-drive-length-hash-and-attribute",
		      [handmadeAttr length] == 5 && [handmadeAttr hash] == [@"hello" hash] &&
		      [[handmadeAttr string] isEqualToString:@"hello"] &&
		      [[handmadeAttr attribute:@"k" atIndex:0 effectiveRange:NULL] isEqual:@"v"] &&
		      [handmadeAttr isEqualToAttributedString:handmadeAttr] &&
		      ! [handmadeAttr isEqualToAttributedString:plainAttr],
		      "-length, -hash, the effective-range attribute door and -isEqualToAttributedString: must be "
		      "written over the two primitives, on a class with no run store at all");
	}

	{
		/*
		 * M7: NSMapTable — the doors landed this session have to be ASSERTED, not merely built. Locals
		 * declared HERE.
		 */
		NSMapTable *table = [NSMapTable strongToStrongObjectsMapTable];
		ProbePrimitiveMapTable *handmadeTable = [[ProbePrimitiveMapTable alloc] init];
		NSDictionary *representation;
		NSUInteger mapSeen = 0;
		id mapKey;

		[table setObject:@"v" forKey:@"k"];
		check("nsmaptable-answer-and-own-reads",
		      [table count] == 1 && [[table objectForKey:@"k"] isEqual:@"v"] &&
		      [[table dictionaryRepresentation] objectForKey:@"k"] == [table objectForKey:@"k"] &&
		      [[[table objectEnumerator] allObjects] count] == 1,
		      "the object API must answer for a table this library made");
		check("nsmaptable-primitives-drive-the-three-doors",
		      [handmadeTable count] == 2 &&
		      [[handmadeTable objectForKey:@"k1"] isEqual:@"v1"] &&
		      [[handmadeTable objectForKey:@"k2"] isEqual:@"v2"] &&
		      [handmadeTable objectForKey:@"absent"] == nil,
		      "a class over the three primitives must answer them");
		representation = [handmadeTable dictionaryRepresentation];
		check("nsmaptable-primitives-drive-dictionaryrepresentation",
		      [[representation objectForKey:@"k1"] isEqual:@"v1"] &&
		      [[representation objectForKey:@"k2"] isEqual:@"v2"] && [representation count] == 2,
		      "-dictionaryRepresentation must be written over -keyEnumerator and -objectForKey:, on a class with no "
		      "table of its own");
		check("nsmaptable-primitives-drive-objectenumerator",
		      [[[handmadeTable objectEnumerator] allObjects] count] == 2,
		      "-objectEnumerator must be written over the primitives");
		for (mapKey in handmadeTable) {
			mapSeen++;
		}
		check("nsmaptable-primitives-drive-fast-enumeration", mapSeen == 2,
		      "fast enumeration must walk the KEY enumerator through the caller's buffer");
	}

	{
		/*
		 * M8: NSNotification — NOT a cluster (one implementation, the front), so §C.3 contributes the
		 * archiver's answer and nothing else. Locals declared HERE.
		 */
		NSNotification *notification =
			[NSNotification notificationWithName:@"ag.note" object:@"o" userInfo:[NSDictionary dictionaryWithObject:@"v" forKey:@"k"]];

		check("nsnotification-archiver-answer-and-payload",
		      [notification classForCoder] == [NSNotification class] &&
		      [notification classForArchiver] == [NSNotification class] &&
		      [[notification name] isEqual:@"ag.note"] && [[notification object] isEqual:@"o"] &&
		      [[[notification userInfo] objectForKey:@"k"] isEqual:@"v"],
		      "the archiver must be told NSNotification, and the three carried values must read back");
	}

	{
		/*
		 * M8: NSCharacterSet — a front whose mutable subclass is PUBLIC. Locals declared HERE.
		 */
		NSCharacterSet *digits = [NSCharacterSet characterSetWithCharactersInString:@"0123456789"];
		NSCharacterSet *digitsByRange = [NSCharacterSet characterSetWithRange:NSMakeRange('0', 10)];
		NSCharacterSet *decimalDigits = [NSCharacterSet decimalDigitCharacterSet];
		NSMutableCharacterSet *mutableChars = [[NSMutableCharacterSet alloc] init];
		ProbePrimitiveCharacterSet *handmadeChars = [[ProbePrimitiveCharacterSet alloc] init];

		[mutableChars addCharactersInString:@"a"];
		check("nscharacterset-archiver-answer-for-front-and-public-subclass",
		      [digits classForCoder] == [NSCharacterSet class] &&
		      [digits classForArchiver] == [NSCharacterSet class] &&
		      [mutableChars classForCoder] == [NSMutableCharacterSet class] &&
		      [handmadeChars classForCoder] == [NSCharacterSet class],
		      "the front must be named to an archiver while NSMutableCharacterSet - a PUBLIC subclass - names "
		      "itself");
		check("nscharacterset-equal-sets-hash-equal",
		      [digits isEqual:digitsByRange] &&
		      [digits hash] == [digitsByRange hash],
		      "two sets with the same members ARE equal, and the equality contract therefore requires one hash "
		      "for both - which hashing the RANGES could not deliver");
		check("nscharacterset-primitives-drive-supersets-and-equality",
		      [handmadeChars characterIsMember:'5'] && ! [handmadeChars characterIsMember:'a'] &&
		      [handmadeChars longCharacterIsMember:0x35] && ! [handmadeChars longCharacterIsMember:0x10005] &&
		      [handmadeChars isEqual:digits] &&
		      [handmadeChars hash] == [digits hash] &&
		      [decimalDigits isSupersetOfSet:handmadeChars] &&
		      ! [handmadeChars isEqual:decimalDigits],
		      "-isSupersetOfSet: (and so equality and the hash) must be written over the two primitives, on a class "
		      "with no range array at all");
	}

	{
		/* THE CHARACTER SET'S NSCoding DOORS (§63.18), over the REAL archive path. Three things are asserted
		 * that a round trip alone would not show: MEMBERSHIP survives (`-characterIsMember:` for a member and a
		 * non-member, which is what the class is FOR), EQUALITY survives (the restored set is equal to the
		 * original, so the ranges — not just the members — came back), and the PUBLIC MUTABLE SUBCLASS's
		 * inherited door answers a MUTABLE set, which is the claim the header makes about this family's shared
		 * layout.
		 *
		 * AND THE ODD-LENGTH REFUSAL IS MEASURED: a corrupt archive carrying three numbers cannot be a whole
		 * number of (location, length) pairs, and the decoder NAMES that instead of rounding it down.
		 *
		 * AND A CORRECTION THIS CHECK COST, worth stating because the failure looked like a library crash: the
		 * MUTABLE case must be reached by archiving a MUTABLE set and unarchiving it (its `-classForCoder`
		 * answers `NSMutableCharacterSet`, so the root entry names it). The first version called
		 * `[[NSMutableCharacterSet alloc] initWithCoder:]` over an IMMUTABLE set's archive — whose `$top` holds
		 * the key `root` and not `NS.ranges` — so the decoder's `-decodeObjectForKey:` RAISED exactly as it
		 * should, and the uncaught exception aborted the probe. **A class's `-initWithCoder:` is only reachable
		 * through its OWN entry**, and the fix is the archive, not the door. */
		NSCharacterSet *set = [NSCharacterSet characterSetWithCharactersInString:@"abcxyz"];
		NSData *setData = [NSKeyedArchiver archivedDataWithRootObject:set];
		NSCharacterSet *backSet = setData != nil
			? [NSKeyedUnarchiver unarchiveObjectWithData:setData] : nil;
		NSMutableCharacterSet *mutableSet = [[NSMutableCharacterSet alloc] init];
		NSMutableData *oddBuffer = [[NSMutableData alloc] init];
		NSKeyedArchiver *oddWriter = [[NSKeyedArchiver alloc]
			initForWritingWithMutableData:oddBuffer];
		NSData *mutableData;
		NSMutableCharacterSet *mutableBack;
		BOOL refusedOdd = NO;

		[mutableSet addCharactersInString:@"xyz"];
		mutableData = [NSKeyedArchiver archivedDataWithRootObject:mutableSet];
		mutableBack = mutableData != nil
			? [NSKeyedUnarchiver unarchiveObjectWithData:mutableData] : nil;
		[oddWriter encodeObject:@[ @1, @2, @3 ] forKey:@"NS.ranges"];
		[oddWriter finishEncoding];
		@try {
			(void)[[NSCharacterSet alloc] initWithCoder:
				[[NSKeyedUnarchiver alloc] initForReadingWithData:oddBuffer]];
		} @catch (NSException *e) {
			refusedOdd = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("nscharacterset-nscoding-round-trip",
		      [set conformsToProtocol:@protocol(NSCoding)] &&
		      backSet != nil &&
		      [backSet characterIsMember:'a'] && [backSet characterIsMember:'z'] &&
		      ![backSet characterIsMember:'1'] && ![backSet characterIsMember:'A'] &&
		      [backSet isEqual:set] && [backSet hash] == [set hash] &&
		      mutableBack != nil &&
		      [mutableBack isKindOfClass:[NSMutableCharacterSet class]] &&
		      [mutableBack characterIsMember:'x'] &&
		      refusedOdd,
		      "membership, equality and the hash survive the round trip; the public mutable subclass's inherited door answers a MUTABLE set; and an archive whose ranges are not pairs is refused by name");
	}

	{
		/*
		 * M8: NSValue — a front holding a payload, with the doors over the three primitives. Locals declared
		 * HERE.
		 */
		int scalar = 4242;
		NSValue *realValue = [NSValue value:&scalar withObjCType:@encode(int)];
		ProbePrimitiveValue *handmadeValue = [[ProbePrimitiveValue alloc] init];
		int readBack = 0;

		check("nsvalue-archiver-answer",
		      [realValue classForCoder] == [NSValue class] &&
		      [realValue classForArchiver] == [NSValue class] &&
		      [handmadeValue classForCoder] == [NSValue class],
		      "the archiver must be told NSValue");
		[handmadeValue getValue:&readBack];
		check("nsvalue-primitives-drive-the-doors",
		      readBack == 4242 && [handmadeValue fnSize] == sizeof(int) &&
		      [handmadeValue isEqualToValue:realValue] && [handmadeValue isEqual:realValue] &&
		      [handmadeValue hash] == [realValue hash] && [handmadeValue description] != nil,
		      "-getValue:, -isEqualToValue: (and so -isEqual:), -hash and -description must be written over the "
		      "three primitives, on a class that copies its payload nowhere");
	}

	printf("FOUNDATION-CLUSTERS DONE\n");

	{
		/* §63.191: THE DEPRECATED BASE64 PAIR and the legacy void -getBytes:. */
		NSData *plain = [@"hello, world" dataUsingEncoding:NSUTF8StringEncoding];
		NSString *enc = [plain base64Encoding];
		NSData *back = [[NSData alloc] initWithBase64Encoding:enc];
		NSData *refused = [[NSData alloc] initWithBase64Encoding:@"not base64!!"];
		unsigned char buffer[32];
		char detail[256];

		memset(buffer, 0, sizeof buffer);
		[plain getBytes:buffer];
		snprintf(detail, sizeof detail, "encoded=[%s] round-trip=%d refused=%d copied=[%.12s]",
			 enc != nil ? [enc UTF8String] : "(nil)",
			 back != nil && [back isEqualToData:plain], refused == nil, (const char *)buffer);
		check("data-deprecated-base64-pair-and-get-bytes",
		      enc != nil && back != nil && [back isEqualToData:plain] && refused == nil &&
		      memcmp(buffer, "hello, world", 12) == 0, detail);
	}
	{
		/* THE NO-COPY DEALLOCATOR: ONCE, at deallocation, with the CALLER's pointer and length. The
		 * autorelease pool is how an ARC probe ends an object's life deterministically. */
		void *mine = malloc(4);
		char detail[192];

		memcpy(mine, "abcd", 4);
		@autoreleasepool {
			NSData *holder = [[NSData alloc] initWithBytesNoCopy:mine length:4
								  deallocator:^(void *bytes, size_t length) {
				fn_probe_deallocator_calls++;
				fn_probe_deallocator_bytes = bytes;
				fn_probe_deallocator_length = length;
			}];
			NSData *expected = [[NSData alloc] initWithBytes:"abcd" length:4];

			if (![holder isEqualToData:expected]) {
				fn_probe_deallocator_calls = -100;	/* the bytes must arrive intact */
			}
		}
		snprintf(detail, sizeof detail, "calls=%d same-pointer=%d length=%lu",
			 fn_probe_deallocator_calls, fn_probe_deallocator_bytes == mine,
			 (unsigned long)fn_probe_deallocator_length);
		check("data-no-copy-deallocator-runs-once",
		      fn_probe_deallocator_calls == 1 && fn_probe_deallocator_bytes == mine &&
		      fn_probe_deallocator_length == 4, detail);
		free(mine);
	}
	{
		/* THE MAPPED-FILE PAIR, ASSERTED AGAINST THE FILE'S CONTENT AND NOT AGAINST SHARING — because
		 * sharing is what MEASUREMENT TOOK AWAY: a MAP_SHARED mapping of this file answered the right
		 * length and ZERO bytes, so the doors read the file and the check asserts what they promise. The
		 * sharing form is owed to the platform, and this comment is where that is recorded. */
		NSString *dir = NSTemporaryDirectory();
		NSError *ignored = nil;
		NSString *path;
		NSData *read;
		char detail[256];

		if (dir == nil || ![[NSFileManager defaultManager] fileExistsAtPath:dir]) {
			dir = [[NSFileManager defaultManager] currentDirectoryPath];
		}
		path = [dir stringByAppendingPathComponent:@"fn_probe_mapped.dat"];
		[[@"0123456789" dataUsingEncoding:NSUTF8StringEncoding] writeToFile:path atomically:NO];
		read = [[NSData alloc] initWithContentsOfMappedFile:path];
		snprintf(detail, sizeof detail, "bytes=[%.10s] length=%lu class-method=%d",
			 read != nil ? (const char *)[read bytes] : "(nil)", (unsigned long)[read length],
			 [[NSData dataWithContentsOfMappedFile:path] isEqualToData:read]);
		check("data-mapped-file-door-reads-the-file",
		      read != nil && [read length] == 10 && memcmp([read bytes], "0123456789", 10) == 0, detail);
	covers("NSData", "bytes");
	covers("NSData", "length");
	covers("NSData", "dataWithContentsOfMappedFile:");
		[[NSFileManager defaultManager] removeItemAtPath:path error:&ignored];
	}

	printf("FOUNDATION-CLUSTERS RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: after a probe the console can stop serving INPUT for
	 * a while, so an `echo $?` the harness types may never run. */
	printf("FOUNDATION-CLUSTERS-STATUS=%d\n", failc ? 1 : 0);
	return failc ? 1 : 0;
}
