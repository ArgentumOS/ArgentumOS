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

	printf("FOUNDATION-CODER RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-CODER-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-CODER DONE\n");
	return failc ? 1 : 0;
}
