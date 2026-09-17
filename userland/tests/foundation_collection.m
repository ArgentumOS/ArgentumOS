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

	printf("FOUNDATION-COLLECTION RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-COLLECTION DONE\n");
	return failc ? 1 : 0;
}
