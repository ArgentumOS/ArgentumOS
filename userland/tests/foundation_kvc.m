/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_kvc, unit 2 of 2 — the checks (ARC). docs/design/foundation-plan.md, F9.
 *
 *   kvc-accessors    -get<Key>/-<key>/-is<Key>, and -set<Key>:, with case folded
 *   kvc-ivar         an object with NO accessors: the runtime's ivar arm, scalar too
 *   kvc-scalar-accessor  a SCALAR accessor PAIR: the read boxed, the write unboxed
 *                    (and a nil through the setter refused rather than cast)
 *   kvc-ivar-super   an ivar a SUPERCLASS declared — the case that tells an
 *                    absolute ivar offset from an inherited-view one
 *   kvc-nil-ivar     an object ivar holding nil is FOUND (it answers nil), while
 *                    the next spelling never gets a chance to raise
 *   kvc-keypath      a key path is the same lookup one dot at a time
 *   kvc-operators    the operators that are folds
 *   kvc-collections  the array MAPS and the dictionary LOOKS UP (or folds on @)
 *   kvc-undefined    THE HONEST DEFAULTS: an undefined key raises, a nil through a
 *                    scalar raises, and a subclass's hook is what answers instead
 *   kvc-validate     the -validate<Key>:error: rule, present and absent
 *   kvc-refusals     KVO, the mutable proxies and -takeValue:forKey: are ABSENT
 *   cross-tu         an object built in the other unit answers here, by name
 */

#import "foundation_kvc.h"
#include <stdio.h>
#include <string.h>
#import <objc/runtime.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-KVC %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-KVC %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* THE DETAIL CARRIES THE MEASUREMENT (§9's lesson): a failure says what the value
 * actually was, and the caller names the clause most likely to have given way. */
static const char *fn_why(id value)
{
	static char why[200];

	if (value == nil) {
		snprintf(why, sizeof why, "(nil)");
	} else if ([value isKindOfClass:[NSString class]]) {
		snprintf(why, sizeof why, "\"%s\"", [(NSString *)value UTF8String]);
	} else if ([value isKindOfClass:[NSNumber class]]) {
		snprintf(why, sizeof why, "%s", [[value description] UTF8String]);
	} else {
		snprintf(why, sizeof why, "<%s>", [[value description] UTF8String]);
	}
	return why;
}

/* An object with accessors AND the ivars behind them, so a read can be told apart
 * from a write that did not take. */
@interface KVCProbeAccessors : NSObject
{
	NSString *_title;
}
- (instancetype)init;
- (NSString *)title;
- (void)setTitle:(NSString *)title;
@end

@implementation KVCProbeAccessors
- (instancetype)init
{
	if ((self = [super init]) != nil) {
		_title = @"initial";
	}
	return self;
}
- (NSString *)title { return _title; }
- (void)setTitle:(NSString *)title { _title = title; }
@end

/* No accessors: id and SCALAR ivars, and both are part of the ivar arm. */
@interface KVCProbeIvars : NSObject
{
	NSString *_name;
	NSInteger _count;
	BOOL _flag;
}
@end

@implementation KVCProbeIvars
@end

/* A SCALAR accessor PAIR, which is the case the first run of this probe died on:
 * -valueForKey: called -amount through -performSelector: (typed as returning `id`)
 * and read the number 20 as a pointer, and -setValue:forKey: would have cast the
 * boxed value straight into the slot. Both directions box/unbox here. */
@interface KVCProbeScalar : NSObject
{
	NSInteger _amount;
}
- (NSInteger)amount;
- (void)setAmount:(NSInteger)amount;
@end

@implementation KVCProbeScalar
- (NSInteger)amount { return _amount; }
- (void)setAmount:(NSInteger)amount { _amount = amount; }
@end

/* The superclass/subclass pair for the ivar-offset case. */
@interface KVCProbeBase : NSObject
{
	NSString *_shared;
}
@end
@implementation KVCProbeBase
@end

@interface KVCProbeChild : KVCProbeBase
{
	NSString *_own;
}
@end
@implementation KVCProbeChild
@end

/* Holds an object, for key paths. */
@interface KVCProbeNested : NSObject
{
	KVCProbeAccessors *_inner;
}
- (instancetype)init;
@end
@implementation KVCProbeNested
- (instancetype)init
{
	if ((self = [super init]) != nil) {
		_inner = [[KVCProbeAccessors alloc] init];
	}
	return self;
}
@end

