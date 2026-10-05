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
#include <CoreFoundation/CFRuntime.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSException.h>
#import <Foundation/NSObject.h>
#import <objc/runtime.h>

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
 * own death, so a subclass counts it. AND FOR THE TWO makeObjectsPerformSelector: DOORS the same class is the
 * instrument for "the message actually arrived" — a counter for the no-argument form and a recorded argument
 * for the one that takes an object, so the check can assert WHAT was sent rather than merely that something
 * was. */
static int probe_deallocs = 0;
static int probe_bumps = 0;
static id probe_last_argument = nil;

@interface FNCollectionProbe : NSObject
- (void)bump;
- (void)bumpWith:(id)anObject;
@end

@implementation FNCollectionProbe
- (void)dealloc
{
	probe_deallocs++;
	[super dealloc];
}

- (void)bump
{
	probe_bumps++;
}

- (void)bumpWith:(id)anObject
{
	probe_bumps++;
	probe_last_argument = anObject;
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

	/* THE ROOT-CLASS CLAIM, ASKED OF THE RUNTIME. NSArray is not an NSObject subclass any more, so its
	 * conformance to the NSObject protocol is a DECLARATION the class makes rather than one it inherits — and
	 * the runtime is what answers the question, so the runtime is what this asks. (The METHOD of that name,
	 * -conformsToProtocol:, is not implemented in this library at all; that is a separate and named gap, so
	 * the runtime function is the honest instrument here.) */
	check("the-class-declares-the-nsobject-protocol-itself",
	      (array != nil) && class_conformsToProtocol(object_getClass(array), @protocol(NSObject)),
	      "the runtime does not report NSArray as conforming to the NSObject protocol");

	/* AND THE WORD COLLISION THE ROOT-CLASS CHANGE EXISTS TO PREVENT. NSObject answers -retainCount from its
	 * own `_refcount`, which sits at offset 16 — the same offset as CFArray's `_count` — so an NSObject
	 * SUBCLASS reports the ELEMENT COUNT as a retain count. The assertion is therefore a pair: this door
	 * agrees with CF's own answer, AND it is not the element count. */
	{
		unsigned long rc = (array != nil) ? (unsigned long)[(id)array retainCount] : 0;
		unsigned long cf = (array != nil) ? (unsigned long)CFGetRetainCount((CFTypeRef)array) : 0;

		note_value("[array retainCount]", rc);
		note_value("CFGetRetainCount of the NSArray", cf);
		check("retainCount-answers-CFs-count-and-not-the-element-count",
		      (array != nil) && (rc == cf) && (rc != 3),
		      "retainCount disagreed with CF's own answer, or answered the element count");
	}

	n = (array != nil) ? (int)[array count] : -1;
	note_value("count", (unsigned long)n);
	check("and-count-reports-what-was-put-in", n == 3, "count did not match the number of items");

	check("objectAtIndex-returns-the-object-that-went-in",
	      (array != nil) && ([array objectAtIndex:0] == a) && ([array objectAtIndex:1] == b) && ([array objectAtIndex:2] == c),
	      "a position did not return the very object that was placed there");

	check("the-subscript-door-agrees-with-objectAtIndex",
	      (array != nil) && ([array objectAtIndexedSubscript:1] == [array objectAtIndex:1]),
	      "the subscript door disagreed with objectAtIndex:");

	/* AND AN INDEX PAST THE END RAISES — the door's OTHER half, and the one that was missing entirely before:
	 * CFArrayGetValueAtIndex is CF's UNCHECKED accessor, so `[array objectAtIndex:3]` on a three-element array
	 * used to read memory the array does not own. The NAME is asserted rather than the mere fact of a throw,
	 * because "something was thrown" would also pass for a fault turned into an exception by another layer. */
	{
		BOOL raised = NO;
		NSString *raisedName = nil;

		if (array != nil) {
			@try {
				(void)[array objectAtIndex:3];
			} @catch (NSException *e) {
				raised = YES;
				raisedName = [e name];
			}
		}
		note_value("[array objectAtIndex:3] raised", (unsigned long)raised);
		check("and-an-index-past-the-end-raises",
		      raised && raisedName != nil && [raisedName isEqual:NSRangeException],
		      "an index past the end did not raise NSRangeException");
	}

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

	/* THE SENTINEL IS APPLE'S, AND THE WIDTH OF THE COMPARISON IS THE POINT. This check used to read
	 * `(int)[array indexOfObject:stranger] == (int)kCFNotFound`, which is a trap twice over: narrowing
	 * NSNotFound (NSIntegerMax) to `int` gives -1, the same value CF's kCFNotFound has, so an `int` comparison
	 * cannot tell the two sentinels apart AT ALL. The door's answer is now compared as what it is —
	 * NSUInteger against NSNotFound — and separately asserted to be the Cocoa number. */
	{
		NSUInteger found = (array != nil) ? (NSUInteger)[array indexOfObject:stranger] : 0;

		note_value("indexOfObject: for a stranger", (unsigned long)found);
		note_value("NSNotFound", (unsigned long)NSNotFound);
		check("and-answers-NSNotFound-for-a-stranger", (array != nil) && (found == NSNotFound),
		      "a stranger did not answer NSNotFound");
		check("and-that-sentinel-is-not-CFs-minus-one", (unsigned long)NSNotFound != (unsigned long)kCFNotFound,
		      "NSNotFound and CF's kCFNotFound are the same number, so the translation is unobservable");
	}

	{
		NSArray *empty = [[NSArray alloc] initWithObjects:NULL count:0];
		check("an-empty-array-has-no-first-and-no-last-object",
		      (empty != nil) && ([empty firstObject] == nil) && ([empty lastObject] == nil) && ([empty count] == 0),
		      "an empty array did not answer nil and zero");
		[empty release];
	}

	/* ---------------------------------------------------------------------------------------------
	 * THE SURFACE, PART ONE — one check per door, and every one compares against an INDEPENDENTLY KNOWN
	 * answer rather than against another door of the same class. `array` is {a, b, c} throughout.
	 * ------------------------------------------------------------------------------------------- */

	/* THE RANGE-TAKING SEARCH, WHICH IS THE DOOR THAT NEEDS NSRange. c sits at index 2, so a search confined
	 * to {0, 2} must MISS it while the same search for b hits — the pair is what shows the range is honoured
	 * rather than ignored. */
	{
		NSUInteger inRange = (array != nil) ? (NSUInteger)[array indexOfObject:b inRange:NSMakeRange(0, 2)] : 0;
		NSUInteger outside = (array != nil) ? (NSUInteger)[array indexOfObject:c inRange:NSMakeRange(0, 2)] : 0;

		note_value("indexOfObject:inRange: of b in {0,2}", (unsigned long)inRange);
		note_value("indexOfObject:inRange: of c in {0,2}", (unsigned long)outside);
		check("indexOfObject-inRange-finds-inside-the-range", (array != nil) && (inRange == 1),
		      "a member inside the range was not found");
		check("and-misses-outside-it", (array != nil) && (outside == NSNotFound),
		      "a member OUTSIDE the range was reported found");
	}

	/* IDENTITY SEARCH. These probe objects compare by identity, so this check can only assert the happy path
	 * and the miss — which is enough to show the walk returns positions and the Apple sentinel. */
	{
		NSUInteger found = (array != nil) ? (NSUInteger)[array indexOfObjectIdenticalTo:a] : 0;
		NSUInteger missed = (array != nil) ? (NSUInteger)[array indexOfObjectIdenticalTo:stranger] : 0;

		check("indexOfObjectIdenticalTo-finds-the-same-pointer", (array != nil) && (found == 0),
		      "the identical search did not find the object at index 0");
		check("and-answers-NSNotFound-for-an-object-it-never-held", (array != nil) && (missed == NSNotFound),
		      "the identical search found an object that was never placed");
	}

	/* THE BULK ACCESSOR, FILLED FROM THE MIDDLE SO A DOOR THAT IGNORED `range` WOULD FAIL: {1, 2} is {b, c}. */
	{
		id buffer[2];
		int filled = 0;

		buffer[0] = nil;
		buffer[1] = nil;
		if (array != nil) {
			[array getObjects:buffer range:NSMakeRange(1, 2)];
			filled = 1;
		}
		check("getObjects-range-fills-from-the-range-given",
		      filled && buffer[0] == b && buffer[1] == c,
		      "getObjects:range: did not write the range's objects into the buffer");
	}

	/* DERIVATION. The new array must have its OWN storage (one longer) and must not disturb the original. */
	{
		NSArray *longer = (array != nil) ? [array arrayByAddingObject:stranger] : nil;
		NSArray *joined = (array != nil) ? [array arrayByAddingObjectsFromArray:longer] : nil;
		NSArray *middle = (array != nil) ? [array subarrayWithRange:NSMakeRange(1, 2)] : nil;

		check("arrayByAddingObject-grows-by-one-with-that-object",
		      longer != nil && [longer count] == 4 && [longer objectAtIndex:3] == stranger
		      && [array count] == 3,
		      "arrayByAddingObject: did not answer a four-element array ending in the object");
		check("arrayByAddingObjectsFromArray-appends-the-other-array",
		      joined != nil && [joined count] == 7 && [joined objectAtIndex:6] == stranger,
		      "arrayByAddingObjectsFromArray: did not append the other array's objects");
		check("subarrayWithRange-takes-the-elements-in-the-range",
		      middle != nil && [middle count] == 2 && [middle objectAtIndex:0] == b && [middle objectAtIndex:1] == c,
		      "subarrayWithRange: did not answer the elements inside the range");
		/* THE DERIVED ARRAYS ARE OURS TO RELEASE (+1), which is the deviation the header states. */
		[longer release];
		[joined release];
		[middle release];
	}

	/* COMPARISON: an equal array built separately, a different one, and one that shares NOTHING.
	 *
	 * `other` IS DELIBERATELY NOT THE "NOTHING SHARED" CASE even though it differs from `array`: it is
	 * {a, b, stranger}, so it shares a and b — and the first version of this check asserted nil against it and
	 * failed, correctly, because the door answered `a`. A disjoint array has to be disjoint on purpose; the
	 * first attempt at this probe got the distinction wrong and the door was right. */
	{
		id twins[3];
		id different[3];
		NSArray *same = nil;
		NSArray *other = nil;
		NSArray *common = nil;
		NSArray *disjoint = nil;

		twins[0] = a; twins[1] = b; twins[2] = c;
		different[0] = a; different[1] = b; different[2] = stranger;
		same = [[NSArray alloc] initWithObjects:twins count:3];
		other = [[NSArray alloc] initWithObjects:different count:3];
		common = [[NSArray alloc] initWithObjects:&c count:1];		/* {c} — shares c */
		disjoint = [[NSArray alloc] initWithObjects:&stranger count:1];	/* {stranger} — shares nothing */

		check("isEqualToArray-says-yes-for-the-same-items",
		      (array != nil) && [array isEqualToArray:same] && [same isEqualToArray:array],
		      "two arrays with the same items were not equal");
		check("and-no-for-a-different-element",
		      (array != nil) && ![array isEqualToArray:other],
		      "arrays differing in one element were reported equal");
		check("firstObjectCommonWithArray-answers-the-first-shared-element",
		      (array != nil) && ([array firstObjectCommonWithArray:common] == c),
		      "the first common element was not the one the other array holds");
		check("and-nil-when-nothing-is-shared",
		      (array != nil) && ([array firstObjectCommonWithArray:disjoint] == nil),
		      "an array sharing nothing answered an element anyway");

		if (same != nil) {
			[same release];
		}
		if (other != nil) {
			[other release];
		}
		if (common != nil) {
			[common release];
		}
		if (disjoint != nil) {
			[disjoint release];
		}
	}

	/* THE MESSAGE-SENDING PAIR. The counter proves the message REACHED every element, and the recorded
	 * argument proves WHAT was sent — which is the difference between the two doors. */
	{
		probe_bumps = 0;
		probe_last_argument = nil;
		if (array != nil) {
			[array makeObjectsPerformSelector:@selector(bump)];
		}
		note_value("elements bumped (no argument)", (unsigned long)probe_bumps);
		check("makeObjectsPerformSelector-reaches-every-element", probe_bumps == 3,
		      "the no-argument form did not reach all three elements");

		probe_bumps = 0;
		if (array != nil) {
			[array makeObjectsPerformSelector:@selector(bumpWith:) withObject:stranger];
		}
		note_value("elements bumped (with argument)", (unsigned long)probe_bumps);
		check("and-withObject-passes-the-same-object-to-each",
		      probe_bumps == 3 && probe_last_argument == stranger,
		      "the form taking an object did not pass that object to every element");
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
