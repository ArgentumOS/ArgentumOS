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
#include <stdio.h>
#include <string.h>

static int okc = 0;
static int failc = 0;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("  FOUNDATION-CLUSTERS %s ok\n", name);
	} else {
		failc++;
		printf("  FOUNDATION-CLUSTERS %s FAIL %s\n", name, detail);
	}
}

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

	printf("FOUNDATION-CLUSTERS DONE\n");
	printf("FOUNDATION-CLUSTERS RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: after a probe the console can stop serving INPUT for
	 * a while, so an `echo $?` the harness types may never run. */
	printf("FOUNDATION-CLUSTERS-STATUS=%d\n", failc ? 1 : 0);
	return failc ? 1 : 0;
}