/* A subclass that ANSWERS what the default raises on — proving the hook is called
 * rather than being an unimplemented selector. */
@interface KVCProbeHook : NSObject
@end
@implementation KVCProbeHook
- (id)valueForUndefinedKey:(NSString *)key
{
	return [NSString stringWithFormat:@"hooked:%@", key];
}
- (void)setValue:(id)value forUndefinedKey:(NSString *)key
{
	/* Deliberately silent for the write side: the probe asserts it did not raise. */
	(void)value;
	(void)key;
}
@end

/* The validation rule: a class MAY have -validate<Key>:error:. */
@interface KVCProbeValidator : NSObject
@end
@implementation KVCProbeValidator
- (BOOL)validateAge:(id *)ioValue error:(NSError **)outError
{
	if ([*ioValue intValue] < 0) {
		if (outError != NULL) {
			*outError = [NSError errorWithDomain:@"probe" code:1 userInfo:nil];
		}
		return NO;
	}
	return YES;
}
@end

int main(void)
{
	{
		KVCProbeAccessors *o = [[KVCProbeAccessors alloc] init];
		NSString *read = [o valueForKey:@"title"];

		[o setValue:@"through kvc" forKey:@"title"];
		check("kvc-accessors",
		      [read isEqualToString:@"initial"] &&
		      [[o valueForKey:@"title"] isEqualToString:@"through kvc"] &&
		      [[o title] isEqualToString:@"through kvc"] &&
		      /* ...and a FRESH, INITIALISED instance starts at its -init value: the
		       * lookup reads the object it was asked about and nothing else. */
		      [[[[KVCProbeAccessors alloc] init] valueForKey:@"title"]
			isEqualToString:@"initial"],
		      fn_why([o valueForKey:@"title"]));
	}

	{
		KVCProbeIvars *o = [[KVCProbeIvars alloc] init];

		[o setValue:@"via ivar" forKey:@"name"];
		[o setValue:@41 forKey:@"count"];
		check("kvc-ivar",
		      [[o valueForKey:@"name"] isEqualToString:@"via ivar"] &&
		      [[o valueForKey:@"count"] intValue] == 41,
		      fn_why([o valueForKey:@"count"]));
	}

	{
		/* Through the ACCESSORS this time, not the ivars: a scalar read has to be
		 * BOXED and a scalar write UNBOXED, and neither is what -performSelector:
		 * does. A nil through the scalar setter is the hook's answer rather than a
		 * zero written into the field. */
		KVCProbeScalar *o = [[KVCProbeScalar alloc] init];
		int nilRaised = 0;

		[o setValue:@7 forKey:@"amount"];
		@try {
			[o setValue:nil forKey:@"amount"];
		} @catch (NSException *e) {
			(void)e;
			nilRaised = 1;
		}
		check("kvc-scalar-accessor",
		      [o amount] == 7 &&
		      [[o valueForKey:@"amount"] intValue] == 7 &&
		      nilRaised == 1 &&
		      /* The refused write left the value ALONE: it refused, it did not
		       * half-write. */
		      [o amount] == 7,
		      nilRaised == 0 ? "a nil through a scalar setter did not raise"
		      : "the scalar did not round-trip");
	}

	{
		/* THE TRAP: `_shared` is declared by KVCProbeBase and read through a
		 * KVCProbeChild. An inherited-view offset (negative) and an absolute one
		 * differ here and are identical everywhere else in this probe. */
		KVCProbeChild *o = [[KVCProbeChild alloc] init];

		[o setValue:@"set through the child" forKey:@"shared"];
		[o setValue:@"its own" forKey:@"own"];
		check("kvc-ivar-super",
		      [[o valueForKey:@"shared"] isEqualToString:@"set through the child"] &&
		      [[o valueForKey:@"own"] isEqualToString:@"its own"],
		      fn_why([o valueForKey:@"shared"]));
	}

	{
		/* An object ivar that IS nil is found. If `found` were conflated with the
		 * value, the lookup would walk on to `_isFlag`, `flag`, `isFlag`, find
		 * none of them and RAISE — so this check fails loudly rather than quietly. */
		KVCProbeIvars *o = [[KVCProbeIvars alloc] init];

		@try {
			id value = [o valueForKey:@"name"];

			check("kvc-nil-ivar", value == nil,
			      value == nil ? "an object ivar holding nil answers nil"
			      : fn_why(value));
		} @catch (NSException *e) {
			check("kvc-nil-ivar", 0,
			      [[e reason] UTF8String]);
		}
	}

	{
		KVCProbeNested *o = [[KVCProbeNested alloc] init];

		[o setValue:@"deeper" forKeyPath:@"inner.title"];
		check("kvc-keypath",
		      [[o valueForKeyPath:@"inner.title"] isEqualToString:@"deeper"],
		      fn_why([o valueForKeyPath:@"inner.title"]));
	}

	{
		NSArray *items = foundation_kvc_items();
		id sum = items != nil ? [items valueForKeyPath:@"@sum.code"] : nil;
		id avg = items != nil ? [items valueForKeyPath:@"@avg.code"] : nil;
		id max = items != nil ? [items valueForKeyPath:@"@max.code"] : nil;
		id min = items != nil ? [items valueForKeyPath:@"@min.code"] : nil;
		id count = items != nil ? [items valueForKeyPath:@"@count"] : nil;
		id unionObjects = items != nil ? [items valueForKeyPath:@"@unionOfObjects.code"] : nil;
		id distinct = items != nil ? [items valueForKeyPath:@"@distinctUnionOfObjects.code"] : nil;

		check("kvc-operators",
		      items != nil && sum != nil && avg != nil && max != nil &&
		      min != nil && count != nil && unionObjects != nil &&
		      distinct != nil &&
		      [count intValue] == 4 &&
		      [sum doubleValue] == 80.0 &&
		      [avg doubleValue] == 20.0 &&
		      [max intValue] == 30 &&
		      [min intValue] == 10 &&
		      [unionObjects count] == 4 &&
		      [distinct count] == 3,
		      distinct == nil ? "(no distinct)"
		      : fn_why(@( (int)[distinct count] )));
	}

	{
		/* THE UNION FAMILY, which needed an NSSet to exist at all. The values are collections, so
		 * the answer is one: arrays for the array pair, sets for the set pair. The fixture's tags
		 * are a=[red,green], b=[green,blue], c=[blue,blue], d=[blue], which is what makes the two
		 * array answers DIFFERENT SIZES (7 and 3) rather than accidentally equal. */
		NSArray *items = foundation_kvc_items();
		id unionArrays = items != nil ? [items valueForKeyPath:@"@unionOfArrays.tags"] : nil;
		id distinctArrays = items != nil
			? [items valueForKeyPath:@"@distinctUnionOfArrays.tags"] : nil;
		id unionSets = items != nil ? [items valueForKeyPath:@"@unionOfSets.tagSet"] : nil;
		id distinctSets = items != nil
			? [items valueForKeyPath:@"@distinctUnionOfSets.tagSet"] : nil;

		check("kvc-collection-unions",
		      unionArrays != nil && distinctArrays != nil &&
		      unionSets != nil && distinctSets != nil &&
		      [unionArrays isKindOfClass:[NSArray class]] &&
		      [distinctArrays isKindOfClass:[NSArray class]] &&
		      [unionSets isKindOfClass:[NSSet class]] &&
		      [distinctSets isKindOfClass:[NSSet class]] &&
		      [unionArrays count] == 7 && [distinctArrays count] == 3 &&
		      [unionSets count] == 3 && [distinctSets count] == 3 &&
		      [unionSets containsObject:@"red"] && [unionSets containsObject:@"blue"] &&
		      ![unionSets containsObject:@"purple"],
		      [[NSString stringWithFormat:@"arrays=%lu/%lu sets=%lu/%lu isSet=%d isArray=%d",
			(unsigned long)(unionArrays != nil ? [unionArrays count] : 0),
			(unsigned long)(distinctArrays != nil ? [distinctArrays count] : 0),
			(unsigned long)(unionSets != nil ? [unionSets count] : 0),
			(unsigned long)(distinctSets != nil ? [distinctSets count] : 0),
			(int)(unionSets != nil && [unionSets isKindOfClass:[NSSet class]]),
			(int)(unionArrays != nil && [unionArrays isKindOfClass:[NSArray class]])] UTF8String]);
	}

	{
		NSArray *items = foundation_kvc_items();
		NSArray *titles = items != nil ? [items valueForKey:@"title"] : nil;
		NSDictionary *dict = @{ @"k" : @"v", @"n" : @2 };
		id looked = [dict valueForKey:@"k"];
		id counted = [dict valueForKey:@"@count"];

		check("kvc-collections",
		      titles != nil && [titles count] == 4 &&
		      [[titles objectAtIndex:0] isEqualToString:@"a"] &&
		      [looked isEqualToString:@"v"] &&
		      [counted intValue] == 2,
		      counted == nil ? "(no @count)" : fn_why(counted));
	}

	{
		/* THE MAPPING KEEPS ITS SHAPE: a nil answer becomes NSNull. This probe could not assert
		 * it before NSNull existed (F13.8c) — the code skipped the element and said there was no
		 * NSNull to substitute. Now the difference is observable, so it is asserted: the mapped
		 * array's count follows the RECEIVER's, and the hole is the singleton itself. */
		NSArray *mixed = [NSArray arrayWithObjects:
			@{ @"a" : @7 },
			@{},
			@{ @"a" : @9 },
			nil];
		NSArray *mapped = [mixed valueForKey:@"a"];

		check("kvc-null-shape",
		      mapped != nil && [mapped count] == 3 &&
		      [[mapped objectAtIndex:0] intValue] == 7 &&
		      [mapped objectAtIndex:1] == [NSNull null] &&
		      [[mapped objectAtIndex:2] intValue] == 9,
		      mapped == nil ? "(no mapping)"
		      : [[NSString stringWithFormat:@"mapped=%lu null=%d",
			(unsigned long)[mapped count],
			(int)([mapped objectAtIndex:1] == [NSNull null])] UTF8String]);
	}

	{
		/* THE THREE SPELLINGS AGREE. Apple's collection operators apply to an array, a set AND a
		 * dictionary; this arm used to accept only the array and RAISE for the other two, while
		 * the -valueForKey: spelling beside it already folded a dictionary's values. One family
		 * answering and raising depending on which spelling was typed is what this asserts
		 * against — measured as a PAIR, the two spellings for the dictionary. */
		NSDictionary *dict = @{ @"a" : @1, @"b" : @2 };
		NSSet *set = [NSSet setWithArray:@[ @1, @2, @3 ]];

		check("kvc-operator-receivers",
		      dict != nil && set != nil &&
		      [[dict valueForKey:@"@count"] intValue] == 2 &&
		      [[dict valueForKeyPath:@"@count"] intValue] == 2 &&
		      [[dict valueForKeyPath:@"@sum"] intValue] == 3 &&
		      [[set valueForKeyPath:@"@count"] intValue] == 3 &&
		      [[set valueForKeyPath:@"@min"] intValue] == 1 &&
		      [[set valueForKeyPath:@"@max"] intValue] == 3,
		      dict == nil ? "(no dict)"
		      : [[NSString stringWithFormat:@"dict=%@/%@ set=%@",
			[dict valueForKey:@"@count"], [dict valueForKeyPath:@"@count"],
			[set valueForKeyPath:@"@count"]] UTF8String]);
	}

	{
		KVCProbeIvars *o = [[KVCProbeIvars alloc] init];
		KVCProbeHook *hook = [[KVCProbeHook alloc] init];
		int undefinedRaised = 0;
		int nilScalarRaised = 0;

		/* Both are written out rather than handed to a helper: a helper taking a
		 * BLOCK would drag the block runtime into a probe that is testing the
		 * runtime's ivar table, which is a different thing. */
		@try {
			(void)[o valueForKey:@"no-such-key"];
		} @catch (NSException *e) {
			(void)e;
			undefinedRaised = 1;
		}
		@try {
			[o setValue:nil forKey:@"count"];
		} @catch (NSException *e) {
			(void)e;
			nilScalarRaised = 1;
		}

		[hook setValue:@"ignored" forKey:@"no-such-key"];
		check("kvc-undefined",
		      /* The defaults RAISE, rather than answering nil... */
		      undefinedRaised == 1 && nilScalarRaised == 1 &&
		      /* ...and an override is what answers instead. Both halves, because
		       * "it raised" alone would pass for a key that cannot be looked up at
		       * all, which is not the same claim. */
		      [[hook valueForKey:@"no-such-key"]
			isEqualToString:@"hooked:no-such-key"],
		      fn_why([hook valueForKey:@"no-such-key"]));
	}

	{
		KVCProbeValidator *v = [[KVCProbeValidator alloc] init];
		KVCProbeIvars *plain = [[KVCProbeIvars alloc] init];
		id good = @30;
		id bad = @(-1);
		id goodValue = good;
		id badValue = bad;
		NSError *error = nil;
		BOOL acceptedGood = [v validateValue:&goodValue forKey:@"age" error:NULL];
		BOOL refusedBad = [v validateValue:&badValue forKey:@"age" error:&error];
		BOOL noRule = [plain validateValue:&goodValue forKey:@"anything" error:NULL];

		check("kvc-validate",
		      acceptedGood == YES && refusedBad == NO && error != nil &&
		      noRule == YES,
		      refusedBad == NO ? "refused (expected)" : "accepted a negative age");
	}

	{
		/* WHERE THIS STOOD, AND WHY IT MOVED (2026-09-18, §11): this check used to assert that KVO,
		 * the mutable proxies and -takeValue:forKey: were ALL ABSENT. **F13.9 SHIPPED KVO, SO THE
		 * CLAIM BECAME FALSE AND THE CHECK WENT RED** — and it stayed red until someone looked,
		 * which is the exact failure mode §11's ledger exists to expose: a probe asserting an
		 * absence is asserting a fact about the TREE, and landing the code invalidates it.
		 *
		 * SO IT IS SPLIT INTO THE TWO THINGS IT WAS CONFLATING: what KVC's own surface now HAS
		 * (which the inventory must DEMAND), and what it still LACKS (which the inventory may still
		 * assert absent, and which §11.3 carries as defects). The absent half is verified against
		 * the tree by grep, not by hope. */
		check("kvc-kvo-present",
		      [NSObject instancesRespondToSelector:sel_registerName(
			  "addObserver:forKeyPath:options:context:")] &&
		      [NSObject instancesRespondToSelector:sel_registerName(
			  "removeObserver:forKeyPath:")] &&
		      [NSObject instancesRespondToSelector:sel_registerName(
			  "willChangeValueForKey:")] &&
		      [NSObject instancesRespondToSelector:sel_registerName(
			  "didChangeValueForKey:")] &&
		      [NSObject instancesRespondToSelector:sel_registerName("valueForKey:")] &&
		      [NSObject instancesRespondToSelector:sel_registerName("setValue:forKey:")],
		      "KVO ships since F13.9, so its surface is demanded here rather than denied");

		check("kvc-refusals",
		      objc_getClass("NSKeyValueObservationInfo") == NULL &&
		      ![NSObject instancesRespondToSelector:sel_registerName(
			  "observeValueForKeyPath:ofObject:change:context:")],
		      "the KVO OBSERVER protocol callback and Apple's private observation-info class are absent");

		{
			/* PRINTED RATHER THAN ASSERTED, because it is the honest state of this row and the
			 * ledger must be able to read it: -mutableArrayValueForKey: is DECLARED in the
			 * header while the MUTABLE PROXIES are not implemented, so whether an instance
			 * RESPONDS to it is a fact worth recording rather than a claim worth making. */
			int i;
			static const char *proxies[] = {
				"mutableArrayValueForKey:", "mutableSetValueForKey:",
				"mutableOrderedSetValueForKey:", "takeValue:forKey:", NULL
			};

			for (i = 0; proxies[i] != NULL; i++) {
				printf("FOUNDATION-KVC proxy-state: %s %s\n", proxies[i],
				       [NSObject instancesRespondToSelector:sel_registerName(proxies[i])]
				       ? "PRESENT" : "absent");
			}
		}
	}

	{
		id theirs = foundation_kvc_accessor_object();
		id theirsIvars = foundation_kvc_ivar_object();

		[theirsIvars setValue:@99 forKey:@"count"];
		check("cross-tu",
		      theirs != nil && theirsIvars != nil &&
		      [[theirs valueForKey:@"title"]
			isEqualToString:@"from the other unit"] &&
		      [[theirs valueForKey:@"code"] intValue] == 42 &&
		      [[theirsIvars valueForKey:@"count"] intValue] == 99,
		      fn_why([theirs valueForKey:@"title"]));
	}

	printf("FOUNDATION-KVC RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-KVC DONE\n");
	return failc ? 1 : 0;
}
