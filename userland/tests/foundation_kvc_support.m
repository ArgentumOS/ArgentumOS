/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_kvc, unit 1 of 2 — the fixtures (ARC). docs/design/foundation-plan.md, F9.
 *
 * The classes here are PRIVATE to this unit, and the classes in the other unit are
 * private to it: two units in one process cannot both define `KVCThing`, and the
 * point of the split is that the OBJECT lives here while the LOOKUP happens there.
 * Only the factories cross.
 */

#import "foundation_kvc.h"

/* An object reached through ACCESSORS, with the ivars behind them. */
@interface KVCSupportAccessor : NSObject
{
	NSString *_title;
	NSInteger _code;
	NSArray *_tags;
	NSSet *_tagSet;
}
- (instancetype)initWithTitle:(NSString *)title code:(NSInteger)code;
- (NSString *)title;
- (void)setTitle:(NSString *)title;
- (NSInteger)code;
- (NSArray *)tags;
- (NSSet *)tagSet;
@end

@implementation KVCSupportAccessor

- (instancetype)initWithTitle:(NSString *)title code:(NSInteger)code
{
	if ((self = [super init]) != nil) {
		_title = title;
		_code = code;
		/* THE COLLECTION-VALUED KEYS the four union operators read. `a` and `b` SHARE a tag and `c`
		 * repeats one WITHIN itself, so @unionOfArrays (7) and @distinctUnionOfArrays (3) cannot
		 * agree by accident, and every set built from these has exactly three members. */
		if ([title isEqualToString:@"a"]) {
			_tags = @[@"red", @"green"];
		} else if ([title isEqualToString:@"b"]) {
			_tags = @[@"green", @"blue"];
		} else if ([title isEqualToString:@"c"]) {
			_tags = @[@"blue", @"blue"];
		} else {
			_tags = @[@"blue"];
		}
		_tagSet = [NSSet setWithArray:_tags];
	}
	return self;
}

- (NSString *)title { return _title; }
- (void)setTitle:(NSString *)title { _title = title; }
- (NSInteger)code { return _code; }
- (NSArray *)tags { return _tags; }
- (NSSet *)tagSet { return _tagSet; }

@end

/* An object with NO accessors at all: every lookup here goes through the ivar
 * arm, which is the half of the family that needs the runtime. */
@interface KVCSupportIvars : NSObject
{
	NSString *_name;
	NSInteger _count;
}
- (instancetype)initWithName:(NSString *)name;
@end

@implementation KVCSupportIvars

- (instancetype)initWithName:(NSString *)name
{
	if ((self = [super init]) != nil) {
		_name = name;
		_count = 7;
	}
	return self;
}

@end

NSArray * _Nullable foundation_kvc_items(void)
{
	/* Four items whose `code` values are 10, 20, 20, 30: the DUPLICATE is what
	 * makes @distinctUnionOfObjects measurable (it must answer three), and the
	 * numbers are chosen so @sum is 80 and @avg is 20 exactly. */
	return @[[[KVCSupportAccessor alloc] initWithTitle:@"a" code:10],
		 [[KVCSupportAccessor alloc] initWithTitle:@"b" code:20],
		 [[KVCSupportAccessor alloc] initWithTitle:@"c" code:20],
		 [[KVCSupportAccessor alloc] initWithTitle:@"d" code:30]];
}

id _Nullable foundation_kvc_accessor_object(void)
{
	return [[KVCSupportAccessor alloc] initWithTitle:@"from the other unit" code:42];
}

id _Nullable foundation_kvc_ivar_object(void)
{
	return [[KVCSupportIvars alloc] initWithName:@"no accessors here"];
}
