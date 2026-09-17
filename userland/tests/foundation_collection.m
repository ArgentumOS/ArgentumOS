/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_collection, unit 2 of 2 — the checks (ARC).
 *
 *   array-basic      count/index/first/last/contains, and out of range is nil
 *   array-equality   content equality — and that ORDER matters
 *   array-mutable    add/insert/remove, and -copy as a snapshot
 *   array-enumerate  `for (id x in array)` visits every element, in order
 *   dict-basic       set/get, overwrite does not grow the count, missing key is nil
 *   dict-key-copy    THE KEY DECISION: a key is COPIED, so mutating the caller's
 *                    object after insert neither loses the value nor touches the
 *                    original's retain count
 *   dict-equality    the same pairs built in a DIFFERENT ORDER are equal and hash alike
 *   dict-enumerate   `for (id k in dict)` yields every key once, and a nested
 *                    collection is held by reference
 *   ownership        an array element is retained on insert and released on removal
 */

#import "foundation_collection.h"
#import <objc/runtime.h>
#include <objc/objc-arc.h>	/* object_getRetainCount_np is declared here */
#include <stdio.h>
#include <string.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-COLLECTION %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-COLLECTION %s FAIL %s\n", name, detail ? detail : "");
	}
}

int main(void)
{
	{
		id __unsafe_unretained objects[3];
		NSArray *a;

		objects[0] = @"one";
		objects[1] = @"two";
		objects[2] = @"three";
		a = [NSArray arrayWithObjects:objects count:3];

		check("array-basic",
		      [a count] == 3 &&
		      [[a objectAtIndex:0] isEqualToString:@"one"] &&
		      [[a firstObject] isEqualToString:@"one"] &&
		      [[a lastObject] isEqualToString:@"three"] &&
		      [a indexOfObject:@"two"] == 1 &&
		      [a containsObject:@"three"] && ![a containsObject:@"four"] &&
		      [a objectAtIndex:9] == nil &&
		      [[NSArray array] count] == 0 &&
		      [[NSArray arrayWithObject:@"solo"] count] == 1,
		      "count/index/first/last/contains; out of range is nil");
	}

	{
		id __unsafe_unretained x[2], y[2], z[2];
		NSArray *ax, *ay, *az;

		x[0] = @"a"; x[1] = @"b";
		y[0] = @"a"; y[1] = @"b";
		z[0] = @"b"; z[1] = @"a";
		ax = [NSArray arrayWithObjects:x count:2];
		ay = [NSArray arrayWithObjects:y count:2];
		az = [NSArray arrayWithObjects:z count:2];

		check("array-equality",
		      [ax isEqualToArray:ay] && [ax isEqual:ay] && [ax hash] == [ay hash] &&
		      ![ax isEqualToArray:az] && ![ax isEqual:@"not an array"] &&
		      ![ax isEqualToArray:[NSArray arrayWithObject:@"a"]],
		      "content equality, and ORDER matters for an array");
	}

	{
		NSMutableArray *m = [NSMutableArray array];
		NSArray *snapshot;

		[m addObject:@"a"];
		[m addObject:@"c"];
		[m insertObject:@"b" atIndex:1];
		snapshot = [m copy];
		[m removeAllObjects];

		check("array-mutable",
		      [m count] == 0 &&
		      [snapshot count] == 3 &&
		      [[snapshot objectAtIndex:0] isEqualToString:@"a"] &&
		      [[snapshot objectAtIndex:1] isEqualToString:@"b"] &&
		      [[snapshot objectAtIndex:2] isEqualToString:@"c"] &&
		      ![m isEqualToArray:snapshot],
		      "add/insert/remove work, and -copy is a snapshot");
	}

	{
		id __unsafe_unretained objects[3];
		NSArray *a;
		unsigned long seen = 0;
		unsigned long i = 0;
		int ordered = 1;

		objects[0] = @"x";
		objects[1] = @"y";
		objects[2] = @"z";
		a = [NSArray arrayWithObjects:objects count:3];

		for (id item in a) {
			ordered = ordered && (i < 3) && [item isEqualToString:objects[i]];
			seen++;
			i++;
		}

		check("array-enumerate",
		      ordered && seen == 3,
		      "for-in visits every element, in order");
	}

	{
		NSMutableDictionary *d = [NSMutableDictionary dictionary];

		[d setObject:@"one" forKey:@"1"];
		[d setObject:@"two" forKey:@"2"];
		[d setObject:@"uno" forKey:@"1"];
		check("dict-basic",
		      [d count] == 2 &&
		      [[d objectForKey:@"1"] isEqualToString:@"uno"] &&
		      [[d objectForKey:@"2"] isEqualToString:@"two"] &&
		      [d objectForKey:@"nope"] == nil &&
		      [d objectForKey:nil] == nil &&
		      [[NSDictionary dictionary] count] == 0,
		      "set/get, overwrite replaces without growing the count, missing key is nil");
	}

	{
		/* THE KEY DECISION, measured both ways: the table keeps a COPY, so the
		 * caller's mutable object can change afterwards without either losing
		 * the value or being retained by the table. */
		NSMutableString *key = [[NSMutableString alloc] initWithUTF8String:"alpha"];
		NSMutableDictionary *d = [NSMutableDictionary dictionary];
		NSString *lookup;
		unsigned long before = object_getRetainCount_np(key);

		[d setObject:@"value" forKey:key];
		[key appendString:@"-changed"];
		lookup = [[NSMutableString alloc] initWithUTF8String:"alpha"];

		check("dict-key-copy",
		      [d count] == 1 &&
		      [[d objectForKey:lookup] isEqualToString:@"value"] &&
		      [d objectForKey:key] == nil &&
		      object_getRetainCount_np(key) == before,
		      "a key is COPIED: mutating it after insert still finds the value, and the original's retain count is unchanged");
	}

	{
		NSMutableDictionary *a = [NSMutableDictionary dictionary];
		NSMutableDictionary *b = [NSMutableDictionary dictionary];

		[a setObject:[NSNumber numberWithInt:1] forKey:@"x"];
		[a setObject:[NSNumber numberWithInt:2] forKey:@"y"];
		[b setObject:[NSNumber numberWithInt:2] forKey:@"y"];
		[b setObject:[NSNumber numberWithInt:1] forKey:@"x"];
		[b setObject:[NSNumber numberWithInt:9] forKey:@"z"];
		[b removeObjectForKey:@"z"];

		check("dict-equality",
		      [a isEqualToDictionary:b] && [a isEqual:b] && [a hash] == [b hash] &&
		      ![a isEqual:[NSMutableDictionary dictionary]] &&
		      ![a isEqual:@"nope"],
		      "the same pairs built in a DIFFERENT ORDER are equal and hash alike");
	}

	{
		NSMutableDictionary *d = [NSMutableDictionary dictionary];
		NSDictionary *nested = foundation_collection_nested();
		unsigned long seen = 0;
		int allKeys = 1;

		[d setObject:@"a" forKey:@"k1"];
		[d setObject:@"b" forKey:@"k2"];
		[d setObject:@"c" forKey:@"k3"];

		for (id k in d) {
			allKeys = allKeys && [k isKindOfClass:[NSString class]] &&
			          [d objectForKey:k] != nil;
			seen++;
		}

		check("dict-enumerate",
		      allKeys && seen == 3 &&
		      [[nested objectForKey:@"count"] intValue] == 2 &&
		      [[nested objectForKey:@"letters"] count] == 2 &&
		      [[[nested objectForKey:@"letters"] objectAtIndex:1]
		          isEqualToString:@"b"],
		      "for-in yields every key once; a nested collection is held by reference");
	}

	{
		NSMutableArray *a = [NSMutableArray array];
		NSString *element = [[NSMutableString alloc] initWithUTF8String:"held"];
		unsigned long before = object_getRetainCount_np(element);
		unsigned long afterAdd;
		unsigned long afterRemove;

		[a addObject:element];
		afterAdd = object_getRetainCount_np(element);
		[a removeObjectAtIndex:0];
		afterRemove = object_getRetainCount_np(element);

		check("ownership",
		      afterAdd == before + 1 && afterRemove == before,
		      "an array element is RETAINED on insert and released on removal");
	}


	{
		/* THE AUDIT, fix A: a NUMBER as a dictionary key. This used to file a
		 * phantom entry - the count grew to 1 and the value was unreachable -
		 * because NSNumber did not implement the copying protocol and this
		 * runtime answers nil for an unimplemented selector instead of raising. */
		NSMutableDictionary *d = [NSMutableDictionary dictionary];
		NSNumber *key = [NSNumber numberWithInt:7];

		[d setObject:@"seven" forKey:key];

		check("number-key",
		      [d count] == 1 &&
		      [[d objectForKey:[NSNumber numberWithInt:7]] isEqualToString:@"seven"] &&
		      [[d objectForKey:[NSNumber numberWithDouble:7.0]] isEqualToString:@"seven"] &&
		      [key conformsToProtocol:@protocol(NSCopying)],
		      "an NSNumber key round-trips; a double 7.0 finds the integer 7");
	}

	{
		/* THE AUDIT, fix B: the spellings Cocoa-shaped code uses. If any of
		 * these names or methods were missing this would not COMPILE - the
		 * compiler is part of the check. */
		NSMutableArray *m = [NSMutableArray array];
		NSArray *snapshot;
		NSMutableDictionary *d = [NSMutableDictionary dictionary];
		NSRange range = NSMakeRange(1, 2);
		NSComparisonResult order =
			[[NSNumber numberWithInt:1] compare:[NSNumber numberWithInt:2]];
		NSUInteger i;

		for (i = 0; i < 3; i++) {
			[m addObject:[NSNumber numberWithInt:(int)i]];
		}
		snapshot = [m copy];
		m[0] = [NSNumber numberWithInt:9];
		[m addObject:[NSNumber numberWithInt:3]];
		m[3] = [NSNumber numberWithInt:4];	/* index == count appends */
		d[@"k"] = @"v";
		d[@"gone"] = @"x";
		d[@"gone"] = nil;			/* nil REMOVES the key */

		check("cocoa-spellings",
		      [[m objectAtIndexedSubscript:0] intValue] == 9 &&
		      [[m objectAtIndexedSubscript:3] intValue] == 4 &&
		      [m count] == 4 &&
		      [[snapshot firstObject] intValue] == 0 && [snapshot count] == 3 &&
		      [snapshot indexOfObject:[NSNumber numberWithInt:5]] == NSNotFound &&
		      ![snapshot containsObject:[NSNumber numberWithInt:5]] &&
		      NSLocationInRange(2, range) && !NSLocationInRange(3, range) &&
		      NSMaxRange(range) == 3 &&
		      order == NSOrderedAscending &&
		      [d count] == 1 && [[d objectForKeyedSubscript:@"k"] isEqualToString:@"v"] &&
		      [d objectForKeyedSubscript:@"gone"] == nil &&
		      [m conformsToProtocol:@protocol(NSFastEnumeration)] &&
		      [m conformsToProtocol:@protocol(NSCopying)] &&
		      [[snapshot performSelector:@selector(firstObject)] intValue] == 0,
		      "subscripts, NSNotFound, NSRange, NSComparisonResult, -performSelector:, conformance");
	}


	{
		/*
		 * THE AUDITED COCOA INVENTORY for NSArray/NSMutableArray.
		 *
		 * `implemented` is Cocoa's documented surface and must all EXIST;
		 * `excluded` is what we deliberately do not ship and must all be ABSENT.
		 * The old check only confirmed what our own header declared — how
		 * +stringWithFormat:arguments: shipped missing — and this one named six
		 * gaps, all implemented in the same pass: the comparator-sort pair,
		 * -enumerateObjectsUsingBlock:, the sorted-range search,
		 * -removeObjectIdenticalTo:inRange: and the ranged replacement.
		 */
		static const char *classSelectors[] = {
			"array", "arrayWithObject:", "arrayWithObjects:count:", "arrayWithArray:",
			"arrayWithObjects:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithObject:", "initWithObjects:count:", "initWithArray:",
			"initWithObjects:",
			"count", "objectAtIndex:", "objectAtIndexedSubscript:",
			"firstObject", "lastObject",
			"indexOfObject:", "indexOfObject:inRange:", "indexOfObjectIdenticalTo:",
			"indexOfObject:inSortedRange:options:usingComparator:", "containsObject:",
			"arrayByAddingObject:", "arrayByAddingObjectsFromArray:",
			"subarrayWithRange:", "getObjects:range:", "componentsJoinedByString:",
			"sortedArrayUsingSelector:", "sortedArrayUsingComparator:",
			"enumerateObjectsUsingBlock:",
			"objectsAtIndexes:", "indexesOfObjectsPassingTest:",
			"isEqualToArray:", "isEqual:", "hash", "description", "copy", "mutableCopy",
			"countByEnumeratingWithState:objects:count:",
			NULL
		};
		static const char *mutableClassSelectors[] = {
			"array", "arrayWithCapacity:", NULL
		};
		static const char *mutableSelectors[] = {
			"initWithCapacity:", "addObject:", "addObjectsFromArray:",
			"insertObject:atIndex:", "removeObjectAtIndex:", "removeLastObject",
			"removeObject:", "removeObject:inRange:",
			"removeObjectIdenticalTo:", "removeObjectIdenticalTo:inRange:",
			"removeObjectsInRange:", "removeAllObjects",
			"replaceObjectAtIndex:withObject:",
			"replaceObjectsInRange:withObjectsFromArray:",
			"replaceObjectsInRange:withObjectsFromArray:range:",
			"setArray:", "exchangeObjectAtIndex:withObjectAtIndex:",
			"sortUsingSelector:", "sortUsingComparator:",
			"insertObjects:atIndexes:", "removeObjectsAtIndexes:",
			"replaceObjectsAtIndexes:withObjects:",
			"setObject:atIndexedSubscript:", NULL
		};
		static const char *excluded[] = {
			/* Needs predicates, descriptors, function pointers or plists. */
			"filteredArrayUsingPredicate:",		/* NSPredicate */
			"sortedArrayUsingDescriptors:",		/* NSSortDescriptor */
			"sortedArrayUsingFunction:context:",	/* C function comparators */
			"sortUsingFunction:context:",		/* C function comparators */
			"arrayWithContentsOfFile:",		/* a plist reader */
			"initWithContentsOfFile:",		/* a plist reader */
			"writeToFile:atomically:",		/* a plist writer */
			/* Needs NSURL. */
			"arrayWithContentsOfURL:",		/* NSURL */
			"initWithContentsOfURL:",		/* NSURL */
			"writeToURL:atomically:",		/* NSURL */
			NULL
		};
		NSArray *probe = [NSArray arrayWithObject:@"x"];
		NSMutableArray *mutable = [[NSMutableArray alloc] init];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSArray respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; mutableClassSelectors[i] != NULL; i++) {
			if (![NSMutableArray respondsToSelector:sel_registerName(mutableClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s (mutable)\n", mutableClassSelectors[i]);
			}
		}
		for (i = 0; mutableSelectors[i] != NULL; i++) {
			if (![mutable respondsToSelector:sel_registerName(mutableSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s (mutable)\n", mutableSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("array-api-complete", complete,
		      "the audited Cocoa inventory for NSArray/NSMutableArray");
	}

	{
		/* The audited Cocoa inventory for NSIndexSet/NSMutableIndexSet — the class
		 * the four methods above are specified in terms of. */
		static const char *classSelectors[] = {
			"indexSet", "indexSetWithIndex:", "indexSetWithIndexesInRange:", NULL
		};
		static const char *mutableClassSelectors[] = {
			"indexSet", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithIndex:", "initWithIndexesInRange:",
			"containsIndex:", "containsIndexesInRange:", "count",
			"firstIndex", "lastIndex",
			"indexGreaterThanIndex:", "indexLessThanIndex:",
			"enumerateIndexesUsingBlock:", "isEqualToIndexSet:",
			"isEqual:", "hash", "description", "copy", "mutableCopy", NULL
		};
		static const char *mutableSelectors[] = {
			"addIndex:", "addIndexesInRange:", "removeIndex:",
			"removeIndexesInRange:", "removeAllIndexes", NULL
		};
		static const char *excluded[] = {
			/* The range- and buffer-based queries: a caller walks the set with
			 * -enumerateIndexesUsingBlock: instead. */
			"countOfIndexesInRange:", "getIndexes:maxCount:inIndexRange:",
			"indexGreaterThanOrEqualToIndex:", "indexLessThanOrEqualToIndex:",
			"firstIndexInRange:", "lastIndexInRange:",
			"enumerateRangesUsingBlock:",
			"enumerateRangesInRange:options:usingBlock:",
			"shiftIndexesStartingAtIndex:by:",
			"addIndexes:", "removeIndexes:", "containsIndexes:",
			NULL
		};
		NSIndexSet *probe = [NSIndexSet indexSetWithIndex:1];
		NSMutableIndexSet *mutable = [[NSMutableIndexSet alloc] init];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSIndexSet respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s (indexset)\n", classSelectors[i]);
			}
		}
		for (i = 0; mutableClassSelectors[i] != NULL; i++) {
			if (![NSMutableIndexSet respondsToSelector:sel_registerName(mutableClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s (indexset mutable)\n", mutableClassSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s (indexset)\n", instanceSelectors[i]);
			}
		}
		for (i = 0; mutableSelectors[i] != NULL; i++) {
			if (![mutable respondsToSelector:sel_registerName(mutableSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s (indexset mutable)\n", mutableSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION present but EXCLUDED (indexset): %s\n", excluded[i]);
			}
		}
		check("indexset-api-complete", complete,
		      "the audited Cocoa inventory for NSIndexSet/NSMutableIndexSet");
	}


	{
		/* The array surface, SPLIT ONE CHECK PER STEP. The combined version
		 * faulted in the guest with no line to point at; a check per step makes
		 * the guest's log name the last thing that worked, which is the only
		 * honest localiser for a segfault. */
		NSMutableArray *m = [NSMutableArray arrayWithObjects:@"b", @"a", @"c", nil];
		NSArray *joined = [NSArray arrayWithObjects:@"x", @"y", nil];
		NSArray *numbers = [NSArray arrayWithObjects:@"1", @"2", @"3", @"4", nil];
		NSArray *one = [NSArray arrayWithObject:@"solo"];

		check("array-varargs",
		      [m count] == 3 && [joined count] == 2 && [numbers count] == 4 &&
		      [one count] == 1 &&
		      [[m objectAtIndex:0] isEqualToString:@"b"] &&
		      [[m objectAtIndex:2] isEqualToString:@"c"],
		      "the nil-terminated variadic creation keeps every element, including the first");
	}

	{
		NSArray *joined = [NSArray arrayWithObjects:@"x", @"y", nil];

		check("array-join",
		      [[joined componentsJoinedByString:@"-"] isEqualToString:@"x-y"] &&
		      [[[NSArray arrayWithObject:@"solo"] componentsJoinedByString:@"-"]
		          isEqualToString:@"solo"] &&
		      [[[NSArray array] componentsJoinedByString:@"-"] isEqualToString:@""],
		      "componentsJoinedByString:, including the one- and zero-element cases");
	}

	{
		NSArray *numbers = [NSArray arrayWithObjects:@"1", @"2", @"3", @"4", nil];
		NSArray *sub = [numbers subarrayWithRange:NSMakeRange(1, 2)];

		check("array-subarray",
		      [sub count] == 2 && [[sub objectAtIndex:0] isEqualToString:@"2"] &&
		      [[sub objectAtIndex:1] isEqualToString:@"3"] &&
		      [[numbers subarrayWithRange:NSMakeRange(3, 99)] count] == 1,
		      "subarrayWithRange:, and a range that runs off the end is clamped");
	}

	{
		NSMutableArray *m = [NSMutableArray arrayWithObjects:@"b", @"a", @"c", nil];
		NSArray *sorted = [m sortedArrayUsingSelector:@selector(compare:)];

		check("array-sort",
		      [sorted count] == 3 &&
		      [[sorted objectAtIndex:0] isEqualToString:@"a"] &&
		      [[sorted objectAtIndex:1] isEqualToString:@"b"] &&
		      [[sorted objectAtIndex:2] isEqualToString:@"c"] &&
		      [[m objectAtIndex:0] isEqualToString:@"b"] &&
		      [sorted indexOfObjectIdenticalTo:[sorted objectAtIndex:0]] == 0,
		      "sortedArrayUsingSelector: sorts without touching the receiver");
	}

	{
		NSMutableArray *m = [NSMutableArray arrayWithObjects:@"b", @"a", @"c", nil];

		check("array-search",
		      [m indexOfObject:@"a" inRange:NSMakeRange(0, 3)] == 1 &&
		      [m indexOfObject:@"a" inRange:NSMakeRange(2, 1)] == NSNotFound &&
		      [m indexOfObject:@"a" inRange:NSMakeRange(5, 1)] == NSNotFound &&
		      [m indexOfObjectIdenticalTo:@"a"] == 1,
		      "ranged equality search, and both bounds cases for NSNotFound");
	}

	{
		NSArray *numbers = [NSArray arrayWithObjects:@"1", @"2", @"3", @"4", nil];
		id __unsafe_unretained held[4];

		held[0] = nil; held[1] = nil; held[2] = nil; held[3] = nil;
		[numbers getObjects:held range:NSMakeRange(1, 2)];
		check("array-getobjects",
		      [held[0] isEqualToString:@"2"] && [held[1] isEqualToString:@"3"] &&
		      held[2] == nil,
		      "getObjects:range: fills exactly the range it was asked for");
	}

	{
		NSMutableArray *changed = [NSMutableArray arrayWithObjects:@"p", @"q", @"r", nil];
		NSArray *joined = [NSArray arrayWithObjects:@"x", @"y", nil];
		int ok = 1;

		printf("FOUNDATION-COLLECTION step exchange\n");
		[changed exchangeObjectAtIndex:0 withObjectAtIndex:2];
		ok = ok && [[changed objectAtIndex:0] isEqualToString:@"r"] &&
		     [[changed objectAtIndex:2] isEqualToString:@"p"];
		printf("FOUNDATION-COLLECTION step removeLast\n");
		[changed removeLastObject];
		ok = ok && [changed count] == 2 && [[changed objectAtIndex:1] isEqualToString:@"q"];
		printf("FOUNDATION-COLLECTION step addObjectsFromArray\n");
		[changed addObjectsFromArray:joined];
		ok = ok && [changed count] == 4 && [[changed objectAtIndex:2] isEqualToString:@"x"];
		printf("FOUNDATION-COLLECTION step replaceObjectsInRange\n");
		[changed replaceObjectsInRange:NSMakeRange(0, 2)
			 withObjectsFromArray:[NSArray arrayWithObject:@"z"]];
		ok = ok && [changed count] == 3 &&
		     [[changed objectAtIndex:0] isEqualToString:@"z"] &&
		     [[changed objectAtIndex:1] isEqualToString:@"x"] &&
		     [changed indexOfObject:@"r"] == NSNotFound;
		printf("FOUNDATION-COLLECTION step setArray\n");
		[changed setArray:[NSArray arrayWithObjects:@"k", @"l", nil]];
		ok = ok && [changed count] == 2 && [[changed objectAtIndex:1] isEqualToString:@"l"];
		check("array-bulk", ok,
		      "exchange, removeLastObject, addObjectsFromArray, replaceObjectsInRange: and setArray:");
	}

	{
		NSArray *joined = [NSArray arrayWithObjects:@"x", @"y", nil];
		NSArray *copy = [NSArray arrayWithArray:joined];
		NSMutableArray *m = [NSMutableArray arrayWithObjects:@"a", @"b", @"c", nil];
		NSArray *grown = [m arrayByAddingObjectsFromArray:joined];
		NSArray *plusOne = [m arrayByAddingObject:@"z"];

		check("array-copy-and-grow",
		      [copy isEqualToArray:joined] && ![copy isEqual:m] &&
		      [grown count] == 5 && [[grown objectAtIndex:4] isEqualToString:@"y"] &&
		      [m count] == 3 &&
		      [plusOne count] == 4 && [[plusOne objectAtIndex:3] isEqualToString:@"z"],
		      "arrayWithArray:, arrayByAddingObjectsFromArray: and arrayByAddingObject: leave the receiver alone");
	}

	{
		NSArray *numbers = [NSArray arrayWithObjects:@"1", @"2", @"3", @"4", nil];
		NSString *distinct = [[NSMutableString alloc] initWithUTF8String:"2"];

		check("array-identity",
		      [numbers indexOfObject:distinct] == 1 &&
		      [numbers indexOfObjectIdenticalTo:distinct] == NSNotFound &&
		      [numbers indexOfObjectIdenticalTo:[numbers objectAtIndex:1]] == 1,
		      "equality finds it where identity does not (a tagged literal is not the owned copy)");
	}


	{
		/*
		 * THE AUDITED COCOA INVENTORY for NSDictionary/NSMutableDictionary.
		 *
		 * `implemented` is Cocoa's documented surface and must all EXIST;
		 * `excluded` is what we deliberately do not ship and must all be ABSENT.
		 * The old check only confirmed what our own header declared — how
		 * +stringWithFormat:arguments: shipped missing — and this one named four
		 * gaps, all implemented in the same pass: +dictionaryWithObjects:forKeys:,
		 * -keysSortedByValueUsingSelector:, -keysSortedByValueUsingComparator: and
		 * -enumerateKeysAndObjectsUsingBlock:.
		 */
		static const char *classSelectors[] = {
			"dictionary", "dictionaryWithObject:forKey:", "dictionaryWithDictionary:",
			"dictionaryWithObjects:forKeys:count:", "dictionaryWithObjectsAndKeys:",
			"dictionaryWithObjects:forKeys:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithObject:forKey:", "initWithDictionary:",
			"initWithObjects:forKeys:count:", "initWithObjectsAndKeys:",
			"count", "objectForKey:", "objectForKeyedSubscript:",
			"allKeys", "allValues", "allKeysForObject:",
			"objectsForKeys:notFoundMarker:", "getObjects:andKeys:",
			"keysSortedByValueUsingSelector:", "keysSortedByValueUsingComparator:",
			"enumerateKeysAndObjectsUsingBlock:",
			"isEqualToDictionary:", "isEqual:", "hash", "description",
			"copy", "mutableCopy",
			"countByEnumeratingWithState:objects:count:", NULL
		};
		static const char *mutableClassSelectors[] = {
			"dictionary", "dictionaryWithCapacity:", NULL
		};
		static const char *mutableSelectors[] = {
			"initWithCapacity:", "setObject:forKey:", "setObject:forKeyedSubscript:",
			"removeObjectForKey:", "removeAllObjects",
			"addEntriesFromDictionary:", "setDictionary:",
			"removeObjectsForKeys:", NULL
		};
		static const char *excluded[] = {
			/* Needs NSEnumerator, which this Foundation does not ship; for-in
			 * covers the need and is what the tests exercise. */
			"keyEnumerator", "objectEnumerator",
			/* Needs KVC. */
			"valueForKey:", "setValue:forKey:",
			/* Needs a plist reader or writer. */
			"dictionaryWithContentsOfFile:", "initWithContentsOfFile:",
			"writeToFile:atomically:", "descriptionInStringsFileFormat",
			/* Needs NSURL. */
			"dictionaryWithContentsOfURL:", "initWithContentsOfURL:",
			"writeToURL:atomically:", NULL
		};
		NSDictionary *probe = [NSDictionary dictionary];
		NSMutableDictionary *mutable = [[NSMutableDictionary alloc] init];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSDictionary respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s\n", instanceSelectors[i]);
			}
		}
		for (i = 0; mutableClassSelectors[i] != NULL; i++) {
			if (![NSMutableDictionary respondsToSelector:sel_registerName(mutableClassSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s (mutable)\n", mutableClassSelectors[i]);
			}
		}
		for (i = 0; mutableSelectors[i] != NULL; i++) {
			if (![mutable respondsToSelector:sel_registerName(mutableSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s (mutable)\n", mutableSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION present but EXCLUDED: %s\n", excluded[i]);
			}
		}
		check("dict-api-complete", complete,
		      "the audited Cocoa inventory for NSDictionary/NSMutableDictionary");
	}


	{
		/* THE NIL-TERMINATED CONSTRUCTOR, on its own: the array family's version
		 * dropped its first element because va_start points PAST the argument
		 * named in the signature, and this one alternates value/key — the same
		 * trap, so it gets its own check with its own markers. */
		NSDictionary *pairs = nil;
		NSDictionary *arrays = nil;
		NSDictionary *copied = nil;
		id values[2];
		id keys[2];

		printf("FOUNDATION-COLLECTION step dict pairs\n");
		pairs = [NSDictionary dictionaryWithObjectsAndKeys:@"v1", @"k1", @"v2", @"k2", nil];
		values[0] = @"a"; values[1] = @"b";
		keys[0] = @"x"; keys[1] = @"y";
		printf("FOUNDATION-COLLECTION step dict arrays\n");
		arrays = [NSDictionary dictionaryWithObjects:values forKeys:keys count:2];
		printf("FOUNDATION-COLLECTION step dict copy\n");
		copied = [NSDictionary dictionaryWithDictionary:arrays];
		printf("FOUNDATION-COLLECTION step dict init varargs\n");
		{
			NSMutableDictionary *built =
				[[NSMutableDictionary alloc] initWithObjectsAndKeys:@"p", @"q", nil];

			check("dict-constructors",
			      [pairs count] == 2 &&
			      [[pairs objectForKey:@"k1"] isEqualToString:@"v1"] &&
			      [[pairs objectForKey:@"k2"] isEqualToString:@"v2"] &&
			      [arrays count] == 2 && [[arrays objectForKey:@"x"] isEqualToString:@"a"] &&
			      [copied isEqualToDictionary:arrays] &&
			      [built count] == 1 && [[built objectForKey:@"q"] isEqualToString:@"p"],
			      "the nil-terminated pairs keep BOTH halves (the array family dropped its first), the two-array form, and the copy constructor");
		}
	}

	{
		NSMutableDictionary *d = [NSMutableDictionary dictionary];
		NSArray *keys = nil;
		NSArray *values = nil;
		NSArray *found = nil;
		NSArray *missing = nil;

		[d setObject:@"one" forKey:@"1"];
		[d setObject:@"two" forKey:@"2"];
		[d setObject:@"two" forKey:@"deux"];
		[d setObject:@"drei" forKey:@"trois"];
		printf("FOUNDATION-COLLECTION step dict allKeys\n");
		keys = [d allKeys];
		printf("FOUNDATION-COLLECTION step dict allValues\n");
		values = [d allValues];
		printf("FOUNDATION-COLLECTION step dict allKeysForObject\n");
		found = [d allKeysForObject:@"two"];
		printf("FOUNDATION-COLLECTION step dict notFoundMarker\n");
		missing = [d objectsForKeys:[NSArray arrayWithObjects:@"1", @"nope", nil]
			       notFoundMarker:@"?"];

		check("dict-views",
		      [keys count] == 4 && [values count] == 4 && [found count] == 2 &&
		      [missing count] == 2 && [[missing objectAtIndex:0] isEqualToString:@"one"] &&
		      [[missing objectAtIndex:1] isEqualToString:@"?"] &&
		      [d count] == 4 && [[d allKeysForObject:@"nope"] count] == 0,
		      "allKeys/allValues, allKeysForObject: for a shared value, and objectsForKeys:notFoundMarker:");
	}

	{
		NSMutableDictionary *a = [NSMutableDictionary dictionary];
		NSMutableDictionary *b = [NSMutableDictionary dictionary];
		NSMutableDictionary *target = [NSMutableDictionary dictionary];
		int ok = 1;

		[a setObject:@"1" forKey:@"one"];
		[a setObject:@"3" forKey:@"three"];
		[b setObject:@"2" forKey:@"two"];
		printf("FOUNDATION-COLLECTION step dict addEntries\n");
		[target addEntriesFromDictionary:a];
		[target addEntriesFromDictionary:b];
		ok = ok && [target count] == 3 &&
		     [[target objectForKey:@"two"] isEqualToString:@"2"];
		printf("FOUNDATION-COLLECTION step dict setDictionary\n");
		[target setDictionary:b];
		ok = ok && [target count] == 1 && [target objectForKey:@"one"] == nil;
		printf("FOUNDATION-COLLECTION step dict removeObjectsForKeys\n");
		[target addEntriesFromDictionary:a];
		[target removeObjectsForKeys:[NSArray arrayWithObjects:@"one", @"three", nil]];
		ok = ok && [target count] == 1 && [[target objectForKey:@"two"] isEqualToString:@"2"];

		check("dict-bulk", ok,
		      "addEntriesFromDictionary: accumulates, setDictionary: replaces, removeObjectsForKeys: drops a list");
	}

	{
		NSMutableDictionary *d = [NSMutableDictionary dictionary];
		id __unsafe_unretained gotValues[3];
		id __unsafe_unretained gotKeys[3];
		unsigned long i;
		int sawOne = 0;

		[d setObject:@"1" forKey:@"one"];
		[d setObject:@"2" forKey:@"two"];
		[d setObject:@"3" forKey:@"three"];
		gotValues[0] = nil; gotKeys[0] = nil;
		gotValues[2] = nil; gotKeys[2] = nil;
		[d getObjects:gotValues andKeys:gotKeys];
		for (i = 0; i < 3; i++) {
			if (gotKeys[i] != nil && [gotKeys[i] isEqualToString:@"one"] &&
			    [gotValues[i] isEqualToString:@"1"]) {
				sawOne = 1;
			}
		}
		check("dict-getobjects",
		      sawOne && gotValues[0] != nil && gotKeys[1] != nil,
		      "getObjects:andKeys: fills parallel arrays with each pair intact");
	}


	{
		/*
		 * The array's block and comparator forms, EXERCISED. The inventory proves
		 * they exist; this proves they work, which is the difference between a
		 * declaration and an implementation.
		 */
		NSMutableArray *m = [NSMutableArray arrayWithObjects:@"b", @"a", @"c", nil];
		NSArray *sorted = [NSArray arrayWithObjects:@"a", @"b", @"c", nil];
		NSArray *reverse = [m sortedArrayUsingComparator:^NSComparisonResult(id left, id right) {
			return [right compare:left];
		}];
		__block NSUInteger seen = 0;
		__block int indexesOk = 1;
		__block int sawExpected = 1;
		NSUInteger insertion;

		[m enumerateObjectsUsingBlock:^(id object, NSUInteger index, BOOL *stop) {
			if (index != seen) {
				indexesOk = 0;
			}
			if (index == 0 && ![object isEqualToString:@"b"]) {
				sawExpected = 0;
			}
			seen++;
			if (index == 1) {
				*stop = YES;	/* stops after the second element */
			}
		}];
		[m sortUsingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];
		insertion = [sorted indexOfObject:@"b"
				    inSortedRange:NSMakeRange(0, 3)
					    options:NSBinarySearchingInsertionIndex
				    usingComparator:^NSComparisonResult(id left, id right) {
					    return [left compare:right];
				    }];

		check("array-blocks",
		      seen == 2 && indexesOk && sawExpected &&
		      [reverse count] == 3 && [[reverse objectAtIndex:0] isEqualToString:@"c"] &&
		      [[reverse objectAtIndex:2] isEqualToString:@"a"] &&
		      [[m objectAtIndex:0] isEqualToString:@"a"] &&
		      [[m objectAtIndex:2] isEqualToString:@"c"] &&
		      insertion == 2 &&
		      [sorted indexOfObject:@"a" inSortedRange:NSMakeRange(0, 3)
				     options:NSBinarySearchingFirstEqual
			     usingComparator:^NSComparisonResult(id left, id right) {
				     return [left compare:right];
			     }] == 0 &&
		      [sorted indexOfObject:@"zz" inSortedRange:NSMakeRange(0, 3)
				     options:NSBinarySearchingInsertionIndex
			     usingComparator:^NSComparisonResult(id left, id right) {
				     return [left compare:right];
			     }] == 3,
		      "enumerateObjectsUsingBlock: (index and stop), both comparator sorts, and the sorted-range search");
	}

	{
		/* The dictionary's block and keys-sorted-by-value forms, exercised. */
		NSMutableDictionary *d = [NSMutableDictionary dictionary];
		__block NSUInteger seen = 0;
		__block int pairsMatch = 1;
		NSArray *byComparator;
		NSArray *bySelector;

		[d setObject:[NSNumber numberWithInt:2] forKey:@"b"];
		[d setObject:[NSNumber numberWithInt:1] forKey:@"a"];
		[d setObject:[NSNumber numberWithInt:3] forKey:@"c"];
		[d enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *stop) {
			(void)stop;
			seen++;
			if (![[d objectForKey:key] isEqual:value]) {
				pairsMatch = 0;
			}
		}];
		byComparator = [d keysSortedByValueUsingComparator:^NSComparisonResult(id left, id right) {
			return [left compare:right];
		}];
		bySelector = [d keysSortedByValueUsingSelector:@selector(compare:)];

		check("dict-blocks",
		      seen == 3 && pairsMatch &&
		      [byComparator count] == 3 &&
		      [[byComparator objectAtIndex:0] isEqualToString:@"a"] &&
		      [[byComparator objectAtIndex:1] isEqualToString:@"b"] &&
		      [[byComparator objectAtIndex:2] isEqualToString:@"c"] &&
		      [[bySelector objectAtIndex:0] isEqualToString:@"a"] &&
		      [[bySelector objectAtIndex:2] isEqualToString:@"c"],
		      "enumerateKeysAndObjectsUsingBlock: pairs each key with its own value, and both keys-sorted-by-value forms order by the VALUES");
	}

	printf("FOUNDATION-COLLECTION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-COLLECTION DONE\n");
	return failc ? 1 : 0;
}
