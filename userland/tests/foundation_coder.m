/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_coder, unit of 1 — F13.12's acceptance for the coder family.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. The archivable class is private to this file.
 *
 * WHAT IT MEASURES, with the numbers in the details:
 *   coder-round-trip-scalars    a name, an integer, a double and a bool across one archive;
 *   coder-round-trip-collections  an array of strings and a dictionary of mixed values;
 *   coder-round-trip-sets       a set and a mutable set, deduplicated BY VALUE, and the archive
 *                               naming the PUBLIC class — plus a counted set's multiplicity counts,
 *                               which ride BESIDE the members because -allObjects does not carry them;
 *   coder-shared-objects        the SAME object referenced twice comes back as ONE object — the
 *                               memo table's whole purpose, asserted by POINTER;
 *   coder-cycle                 an object whose link points back at its parent fails to terminate
 *                               unless an index is reserved before the contents are written;
 *   coder-null-and-nil          a nil link is a nil link again, and an NSNull is an NSNull;
 *   coder-archive-shape         the data IS a property list with $version/$objects/$top, and the
 *                               class entry carries its $classes chain;
 *   coder-base-raises           the abstract NSCoder's doors RAISE rather than answering a zero
 *                               that looks like data.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

/* THE ARCHIVABLE CLASS: both halves of the protocol, and a link that can point anywhere — including
 * back at an ancestor, which is the case an archive has to survive. */
@interface CoderNode : NSObject <NSCoding>
{
	NSString *_name;
	NSInteger _count;
	double _ratio;
	BOOL _flag;
	NSArray *_tags;
	NSDictionary *_meta;
	CoderNode *_link;
}
+ (instancetype)nodeWithName:(NSString *)name count:(NSInteger)count;
- (NSString *)name;
- (NSInteger)count;
- (double)ratio;
- (BOOL)flag;
- (void)setRatio:(double)ratio;
- (void)setFlag:(BOOL)flag;
- (NSArray *)tags;
- (void)setTags:(NSArray *)tags;
- (NSDictionary *)meta;
- (void)setMeta:(NSDictionary *)meta;
- (CoderNode *)link;
- (void)setLink:(CoderNode *)link;
@end

@implementation CoderNode

+ (instancetype)nodeWithName:(NSString *)name count:(NSInteger)count
{
	CoderNode *node = [[self alloc] init];

	node->_name = name;
	node->_count = count;
	return node;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_name forKey:@"name"];
	[coder encodeInteger:_count forKey:@"count"];
	[coder encodeDouble:_ratio forKey:@"ratio"];
	[coder encodeBool:_flag forKey:@"flag"];
	[coder encodeObject:_tags forKey:@"tags"];
	[coder encodeObject:_meta forKey:@"meta"];
	[coder encodeObject:_link forKey:@"link"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	if ((self = [super init]) != nil) {
		_name = [coder decodeObjectForKey:@"name"];
		_count = [coder decodeIntegerForKey:@"count"];
		_ratio = [coder decodeDoubleForKey:@"ratio"];
		_flag = [coder decodeBoolForKey:@"flag"];
		_tags = [coder decodeObjectForKey:@"tags"];
		_meta = [coder decodeObjectForKey:@"meta"];
		_link = [coder decodeObjectForKey:@"link"];
	}
	return self;
}

- (NSString *)name { return _name; }
- (NSInteger)count { return _count; }
- (double)ratio { return _ratio; }
- (BOOL)flag { return _flag; }
- (void)setRatio:(double)ratio { _ratio = ratio; }
- (void)setFlag:(BOOL)flag { _flag = flag; }
- (NSArray *)tags { return _tags; }
- (void)setTags:(NSArray *)tags { _tags = tags; }
- (NSDictionary *)meta { return _meta; }
- (void)setMeta:(NSDictionary *)meta { _meta = meta; }
- (CoderNode *)link { return _link; }
- (void)setLink:(CoderNode *)link { _link = link; }

@end

/* ---- W9's two delegates, which are what the archiver's and unarchiver's doors exist FOR ---- */

/* AN ARCHIVER OBSERVER THAT ALSO SUBSTITUTES: it counts both observation doors, notes whether the two finish
 * doors arrive in order, and REWRITES one value — so the check can prove the substitution REACHED the archive
 * rather than merely that the method was called. */
@interface W9ArchiverDelegate : NSObject <NSKeyedArchiverDelegate>
{
@public
	int willCount;
	int didCount;
	BOOL sawWillFinish;
	BOOL didFinishAfterWill;
}
@end

@implementation W9ArchiverDelegate

- (nullable id)archiver:(NSKeyedArchiver *)archiver willEncodeObject:(id)object
{
	willCount++;
	if ([object isEqual:@"redact-me"]) {
		return @"redacted";	/* THE SUBSTITUTION: what gets encoded is the ANSWER */
	}
	return object;
}

- (nullable id)archiver:(NSKeyedArchiver *)archiver didEncodeObject:(nullable id)object
{
	didCount++;
	return object;
}

