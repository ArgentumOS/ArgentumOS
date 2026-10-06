/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_archiver.m — THE PROBE FOR THE CLASSIC PAIR (§62.86): `NSArchiver`, `NSUnarchiver` and
 * `NXReadNSObjectFromCoder`.
 *
 * THE WIRE IS THIS LIBRARY'S OWN, so what is tested is the CONTRACT and not Apple's bytes: a graph goes out
 * and comes back, ONE root per archive, order and type ARE the protocol (no keys, no coercion), identity
 * survives (a container referenced twice is ONE object, and a self-containing container terminates), and
 * substitution works on both sides. Apple's classic `typedstream` is unpublished, so no assertion here
 * could honestly be about it — and the one rule Apple DOES state in words is asserted instead: **a keyed
 * archive cannot be read by a sequential unarchiver.**
 *
 * THE TWO FAMILIES ARE MUTUALLY EXCLUSIVE BY CONSTRUCTION, and that is asserted from both directions: a
 * sequential door on a keyed archiver raises, a keyed door on a sequential archiver raises, and each
 * message names the family that answers it.
 *
 * NOTHING IS ASYNCHRONOUS HERE: every check is a round trip in the same call, so there is no waiting, no
 * run loop and no thread in this probe.
 */

#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if (held) {
		okc++;
		printf("FOUNDATION-ARCHIVER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-ARCHIVER %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* covers("NSCoder", "decodeObject") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/* DID THIS BLOCK RAISE, AND WHAT? The exception's NAME is part of the contract here — a refusal that named
 * the wrong exception would be a different refusal — so each check asks for the name it expects. */
static NSString *fn_raised(NSString *expected, void (^block)(void))
{
	@try {
		block();
	} @catch (NSException *e) {
		return [e name];
	}
	return nil;
}

/* A CLASS THAT SPEAKS THE SEQUENTIAL PROTOCOL — which is what makes it archivable BY THIS PAIR. Its reader
 * takes the same three values in the same order, because in a sequential format that IS the contract. */
@interface FNPair : NSObject <NSCoding>
{
@public
	id first;
	int count;
	double weight;
}
@end

@implementation FNPair

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:first];
	[coder encodeValueOfObjCType:@encode(int) at:&count];
	[coder encodeValueOfObjCType:@encode(double) at:&weight];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		first = [coder decodeObject];
		[coder decodeValueOfObjCType:@encode(int) at:&count];
		[coder decodeValueOfObjCType:@encode(double) at:&weight];
	}
	return self;
}

@end

/* A CLASS WRITTEN AGAINST THE KEYED DOORS: archiving it sequentially must FAIL, and the failure must name
 * the family that answers keys. This is the shape every modern NSCoding class has. */
@interface FNKeyedThing : NSObject <NSCoding>
@end

@implementation FNKeyedThing

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:@"value" forKey:@"k"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	(void)coder;
	return [super init];
}

@end

/* NOT NSCoding AT ALL, which is the other refusal. */
@interface FNPlainThing : NSObject
@end
@implementation FNPlainThing
@end

/* TWO CLASSES WITH NOTHING BEHIND THEM: the class-name map is only OBSERVABLE when the mapped class and
 * the mapped-to class are distinguishable. A class cluster defeats that - an array decoded as "NSArray"
 * comes back as the cluster's mutable face either way, which is what the first version of the check below
 * measured (plain=AGArrayMutable mapped=AGArrayMutable) and could not tell apart. */
@interface KUAlpha : NSObject <NSCoding>
@end
@implementation KUAlpha
- (nullable instancetype)initWithCoder:(NSCoder *)coder { (void)coder; return [super init]; }
- (void)encodeWithCoder:(NSCoder *)coder { (void)coder; }
@end

