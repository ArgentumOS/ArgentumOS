/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_sort, unit 1 of 2 — the fixtures (MRR). docs/design/foundation-plan.md, F10.
 *
 * The classes here are PRIVATE to this unit; the check unit has its own. Only the factories and
 * the one descriptor cross, which is the point: a key resolved by NAME across a translation unit
 * can only have been resolved by the runtime — through KVC (F9).
 */

#import "foundation_sort.h"

/*
 * A person whose fields are reached through ACCESSORS, so the descriptor's key resolution runs
 * the KVC accessor arm. `rank` is a SCALAR, which is the arm that has to BOX (F9's probe found
 * that by crashing on it), and `note` is left nil so that the nil-value rule is reachable.
 */
@interface SortPerson : NSObject
{
	NSString *_name;
	NSInteger _rank;
	NSString *_note;
}
- (instancetype)initWithName:(NSString *)name rank:(NSInteger)rank;
- (NSString *)name;
- (NSInteger)rank;
- (nullable NSString *)note;
@end

@implementation SortPerson

- (instancetype)initWithName:(NSString *)name rank:(NSInteger)rank
{
	if ((self = [super init]) != nil) {
		_name = name;
		_rank = rank;
		_note = nil;		/* deliberately: -note is the nil-value case */
	}
	return self;
}

- (NSString *)name { return _name; }
- (NSInteger)rank { return _rank; }
- (nullable NSString *)note { return _note; }

@end

NSArray * _Nullable foundation_sort_people(void)
{
	return @[[[SortPerson alloc] initWithName:@"ann" rank:3],
		 [[SortPerson alloc] initWithName:@"bob" rank:1],
		 [[SortPerson alloc] initWithName:@"ann" rank:2],
		 [[SortPerson alloc] initWithName:@"cid" rank:4]];
}

NSArray * _Nullable foundation_sort_nil_value(void)
{
	/* One element only: the descriptor must refuse to order a value that is not there. */
	return @[[[SortPerson alloc] initWithName:@"solo" rank:1]];
}

NSSortDescriptor * _Nullable foundation_sort_by_name(void)
{
	return [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES];
}
