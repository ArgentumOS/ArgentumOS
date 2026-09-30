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

/* A nil WHERE A NIL IS THE POINT: -objectForKey: and -compare: take a nonnull argument, and the
 * checks below hand them nil ON PURPOSE to see the refusal. Fetching it says so; writing the
 * literal at the call site would be a -Wnonnull finding of its own that had nothing to do with the
 * claim (the same shape as foundation_predicate's fn_no_predicate, and the same reason). */
static id fn_no_object(void)
{
	return nil;
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
		      [[NSArray array] count] == 0 &&
		      [[NSArray arrayWithObject:@"solo"] count] == 1,
		      "count/index/first/last/contains - the out-of-range case has ONE home, in foundation_core, where D10 asserts that it RAISES");
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
		      [d objectForKey:fn_no_object()] == nil &&
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
			"arrayWithObjects:",
			/* D7's kind (D), plist half: these SHIPPED, so the inventory demands them. */
			"arrayWithContentsOfFile:", "arrayWithContentsOfURL:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithObject:", "initWithObjects:count:", "initWithArray:",
			"initWithObjects:",
			"initWithContentsOfFile:", "initWithContentsOfURL:",
			"writeToFile:atomically:", "writeToURL:atomically:",
			"count", "objectAtIndex:", "objectAtIndexedSubscript:",
			"firstObject", "lastObject",
			"indexOfObject:", "indexOfObject:inRange:", "indexOfObjectIdenticalTo:",
			"indexOfObject:inSortedRange:options:usingComparator:", "containsObject:",
			"arrayByAddingObject:", "arrayByAddingObjectsFromArray:",
			"subarrayWithRange:", "getObjects:range:", "componentsJoinedByString:",
			"sortedArrayUsingSelector:", "sortedArrayUsingComparator:",
			"sortedArrayUsingDescriptors:", "sortedArrayUsingFunction:context:",
			"enumerateObjectsUsingBlock:",
			"objectsAtIndexes:", "indexesOfObjectsPassingTest:",
			"filteredArrayUsingPredicate:",		/* NSPredicate — F11a */
			"isEqualToArray:", "isEqual:", "hash", "description", "copy", "mutableCopy",
			"countByEnumeratingWithState:objects:count:",
			NULL
		};
		static const char *mutableClassSelectors[] = {
			"array", "arrayWithCapacity:", NULL
		};
		static const char *mutableSelectors[] = {
			"initWithCapacity:", "addObject:", "addObjectsFromArray:",
			"filterUsingPredicate:",		/* NSPredicate — F11a, the mutable form */
			"insertObject:atIndex:", "removeObjectAtIndex:", "removeLastObject",
			"removeObject:", "removeObject:inRange:",
			"removeObjectIdenticalTo:", "removeObjectIdenticalTo:inRange:",
			"removeObjectsInRange:", "removeAllObjects",
			"replaceObjectAtIndex:withObject:",
			"replaceObjectsInRange:withObjectsFromArray:",
			"replaceObjectsInRange:withObjectsFromArray:range:",
			"setArray:", "exchangeObjectAtIndex:withObjectAtIndex:",
			"sortUsingSelector:", "sortUsingComparator:",
			"sortUsingDescriptors:", "sortUsingFunction:context:",
			"insertObjects:atIndexes:", "removeObjectsAtIndexes:",
			"replaceObjectsAtIndexes:withObjects:",
			"setObject:atIndexedSubscript:", NULL
		};
		static const char *excluded[] = {
			/* NO predicate or descriptor name is left here: F10 moved the sort forms and F11a
			 * moved the filter into the REQUIRED lists above, which is the inventory rule working
			 * in the direction it was written for. What remains is not a class that is missing —
			 * it is the URL-taking FORM. */
			/* THE THREE URL FORMS USED TO BE LISTED HERE as "not shipped". They are
			 * IMPLEMENTED now (delegation to NSPropertyListSerialization with a root-class
			 * check — §11.6.1 D7's plist half), so they moved to the required lists above in
			 * the same change. */			NULL
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
		/* THE AUDITED COCOA INVENTORY for NSEnumerator — the class the array and
		 * dictionary enumerator methods hand back — plus the cursor walking a
		 * sequence, because a declared method that never runs proves nothing. */
		static const char *instanceSelectors[] = {
			"nextObject", "allObjects",
			"countByEnumeratingWithState:objects:count:",
			"isEqual:", "hash", "description", NULL
		};
		NSEnumerator *probe = [[NSArray arrayWithObject:@"x"] objectEnumerator];
		int complete = 1;
		int i;

		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s (enumerator)\n", instanceSelectors[i]);
			}
		}
		{
			NSArray *sequence = [NSArray arrayWithObjects:@"a", @"b", @"c", nil];
			NSEnumerator *forward = [sequence objectEnumerator];
			NSEnumerator *backward = [sequence reverseObjectEnumerator];
			NSEnumerator *spent = [sequence objectEnumerator];
			id object = nil;
			int ok = 1;

			if (![[forward nextObject] isEqualToString:@"a"] ||
			    ![[forward nextObject] isEqualToString:@"b"] ||
			    ![[forward nextObject] isEqualToString:@"c"]) {
				ok = 0;
			}
			/* A spent cursor answers nil, for ever, rather than wrapping. */
			if ([forward nextObject] != nil || [forward nextObject] != nil) {
				ok = 0;
			}
			if (![[backward nextObject] isEqualToString:@"c"] ||
			    ![[backward nextObject] isEqualToString:@"b"] ||
			    ![[backward nextObject] isEqualToString:@"a"] ||
			    [backward nextObject] != nil) {
				ok = 0;
			}
			object = [[[sequence objectEnumerator] allObjects] lastObject];
			if (![object isEqualToString:@"c"]) {
				ok = 0;
			}
			/* -allObjects takes what is LEFT and leaves the cursor spent. */
			if ([[spent allObjects] count] != 3 || [spent nextObject] != nil) {
				ok = 0;
			}
			check("enumerator-api-complete", complete && ok,
			      "the audited Cocoa inventory for NSEnumerator, and the cursor walks");
		}
	}

	{
		/* PLIST SERIALIZATION — a new class, so the audited inventory applies to
		 * it, AND a real round trip through the PUBLIC endpoints: Apple-shaped XML
		 * in, objects out, written back, and required to come home equal. The
		 * refusal clause is the contract that matters most: an UNKNOWN element must
		 * fail rather than be skipped, because libconfig reads these files through
		 * the same C core. */
		static const char *classSelectors[] = {
			"propertyListWithData:options:format:error:",
			"dataWithPropertyList:format:options:error:",
			"propertyList:isValidForFormat:", NULL
		};
		static const char *excluded[] = {
			/* NSStream forms (a dependency this library does not have), and the deprecation-era entry
			 * points - which §62.24 (2026-09-26) turned from policy exclusions into OWED rows: every
			 * entry below is unimplemented work, and this array is that distance to zero. */
			"propertyListWithStream:options:format:error:",
			"writePropertyList:toStream:format:options:error:",
			"propertyListFromData:mutabilityOption:format:errorDescription:",
			NULL
		};
		static const char document[] =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<plist version=\"1.0\"><dict>"
			"<key>a</key><integer>7</integer>"
			"<key>b</key><string>x &amp; y</string>"
			"<key>c</key><true/>"
			"<key>d</key><real>0.5</real>"
			"<key>e</key><data>AAEC</data>"
			"<key>f</key><array><string>one</string></array>"
			"</dict></plist>\n";
		static const char unknownElement[] =
			"<plist version=\"1.0\"><dict><key>a</key><widget/></dict></plist>";
		int complete = 1;
		int behaviour = 1;
		int i;
		NSData *input = [NSData dataWithBytes:document length:strlen(document)];
		NSError *error = nil;
		NSPropertyListFormat format = (NSPropertyListFormat)0;
		id parsed;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSPropertyListSerialization respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s (plist)\n", classSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([NSPropertyListSerialization respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION present but EXCLUDED (plist): %s\n", excluded[i]);
			}
		}
		parsed = [NSPropertyListSerialization propertyListWithData:input
								  options:NSPropertyListImmutable
								   format:&format
								    error:&error];
		if (![parsed isKindOfClass:[NSDictionary class]] ||
		    [[parsed objectForKey:@"a"] longLongValue] != 7 ||
		    ![[parsed objectForKey:@"b"] isEqualToString:@"x & y"] ||
		    ![[parsed objectForKey:@"c"] boolValue] ||
		    [[parsed objectForKey:@"d"] doubleValue] != 0.5 ||
		    [[parsed objectForKey:@"e"] length] != 3 ||
		    [[parsed objectForKey:@"f"] count] != 1 ||
		    format != NSPropertyListXMLFormat_v1_0) {
			behaviour = 0;
		}
		/* Written back, then read again: the object must come home EQUAL. */
		if (behaviour) {
			NSData *written = [NSPropertyListSerialization dataWithPropertyList:parsed
										     format:NSPropertyListXMLFormat_v1_0
										    options:0
										      error:&error];
			id again = (written != nil)
				? [NSPropertyListSerialization propertyListWithData:written
									    options:NSPropertyListImmutable
									     format:NULL
									      error:&error]
				: nil;

			if (again == nil || ![again isEqual:parsed]) {
				behaviour = 0;
			}
		}
		/* REFUSAL: an unknown element is an error that says why. */
		{
			NSData *bad = [NSData dataWithBytes:unknownElement length:strlen(unknownElement)];
			NSError *badError = nil;

			if ([NSPropertyListSerialization propertyListWithData:bad
								      options:NSPropertyListImmutable
								       format:NULL
									error:&badError] != nil ||
			    badError == nil) {
				behaviour = 0;
			}
		}
		/* The convenience CATEGORIES exist on the classes, and agree. */
		{
			NSString *text = [NSString stringWithUTF8String:document];
			id viaString = [text propertyList];

			if (viaString == nil || ![viaString isEqual:parsed]) {
				behaviour = 0;
			}
		}
		check("plist-serialization", complete && behaviour,
		      "the NSPropertyListSerialization inventory, a round trip through the public endpoints, -propertyList, and the refusal of an unknown element");
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
			"isEqual:", "hash", "description", "copy", "mutableCopy", 			"countOfIndexesInRange:",
			"indexGreaterThanOrEqualToIndex:", "indexLessThanOrEqualToIndex:",
			"getIndexes:maxCount:inIndexRange:",
			"enumerateRangesUsingBlock:", "enumerateRangesWithOptions:usingBlock:",
			"enumerateRangesInRange:options:usingBlock:",
