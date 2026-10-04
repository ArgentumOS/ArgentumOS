/*
 * foundation_collection — NSArray, whose storage is a CFArray.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS PROVES, AND WHY THESE CHECKS. An ordered collection is where "an NS object IS a CF object" is
 * easiest to fake: a class keeping its own item vector and building a CFArray on demand would satisfy every
 * signature while the two worlds held different objects. So the last two checks are the ones that fail when
 * the class is a copy rather than a face:
 *
 *   * and-CFs-own-C-door-sees-this-object-as-a-CFArray — CFArrayGetCount, CF's own code, walked over an
 *     object this library built. That is the cast, and it is the whole claim;
 *   * the-array-holds-its-items-with-CFs-own-callbacks — an item put in, our reference dropped, must survive;
 *     and must die when the ARRAY is released. That is ownership done by CF rather than by hand.
 *
 * The rest are the doors themselves, checked against values rather than against their own implementation:
 * every one compares a result to an independently known answer, because a door that agrees with itself is
 * not evidence.
 */

#include <stdio.h>
#include <CoreFoundation/CFArray.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSObject.h>

static int ok_count = 0;
static int fail_count = 0;

static void note_value(const char *what, unsigned long value) {
	printf("FOUNDATION-COLLECTION note %s = 0x%lx\n", what, value);
	fflush(stdout);
}

static void check(const char *name, int ok, const char *detail) {
	if (ok) {
		ok_count++;
		printf("FOUNDATION-COLLECTION %s ok\n", name);
	} else {
		fail_count++;
		printf("FOUNDATION-COLLECTION %s FAIL %s\n", name, detail ? detail : "");
	}
	fflush(stdout);
}

/* The instrument for "alive" versus "quietly gone", as in the object probe: the base class cannot report its
 * own death, so a subclass counts it. */
static int probe_deallocs = 0;

@interface FNCollectionProbe : NSObject
@end

@implementation FNCollectionProbe
- (void)dealloc
{
	probe_deallocs++;
	[super dealloc];
}
@end