- (void)archiverWillFinish:(NSKeyedArchiver *)archiver { sawWillFinish = YES; }
- (void)archiverDidFinish:(NSKeyedArchiver *)archiver { didFinishAfterWill = sawWillFinish; }

@end

/* AN UNARCHIVER OBSERVER THAT RESCUES A CLASS: the archive names `W9VanishedClass`, this process has no such
 * class, and the delegate answers the class to decode INSTEAD. That is the door's purpose — and it is also
 * the door an attacker would push a class the reader DOES have through, which is why the refusal is a
 * first-class answer. */
@interface W9UnarchiverDelegate : NSObject <NSKeyedUnarchiverDelegate>
{
@public
	int cannotCount;
	int didCount;
	BOOL sawWillFinish;
	BOOL didFinishAfterWill;
}
@end

@implementation W9UnarchiverDelegate

- (nullable Class)unarchiver:(NSKeyedUnarchiver *)unarchiver
   cannotDecodeObjectOfClassName:(NSString *)name
	      originalClasses:(NSArray *)classNames
{
	cannotCount++;
	(void)classNames;
	if ([name isEqualToString:@"W9VanishedClass"]) {
		return [CoderNode class];
	}
	return Nil;	/* the refusal */
}

- (nullable id)unarchiver:(NSKeyedUnarchiver *)unarchiver didDecodeObject:(nullable id)object
{
	didCount++;
	return object;
}

- (void)unarchiverWillFinish:(NSKeyedUnarchiver *)unarchiver { sawWillFinish = YES; }
- (void)unarchiverDidFinish:(NSKeyedUnarchiver *)unarchiver { didFinishAfterWill = sawWillFinish; }

@end

/* THE CLASS THE TRANSFORMER MUST REFUSE: an NSCoding class that is NOT among the allowed top-level classes,
 * which makes the malicious-root case the natural one rather than a contrived one. */
@interface W9ForeignRoot : NSObject <NSCoding>
{
	NSString *_payload;
}
- (instancetype)initWithPayload:(NSString *)payload;
- (NSString *)payload;
@end

@implementation W9ForeignRoot

- (instancetype)initWithPayload:(NSString *)payload
{
	self = [super init];
	if (self != nil) {
		_payload = payload;
	}
	return self;
}

- (NSString *)payload { return _payload; }

- (void)encodeWithCoder:(NSCoder *)coder { [coder encodeObject:_payload forKey:@"payload"]; }
- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		_payload = [coder decodeObjectForKey:@"payload"];
	}
	return self;
}

@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-CODER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-CODER %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* A BYTE-RUN SEARCH, spelled out rather than reaching for `memmem`, so the probe depends on nothing the
 * guest's libc may or may not declare. The archive is plist TEXT and a class name is ASCII, so this is the
 * whole instrument the "which class did the archive name" checks need. */
static int fn_contains(const unsigned char *haystack, unsigned long length, const char *needle)
{
	unsigned long n = 0;
	unsigned long i;

	while (needle[n] != '\0') {
		n++;
	}
	if (n == 0 || length < n) {
		return 0;
	}
	for (i = 0; i + n <= length; i++) {
		unsigned long j;

		for (j = 0; j < n; j++) {
			if (haystack[i + j] != (unsigned char)needle[j]) {
				break;
			}
		}
		if (j == n) {
			return 1;
		}
	}
	return 0;
}

/* A ROUND TRIP, in one place: archive the object and bring it back. */
static id fn_round_trip(id object)
{
	NSData *data = [NSKeyedArchiver archivedDataWithRootObject:object];

	return data != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:data] : nil;
}