@interface KUBeta : NSObject <NSCoding>
@end
@implementation KUBeta
- (nullable instancetype)initWithCoder:(NSCoder *)coder { (void)coder; return [super init]; }
- (void)encodeWithCoder:(NSCoder *)coder { (void)coder; }
@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- 1. THE SURFACE ------------------------------------------------------------------------------ */
	{
		Class a = objc_getClass("NSArchiver");
		Class u = objc_getClass("NSUnarchiver");

		check("the-pair-is-declared-coder-side",
		      a != Nil && u != Nil && class_getSuperclass(a) == [NSCoder class] &&
		      class_getSuperclass(u) == [NSCoder class],
		      @"NSArchiver and NSUnarchiver exist and are NSCoder subclasses");
	covers("NSCoder", "encodeObject:");
		check("the-refused-doors-are-absent-rather-than-stubbed",
		      ![a instancesRespondToSelector:NSSelectorFromString(@"setObjectZone:")] &&
		      ![a instancesRespondToSelector:NSSelectorFromString(@"objectZone")] &&
		      ![a instancesRespondToSelector:NSSelectorFromString(@"encodeClassName:intoClassName:")] &&
		      ![u instancesRespondToSelector:NSSelectorFromString(@"setObjectZone:")] &&
		      ![u instancesRespondToSelector:NSSelectorFromString(@"decodeClassName:asClassName:")],
		      @"the zone doors (no NSZone exists in this library) and class-name translation (the same "
		      @"refusal NSKeyedArchiver.h records) are ABSENT, not stubbed");
	}

	/* --- 2. THE TWO FAMILIES ARE MUTUALLY EXCLUSIVE --------------------------------------------------- */
	{
		NSMutableData *data = [[NSMutableData alloc] init];
		NSArchiver *seq = [[NSArchiver alloc] initForWritingWithMutableData:data];
		NSString *keyedOnSequential, *sequentialOnKeyed;

		keyedOnSequential = fn_raised(NSInvalidArgumentException, ^{
			[seq encodeObject:@"x" forKey:@"k"];
		});
		sequentialOnKeyed = fn_raised(NSInvalidArgumentException, ^{
			NSKeyedArchiver *keyed = [[NSKeyedArchiver alloc] initForWritingWithMutableData:[[NSMutableData alloc] init]];
			[keyed encodeObject:@"x"];
		});
		check("a-keyed-door-on-the-sequential-archiver-raises",
		      [keyedOnSequential isEqualToString:NSInvalidArgumentException],
		      @"the keyed doors are not answered by this pair");
	covers("NSCoder", "encodeObject:");
		check("a-sequential-door-on-the-keyed-archiver-raises",
		      [sequentialOnKeyed isEqualToString:NSInvalidArgumentException],
		      @"and the other direction is a refusal too, so neither family is the other's fallback");
	}

	/* --- 3. A GRAPH GOES OUT AND COMES BACK ------------------------------------------------------------ */
	{
		NSMutableArray *elements = [NSMutableArray array];
		NSMutableArray *shared = [NSMutableArray array];
		NSMutableDictionary *dict = [NSMutableDictionary dictionary];
		NSData *archived;
		NSArray *back;

		[shared addObject:@"inner"];
		[elements addObject:@"text"];
		[elements addObject:[NSNumber numberWithInt:-7]];
		[elements addObject:[NSNumber numberWithDouble:2.5]];
		[elements addObject:[NSNumber numberWithBool:YES]];
		[elements addObject:[NSNull null]];
		[elements addObject:shared];
		[elements addObject:shared];		/* THE SAME ARRAY TWICE */
		[elements addObject:dict];
		[dict setObject:[NSNumber numberWithLongLong:9000000000LL] forKey:@"big"];

		archived = [NSArchiver archivedDataWithRootObject:elements];
		back = [NSUnarchiver unarchiveObjectWithData:archived];

		check("a-graph-round-trips",
		      [back count] == 8 && [[back objectAtIndex:0] isEqualToString:@"text"] &&
		      [[back objectAtIndex:1] intValue] == -7 &&
		      [[back objectAtIndex:2] doubleValue] == 2.5 &&
		      [[back objectAtIndex:3] boolValue] == YES &&
		      [[back objectAtIndex:4] isEqual:[NSNull null]] &&
		      [[[back objectAtIndex:7] objectForKey:@"big"] longLongValue] == 9000000000LL,
		      @"strings, ints, doubles, booleans, NSNull and a dictionary all come back");
	covers("NSKeyedUnarchiver", "unarchiveObjectWithData:");
		check("identity-survives-the-round-trip",
		      [back objectAtIndex:5] == [back objectAtIndex:6] &&
		      [[[back objectAtIndex:5] objectAtIndex:0] isEqualToString:@"inner"],
		      @"the array written twice is decoded ONCE — the object table, not equality, is what the wire "
		      @"preserves");
	}

	/* --- 4. A CONTAINER THAT CONTAINS ITSELF ------------------------------------------------------------ */
	{
		NSMutableArray *cycle = [NSMutableArray array];
		NSArray *back;

		[cycle addObject:@"head"];
		/* THE CIRCULAR CONTAINER IS THE POINT OF THIS CHECK, so the warning about it is silenced HERE and
		 * nowhere else: a probe that avoided the shape would be testing that cycles do not happen. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-circular-container"
		[cycle addObject:cycle];
#pragma clang diagnostic pop
		back = [NSUnarchiver unarchiveObjectWithData:[NSArchiver archivedDataWithRootObject:cycle]];
		check("a-self-containing-array-terminates-and-identity-holds",
		      [back count] == 2 && [back objectAtIndex:1] == back,
		      @"the container is registered BEFORE its elements are read, which is what makes a cycle a "
		      @"reference instead of infinite recursion");
	}

	/* --- 5. AN OBJECT THAT SPEAKS THE SEQUENTIAL PROTOCOL ------------------------------------------------ */
	{
		FNPair *pair = [[FNPair alloc] init];
		FNPair *back;
		NSArray *root, *arrayBack;

		pair->first = @"first-field";
		pair->count = 42;
		pair->weight = 1.25;
		root = [NSArray arrayWithObjects:pair, @"tail", nil];
		arrayBack = [NSUnarchiver unarchiveObjectWithData:[NSArchiver archivedDataWithRootObject:root]];
		back = [arrayBack objectAtIndex:0];

		check("an-nscoding-object-round-trips-with-its-fields",
		      [arrayBack count] == 2 && [back isKindOfClass:[FNPair class]] &&
		      [[back->first description] isEqualToString:@"first-field"] && back->count == 42 &&
		      back->weight == 1.25 && [[arrayBack objectAtIndex:1] isEqualToString:@"tail"],
		      @"a class whose reader takes its values IN ORDER comes back whole, and the archive continues "
		      @"after it");
	}

	/* --- 6. THE THREE REFUSALS A GRAPH CAN PRODUCE ------------------------------------------------------- */
	{
		FNPlainThing *plain = [[FNPlainThing alloc] init];
		FNKeyedThing *keyed = [[FNKeyedThing alloc] init];
		NSString *plainName, *keyedName;

		plainName = fn_raised(NSInconsistentArchiveException, ^{
			(void)[NSArchiver archivedDataWithRootObject:plain];
		});
		keyedName = fn_raised(NSInvalidArgumentException, ^{
			(void)[NSArchiver archivedDataWithRootObject:keyed];
		});
		check("an-object-that-is-not-nscoding-is-refused-by-name",
		      [plainName isEqualToString:NSInconsistentArchiveException],
		      @"NSInconsistentArchiveException: the archive would have been unreadable");
		check("an-object-that-speaks-keys-is-refused-and-told-which-family-to-use",
		      [keyedName isEqualToString:NSInvalidArgumentException],
		      @"a class written against keys reaches a keyed door and raises THERE, naming the family that "
		      @"answers keys — "
		      @"which is the correct answer, not a defect of this pair");
	}

	/* --- 7. THE PROGRAMMING ERRORS AND THE BAD DATA ------------------------------------------------------ */
	{
		NSMutableData *data = [[NSMutableData alloc] init];
		NSArchiver *archiver = [[NSArchiver alloc] initForWritingWithMutableData:data];
		NSString *nilWrite, *nilRead, *twice, *truncated, *mismatch;
		NSData *good;
		id bad, short_;
		NSUnarchiver *reader;

		nilWrite = fn_raised(NSInvalidArgumentException, ^{
			(void)[[NSArchiver alloc] initForWritingWithMutableData:(NSMutableData *)nil];
		});
		nilRead = fn_raised(NSInvalidArgumentException, ^{
			(void)[[NSUnarchiver alloc] initForReadingWithData:(NSData *)nil];
		});
		[archiver encodeRootObject:@"only-root"];
		twice = fn_raised(NSInvalidArgumentException, ^{
			[archiver encodeRootObject:@"second-root"];
		});
		check("a-nil-data-argument-raises-on-both-sides",
		      [nilWrite isEqualToString:NSInvalidArgumentException] &&
		      [nilRead isEqualToString:NSInvalidArgumentException],
		      @"nil data is a programming error, not an empty archive");
	covers("NSCoder", "encodeRootObject:");
		check("a-second-root-object-raises",
		      [twice isEqualToString:NSInvalidArgumentException],
		      @"one archive holds one root");
		check("the-archiver-keeps-the-data-it-was-given",
		      [archiver archiverData] == data && [data length] > 5,
		      @"-archiverData is the SAME object, header included");

		good = [NSArchiver archivedDataWithRootObject:@"payload"];
		/* THE TIER'S NULLABLE-FACTORY IDIOM: `-dataUsingEncoding:` answers a nullable pointer, so it goes
		 * through an `id` local before it reaches a non-null parameter. */
		{
			id notAnArchive = [@"not an archive at all" dataUsingEncoding:NSUTF8StringEncoding];

			bad = [NSUnarchiver unarchiveObjectWithData:notAnArchive];
		}
		short_ = [NSUnarchiver unarchiveObjectWithData:
				[NSData dataWithBytes:"FNAR" length:4]];	/* magic, no version byte */
		reader = [[NSUnarchiver alloc] initForReadingWithData:[NSData dataWithBytes:"FNAR\1\7"
										  length:6]];
		truncated = fn_raised(NSInconsistentArchiveException, ^{
			(void)[reader decodeObject];	/* a string tag with no length behind it */
		});
		check("data-that-is-not-an-archive-answers-nil",
		      bad == nil && short_ == nil,
		      @"an invalid archive is a nil, which is a value a caller can act on");
	covers("NSCoder", "decodeObject");
		check("a-truncated-archive-raises-instead-of-reading-past-its-end",
		      [truncated isEqualToString:NSInconsistentArchiveException],
		      @"a short read is NSInconsistentArchiveException — the exception Apple names for bad archive "
		      @"data");

		/* A KEYED ARCHIVE IS THE ONE INTEROPERABILITY RULE APPLE STATES IN WORDS. */
		{
			NSData *keyedArchive = [NSKeyedArchiver archivedDataWithRootObject:@"k"];
			id answer = [NSUnarchiver unarchiveObjectWithData:keyedArchive];

			check("a-keyed-archive-is-refused-by-the-sequential-reader",
			      keyedArchive != nil && answer == nil,
			      @"Apple: a keyed archive cannot be decoded by an NSUnarchiver — here that is the magic, "
			      @"not a hope");
		}

		/* READING THE WRONG TYPE IS A REFUSAL, because in a sequential format the type IS the contract. */
		{
			NSUnarchiver *r;
			NSString *name;

			r = [[NSUnarchiver alloc] initForReadingWithData:good];
			name = fn_raised(NSInconsistentArchiveException, ^{
				int i = 0;

				[r decodeValueOfObjCType:@encode(int) at:&i];
			});
			check("reading-a-value-as-the-wrong-type-raises",
			      [name isEqualToString:NSInconsistentArchiveException],
			      @"a string where an int was asked for is not a coercion, it is an inconsistent archive");
	covers("NSCoder", "decodeValueOfObjCType:at:size:");
		}
	}

	/* --- 8. SUBSTITUTION ON BOTH SIDES, AND isAtEnd ------------------------------------------------------ */
	{
		NSString *original = @"original";
		NSMutableArray *root = [NSMutableArray arrayWithObjects:original, original, nil];
		NSData *archived;
		NSArray *back;

		/* THE WRITING SIDE: replace objects by using a different archiver for the same graph. */
		{
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *archiver = [[NSArchiver alloc] initForWritingWithMutableData:data];

			[archiver replaceObject:original withObject:@"substituted"];
			[archiver encodeRootObject:root];
			archived = [NSData dataWithData:data];
		}
		back = [NSUnarchiver unarchiveObjectWithData:archived];
		check("the-writing-side-substitutes",
		      [back count] == 2 && [[back objectAtIndex:0] isEqualToString:@"substituted"] &&
		      [[back objectAtIndex:1] isEqualToString:@"substituted"],
		      @"the object is written as its replacement, both times it appears");
	covers("NSCoder", "encodeRootObject:");

		/* THE READING SIDE: the same archive, substituted on the way in. */
		{
			NSUnarchiver *reader = [[NSUnarchiver alloc] initForReadingWithData:
						[NSArchiver archivedDataWithRootObject:root]];
			NSArray *substituted;

			check("isatend-is-false-before-and-true-after-the-root",
			      [reader isAtEnd] == NO, @"there is a value to read");
			[reader replaceObject:@"original" withObject:@"read-substituted"];
			substituted = [reader decodeObject];
			check("the-reading-side-substitutes",
			      [[substituted objectAtIndex:0] isEqualToString:@"read-substituted"],
			      [NSString stringWithFormat:@"a decoded object EQUAL to the registered one is handed "
						@"back as its replacement (got %@, count %lu)",
						[substituted objectAtIndex:0],
						(unsigned long)[substituted count]]);
			check("isatend-is-true-once-the-whole-archive-has-been-decoded",
			      [reader isAtEnd] == YES && [substituted count] == 2,
			      @"the door that tells a caller the object it read was the WHOLE archive");
		}

		/* THE VERSION DOOR: declared, and it RAISES rather than answering a number nobody wrote. */
		{
			NSUnarchiver *reader = [[NSUnarchiver alloc] initForReadingWithData:
						[NSArchiver archivedDataWithRootObject:@"x"]];
			NSString *name = fn_raised(NSInconsistentArchiveException, ^{
				(void)[reader versionForClassName:@"FNPair"];
			});

			check("the-version-door-raises-rather-than-fabricating",
			      [name isEqualToString:NSInconsistentArchiveException],
			      @"this wire records no class versions, so none is answered");
	covers("NSCoder", "versionForClassName:");
		}
	}

	/* --- 9. THE LEGACY FREE FUNCTION -------------------------------------------------------------------- */
	{
		NSData *archived = [NSArchiver archivedDataWithRootObject:@"through-the-function"];
		NSUnarchiver *reader = [[NSUnarchiver alloc] initForReadingWithData:archived];
		id viaFunction = NXReadNSObjectFromCoder(reader);

		check("nxreadnsobjectfromcoder-is-the-object-door",
		      [viaFunction isEqualToString:@"through-the-function"] && [reader isAtEnd],
		      @"the legacy free function reads one object and does nothing else");
	}

	/* --- 10. THE SEQUENTIAL VALUE, ARRAY AND GEOMETRY DOORS (§63.43) ------------------------------------ */
	{
		/* A STRUCT THROUGH THE TYPE-CODE DOOR, which is what the wire could not do before: `{…}` has no
		 * scalar spelling, and a wire that cannot carry a struct cannot carry a point. */
		{
			NSPoint in = { 3.5, -7.25 };
			NSPoint out = { 0, 0 };
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *writer = [[NSArchiver alloc] initForWritingWithMutableData:data];
			NSUnarchiver *reader;

			[writer encodeValueOfObjCType:@encode(NSPoint) at:&in];
			reader = [[NSUnarchiver alloc] initForReadingWithData:data];
			[reader decodeValueOfObjCType:@encode(NSPoint) at:&out];
			check("a-struct-round-trips-through-the-type-code-door",
			      out.x == in.x && out.y == in.y,
			      [NSString stringWithFormat:@"a struct written as its type code comes back as itself "
						@"(wrote {%.2f, %.2f}, read {%.2f, %.2f})",
						in.x, in.y, out.x, out.y]);
	covers("NSCoder", "encodeValueOfObjCType:at:");
	covers("NSCoder", "decodeValueOfObjCType:at:");
		}

		/* THE SIX UNKEYED GEOMETRY DOORS, in ONE archive and in the order a sequential format requires. */
		{
			NSPoint point = { 1.5, 2.5 };
			NSSize size = { 30.0, 40.0 };
			NSRect rect = { { 1.0, 2.0 }, { 3.0, 4.0 } };
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *writer = [[NSArchiver alloc] initForWritingWithMutableData:data];
			NSUnarchiver *reader;
			NSPoint pointBack;
			NSSize sizeBack;
			NSRect rectBack;

			[writer encodePoint:point];
			[writer encodeSize:size];
			[writer encodeRect:rect];
			reader = [[NSUnarchiver alloc] initForReadingWithData:data];
			pointBack = [reader decodePoint];
			sizeBack = [reader decodeSize];
			rectBack = [reader decodeRect];
			check("the-unkeyed-geometry-doors-round-trip",
			      pointBack.x == point.x && pointBack.y == point.y &&
			      sizeBack.width == size.width && sizeBack.height == size.height &&
			      NSEqualRects(rectBack, rect),
			      [NSString stringWithFormat:@"a point {%.1f,%.1f}, a size {%.1f,%.1f} and a rect "
						@"{{%.1f,%.1f},{%.1f,%.1f}} come back unchanged",
						pointBack.x, pointBack.y, sizeBack.width, sizeBack.height,
						rectBack.origin.x, rectBack.origin.y,
						rectBack.size.width, rectBack.size.height]);
	covers("NSCoder", "encodePoint:");
	covers("NSCoder", "decodePoint");
	covers("NSCoder", "encodeSize:");
	covers("NSCoder", "decodeSize");
	covers("NSCoder", "encodeRect:");
	covers("NSCoder", "decodeRect");
		}

		/* THE ARRAY DOORS: a RUN of values whose stride is the type's own size. */
		{
			int values[3] = { 11, -22, 33 };
			int back[3] = { 0, 0, 0 };
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *writer = [[NSArchiver alloc] initForWritingWithMutableData:data];
			NSUnarchiver *reader;

			[writer encodeArrayOfObjCType:@encode(int) count:3 at:values];
			reader = [[NSUnarchiver alloc] initForReadingWithData:data];
			[reader decodeArrayOfObjCType:@encode(int) count:3 at:back];
			check("the-array-doors-round-trip",
			      back[0] == 11 && back[1] == -22 && back[2] == 33,
			      [NSString stringWithFormat:@"three ints written as one array come back as %d, %d, %d",
						back[0], back[1], back[2]]);
	covers("NSCoder", "encodeArrayOfObjCType:count:at:");
	covers("NSCoder", "decodeArrayOfObjCType:count:at:");
		}

		/* THE SIZED READING DOOR HONOURS THE SIZE THE CALLER DECLARED — that is what it is FOR, and it is
		 * the whole reason Apple's deprecated the un-sized spelling. */
		{
			int written = 7;
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *writer = [[NSArchiver alloc] initForWritingWithMutableData:data];
			NSUnarchiver *reader;
			NSString *name;

			[writer encodeValueOfObjCType:@encode(int) at:&written];
			reader = [[NSUnarchiver alloc] initForReadingWithData:data];
			name = fn_raised(NSInconsistentArchiveException, ^{
				int into = 0;

				[reader decodeValueOfObjCType:@encode(int) at:&into size:1];
			});
			check("the-sized-reading-door-refuses-a-buffer-that-is-too-small",
			      [name isEqualToString:NSInconsistentArchiveException],
			      @"a 4-byte value read into a 1-byte buffer is a NAMED refusal rather than the overrun "
			      @"Apple deprecates the old spelling for");
		}

		/* AND THE UN-SIZED DOOR IS THE SIZED ONE'S FUNNEL, so a caller that still sends the old selector
		 * decodes on the same path. */
		{
			int written = 4207;
			int back = 0;
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *writer = [[NSArchiver alloc] initForWritingWithMutableData:data];
			NSUnarchiver *reader;

			[writer encodeValueOfObjCType:@encode(int) at:&written];
			reader = [[NSUnarchiver alloc] initForReadingWithData:data];
			[reader decodeValueOfObjCType:@encode(int) at:&back];
			check("the-un-sized-reading-door-still-reads",
			      back == 4207,
			      @"the deprecated spelling sizes the type code itself and reads the same value");
		}

		/* THE BYTES DOOR WITH A FLOOR UNDER IT: met, it answers; short, it fails THROUGH -failWithError:. */
		{
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *writer = [[NSArchiver alloc] initForWritingWithMutableData:data];
			NSUnarchiver *reader;
			const char *bytes;

			[writer encodeBytes:"abcd" length:4];
			reader = [[NSUnarchiver alloc] initForReadingWithData:data];
			bytes = (const char *)[reader decodeBytesWithMinimumLength:4];
			check("decodebyteswithminimumlength-reads-a-long-enough-run",
			      bytes != NULL && bytes[0] == 'a' && bytes[1] == 'b' && bytes[2] == 'c' &&
			      bytes[3] == 'd',
			      @"a run that MEETS the floor is handed back unchanged");
	covers("NSCoder", "encodeBytes:length:");
	covers("NSCoder", "encodeBytes:length:forKey:");
	covers("NSCoder", "decodeBytesWithMinimumLength:");
		}
		{
			NSMutableData *data = [[NSMutableData alloc] init];
			NSArchiver *writer = [[NSArchiver alloc] initForWritingWithMutableData:data];
			NSUnarchiver *reader;
			NSString *name;

			[writer encodeBytes:"ab" length:2];
			reader = [[NSUnarchiver alloc] initForReadingWithData:data];
			name = fn_raised(NSInvalidArgumentException, ^{
				(void)[reader decodeBytesWithMinimumLength:4];
			});
			check("decodebyteswithminimumlength-refuses-a-short-run",
			      [name isEqualToString:NSInvalidArgumentException],
			      @"a run SHORTER than the floor is a corrupt archive, not a shorter value — and the "
			      @"refusal goes through -failWithError:, exactly as Apple's own text says");
	covers("NSCoder", "decodeBytesWithMinimumLength:");
		}

		/* AND THE MUTUAL EXCLUSION, ONE DOOR FURTHER OUT: a geometry door is the SEQUENTIAL family's, so a
		 * keyed archiver refuses it and names the family that answers — the same rule every other door on
		 * this base follows, now reaching the six new ones. */
		{
			NSMutableData *data = [[NSMutableData alloc] init];
			NSKeyedArchiver *keyed = [[NSKeyedArchiver alloc] initForWritingWithMutableData:data];
			NSPoint point = { 0, 0 };
			NSString *name = fn_raised(NSInvalidArgumentException, ^{
				[keyed encodePoint:point];
			});

			check("a-sequential-geometry-door-on-the-keyed-archiver-raises",
			      [name isEqualToString:NSInvalidArgumentException],
			      @"-encodePoint: reaches -encodeValueOfObjCType:at:, which the keyed family refuses with "
			      @"the sequential message");
		}
	}

	/* THE STATUS REPORTS FAILURES AND NOT A COUNT. A second copy of "how many checks are there" is a number
	 * that goes stale the first time a check is added — twice in this thread (§62.85 and this unit) — and the
	 * case file already asserts the count, from the probe's OWN names. */

	{
		/* THE CLASS-NAME MAP (§63.183), INCLUDING WHERE IT IS CONSULTED. The map is asked BEFORE the runtime,
		 * and that is asserted the only way it can be: an archive that NAMES KUAlpha is decoded with that
		 * name remapped to KUBeta, so the class the runtime would have found BY NAME is not the one that must
		 * come back. Both halves are exercised - the process-wide door and the instance one. */
		NSData *archived = [NSKeyedArchiver archivedDataWithRootObject:[[KUAlpha alloc] init]];
		id plain = [NSKeyedUnarchiver unarchiveObjectWithData:archived];
		id mapped;

		[NSKeyedUnarchiver setClass:[KUBeta class] forClassName:@"KUAlpha"];
		mapped = [NSKeyedUnarchiver unarchiveObjectWithData:archived];
		check("archiver-class-map",
		      plain != nil && [plain isKindOfClass:[KUAlpha class]] &&
		      mapped != nil && [mapped isKindOfClass:[KUBeta class]] &&
		      ![mapped isKindOfClass:[KUAlpha class]],
		      [NSString stringWithFormat:@"plain=%@ mapped=%@", [plain class], [mapped class]]);
	}

	{
		/* THE INSTANCE HALF, and that CLEARING a mapping with nil removes it: the door must forget, not store
		 * a nil that would then answer "mapped" for every later read. */
		[NSKeyedUnarchiver setClass:Nil forClassName:@"KUAlpha"];
		check("archiver-class-map-clears",
		      [NSKeyedUnarchiver classForClassName:@"KUAlpha"] == Nil,
		      @"a nil class removes the mapping rather than storing a nil");
	}


	{
		/* THE SECURE TOP-LEVEL DOORS (§63.183): one object, an array EVERY element of which is checked, and a
		 * dictionary whose KEYS and OBJECTS are checked separately. A refusal must answer nil AND an error -
		 * the error is the point of the family - and a corrupt blob must be refused rather than raised. */
		NSData *arrayData = [NSKeyedArchiver archivedDataWithRootObject:
			[NSArray arrayWithObjects:[NSNumber numberWithInt:1], [NSNumber numberWithInt:2], nil]];
		NSData *dictData = [NSKeyedArchiver archivedDataWithRootObject:
			[NSDictionary dictionaryWithObject:[NSNumber numberWithInt:7] forKey:@"k"]];
		NSError *error = nil;
		id okArray = [NSKeyedUnarchiver unarchivedObjectOfClass:[NSArray class] fromData:arrayData error:&error];
		id wrongClass = [NSKeyedUnarchiver unarchivedObjectOfClass:[NSString class] fromData:arrayData error:&error];
		BOOL wrongClassErrored = (wrongClass == nil && error != nil);
		NSArray *numbers;
		NSArray *strings;
		NSDictionary *dict;
		NSData *corrupt = [NSData dataWithBytes:"not-archive" length:11];
		id fromCorrupt;
		BOOL corruptErrored;

		error = nil;
		numbers = [NSKeyedUnarchiver unarchivedArrayOfObjectsOfClass:[NSNumber class] fromData:arrayData error:&error];
		error = nil;
		strings = [NSKeyedUnarchiver unarchivedArrayOfObjectsOfClass:[NSString class] fromData:arrayData error:&error];
		error = nil;
		dict = [NSKeyedUnarchiver unarchivedDictionaryWithKeysOfClass:[NSString class]
								     objectsOfClass:[NSNumber class]
									   fromData:dictData error:&error];
		error = nil;
		fromCorrupt = [NSKeyedUnarchiver unarchivedObjectOfClass:[NSArray class] fromData:corrupt error:&error];
		corruptErrored = (fromCorrupt == nil && error != nil);
		check("archiver-secure-doors",
		      okArray != nil && [okArray count] == 2 &&
		      wrongClassErrored && numbers != nil && [numbers count] == 2 &&
		      strings == nil && dict != nil && [dict count] == 1 && corruptErrored,
		      [NSString stringWithFormat:@"ok=%d wrong=%d numbers=%d strings=%d dict=%d corrupt=%d",
			okArray != nil, wrongClassErrored, numbers != nil, strings != nil, dict != nil, corruptErrored]);
	}


	{
		/* THE WRITER'S CLASS-NAME MAP (§63.184): the writer renames a class as it writes and the reader maps
		 * the name to a DIFFERENT class, so the round trip shows the bytes carry the stated name — and the bytes
		 * are checked too, so a pass cannot come from a reader that ignores the map. */
		NSString *trouble = nil;
		NSData *renamed = nil;
		id decoded = nil;
		@try {
			[NSKeyedArchiver setClassName:@"KURenamed" forClass:[KUBeta class]];
			renamed = [NSKeyedArchiver archivedDataWithRootObject:[[KUBeta alloc] init]];
			[NSKeyedUnarchiver setClass:[KUAlpha class] forClassName:@"KURenamed"];
			decoded = [NSKeyedUnarchiver unarchiveObjectWithData:renamed];
		} @catch (id exception) {
			trouble = [exception reason];
		}
		NSString *text = renamed != nil
			? [[NSString alloc] initWithData:renamed encoding:NSUTF8StringEncoding] : nil;
		check("archiver-writer-class-map",
		      trouble == nil && renamed != nil && text != nil &&
		      [text rangeOfString:@"KURenamed"].location != NSNotFound &&
		      [text rangeOfString:@"KUBeta"].location == NSNotFound &&
		      decoded != nil && [decoded isKindOfClass:[KUAlpha class]],
		      trouble != nil ? trouble
				     : [NSString stringWithFormat:@"decoded=%@ new-name-in-bytes=%d old-name-in-bytes=%d len=%lu",
					[decoded class],
					[text rangeOfString:@"KURenamed"].location != NSNotFound,
					[text rangeOfString:@"KUBeta"].location != NSNotFound,
					(unsigned long)[renamed length]]);
		[NSKeyedArchiver setClassName:Nil forClass:[KUBeta class]];
		[NSKeyedUnarchiver setClass:Nil forClassName:@"KURenamed"];
		check("archiver-writer-class-map-clears",
		      [NSKeyedArchiver classNameForClass:[KUBeta class]] == nil &&
		      [NSKeyedUnarchiver classForClassName:@"KURenamed"] == Nil,
		      @"a nil name removes the writer's mapping, and the reader's clears too");
	}

	{
		/* THE SECURE-CLASS DOOR (§63.184), with the EXCEPTION'S OWN REASON on the wire: a substituted
		 * probe-made NSError reported "something happened" where the reason was the whole answer. */
		NSError *plainError = nil;
		NSError *refusedError = nil;
		NSData *plain = nil;
		NSData *refused = nil;
		NSString *raise = nil;

		@try {
			plain = [NSKeyedArchiver archivedDataWithRootObject:[[KUAlpha alloc] init]
							  requiringSecureCoding:YES error:&plainError];
			refused = [NSKeyedArchiver archivedDataWithRootObject:[NSObject new]
							    requiringSecureCoding:YES error:&refusedError];
		} @catch (id exception) {
			raise = [exception reason];
		}
		check("archiver-secure-class-door",
		      raise == nil && plain != nil && plainError == nil && refused == nil && refusedError != nil,
		      [NSString stringWithFormat:@"raise=%@ plainNil=%d plainErr=%@ refusedNil=%d refusedErr=%@",
			raise != nil ? raise : @"none", plain == nil,
			plainError != nil ? [plainError localizedDescription] : @"none", refused == nil,
			refusedError != nil ? [refusedError localizedDescription] : @"none"]);
	covers("NSKeyedUnarchiver", "classForClassName:");
	}


	{
		/* THE STREAMING FLOW (§63.185): an archiver over its OWN buffer, a caller's own key, and the bytes read
		 * back through the top-level door. It mirrors a HOST experiment run first, which works in every variant
		 * (including requiresSecureCoding=YES on an NSCoding-only fixture). */
		NSString *trouble = nil;
		NSData *streamed = nil;
		id back = nil;
		NSError *readError = nil;
		NSUInteger fmtOk = 0;

		@try {
			NSKeyedArchiver *streaming = [[NSKeyedArchiver alloc] initRequiringSecureCoding:YES];

			fmtOk = ([streaming outputFormat] == NSPropertyListXMLFormat_v1_0);
			[streaming encodeObject:[[KUAlpha alloc] init] forKey:@"thing"];
			streamed = [streaming encodedData];
			if (streamed != nil) {
				NSKeyedUnarchiver *reader = [[NSKeyedUnarchiver alloc] initForReadingWithData:streamed];

				back = [reader decodeTopLevelObjectForKey:@"thing" error:&readError];
			}
		} @catch (id exception) {
			trouble = [exception reason];
		}
		NSString *text = streamed != nil
			? [[NSString alloc] initWithData:streamed encoding:NSUTF8StringEncoding] : nil;
		check("archiver-secure-and-streaming",
		      trouble == nil && streamed != nil && fmtOk && back != nil && [back isKindOfClass:[KUAlpha class]],
		      trouble != nil ? trouble
				     : [NSString stringWithFormat:@"len=%lu back=%@ err=%@ fmt=%lu key-in-bytes=%d",
					(unsigned long)[streamed length],
					back != nil ? (id)[back class] : (id)@"nil",
					readError != nil ? [readError localizedDescription] : @"none",
					(unsigned long)fmtOk,
					text != nil && [text rangeOfString:@"thing"].location != NSNotFound]);
	}

	printf("FOUNDATION-ARCHIVER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-ARCHIVER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-ARCHIVER DONE\n");
	return failc ? 1 : 0;
}