int main(void)
{
	FNCollectionProbe *a = [[FNCollectionProbe alloc] init];
	FNCollectionProbe *b = [[FNCollectionProbe alloc] init];
	FNCollectionProbe *c = [[FNCollectionProbe alloc] init];
	FNCollectionProbe *stranger = [[FNCollectionProbe alloc] init];
	NSArray *array = nil;
	id items[3];
	int n = 0;

	if (a == nil || b == nil || c == nil || stranger == nil) {
		printf("FOUNDATION-COLLECTION all-objects-allocated FAIL an object came back nil\n");
		printf("FOUNDATION-COLLECTION RESULT ok=0 fail=1\n");
		printf("FOUNDATION-COLLECTION DONE\n");
		fflush(stdout);
		return 1;
	}
	check("an-object-can-be-allocated-to-put-in", 1, NULL);

	items[0] = a;
	items[1] = b;
	items[2] = c;

	array = [[NSArray alloc] initWithObjects:items count:3];
	/* THE DECISIVE READ: the header word the instant the object exists, before ANY door runs. The library
	 * proved +alloc and the initialiser both leave it set, so a zero here means the object was never right,
	 * and a set value here means a later operation clears it. */
	if (array != nil) {
		const unsigned long *w = (const unsigned long *)array;
		note_value("IMMEDIATELY after creation: word 0", w[0]);
		note_value("IMMEDIATELY after creation: word 1", w[1]);
		note_value("IMMEDIATELY after creation: word 2", w[2]);
	}
	check("an-array-can-be-made-from-a-vector", array != nil, "the initializer answered nil");

	n = (array != nil) ? (int)[array count] : -1;
	note_value("count", (unsigned long)n);
	check("and-count-reports-what-was-put-in", n == 3, "count did not match the number of items");

	check("objectAtIndex-returns-the-object-that-went-in",
	      (array != nil) && ([array objectAtIndex:0] == a) && ([array objectAtIndex:1] == b) && ([array objectAtIndex:2] == c),
	      "a position did not return the very object that was placed there");

	check("the-subscript-door-agrees-with-objectAtIndex",
	      (array != nil) && ([array objectAtIndexedSubscript:1] == [array objectAtIndex:1]),
	      "the subscript door disagreed with objectAtIndex:");

	check("firstObject-and-lastObject-are-the-two-ends",
	      (array != nil) && ([array firstObject] == a) && ([array lastObject] == c),
	      "the ends are not the items that were placed at the ends");

	check("containsObject-finds-a-member", (array != nil) && [array containsObject:b],
	      "a member was not found");

	check("and-does-not-claim-a-stranger", (array != nil) && ![array containsObject:stranger],
	      "an object that was never placed was reported as a member");

	n = (array != nil) ? (int)[array indexOfObject:c] : -1;
	note_value("indexOfObject:", (unsigned long)n);
	check("indexOfObject-answers-the-position", n == 2, "the position did not match where it was placed");

	n = (array != nil) ? (int)[array indexOfObject:stranger] : 0;
	note_value("indexOfObject: for a stranger", (unsigned long)n);
	check("and-answers-CFs-sentinel-for-a-stranger", n == (int)kCFNotFound,
	      "a stranger did not answer kCFNotFound");

	{
		NSArray *empty = [[NSArray alloc] initWithObjects:NULL count:0];
		check("an-empty-array-has-no-first-and-no-last-object",
		      (empty != nil) && ([empty firstObject] == nil) && ([empty lastObject] == nil) && ([empty count] == 0),
		      "an empty array did not answer nil and zero");
		[empty release];
	}

	/* OWNERSHIP BY CF. One item, our reference dropped: it must live, because the array's callbacks hold it. */
	{
		NSArray *one = nil;
		id single[1];
		int before = probe_deallocs;
		int alive = 0;
		int gone = 0;

		single[0] = stranger;
		one = [[NSArray alloc] initWithObjects:single count:1];
		[stranger release];
		alive = (probe_deallocs == before);
		[one release];
		gone = (probe_deallocs == before + 1);

		note_value("deallocs before the array was released", (unsigned long)before);
		check("the-array-holds-its-items-with-CFs-own-callbacks", alive,
		      "the item died while the array still held it -- CF's callbacks did not retain it");
		check("and-the-array-is-what-ends-it", gone,
		      "the item outlived the array -- CF's callbacks did not release it");
	}

	/* THE REGISTRATION, MEASURED BEFORE THE DOOR THAT DEPENDS ON IT. CFGetTypeID answers what CF thinks an
	 * object IS, and it is the public form of the read-back CFNXBridgeClassToType returns. The pair below is
	 * the control and the subject: a CF STRING and OUR array, each asked which type CF says it has.
	 *   - the string's two numbers must match (CFString's registration is the one already proven);
	 *   - the array's two must match too, or the class was never registered for CFArray's type. */
	{
		CFStringRef cfText = CFStringCreateWithCString(kCFAllocatorDefault, "probe", kCFStringEncodingUTF8);
		if (cfText != NULL) {
			note_value("CFStringGetTypeID", (unsigned long)CFStringGetTypeID());
			note_value("CFGetTypeID of a CF string (control)", (unsigned long)CFGetTypeID(cfText));
			CFRelease(cfText);
		}
	}
	note_value("CFArrayGetTypeID", (unsigned long)CFArrayGetTypeID());
	/* THE HEADER WORD ITSELF, read straight out of the object, so a failed WRITE can be told from a reader
	 * that looks elsewhere. Word 1 is CF's info word and word 2 is the count, which must still be 1. */
	if (array != nil) {
		const unsigned long *words = (const unsigned long *)array;
		note_value("word 0 of the NSArray (its class)", words[0]);
		note_value("word 1 of the NSArray (CF's info word)", words[1]);
		note_value("word 2 of the NSArray (the count)", words[2]);
	}
	note_value("CFGetTypeID of the NSArray", (unsigned long)((array != nil) ? CFGetTypeID((CFTypeRef)array) : 0));

	/* CF'S OWN DOOR. The cast, performed on an object this library built. */
	n = (array != nil) ? (int)CFArrayGetCount((CFArrayRef)array) : -1;
	note_value("CFArrayGetCount on the NSArray", (unsigned long)n);
	check("and-CFs-own-C-door-sees-this-object-as-a-CFArray", n == 3,
	      "CF's own code did not accept this object as a CFArray");

	/* STRANGER WAS RELEASED ABOVE, so it must not be touched again. */
	[array release];
	[c release];
	[b release];
	[a release];

	printf("FOUNDATION-COLLECTION RESULT ok=%d fail=%d\n", ok_count, fail_count);
	printf("FOUNDATION-COLLECTION DONE\n");
	fflush(stdout);
	return (fail_count == 0) ? 0 : 1;
}