int main(void)
{
	{
		CoderNode *node = [CoderNode nodeWithName:@"first" count:41];
		CoderNode *back;

		[node setRatio:2.5];
		[node setFlag:YES];
		back = fn_round_trip(node);
		check("coder-round-trip-scalars",
		      back != nil && [back isKindOfClass:[CoderNode class]] &&
		      [[back name] isEqualToString:@"first"] && [back count] == 41 &&
		      [back ratio] == 2.5 && [back flag],
		      [NSString stringWithFormat:@"name=%@ count=%ld ratio=%g flag=%d",
			back != nil ? [back name] : @"(nil)",
			(long)(back != nil ? [back count] : -1),
			back != nil ? [back ratio] : 0.0, back != nil ? (int)[back flag] : -1]);
	}

	{
		CoderNode *node = [CoderNode nodeWithName:@"collections" count:1];
		CoderNode *back;

		[node setTags:@[@"red", @"green", @"blue"]];
		[node setMeta:@{ @"answer" : @42, @"note" : @"text" }];
		back = fn_round_trip(node);
		check("coder-round-trip-collections",
		      back != nil && [[back tags] count] == 3 &&
		      [[[back tags] objectAtIndex:1] isEqualToString:@"green"] &&
		      [[back meta] count] == 2 &&
		      [[[back meta] objectForKey:@"answer"] integerValue] == 42 &&
		      [[[back meta] objectForKey:@"note"] isEqualToString:@"text"],
		      [NSString stringWithFormat:@"tags=%@ meta=%@",
			back != nil ? [back tags] : @"(nil)",
			back != nil ? [back meta] : @"(nil)"]);
	}

	{
		/* A SET AND A MUTABLE SET. A set is a collection the archiver knows by KIND, like an array
		 * or a dictionary — which is why its members ride under the same `NS.objects` key — and the
		 * two things this check is for are the members surviving BY VALUE (the duplicate "two" is
		 * one member) and the archive carrying the PUBLIC class name, which is §C.4's hinge
		 * measured on this family's own bytes. */
		NSSet *plain = [NSSet setWithArray:@[@"one", @"two", @"three"]];
		NSSet *withDuplicate = [NSSet setWithArray:@[@"one", @"two", @"three", @"two"]];
		NSMutableSet *mutableSet = [NSMutableSet setWithArray:@[@"a", @"b"]];
		NSSet *backPlain = fn_round_trip(plain);
		NSMutableSet *backMutable = fn_round_trip(mutableSet);
		NSData *setArchive = [NSKeyedArchiver archivedDataWithRootObject:withDuplicate];

		check("coder-round-trip-sets",
		      backPlain != nil && [backPlain isKindOfClass:[NSSet class]] &&
		      [backPlain count] == 3 && [backPlain containsObject:@"two"] &&
		      [backPlain member:@"three"] != nil &&
		      backMutable != nil && [backMutable isKindOfClass:[NSMutableSet class]] &&
		      [backMutable count] == 2 && [backMutable containsObject:@"a"],
		      [NSString stringWithFormat:@"set=%@ mutable=%@",
			backPlain != nil ? backPlain : @"(nil)",
			backMutable != nil ? backMutable : @"(nil)"]);
		check("the-set-archive-names-the-public-class",
		      setArchive != nil &&
		      fn_contains([setArchive bytes], [setArchive length], "NSSet") &&
		      !fn_contains([setArchive bytes], [setArchive length], "AGSet"),
		      @"a set's archive must name NSSet and no private concrete class");
	}

	{
		/* AN NSCountedSet IS THE ONE SET WHOSE MEMBERS ALONE ARE NOT ITS STATE: `-allObjects` and
		 * `-count` describe it by DISTINCT members, so the multiplicities are a SECOND payload and
		 * this is the check that they make the round trip rather than being silently dropped. */
		NSCountedSet *counted = [NSCountedSet set];

		[counted addObject:@"x"];
		[counted addObject:@"x"];
		[counted addObject:@"x"];
		[counted addObject:@"y"];
		{
			NSCountedSet *back = fn_round_trip(counted);

			check("coder-round-trip-counted-set",
			      back != nil && [back isKindOfClass:[NSCountedSet class]] &&
			      [back count] == 2 && [back countForObject:@"x"] == 3 &&
			      [back countForObject:@"y"] == 1 && [back countForObject:@"z"] == 0,
			      [NSString stringWithFormat:@"distinct=%lu x=%lu y=%lu",
				(unsigned long)(back != nil ? [back count] : 0),
				(unsigned long)(back != nil ? [back countForObject:@"x"] : 0),
				(unsigned long)(back != nil ? [back countForObject:@"y"] : 0)]);
		}
	}

	{
		/* AN ORDERED SET: the members are UNIQUE like a set's and they have an ORDER unlike one's —
		 * which is the property this check exists for, and the reason it asserts the member ARRAY
		 * rather than membership. The entry is array-shaped (`NS.objects`), so the order IS the
		 * `NS.objects` sequence, and the reader must re-add in exactly that order. */
		NSOrderedSet *ordered = [NSOrderedSet orderedSetWithArray:@[@"charlie", @"alpha", @"bravo"]];
		NSOrderedSet *back = fn_round_trip(ordered);
		NSData *orderedArchive = [NSKeyedArchiver archivedDataWithRootObject:ordered];

		check("coder-round-trip-ordered-set",
		      back != nil && [back isKindOfClass:[NSOrderedSet class]] && [back count] == 3 &&
		      [[back array] isEqualToArray:@[@"charlie", @"alpha", @"bravo"]] &&
		      orderedArchive != nil &&
		      fn_contains([orderedArchive bytes], [orderedArchive length], "NSOrderedSet") &&
		      !fn_contains([orderedArchive bytes], [orderedArchive length], "AGOrderedSet"),
		      [NSString stringWithFormat:@"array=%@ namesPublic=%d",
			back != nil ? [back array] : @"(nil)",
			(orderedArchive != nil &&
			 fn_contains([orderedArchive bytes], [orderedArchive length], "NSOrderedSet"))]);
	}

	{
		CoderNode *shared = [CoderNode nodeWithName:@"shared" count:7];
		CoderNode *left = [CoderNode nodeWithName:@"left" count:1];
		CoderNode *right = [CoderNode nodeWithName:@"right" count:2];
		id back;			/* an ARRAY of nodes comes back, not a node */

		[left setLink:shared];
		[right setLink:shared];
		shared = nil;			/* the graph below is what matters, not this handle */
		/* `id`, because what comes back is an ARRAY and not another node — the round trip does not
		 * promise the root's class, and the check asks about its members. */
		back = fn_round_trip(@[left, right]);
		check("coder-shared-objects",
		      back != nil && [back count] == 2 &&
		      [[back objectAtIndex:0] link] != nil &&
		      [[back objectAtIndex:0] link] == [[back objectAtIndex:1] link] &&
		      [[[[back objectAtIndex:0] link] name] isEqualToString:@"shared"],
		      [NSString stringWithFormat:@"sameObject=%d name=%@",
			(int)(back != nil && [back count] == 2 &&
			      [[back objectAtIndex:0] link] == [[back objectAtIndex:1] link]),
			back != nil && [back count] == 2 ? [[[back objectAtIndex:0] link] name] : @"(nil)"]);
	}

	{
		CoderNode *parent = [CoderNode nodeWithName:@"parent" count:1];
		CoderNode *child = [CoderNode nodeWithName:@"child" count:2];
		CoderNode *back;
		CoderNode *backChild;

		[parent setLink:child];
		[child setLink:parent];		/* A CYCLE: neither can be finished before the other starts */
		back = fn_round_trip(parent);
		backChild = back != nil ? [back link] : nil;
		check("coder-cycle",
		      back != nil && backChild != nil &&
		      [[backChild name] isEqualToString:@"child"] &&
		      [backChild link] == back,
		      [NSString stringWithFormat:@"child=%@ pointsBack=%d",
			backChild != nil ? [backChild name] : @"(nil)",
			(int)(backChild != nil && [backChild link] == back)]);
	}

	{
		CoderNode *node = [CoderNode nodeWithName:@"holes" count:1];
		CoderNode *back;

		[node setTags:@[@"kept", [NSNull null]]];
		back = fn_round_trip(node);
		check("coder-null-and-nil",
		      back != nil && [back link] == nil && [[back tags] count] == 2 &&
		      [[[[back tags] objectAtIndex:1] description] isEqualToString:@"<null>"],
		      [NSString stringWithFormat:@"link=%@ tags=%@",
			back != nil && [back link] != nil ? @"not nil" : @"nil",
			back != nil ? [back tags] : @"(nil)"]);
	}

	{
		CoderNode *node = [CoderNode nodeWithName:@"shape" count:1];
		NSData *data = [NSKeyedArchiver archivedDataWithRootObject:node];
		id plist = data != nil
			 ? [NSPropertyListSerialization propertyListWithData:data options:0
								      format:NULL error:NULL]
			 : nil;
		NSArray *objects = nil;
		NSDictionary *classEntry = nil;

		if ([plist isKindOfClass:[NSDictionary class]]) {
			objects = [(NSDictionary *)plist objectForKey:@"$objects"];
		}
		if (objects != nil && [objects count] > 1 &&
		    [[objects objectAtIndex:1] isKindOfClass:[NSDictionary class]]) {
			id classSlot = [[objects objectAtIndex:1] objectForKey:@"$class"];

			if ([classSlot isKindOfClass:[NSDictionary class]]) {
				id index = [classSlot objectForKey:@"$ref"];

				if (index != nil) {
					classEntry = [objects objectAtIndex:[index unsignedIntegerValue]];
				}
			}
		}
		check("coder-archive-shape",
		      plist != nil && [plist isKindOfClass:[NSDictionary class]] &&
		      [[(NSDictionary *)plist objectForKey:@"$version"] integerValue] == 1 &&
		      objects != nil && [objects count] >= 3 &&
		      [[objects objectAtIndex:0] isEqualToString:@"$null"] &&
		      [[(NSDictionary *)plist objectForKey:@"$top"] objectForKey:@"root"] != nil &&
		      classEntry != nil &&
		      [[classEntry objectForKey:@"$classname"] isEqualToString:@"CoderNode"] &&
		      [[classEntry objectForKey:@"$classes"] count] >= 2,
		      [NSString stringWithFormat:@"objects=%lu class=%@ chain=%@",
			(unsigned long)(objects != nil ? [objects count] : 0),
			classEntry != nil ? [classEntry objectForKey:@"$classname"] : @"(none)",
			classEntry != nil ? [classEntry objectForKey:@"$classes"] : @"(none)"]);
	}

	{
		NSCoder *abstract = [[NSCoder alloc] init];
		BOOL raised = NO;

		@try {
			[abstract encodeObject:@"anything" forKey:@"key"];
		} @catch (NSException *e) {
			(void)e;
			raised = YES;
		}
		check("coder-base-raises",
		      abstract != nil && raised,
		      raised ? @"raised" : @"the abstract NSCoder accepted a write");
	}

	/* ---- W9: the delegates, Cocoa's instance flow, and the secure transformer ---- */
	{
		/*
		 * THE ARCHIVER'S DOORS, through the INSTANCE FLOW — the only way a delegate can be reached, and
		 * the reason this unit implemented that flow: a delegate belongs to an instance.
		 *
		 * The substitution is asserted THROUGH THE ARCHIVE rather than by the call count, because a door
		 * that is called and ignored looks exactly like one that works.
		 */
		NSMutableData *buffer = [[NSMutableData alloc] init];
		NSKeyedArchiver *archiver = [[NSKeyedArchiver alloc] initForWritingWithMutableData:buffer];
		W9ArchiverDelegate *archiverDelegate = [[W9ArchiverDelegate alloc] init];
		NSArray *payload = @[@"keep-me", @"redact-me"];
		NSArray *decoded;

		[archiver setDelegate:archiverDelegate];
		[archiver encodeObject:payload forKey:@"root"];
		[archiver finishEncoding];
		decoded = [NSKeyedUnarchiver unarchiveObjectWithData:buffer];

		check("coder-archiver-delegate",
		      archiverDelegate->willCount > 0 &&
		      archiverDelegate->didCount >= archiverDelegate->willCount &&
		      archiverDelegate->didFinishAfterWill &&
		      [decoded count] == 2 &&
		      [[decoded objectAtIndex:0] isEqual:@"keep-me"] &&
		      /* THE SUBSTITUTION IS IN THE ARCHIVE: "redact-me" was never written, "redacted" was. */
		      [[decoded objectAtIndex:1] isEqual:@"redacted"],
		      [NSString stringWithFormat:@"will=%d did=%d finishOrder=%d back=%@",
			 archiverDelegate->willCount, archiverDelegate->didCount,
			 archiverDelegate->didFinishAfterWill, decoded]);

		/* THE INSTANCE FLOW ITSELF: the archive is IN THE CALLER'S BUFFER, and a real object graph
		 * survives it. Before W9 the buffer was accepted and left EMPTY (measured: 0 bytes). */
		{
			NSMutableData *second = [[NSMutableData alloc] init];
			NSKeyedArchiver *writer = [[NSKeyedArchiver alloc] initForWritingWithMutableData:second];
			CoderNode *node = [CoderNode nodeWithName:@"through-the-flow" count:7];
			CoderNode *back;

			[writer encodeObject:node forKey:@"root"];
			[writer finishEncoding];
			back = [NSKeyedUnarchiver unarchiveObjectWithData:second];
			check("coder-instance-flow",
			      [second length] > 0 && [back isKindOfClass:[CoderNode class]] &&
			      [[back name] isEqualToString:@"through-the-flow"] && [back count] == 7,
			      [NSString stringWithFormat:@"bytes=%lu name=%@",
				(unsigned long)[second length],
				[back isKindOfClass:[CoderNode class]] ? [back name] : @"(wrong class)"]);
		}

		/*
		 * THE UNARCHIVER'S CLASS DOOR: an archive that NAMES A CLASS THIS PROCESS DOES NOT HAVE. The name is
		 * broken in the archive's own bytes, so the reader really is handed one it cannot resolve — and the
		 * delegate answers the class to use instead.
		 */
		{
			NSData *good = [NSKeyedArchiver archivedDataWithRootObject:
					[CoderNode nodeWithName:@"rescued" count:3]];
			NSString *xml = [[NSString alloc] initWithData:good encoding:NSUTF8StringEncoding];
			NSString *broken = [xml stringByReplacingOccurrencesOfString:@"CoderNode"
									withString:@"W9VanishedClass"];
			NSData *brokenData = [broken dataUsingEncoding:NSUTF8StringEncoding];
			NSKeyedUnarchiver *reader = [[NSKeyedUnarchiver alloc] initForReadingWithData:brokenData];
			W9UnarchiverDelegate *unarchiverDelegate = [[W9UnarchiverDelegate alloc] init];
			id rescued;

			[reader setDelegate:unarchiverDelegate];
			rescued = [reader decodeObjectForKey:@"root"];
			[reader finishDecoding];
			check("coder-unarchiver-delegate",
			      unarchiverDelegate->cannotCount > 0 &&
			      unarchiverDelegate->didFinishAfterWill &&
			      [rescued isKindOfClass:[CoderNode class]] &&
			      [[rescued name] isEqualToString:@"rescued"] && [rescued count] == 3,
			      [NSString stringWithFormat:@"cannot=%d finishOrder=%d rescued=%@",
				unarchiverDelegate->cannotCount, unarchiverDelegate->didFinishAfterWill,
				[rescued isKindOfClass:[CoderNode class]] ? [rescued name] : @"(not rescued)"]);
		}
	}

	{
		/*
		 * THE SECURE TRANSFORMER, WHERE THE REFUSAL IS THE CHECK: an allowed root round-trips both ways, and
		 * an archive whose ROOT IS NOT ALLOWED answers nil. The second half is why the class exists —
		 * `W9ForeignRoot` is a real NSCoding class, so without the check this transformer would instantiate
		 * whatever an archive named.
		 *
		 * The NAME is asserted to resolve with no registration, because that is how a property list refers
		 * to it: the constant spells the class.
		 */
		NSSecureUnarchiveFromDataTransformer *transformer =
			[[NSSecureUnarchiveFromDataTransformer alloc] init];
		NSData *allowedData = [NSKeyedArchiver archivedDataWithRootObject:@[@"one", @"two"]];
		NSData *foreignData = [NSKeyedArchiver archivedDataWithRootObject:
					[[W9ForeignRoot alloc] initWithPayload:@"do-not-instantiate"]];
		id allowedBack = [transformer transformedValue:allowedData];
		id foreignBack = [transformer transformedValue:foreignData];
		NSData *reArchived = [transformer reverseTransformedValue:@[@"one", @"two"]];
		NSValueTransformer *byName = [NSValueTransformer valueTransformerForName:
						NSSecureUnarchiveFromDataTransformerName];

		check("value-transformer-secure-unarchive",
		      [[transformer class] allowsReverseTransformation] &&
		      [allowedBack isKindOfClass:[NSArray class]] && [allowedBack count] == 2 &&
		      /* THE REFUSAL: a root outside +allowedTopLevelClasses answers nil. */
		      foreignBack == nil &&
		      reArchived != nil && [reArchived length] > 0 &&
		      /* AND THE NAME RESOLVES WITHOUT REGISTRATION, because the constant IS the class name. */
		      [byName isKindOfClass:[NSSecureUnarchiveFromDataTransformer class]] &&
		      [[[transformer class] allowedTopLevelClasses] count] > 0,
		      [NSString stringWithFormat:@"allowedCount=%lu foreignIsNil=%d nameOk=%d",
			(unsigned long)[[[transformer class] allowedTopLevelClasses] count],
			(int)(foreignBack == nil),
			(int)([byName isKindOfClass:[NSSecureUnarchiveFromDataTransformer class]])]);
	}

	{
		/* THE VALUE TYPES' OWN DOORS (§63.22), DRIVEN DIRECTLY — WHICH IS THE ONLY WAY TO REACH THEM, and the
		 * reason is worth stating in the probe itself: `NSKeyedArchiver` writes these classes INLINE
		 * (`fn_is_value_type`, Apple's own shape for them), so `-encodeObject:` never consults a door. A caller
		 * holding a coder may; `-encodeWithCoder:`/`-initWithCoder:` ARE the NSCoding protocol's methods, and
		 * that is what this block uses.
		 *
		 * ONE ARCHIVE PER VALUE, because a door's keys are FIXED — every string uses "NS.string" — so two values
		 * through one archiver would collide on the very keys that make the pair work. */
#define FN_CODER_DOOR_ROUND_TRIP(VALUE, CLS, VAR) \
		do { \
			NSMutableData *fnData = [[NSMutableData alloc] init]; \
			NSKeyedArchiver *fnWriter = [[NSKeyedArchiver alloc] initForWritingWithMutableData:fnData]; \
			[(VALUE) encodeWithCoder:fnWriter]; \
			[fnWriter finishEncoding]; \
			NSKeyedUnarchiver *fnReader = [[NSKeyedUnarchiver alloc] initForReadingWithData:fnData]; \
			VAR = [[CLS alloc] initWithCoder:fnReader]; \
		} while (0)

		NSString *strBack;
		NSNumber *numBack;
		NSNumber *numBack2;		/* the second number is a FLOAT, which is what proves the type travels */
		NSValue *valBack;
		NSLocale *locBack;
		NSDate *dateBack;
		NSData *dataBack;

		FN_CODER_DOOR_ROUND_TRIP(@"a string, with ✓ and 42", [NSString class], strBack);
		FN_CODER_DOOR_ROUND_TRIP(@((long long)-9007199254740993LL), [NSNumber class], numBack);
		FN_CODER_DOOR_ROUND_TRIP(@2.5f, [NSNumber class], numBack2);
		FN_CODER_DOOR_ROUND_TRIP([NSValue valueWithRange:NSMakeRange(3, 4)], [NSValue class], valBack);
		FN_CODER_DOOR_ROUND_TRIP([[NSLocale alloc] initWithLocaleIdentifier:@"tr_TR"],
					 [NSLocale class], locBack);
		FN_CODER_DOOR_ROUND_TRIP([NSDate dateWithTimeIntervalSince1970:1234.5], [NSDate class], dateBack);
		FN_CODER_DOOR_ROUND_TRIP([NSData dataWithBytes:"bytes" length:5], [NSData class], dataBack);
#undef FN_CODER_DOOR_ROUND_TRIP

		check("the-four-value-type-doors-round-trip",
		      strBack != nil && [strBack isEqualToString:@"a string, with ✓ and 42"] &&
		      /* THE TYPE SURVIVES THE NUMBER, which is what the type key in the door is FOR: a 54-bit integer
		       * and a float both come back as what they went in as, not as doubles. The encoding is compared by
		       * ITS FIRST CHARACTER rather than with `strcmp`, so the probe needs no `<string.h>`. */
		      numBack != nil && [numBack longLongValue] == -9007199254740993LL &&
		      [numBack objCType][0] == 'q' &&
		      numBack2 != nil && [numBack2 floatValue] == 2.5f &&
		      [numBack2 objCType][0] == 'f' &&
		      valBack != nil && [valBack isKindOfClass:[NSValue class]] &&
		      [valBack rangeValue].location == 3 && [valBack rangeValue].length == 4 &&
		      locBack != nil && [[locBack localeIdentifier] isEqualToString:@"tr_TR"],
		      [NSString stringWithFormat:@"string=%@ int=%@(%s/%lld) float=%@(%s) value=%@ locale=%@",
			strBack != nil ? strBack : @"(nil)",
			numBack != nil ? numBack : @"(nil)",
			numBack != nil ? [numBack objCType] : "?",
			numBack != nil ? [numBack longLongValue] : 0LL,
			numBack2 != nil ? numBack2 : @"(nil)",
			numBack2 != nil ? [numBack2 objCType] : "?",
			valBack != nil ? valBack : @"(nil)",
			locBack != nil ? [locBack localeIdentifier] : @"(nil)"]);

		/* AND THESE TWO WERE SHIPPED WITH NO CHECK AT ALL UNTIL NOW: NSDate and NSData have had coder doors
		 * since their own sections, exercised by nothing, because the archive's inline path never reaches them.
		 * They are not new work — they are new COVERAGE, and the same driver proves they were right. */
		check("the-date-and-data-doors-round-trip",
		      dateBack != nil && [dateBack timeIntervalSince1970] == 1234.5 &&
		      dataBack != nil && [dataBack length] == 5 &&
		      /* THE CLASS'S OWN EQUALITY, rather than memcmp: it is the same question and it keeps this probe
		       * free of `<string.h>` (the first draft's memcmp was an implicit-declaration error). */
		      [dataBack isEqualToData:[NSData dataWithBytes:"bytes" length:5]],
		      [NSString stringWithFormat:@"date=%g dataLength=%lu",
			dateBack != nil ? [dateBack timeIntervalSince1970] : -1.0,
			(unsigned long)(dataBack != nil ? [dataBack length] : 0)]);
	}

	/* ---- THE TYPE-CHECKED OBJECT DOORS, THE DECODE-ERROR DOORS, AND THE SECURE GATE ---------------- */
	{
		/* ONE ARCHIVE, READ THROUGH THE INSTANCE FLOW, so the keys are the caller's: a top-level array,
		 * a top-level string, and the two FIXED-WIDTH integers (whose width is the point — the same
		 * 54-bit value that a double would round must come back exactly). */
		NSMutableData *buffer = [[NSMutableData alloc] init];
		NSKeyedArchiver *writer = [[NSKeyedArchiver alloc] initForWritingWithMutableData:buffer];
		NSKeyedUnarchiver *reader;

		[writer encodeObject:@[@"one", @"two"] forKey:@"arr"];
		[writer encodeObject:@"hello" forKey:@"str"];
		[writer encodeInt32:(int32_t)123456 forKey:@"i32"];
		[writer encodeInt64:(int64_t)9007199254740993LL forKey:@"i64"];
		[writer finishEncoding];

		reader = [[NSKeyedUnarchiver alloc] initForReadingWithData:buffer];
		{
			NSArray *arr = [reader decodeObjectOfClass:[NSArray class] forKey:@"arr"];
			NSString *str = [reader decodeObjectOfClass:[NSString class] forKey:@"str"];
			int32_t i32 = [reader decodeInt32ForKey:@"i32"];
			int64_t i64 = [reader decodeInt64ForKey:@"i64"];

			check("coder-typed-object-doors",
			      [arr isKindOfClass:[NSArray class]] && [arr count] == 2 &&
			      [[arr objectAtIndex:0] isEqualToString:@"one"] &&
			      [str isEqualToString:@"hello"] &&
			      i32 == (int32_t)123456 && i64 == (int64_t)9007199254740993LL,
			      [NSString stringWithFormat:@"arr=%@ str=%@ i32=%d i64=%lld",
			       arr, str, (int) i32, (long long) i64]);
		}

		/* THE REFUSAL IS THE FEATURE: the array key is NOT a string, and under the DEFAULT policy
		 * (raise) the door raises rather than answering the wrong class. */
		{
			BOOL raised = NO;

			@try {
				(void)[reader decodeObjectOfClass:[NSString class] forKey:@"arr"];
			} @catch (NSException *e) {
				(void)e;
				raised = YES;
			}
			check("coder-typed-object-door-refusal", raised,
			      raised ? @"raised" : @"a wrong-class decode was accepted");
		}

		/* THE POLICY DOOR: under SetErrorAndReturn the SAME refusal is a VALUE — nil, with -error set. */
		{
			id wrong;

			[reader setDecodingFailurePolicy:NSDecodingFailurePolicySetErrorAndReturn];
			wrong = [reader decodeObjectOfClass:[NSString class] forKey:@"arr"];
			check("coder-decode-failure-policy",
			      wrong == nil && [reader error] != nil,
			      [NSString stringWithFormat:@"value=%@ error=%@", wrong, [reader error]]);
		}

		/* THE TOP-LEVEL ERROR DOORS: a key that names nothing is nil + NSError; a key that names
		 * something decodes with no error. */
		{
			NSError *missing = nil;
			NSError *present = nil;
			id nothing = [reader decodeTopLevelObjectForKey:@"nope" error:&missing];
			id something = [reader decodeTopLevelObjectForKey:@"str" error:&present];

			check("coder-top-level-error-door",
			      nothing == nil && missing != nil &&
			      [something isEqualToString:@"hello"] && present == nil,
			      [NSString stringWithFormat:@"missing=%@ present=%@", missing, something]);
		}
	}

	{
		/* -decodeTopLevelObjectAndReturnError: IS THE ROOT'S SPELLING, and the class method writes its
		 * root under "root", so an archive from +archivedDataWithRootObject: reads back through it. */
		NSData *data = [NSKeyedArchiver archivedDataWithRootObject:
				[CoderNode nodeWithName:@"root-door" count:9]];
		NSKeyedUnarchiver *reader = [[NSKeyedUnarchiver alloc] initForReadingWithData:data];
		NSError *error = nil;
		id root = [reader decodeTopLevelObjectAndReturnError:&error];

		check("coder-top-level-root-door",
		      root != nil && [root isKindOfClass:[CoderNode class]] &&
		      [[root name] isEqualToString:@"root-door"] && [root count] == 9 && error == nil,
		      [NSString stringWithFormat:@"root=%@ error=%@",
		       [root isKindOfClass:[CoderNode class]] ? [root name] : @"(wrong class)", error]);
	}

	{
		/* THE COLLECTION-CLASS DOORS: the array and the dictionary each checked element-by-element. */
		NSMutableData *buffer = [[NSMutableData alloc] init];
		NSKeyedArchiver *writer = [[NSKeyedArchiver alloc] initForWritingWithMutableData:buffer];
		NSKeyedUnarchiver *reader;
		NSArray *arr;
		NSDictionary *dict;

		[writer encodeObject:(@[@"a", @"b", @"c"]) forKey:@"arr"];
		[writer encodeObject:(@{ @"k" : @"v" }) forKey:@"dict"];
		[writer finishEncoding];

		reader = [[NSKeyedUnarchiver alloc] initForReadingWithData:buffer];
		arr = [reader decodeArrayOfObjectsOfClass:[NSString class] forKey:@"arr"];
		dict = [reader decodeDictionaryWithKeysOfClass:[NSString class]
					       objectsOfClass:[NSString class]
						       forKey:@"dict"];

		check("coder-collection-class-doors",
		      [arr isKindOfClass:[NSArray class]] && [arr count] == 3 &&
		      [dict isKindOfClass:[NSDictionary class]] && [dict count] == 1 &&
		      [[dict objectForKey:@"k"] isEqualToString:@"v"],
		      [NSString stringWithFormat:@"arr=%@ dict=%@", arr, dict]);
	}

	{
		/* SECURE CODING, TURNED ON: an archive whose root class does not claim NSSecureCoding is
		 * REFUSED. CoderNode adopts NSCoding only, so this is the refusal and not a contrived one — the
		 * enforcement NSCoding.h used to say the unarchiver did not yet perform. */
		NSData *data = [NSKeyedArchiver archivedDataWithRootObject:
				[CoderNode nodeWithName:@"secure" count:1]];
		NSKeyedUnarchiver *reader = [[NSKeyedUnarchiver alloc] initForReadingWithData:data];
		BOOL raised = NO;

		[reader setRequiresSecureCoding:YES];
		@try {
			(void)[reader decodeObjectForKey:@"root"];
		} @catch (NSException *e) {
			(void)e;
			raised = YES;
		}
		check("coder-secure-coding-gate", raised,
		      raised ? @"refused" : @"a non-NSSecureCoding class was decoded under secure coding");
	}

	{
		/* -encodeConditionalObject:forKey: WRITES THE REFERENCE ONLY WHEN THE OBJECT IS ALREADY IN THE
		 * ARCHIVE: "shared" is encoded normally first, so its conditional key gets it; "stranger" was
		 * never encoded, so its conditional key writes nil. */
		CoderNode *shared = [CoderNode nodeWithName:@"shared" count:1];
		CoderNode *stranger = [CoderNode nodeWithName:@"stranger" count:2];
		NSMutableData *buffer = [[NSMutableData alloc] init];
		NSKeyedArchiver *writer = [[NSKeyedArchiver alloc] initForWritingWithMutableData:buffer];
		NSKeyedUnarchiver *reader;
		id again;

		[writer encodeObject:shared forKey:@"first"];
		[writer encodeConditionalObject:shared forKey:@"again"];
		[writer encodeConditionalObject:stranger forKey:@"unseen"];
		[writer finishEncoding];

		reader = [[NSKeyedUnarchiver alloc] initForReadingWithData:buffer];
		again = [reader decodeObjectForKey:@"again"];
		check("coder-conditional-object",
		      again != nil && [again isKindOfClass:[CoderNode class]] &&
		      [[again name] isEqualToString:@"shared"] &&
		      [reader decodeObjectForKey:@"unseen"] == nil,
		      [NSString stringWithFormat:@"again=%@ unseenIsNil=%d",
		       [again isKindOfClass:[CoderNode class]] ? [again name] : @"(nil)",
		       (int)([reader decodeObjectForKey:@"unseen"] == nil)]);
	}

	printf("FOUNDATION-CODER RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-CODER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CODER DONE\n");
	return failc ? 1 : 0;
}