NULL
		};
		static const char *mutableSelectors[] = {
			"addIndex:", "addIndexesInRange:", "removeIndex:",
			"removeIndexesInRange:", "removeAllIndexes", NULL
		};
		static const char *excluded[] = {
			/* The range- and buffer-based queries: a caller walks the set with
			 * -enumerateIndexesUsingBlock: instead. */
			/* FOUR MORE LEFT THIS LIST IN §23 and are DEMANDED above: the buffer form (whose
			 * in/out range contract turned out to be DOCUMENTED, with a worked example) and the
			 * three range enumerators.
			 *
			 * AND TWO WERE NEVER APPLE'S API AT ALL: -firstIndexInRange: and -lastIndexInRange:
			 * appear on no documented NSIndexSet page — the neighbours are -firstIndex/-lastIndex
			 * and -indexInRange:options:passingTest: — so they are REMOVED rather than left
			 * standing as a claim this probe could never honour. A REFUSAL THAT TURNS OUT TO BE
			 * CORRECT IS NOT THE PART THAT WAS WRONG; THE CLAIM WAS. */
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
		/* NSIndexPath: the ordered path (stage D). Construction in both shapes,
		 * O(1) position reads, the two derived forms, ordering, value semantics
		 * and our one-line -description — plus the REFUSALS, exercised as real
		 * exceptions because that is what the header documents: a position past
		 * -length, trimming an empty path, and -compare: with nil. */
		NSUInteger two[2] = { 3, 1 };
		NSIndexPath *path = [NSIndexPath indexPathWithIndexes:two length:2];
		NSIndexPath *same = [NSIndexPath indexPathWithIndexes:two length:2];
		NSIndexPath *single = [NSIndexPath indexPathWithIndex:5];
		NSIndexPath *extended = [path indexPathByAddingIndex:4];
		NSIndexPath *trimmed = [extended indexPathByRemovingLastIndex];
		NSIndexPath *reversed = [NSIndexPath indexPathWithIndexes:two length:1];	/* the {3} prefix */
		NSIndexPath *empty = [[NSIndexPath alloc] init];
		NSUInteger buffer[2] = { 0, 0 };
		NSUInteger slice[1] = { 0 };
		NSUInteger one = 0;
		BOOL refusedPosition = NO;
		BOOL refusedTrim = NO;
		BOOL refusedNil = NO;

		[path getIndexes:buffer];
		[path getIndexes:slice range:NSMakeRange(1, 1)];
		[single getIndexes:&one];

		check("indexpath-basics",
		      [path length] == 2 &&
		      [path indexAtPosition:0] == 3 && [path indexAtPosition:1] == 1 &&
		      buffer[0] == 3 && buffer[1] == 1 && slice[0] == 1 && one == 5 &&
		      [single length] == 1 &&
		      [extended length] == 3 && [extended indexAtPosition:2] == 4 &&
		      [trimmed isEqual:path] && [trimmed length] == 2 &&
		      [empty length] == 0 &&
		      [path isEqual:same] && [path hash] == [same hash] &&
		      ![path isEqual:reversed] && [reversed compare:path] == NSOrderedAscending &&
		      [path compare:same] == NSOrderedSame &&
		      [path compare:extended] == NSOrderedAscending &&
		      [path copy] == path &&	/* immutable: -copy is self */
		      [[path description] isEqualToString:@"<NSIndexPath: 2 position(s) 3-1>"],
		      "construction, O(1) positions, both derived forms, ordering, equality/hash and -description");
		@try {
			(void)[path indexAtPosition:2];
		} @catch (NSException *e) {
			refusedPosition = [[e name] isEqualToString:NSRangeException];
		}
		@try {
			(void)[empty indexPathByRemovingLastIndex];
		} @catch (NSException *e) {
			refusedTrim = [[e name] isEqualToString:NSRangeException];
		}
		@try {
			(void)[path compare:fn_no_object()];
		} @catch (NSException *e) {
			refusedNil = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("indexpath-refusals",
		      refusedPosition && refusedTrim && refusedNil,
		      "a position past -length and trimming an empty path raise NSRangeException; -compare: with nil raises NSInvalidArgumentException");
	}

	{
		/* The audited Cocoa inventory for NSIndexPath. The exclusions are the
		 * toolkit's additions (row/section/item are a UIKit-side category here, not
		 * Foundation). THE CODING PROTOCOLS USED TO BE NAMED HERE AS UNSHIPPED, and that
		 * claim went stale twice over: NSCoding, NSCoder and NSKeyedArchiver all ship, and
		 * NSIndexPath now CONFORMS (D7's kind (D)). */
		static const char *classSelectors[] = {
			"indexPathWithIndex:", "indexPathWithIndexes:length:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithIndex:", "initWithIndexes:length:",
			"indexAtPosition:", "length",
			"getIndexes:", "getIndexes:range:",
			"indexPathByAddingIndex:", "indexPathByRemovingLastIndex",
			"compare:", "isEqual:", "hash", "description",
			"copy",
			"initWithCoder:", "encodeWithCoder:",
			NULL
		};
		static const char *excluded[] = {
			"indexPathForRow:inSection:", "indexPathForItem:inSection:",
			"section", "row", "item",
			/* THE TWO NSCoding ENTRIES USED TO BE LISTED HERE. NSIndexPath implements them
			 * now, so they are DEMANDED above in the same change. */
			NULL
		};
		NSIndexPath *probe = [NSIndexPath indexPathWithIndex:1];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSIndexPath respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing +%s (indexpath)\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION missing -%s (indexpath)\n", instanceSelectors[i]);
			}
		}
		for (i = 0; excluded[i] != NULL; i++) {
			if ([probe respondsToSelector:sel_registerName(excluded[i])]) {
				complete = 0;
				printf("FOUNDATION-COLLECTION present but EXCLUDED (indexpath): %s\n", excluded[i]);
			}
		}
		check("indexpath-api-complete", complete,
		      "the audited Cocoa inventory for NSIndexPath");
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
			"dictionaryWithObjects:forKeys:",
			"dictionaryWithObjects:forKeys:",
			/* D7's kind (D), plist half: implemented in the SKIN, so DEMANDED here. */
			"dictionaryWithContentsOfFile:", "dictionaryWithContentsOfURL:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithObject:forKey:", "initWithDictionary:",
			"initWithObjects:forKeys:count:", "initWithObjectsAndKeys:",
			"initWithContentsOfFile:", "initWithContentsOfURL:",
			"writeToFile:atomically:", "writeToURL:atomically:",
			"count", "objectForKey:", "objectForKeyedSubscript:",
			"allKeys", "allValues", "allKeysForObject:",
			"objectsForKeys:notFoundMarker:", "getObjects:andKeys:",
			"keysSortedByValueUsingSelector:", "keysSortedByValueUsingComparator:",
			"enumerateKeysAndObjectsUsingBlock:",
			"keyEnumerator", "objectEnumerator",
			"isEqualToDictionary:", "isEqual:", "hash", "description",
			"copy", "mutableCopy",
			/* KVC SHIPS (F9, NSKeyValueCoding.h): NSDictionary answers
			 * -valueForKey: (a LOOKUP, or a fold on an @-led key) and inherits
			 * -setValue:forKey: from NSObject's NSKeyValueCoding category — which
			 * raises for a key it cannot reach, which is the right answer for an
			 * immutable dictionary. Both sat in the excluded list below for as long
			 * as KVC was a missing dependency. */
			"valueForKey:", "setValue:forKey:",
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
			/* A strings-file form is NOT a property list: it needs its own
			 * writer, so it stays excluded while the plist forms above ship. */
			"descriptionInStringsFileFormat",
			/* NSURL SHIPS (F8); the URL-taking FORM is what is absent, and the plist
			 * forms above take paths. */
			/* THE THREE URL FORMS USED TO BE LISTED HERE as not shipped. They are
			 * IMPLEMENTED now (in the skin), so they are DEMANDED in the required lists
			 * above instead - the half of the inventory that keeps this honest. */
			NULL
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
		/* THE FIRST-MATCH SEARCH (§63.8), and the property that separates it from the exhaustive
		 * -indexesOfObjectsPassingTest: is HOW MANY TIMES the predicate runs: the walk must STOP at the
		 * match, because a predicate may have side effects. The CALL COUNT is asserted rather than the index
		 * alone, which would pass for a full scan that happened to find the right element first. */
		NSArray *items = [NSArray arrayWithObjects:@"one", @"two", @"three", @"four", nil];
		__block NSUInteger calls = 0;
		__block NSUInteger exhaustiveCalls = 0;
		NSUInteger found = [items indexOfObjectPassingTest:^BOOL(id object, NSUInteger index, BOOL *stop) {
			calls++;
			return [object isEqualToString:@"two"];
		}];
		NSIndexSet *matched = [items indexesOfObjectsPassingTest:
			^BOOL(id object, NSUInteger index, BOOL *stop) {
			exhaustiveCalls++;
			return index >= 2;
		}];
		NSUInteger missing = [items indexOfObjectPassingTest:
			^BOOL(id object, NSUInteger index, BOOL *stop) {
			return [object isEqualToString:@"absent"];
		}];

		check("array-first-match-search",
		      found == 1 && calls == 2 &&
		      matched != nil && [matched count] == 2 &&
		      [matched containsIndex:2] && [matched containsIndex:3] &&
		      exhaustiveCalls == 4 && missing == NSNotFound,
		      "the first-match walk STOPS at the match (2 predicate calls for a match at index 1), the indexes form visits all four, and an absent value answers NSNotFound");
	}

	{
		/* THE ARRAY'S NSCoding DOORS (§63.12), driven DIRECTLY — the only way to reach them, since the
		 * archiver's structural branch recognises an array by KIND and never asks the class. A mutable array
		 * is used as the source so the ROUND TRIP also shows the class-choosing rule: the immutable front must
		 * answer an immutable array and the mutable class a mutable one, from the same bytes. */
		NSMutableArray *source = [NSMutableArray arrayWithObjects:@"a", @"b", @"c", nil];
		NSMutableData *buffer = [NSMutableData data];
		NSKeyedArchiver *writer = [[NSKeyedArchiver alloc] initForWritingWithMutableData:buffer];
		NSArray *back;
		NSMutableArray *mutableBack;

		[source encodeWithCoder:writer];
		[writer finishEncoding];
		back = [[NSArray alloc] initWithCoder:
			[[NSKeyedUnarchiver alloc] initForReadingWithData:buffer]];
		mutableBack = [[NSMutableArray alloc] initWithCoder:
			[[NSKeyedUnarchiver alloc] initForReadingWithData:buffer]];
		check("array-nscoding-doors",
		      [source conformsToProtocol:@protocol(NSCoding)] &&
		      [NSMutableArray conformsToProtocol:@protocol(NSCoding)] &&
		      back != nil && [back count] == 3 &&
		      [[back objectAtIndex:1] isEqualToString:@"b"] &&
		      ![back isKindOfClass:[NSMutableArray class]] &&
		      mutableBack != nil && [mutableBack count] == 3 &&
		      [mutableBack isKindOfClass:[NSMutableArray class]],
		      "the pair round-trips through the class's own doors, and the class-choosing rule survives it (an immutable front answers an immutable array, the mutable class a mutable one)");
	}

	{
		/* THE DICTIONARY'S NSCoding DOORS (§63.13), the pair that is NOT a substitution: TWO payloads, written
		 * and read as a PAIR. The check asserts the PAIRING rather than the contents — a decoder that read the
		 * two arrays and rebuilt a dictionary from them in the WRONG ORDER, or that zipped them one position
		 * out, would keep every key and every value and still be wrong; `-objectForKey:` for each key is what
		 * catches that. The source is a MUTABLE dictionary so the class-choosing rule is exercised too. */
		NSMutableDictionary *source = [NSMutableDictionary dictionary];
		NSMutableData *buffer = [NSMutableData data];
		NSKeyedArchiver *writer;
		NSDictionary *back;
		NSMutableDictionary *mutableBack;

		[source setObject:@"one" forKey:@"a"];
		[source setObject:@"two" forKey:@"b"];
		[source setObject:@"three" forKey:@"c"];
		writer = [[NSKeyedArchiver alloc] initForWritingWithMutableData:buffer];
		[source encodeWithCoder:writer];
		[writer finishEncoding];
		back = [[NSDictionary alloc] initWithCoder:
			[[NSKeyedUnarchiver alloc] initForReadingWithData:buffer]];
		mutableBack = [[NSMutableDictionary alloc] initWithCoder:
			[[NSKeyedUnarchiver alloc] initForReadingWithData:buffer]];
		check("dict-nscoding-doors",
		      [source conformsToProtocol:@protocol(NSCoding)] &&
		      [NSMutableDictionary conformsToProtocol:@protocol(NSCoding)] &&
		      back != nil && [back count] == 3 &&
		      [[back objectForKey:@"a"] isEqualToString:@"one"] &&
		      [[back objectForKey:@"b"] isEqualToString:@"two"] &&
		      [[back objectForKey:@"c"] isEqualToString:@"three"] &&
		      ![back isKindOfClass:[NSMutableDictionary class]] &&
		      mutableBack != nil && [mutableBack count] == 3 &&
		      [[mutableBack objectForKey:@"c"] isEqualToString:@"three"] &&
		      [mutableBack isKindOfClass:[NSMutableDictionary class]],
		      "the key/value PAIRING survives the round trip (checked key by key, not by count), and the class-choosing rule survives it too");
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

	{
		/* EXPERIMENT (a) OF §11.6.1 D6: CALL THE HOOK DIRECTLY, which isolates the handler and the
		 * raise from clang's loop entirely. Two outcomes and both are informative: if it raises
		 * and is caught, this library's definition is the one that runs and the crash lives in the
		 * loop; if the process instead prints "Mutation occurred during enumeration." and aborts,
		 * the RUNTIME'S default is still in force and that abort IS the crash. */
		NSMutableArray *items = [NSMutableArray arrayWithObjects:@"a", @"b", nil];
		BOOL caught = NO;
		NSString *name = nil;

		@try {
			objc_enumerationMutation(items);
		} @catch (NSException *e) {
			caught = YES;
			name = [e name];
		}
		check("mutation-handler-direct",
		      caught && name != nil && [name isEqualToString:NSGenericException],
		      caught ? [name UTF8String]
			: "objc_enumerationMutation returned instead of raising");
	}

	{
		/* EXPERIMENT (b): the same hook through clang's fast-enumeration check, which is where the
		 * crash was. The two checks in one run are the point: printed checks SURVIVE a crash, so
		 * whatever this run prints is the answer either way. */
		NSMutableArray *items = [NSMutableArray arrayWithObjects:@"a", @"b", @"c", nil];
		BOOL caught = NO;
		NSString *name = nil;
		NSUInteger rounds = 0;

		@try {
			for (NSString *item in items) {
				(void)item;
				rounds++;
				[items addObject:@"d"];
			}
		} @catch (NSException *e) {
			caught = YES;
			name = [e name];
		}
		check("fast-enum-mutation-raises",
		      caught && name != nil && [name isEqualToString:NSGenericException] && rounds >= 1,
		      caught ? [[NSString stringWithFormat:@"caught %@ after %lu round(s)",
				name, (unsigned long)rounds] UTF8String]
			: "NO exception: mutation during enumeration is not detected");
	}

	{
		/* THE PLIST FILE FORM round-trips, and a plist whose ROOT IS NOT AN ARRAY is refused
		 * rather than coerced (D7's kind (D), plist half). Split per call: printed checks survive
		 * a fault, so the last name printed localizes it. */
		NSArray *out = [NSArray arrayWithObjects:@"one", @"two", nil];
		NSString *path = @"/System/Temporary Files/fnarray-plist";
		BOOL wrote = [out writeToFile:path atomically:YES];
		NSArray *back = wrote ? [NSArray arrayWithContentsOfFile:path] : nil;

		check("array-plist-file",
		      wrote && back != nil && [back isEqualToArray:out],
		      [[NSString stringWithFormat:@"wrote=%d back=%lu",
			(int)wrote, (unsigned long)(back != nil ? [back count] : 0)] UTF8String]);
		remove([path UTF8String]);
	}

	{
		NSArray *out = [NSArray arrayWithObjects:@"one", @"two", nil];
		NSString *path = @"/System/Temporary Files/fnarray-plist-url";
		NSURL *url = [NSURL fileURLWithPath:path];
		BOOL wrote = [out writeToURL:url atomically:YES];
		NSArray *back = wrote ? [NSArray arrayWithContentsOfURL:url] : nil;

		check("array-plist-url",
		      wrote && back != nil && [back isEqualToArray:out],
		      [[NSString stringWithFormat:@"wrote=%d back=%lu",
			(int)wrote, (unsigned long)(back != nil ? [back count] : 0)] UTF8String]);
		remove([path UTF8String]);
	}

	{
		/* A DICTIONARY PLIST READ AS AN ARRAY ANSWERS NIL: the root-class check is the point,
		 * and it is measured through the FILE form rather than asserted in prose. */
		NSString *path = @"/System/Temporary Files/fnarray-plist-wrong";
		NSData *dictData = [NSPropertyListSerialization
			dataWithPropertyList:[NSDictionary dictionaryWithObject:@"v" forKey:@"k"]
				      format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
		BOOL wrote = [dictData writeToFile:path atomically:YES];
		NSArray *wrong = [NSArray arrayWithContentsOfFile:path];

		check("array-plist-wrong-root",
		      wrote && wrong == nil,
		      [[NSString stringWithFormat:@"wrote=%d wrongRoot=%d",
			(int)wrote, (int)(wrong == nil)] UTF8String]);
		remove([path UTF8String]);
	}

	{
		/* NSDictionary'S PLIST FORMS, mirroring NSArray's: a file round trip, a URL round trip, and
		 * a plist whose ROOT IS AN ARRAY refused rather than coerced (D7's kind (D), plist half).
		 * One call per check, because printed checks survive a fault. */
		NSDictionary *out = [NSDictionary dictionaryWithObjectsAndKeys:@"v", @"k", nil];
		NSString *path = @"/System/Temporary Files/fndict-plist";
		BOOL wrote = [out writeToFile:path atomically:YES];
		NSDictionary *back = wrote ? [NSDictionary dictionaryWithContentsOfFile:path] : nil;

		check("dictionary-plist-file",
		      wrote && back != nil && [back isEqualToDictionary:out],
		      [[NSString stringWithFormat:@"wrote=%d back=%lu",
			(int)wrote, (unsigned long)(back != nil ? [back count] : 0)] UTF8String]);
		remove([path UTF8String]);
	}

	{
		NSDictionary *out = [NSDictionary dictionaryWithObjectsAndKeys:@"v", @"k", nil];
		NSString *path = @"/System/Temporary Files/fndict-plist-url";
		NSURL *url = [NSURL fileURLWithPath:path];
		BOOL wrote = [out writeToURL:url atomically:YES];
		NSDictionary *back = wrote ? [NSDictionary dictionaryWithContentsOfURL:url] : nil;

		check("dictionary-plist-url",
		      wrote && back != nil && [back isEqualToDictionary:out],
		      [[NSString stringWithFormat:@"wrote=%d back=%lu",
			(int)wrote, (unsigned long)(back != nil ? [back count] : 0)] UTF8String]);
		remove([path UTF8String]);
	}

	{
		NSString *path = @"/System/Temporary Files/fndict-plist-wrong";
		NSData *arrayData = [NSPropertyListSerialization
			dataWithPropertyList:[NSArray arrayWithObject:@"x"]
				      format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
		BOOL wrote = [arrayData writeToFile:path atomically:YES];
		NSDictionary *wrong = [NSDictionary dictionaryWithContentsOfFile:path];

		check("dictionary-plist-wrong-root",
		      wrote && wrong == nil,
		      [[NSString stringWithFormat:@"wrote=%d wrongRoot=%d",
			(int)wrote, (int)(wrong == nil)] UTF8String]);
		remove([path UTF8String]);
	}

	{
		/* NSIndexPath'S NSCoding PAIR, through our own archiver (D7's kind (D)): the positions go
		 * out as ONE BYTE BLOB, which is Apple's own trick for this class and also what keeps it
		 * from depending on NSArray conforming first. */
		NSUInteger positions[3] = { 3, 1, 4 };
		NSIndexPath *out = [NSIndexPath indexPathWithIndexes:positions length:3];
		NSData *archive = [NSKeyedArchiver archivedDataWithRootObject:out];
		NSIndexPath *back = archive != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:archive] : nil;

		check("indexpath-nscoding-round-trip",
		      archive != nil && back != nil && [back length] == 3 && [back isEqual:out],
		      [[NSString stringWithFormat:@"archive=%lu back=%lu equal=%d",
			(unsigned long)(archive != nil ? [archive length] : 0),
			(unsigned long)(back != nil ? [back length] : 0),
			(int)(back != nil && [back isEqual:out])] UTF8String]);
	}

	{
		/* THE RANGE-BASED QUERIES (D7's kind (D)). Two ranges - 10..14 and 30..31 - so a count
		 * over a range that STRADDLES both is not accidentally equal to either, and each
		 * neighbour has an answer inside a range, between ranges, and off the end. */
		NSMutableIndexSet *set = [NSMutableIndexSet indexSet];

		[set addIndexesInRange:NSMakeRange(10, 5)];
		[set addIndexesInRange:NSMakeRange(30, 2)];
	{
		/* THE BUFFER FORM (§23): APPLE'S OWN WORKED EXAMPLE IS THE ASSERTION — for the contiguous
		 * indexes 1-100, asking with range (1,100) and a buffer of 20 copies 1-20 and leaves the
		 * range as (21,80) — plus the NULL range (every index) and a SPARSE set, where the remainder
		 * comes from the LAST INDEX COPIED rather than from the count, which is where a count-based
		 * implementation would differ. */
		NSMutableIndexSet *set = [NSMutableIndexSet indexSet];
		NSUInteger buffer[20];
		NSUInteger written;
		NSUInteger small[2];
		NSRange rest = NSMakeRange(1, 100);
		NSRange all = NSMakeRange(0, 0);
		NSRange sparse = NSMakeRange(0, 50);
		NSUInteger sparseWritten;

		[set addIndexesInRange:NSMakeRange(1, 100)];
		written = [set getIndexes:buffer maxCount:20 inIndexRange:&rest];
		all.length = 0;
		(void)[set getIndexes:small maxCount:0 inIndexRange:&all];
		[set removeAllIndexes];
		[set addIndex:5];
		[set addIndex:40];
		[set addIndex:41];
		sparseWritten = [set getIndexes:small maxCount:2 inIndexRange:&sparse];

		check("indexset-buffer-form",
		      written == 20 && buffer[0] == 1 && buffer[19] == 20 &&
		      rest.location == 21 && rest.length == 80 &&
		      sparseWritten == 2 && small[0] == 5 && small[1] == 40 &&
		      sparse.location == 41 && sparse.length == 9,
		      [[NSString stringWithFormat:@"written=%lu first=%lu last=%lu rest=(%lu,%lu) sparse=(%lu,%lu)",
			(unsigned long)written, (unsigned long)buffer[0], (unsigned long)buffer[19],
			(unsigned long)rest.location, (unsigned long)rest.length,
			(unsigned long)sparse.location, (unsigned long)sparse.length] UTF8String]);
	}

	{
		/* THE RANGE ENUMERATORS: the block sees the receiver's OWN ranges — this class IS a range
		 * list — ascending, or descending under NSEnumerationReverse, and it may STOP the walk. The
		 * in-range form sees the INTERSECTION, and a range that does not overlap is not reported at
		 * all. */
		NSMutableIndexSet *set = [NSMutableIndexSet indexSet];
		NSMutableArray *forward = [[NSMutableArray alloc] init];
		NSMutableArray *backward = [[NSMutableArray alloc] init];
		NSMutableArray *clipped = [[NSMutableArray alloc] init];
		__block NSUInteger stopped = 0;

		[set addIndexesInRange:NSMakeRange(2, 3)];	/* 2-4 */
		[set addIndexesInRange:NSMakeRange(10, 2)];	/* 10-11 */
		[set enumerateRangesUsingBlock:^(NSRange range, BOOL *stop) {
			[forward addObject:[NSValue valueWithRange:range]];
		}];
		[set enumerateRangesWithOptions:NSEnumerationReverse usingBlock:^(NSRange range, BOOL *stop) {
			[backward addObject:[NSValue valueWithRange:range]];
		}];
		[set enumerateRangesInRange:NSMakeRange(3, 6) options:0 usingBlock:^(NSRange range, BOOL *stop) {
			[clipped addObject:[NSValue valueWithRange:range]];
		}];
		[set enumerateRangesUsingBlock:^(NSRange range, BOOL *stop) {
			stopped++;
			*stop = YES;
		}];

		check("indexset-enumerate-ranges",
		      [forward count] == 2 &&
		      [[forward objectAtIndex:0] rangeValue].location == 2 &&
		      [[forward objectAtIndex:1] rangeValue].location == 10 &&
		      [backward count] == 2 &&
		      [[backward objectAtIndex:0] rangeValue].location == 10 &&
		      [[backward objectAtIndex:1] rangeValue].location == 2 &&
		      [clipped count] == 1 &&
		      [[clipped objectAtIndex:0] rangeValue].location == 3 &&
		      [[clipped objectAtIndex:0] rangeValue].length == 2 &&
		      stopped == 1,
		      [[NSString stringWithFormat:@"forward=%lu backward=%lu clipped=%lu stopped=%lu",
			(unsigned long)[forward count], (unsigned long)[backward count],
			(unsigned long)[clipped count], (unsigned long)stopped] UTF8String]);
	}

		check("indexset-range-queries",
		      [set countOfIndexesInRange:NSMakeRange(12, 20)] == 5 &&
		      [set countOfIndexesInRange:NSMakeRange(100, 5)] == 0 &&
		      [set countOfIndexesInRange:NSMakeRange(0, 11)] == 1 &&
		      [set indexGreaterThanOrEqualToIndex:12] == 12 &&
		      [set indexGreaterThanOrEqualToIndex:15] == 30 &&
		      [set indexGreaterThanOrEqualToIndex:32] == NSNotFound &&
		      [set indexLessThanOrEqualToIndex:29] == 14 &&
		      [set indexLessThanOrEqualToIndex:31] == 31 &&
		      [set indexLessThanOrEqualToIndex:9] == NSNotFound,
		      [[NSString stringWithFormat:@"count=%lu/%lu/%lu ge=%lu/%lu/%lu le=%lu/%lu/%lu",
			(unsigned long)[set countOfIndexesInRange:NSMakeRange(12, 20)],
			(unsigned long)[set countOfIndexesInRange:NSMakeRange(100, 5)],
			(unsigned long)[set countOfIndexesInRange:NSMakeRange(0, 11)],
			(unsigned long)[set indexGreaterThanOrEqualToIndex:12],
			(unsigned long)[set indexGreaterThanOrEqualToIndex:15],
			(unsigned long)[set indexGreaterThanOrEqualToIndex:32],
			(unsigned long)[set indexLessThanOrEqualToIndex:29],
			(unsigned long)[set indexLessThanOrEqualToIndex:31],
			(unsigned long)[set indexLessThanOrEqualToIndex:9]] UTF8String]);
	}

	printf("FOUNDATION-COLLECTION RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-COLLECTION-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-COLLECTION DONE\n");
	return failc ? 1 : 0;
}
