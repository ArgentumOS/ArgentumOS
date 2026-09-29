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

	printf("FOUNDATION-CLUSTERS DONE\n");
	printf("FOUNDATION-CLUSTERS RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: after a probe the console can stop serving INPUT for
	 * a while, so an `echo $?` the harness types may never run. */
	printf("FOUNDATION-CLUSTERS-STATUS=%d\n", failc ? 1 : 0);
	return failc ? 1 : 0;
}
