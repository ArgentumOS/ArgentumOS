/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_pointers — the acceptance probe for W13a: NSPointerFunctions and NSPointerArray.
 * docs/design/foundation-plan.md §12.3 W13.
 *
 * WHAT THESE CHECKS ARE FOR. A pointer collection's whole behaviour is decided by callouts that no reader can
 * see, so the checks here are aimed at what would otherwise be invisible:
 *
 *   * `pointer-functions-personalities` — the OPTIONS actually select different decision functions. Two equal
 *     strings must agree under the object personality and DISAGREE under the object-pointer one, which is the
 *     difference between `-isEqual:` and identity that the personalities exist to name.
 *   * `pointer-array-slots` — the slots are slots: a NULL occupies one, `count` reports it, and only
 *     `-compact` closes the gap.
 *   * `pointer-array-ownership` — the MEMORY POLICY, measured by what happens to an object when the caller
 *     drops its reference. Strong keeps it alive; weak does NOT, and does NOT zero the slot either, which is
 *     the one behaviour of a weak pointer array worth being explicit about.
 *   * `pointer-array-copy-in` — the CopyIn flag is a MEMORY decision, not a personality one, so it has to be
 *     checked on its own: the array must hold a COPY, proven by mutating the original afterwards.
 *   * `pointer-array-archive` — the object personalities round-trip through NSCoding, and a personality whose
 *     pointers are NOT objects is REFUSED by name rather than written as something a reader would misread.
 */

#include <stdio.h>
#import <Foundation/Foundation.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-POINTERS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-POINTERS %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "(no detail)");
	}
}

/* A CLASS THAT COUNTS ITS OWN DEATHS, which is how the memory policies are measured: "was it freed" is the
 * question a retention policy answers, and a static counter is the only way to see it from outside. */
static int w13m_deaths = 0;

@interface W13Mortal : NSObject
{
	NSString *_name;
}
- (instancetype)initWithName:(NSString *)name;
- (NSString *)name;
@end

@implementation W13Mortal

- (instancetype)initWithName:(NSString *)name
{
	self = [super init];
	if (self != nil) {
		_name = [name copy];
	}
	return self;
}

- (NSString *)name { return _name; }

- (void)dealloc
{
	/* ARC OWNS THE IVAR AND THE SUPER CALL, so this records the death and nothing else. */
	w13m_deaths++;
}

@end

int main(void)
{
	@autoreleasepool {
		/* ---- the personalities: the same two values, decided differently ---- */
		{
			NSPointerFunctions *byEquality = [[NSPointerFunctions alloc]
				initWithOptions:NSPointerFunctionsObjectPersonality];
			NSPointerFunctions *byIdentity = [[NSPointerFunctions alloc]
				initWithOptions:NSPointerFunctionsObjectPointerPersonality];
			NSPointerFunctions *opaque = [[NSPointerFunctions alloc]
				initWithOptions:NSPointerFunctionsOpaquePersonality];
			NSPointerFunctions *cstring = [[NSPointerFunctions alloc]
				initWithOptions:NSPointerFunctionsCStringPersonality];
			/* TWO EQUAL, DISTINCT OBJECTS — the case the two object personalities answer differently. */
			NSString *one = [NSString stringWithFormat:@"same-%d", 7];
			NSString *two = [NSString stringWithFormat:@"same-%d", 7];
			BOOL equalObjects = [one isEqual:two] && one != two;
			/* Two equal C STRINGS IN DIFFERENT BUFFERS, for the personality that reads bytes. */
			char first[16];
			char second[16];

			snprintf(first, sizeof first, "bytes-%d", 3);
			snprintf(second, sizeof second, "bytes-%d", 3);

			check("pointer-functions-personalities",
			      equalObjects &&
			      [byEquality fnIsEqual:(__bridge const void *)one to:(__bridge const void *)two] &&
			      ![byIdentity fnIsEqual:(__bridge const void *)one to:(__bridge const void *)two] &&
			      /* THE WEAKER QUESTION FOLLOWS: equal objects must also HASH equally under the object
			       * personality, and two distinct objects need not under the pointer one. */
			      [byEquality fnHash:(__bridge const void *)one] == [byEquality fnHash:(__bridge const void *)two] &&
			      /* THE C STRING PERSONALITY READS CONTENT, so two different buffers compare equal. */
			      [cstring fnIsEqual:(const void *)first to:(const void *)second] &&
			      /* AND OPAQUE HAS NO DESCRIPTION FUNCTION AT ALL, which is a real difference: there is no
			       * sensible text for an address. */
			      [opaque descriptionFunction] == NULL &&
			      [byEquality descriptionFunction] != NULL,
			      [NSString stringWithFormat:@"equal=%@/%d/%@ opaqueDesc=%p",
				one, (int)equalObjects, two,
				(void *)[opaque descriptionFunction]]);
		}

		/* ---- the slots: a NULL is a slot ---- */
		{
			NSPointerArray *array = [[NSPointerArray alloc] initWithOptions:
						NSPointerFunctionsObjectPersonality];
			NSString *first = @"first";
			NSString *second = @"second";

			[array addPointer:(__bridge void *)first];
			[array addPointer:NULL];
			[array addPointer:(__bridge void *)second];
			{
				BOOL threeSlots = [array count] == 3;
				BOOL middleIsNull = [array pointerAtIndex:1] == NULL;
				BOOL order = [array pointerAtIndex:0] == (__bridge void *)first &&
					     [array pointerAtIndex:2] == (__bridge void *)second;

				/* INSERTING MOVES THE TAIL, and REPLACING SWAPS ONE SLOT. */
				[array insertPointer:(__bridge void *)@"inserted" atIndex:0];
				{
					BOOL inserted = [array count] == 4 &&
							[array pointerAtIndex:0] != (__bridge void *)first &&
							[array pointerAtIndex:1] == (__bridge void *)first;

					[array replacePointerAtIndex:0 withPointer:(__bridge void *)@"replaced"];
					{
						NSString *at0 = (NSString *)[array pointerAtIndex:0];
						/* COMPACT REMOVES ONLY THE NULLS, closing the gap and shortening. */
						[array compact];
						check("pointer-array-slots",
						      threeSlots && middleIsNull && order && inserted &&
						      [at0 isEqual:@"replaced"] &&
						      [array count] == 3 &&
						      [array pointerAtIndex:0] != NULL,
						      [NSString stringWithFormat:@"count=%lu at0=%@",
							(unsigned long)[array count], at0]);
					}
				}
			}
		}

		/* ---- the memory policy, measured by WHO DIES ---- */
		{
			/* THE POLICY IS MEASURED BY LIFETIME, because ARC (which this probe is compiled with) forbids
			 * both an explicit release and a retainCount — so neither can be the instrument. The objects are
			 * created INSIDE an autorelease pool and go out of scope with it; what happens next is the
			 * policy's answer: the strong array kept its element alive, the weak one did not. */
			NSPointerArray *strong = [NSPointerArray strongObjectsPointerArray];
			NSPointerArray *weak = [NSPointerArray weakObjectsPointerArray];
			BOOL weakDied;
			BOOL strongAlive;
			BOOL weakSlotNotZeroed;

			w13m_deaths = 0;
			@autoreleasepool {
				W13Mortal *strongHeld = [[W13Mortal alloc] initWithName:@"strong-held"];
				W13Mortal *weakHeld = [[W13Mortal alloc] initWithName:@"weak-held"];

				[strong addPointer:(__bridge void *)strongHeld];
				[weak addPointer:(__bridge void *)weakHeld];
			}
			/* ONE DEATH, NOT TWO: only the object nothing retained has gone. */
			weakDied = (w13m_deaths == 1);
			strongAlive = [strong pointerAtIndex:0] != NULL &&
				      [(id)[strong pointerAtIndex:0] name] != nil;
			/* THE WEAK SLOT STILL HOLDS THE ADDRESS of the dead object — "not zeroed", which is the one
			 * thing about a weak pointer array a caller must not misunderstand. */
			weakSlotNotZeroed = [weak pointerAtIndex:0] != NULL;

			/* LETTING THE ARRAY GO IS WHAT RELINQUISHES ITS ELEMENT. */
			strong = nil;
			check("pointer-array-ownership",
			      weakDied && strongAlive && weakSlotNotZeroed && w13m_deaths == 2,
			      [NSString stringWithFormat:@"deaths=%d weakDied=%d strongAlive=%d notZeroed=%d",
				w13m_deaths, (int)weakDied, (int)strongAlive, (int)weakSlotNotZeroed]);
		}

		/* ---- CopyIn is a memory decision, so it is checked apart from the personality ---- */
		{
			NSPointerArray *copying = [[NSPointerArray alloc] initWithOptions:
				(NSPointerFunctionsObjectPersonality | NSPointerFunctionsCopyIn)];
			NSMutableString *original = [NSMutableString stringWithString:@"before"];
			NSString *held;

			[copying addPointer:(__bridge void *)original];
			[original appendString:@"-after"];	/* THE ORIGINAL CHANGES; A COPY WOULD NOT. */
			held = (NSString *)[copying pointerAtIndex:0];

			check("pointer-array-copy-in",
			      held != nil && held != original && [held isEqual:@"before"] &&
			      [original isEqual:@"before-after"],
			      [NSString stringWithFormat:@"held=%@ original=%@ samePointer=%d",
				held, original, (int)(held == original)]);
		}

		/* ---- serialisation: the objects, or a refusal ---- */
		{
			NSPointerArray *array = [NSPointerArray strongObjectsPointerArray];
			NSPointerArray *pointees = nil;
			BOOL refused = NO;

			[array addPointer:(__bridge void *)@"one"];
			[array addPointer:(__bridge void *)@"two"];
			{
				NSData *bytes = [NSKeyedArchiver archivedDataWithRootObject:array];
				@try {
					pointees = [NSKeyedUnarchiver unarchiveObjectWithData:bytes];
				} @catch (NSException *e) {
				}
			}
			{
				/* A PERSONALITY WHOSE POINTERS ARE NOT OBJECTS CANNOT BE ARCHIVED, and the door says so
				 * rather than writing numbers a reader would take for objects. */
				NSPointerArray *numbers = [[NSPointerArray alloc] initWithOptions:
					(NSPointerFunctionsIntegerPersonality | NSPointerFunctionsStrongMemory)];

				[numbers addPointer:(void *)(uintptr_t)42];
				@try {
					[NSKeyedArchiver archivedDataWithRootObject:numbers];
				} @catch (NSException *e) {
					(void)e;
					refused = YES;
				}
				check("pointer-array-archive",
				      pointees != nil && [pointees count] == 2 &&
				      [(NSString *)[pointees pointerAtIndex:0] isEqual:@"one"] &&
				      refused,
				      [NSString stringWithFormat:@"back=%lu first=%@ refused=%d",
					(unsigned long)[pointees count],
					pointees != nil && [pointees count] > 0
						? (NSString *)[pointees pointerAtIndex:0] : @"(nil)",
					(int)refused]);
			}
		}

		/* ---- NSHashTable: membership, and the personality that decides what "already there" means ---- */
		{
			NSHashTable *byEquality = [NSHashTable hashTableWithOptions:
							NSPointerFunctionsObjectPersonality];
			NSHashTable *byIdentity = [NSHashTable hashTableWithOptions:
							NSPointerFunctionsObjectPointerPersonality];
			NSString *a = [NSString stringWithFormat:@"member-%d", 5];
			NSString *b = [NSString stringWithFormat:@"member-%d", 5];
			BOOL distinct = a != b && [a isEqual:b];

			[byEquality addObject:a];
			[byEquality addObject:b];	/* EQUAL TO THE FIRST: not a second member */
			[byIdentity addObject:a];
			[byIdentity addObject:b];	/* A DIFFERENT POINTER: a second member */
			{
				NSUInteger equalityCount = [byEquality count];
				NSUInteger identityCount = [byIdentity count];
				BOOL duplicateRefused = equalityCount == 1;
				BOOL memberAnswers = [[byEquality member:b] isEqual:a];

				[byEquality addObject:@"extra"];
				[byEquality removeObject:a];	/* by EQUALITY, so the stored member goes */
				{
					BOOL removed = [byEquality count] == 1 && ![byEquality containsObject:a] &&
						       [byEquality containsObject:@"extra"];
					BOOL anyIsMember = [[byEquality anyObject] isEqual:@"extra"];
					BOOL all = [[byEquality allObjects] count] == 1;
					BOOL representation = [[byEquality setRepresentation] count] == 1;

					[byEquality removeAllObjects];
					check("hash-table-membership",
					      distinct && duplicateRefused && identityCount == 2 &&
					      memberAnswers && removed && anyIsMember && all && representation &&
					      [byEquality count] == 0,
					      [NSString stringWithFormat:
						@"eq=%lu id=%lu removed=%d all=%d rep=%d",
						(unsigned long)equalityCount, (unsigned long)identityCount,
						(int)removed, (int)all, (int)representation]);
				}
			}
		}

		/* ---- the rehash, which only a table full of entries can exercise ---- */
		{
			NSHashTable *table = [[NSHashTable alloc] initWithOptions:
						NSPointerFunctionsObjectPersonality capacity:2];
			NSUInteger i;
			BOOL allFound = YES;
			NSUInteger found = 0;

			/* MANY MORE INSERTS THAN ANY INITIAL CAPACITY, so the table grows repeatedly. */
			for (i = 0; i < 200; i++) {
				[table addObject:[NSString stringWithFormat:@"item-%lu", (unsigned long)i]];
			}
			/* AND A CHURN OF REMOVALS AND RE-ADDS, which is what fills the table with TOMBSTONES — the
			 * case a removal that merely emptied its slot would break. */
			for (i = 0; i < 200; i += 2) {
				[table removeObject:[NSString stringWithFormat:@"item-%lu", (unsigned long)i]];
			}
			for (i = 0; i < 200; i += 2) {
				[table addObject:[NSString stringWithFormat:@"item-%lu", (unsigned long)i]];
			}
			for (i = 0; i < 200; i++) {
				NSString *probe = [NSString stringWithFormat:@"item-%lu", (unsigned long)i];

				if ([table containsObject:probe]) {
					found++;
				} else {
					allFound = NO;
				}
			}
			check("hash-table-growth",
			      allFound && found == 200 && [table count] == 200,
			      [NSString stringWithFormat:@"found=%lu count=%lu",
				(unsigned long)found, (unsigned long)[table count]]);
		}

		/* ---- the set operations ---- */
		{
			NSHashTable *left = [NSHashTable hashTableWithOptions:NSPointerFunctionsObjectPersonality];
			NSHashTable *right = [NSHashTable hashTableWithOptions:NSPointerFunctionsObjectPersonality];
			NSHashTable *unionOf = [NSHashTable hashTableWithOptions:NSPointerFunctionsObjectPersonality];
			NSHashTable *intersection = [NSHashTable hashTableWithOptions:NSPointerFunctionsObjectPersonality];
			NSHashTable *difference = [NSHashTable hashTableWithOptions:NSPointerFunctionsObjectPersonality];

			[left addObject:@"one"];
			[left addObject:@"two"];
			[right addObject:@"two"];
			[right addObject:@"three"];
			[unionOf unionHashTable:left];
			[unionOf unionHashTable:right];
			[intersection unionHashTable:left];
			[intersection intersectHashTable:right];
			[difference unionHashTable:left];
			[difference minusHashTable:right];

			check("hash-table-set-operations",
			      [unionOf count] == 3 && [intersection count] == 1 &&
			      [intersection containsObject:@"two"] &&
			      [difference count] == 1 && [difference containsObject:@"one"] &&
			      [left intersectsHashTable:right] &&
			      [difference isSubsetOfHashTable:left] &&
			      [left isEqualToHashTable:unionOf] == NO,
			      [NSString stringWithFormat:@"union=%lu intersection=%lu minus=%lu",
				(unsigned long)[unionOf count], (unsigned long)[intersection count],
				(unsigned long)[difference count]]);
		}

		/* ---- NSMapTable: the pairs ---- */
		{
			NSMapTable *map = [NSMapTable strongToStrongObjectsMapTable];
			NSDictionary *representation;

			[map setObject:@"value-one" forKey:@"key-one"];
			[map setObject:@"value-two" forKey:@"key-two"];
			/* RE-SETTING A KEY REPLACES its value rather than adding a second entry. */
			[map setObject:@"value-two-b" forKey:@"key-two"];
			representation = [map dictionaryRepresentation];
			{
				BOOL replaced = [[map objectForKey:@"key-two"] isEqual:@"value-two-b"];
				BOOL absent = [map objectForKey:@"nowhere"] == nil;
				BOOL counted = [map count] == 2;

				[map removeObjectForKey:@"key-one"];
				check("map-table-pairs",
				      counted && replaced && absent &&
				      [[representation objectForKey:@"key-two"] isEqual:@"value-two-b"] &&
				      [map count] == 1 && [map objectForKey:@"key-one"] == nil,
				      [NSString stringWithFormat:@"count=%lu two=%@ rep=%lu",
					(unsigned long)[map count], [map objectForKey:@"key-two"],
					(unsigned long)[representation count]]);
			}
		}

		/* ---- the two sides are configured INDEPENDENTLY ---- */
		{
			/*
			 * STRONG KEYS, WEAK VALUES: the entry outlives its value, which is a pairing an NSDictionary
			 * cannot express at all. THE CHECK IS DELIBERATELY MADE WITHOUT ENUMERATING OR DEREFERENCING
			 * THE VALUE, and that is a finding rather than a convenience: BOTH `-objectEnumerator` and
			 * ARC's fast enumeration RETAIN each element they hand out, so walking a table whose weak
			 * value has already died sends a message to freed memory and the probe SEGFAULTS. The
			 * lifetime question and the enumeration question are therefore asked about DIFFERENT tables.
			 */
			NSMapTable *weakValues = [NSMapTable strongToWeakObjectsMapTable];
			NSMapTable *aliveValues = [NSMapTable strongToStrongObjectsMapTable];

			w13m_deaths = 0;
			@autoreleasepool {
				W13Mortal *mortal = [[W13Mortal alloc] initWithName:@"fill"];

				[weakValues setObject:mortal forKey:@"the-key"];
			}
			/* THE VALUE DIED, THE ENTRY REMAINS, and the lookup answers the dangling pointer rather than
			 * dropping the entry — "not zeroed", exactly as the weak pointer array behaves. Comparing it
			 * to nil is safe; touching it would not be. */
			[aliveValues setObject:@"one" forKey:@"k1"];
			[aliveValues setObject:@"two" forKey:@"k2"];
			{
				BOOL valueDied = w13m_deaths == 1;
				/*
				 * `count` AND NOT `objectForKey:`, and the reason is a measured hazard rather than taste:
				 * THIS PROBE IS ARC, and an ARC CALL SITE RETAINS THE `+0` RETURN VALUE. Looking up the
				 * dead weak value therefore sends `-retain` to freed memory and the probe SEGFAULTS —
				 * the lookup itself, before the caller does anything with the answer. The entry's
				 * EXISTENCE is the claim this check can safely make; reading the pointer inside it is not.
				 */
				BOOL entryRemains = [weakValues count] == 1;
				NSEnumerator *keys = [aliveValues keyEnumerator];
				NSEnumerator *values = [aliveValues objectEnumerator];
				NSUInteger keyCount = 0;
				NSUInteger valueCount = 0;
				id each;
				NSUInteger fastCount = 0;

				while ([keys nextObject] != nil) {
					keyCount++;
				}
				while ((each = [values nextObject]) != nil) {
					(void)each;
					valueCount++;
				}
				for (id v in aliveValues) {	/* FAST ENUMERATION YIELDS THE VALUES */
					(void)v;
					fastCount++;
				}
				check("map-table-sides-and-enumeration",
				      valueDied && entryRemains &&
				      keyCount == 2 && valueCount == 2 && fastCount == 2,
				      [NSString stringWithFormat:
					@"deaths=%d remains=%d keys=%lu values=%lu fast=%lu",
					w13m_deaths, (int)entryRemains, (unsigned long)keyCount,
					(unsigned long)valueCount, (unsigned long)fastCount]);
			}
		}

		/* ---- NSHashTable's fast enumeration, on a table whose members are all alive ---- */
		{
			NSHashTable *table = [NSHashTable hashTableWithOptions:NSPointerFunctionsObjectPersonality];
			NSUInteger fast = 0;
			NSUInteger viaSnapshot = 0;

			[table addObject:@"a"];
			[table addObject:@"b"];
			[table addObject:@"c"];
			for (id member in table) {
				(void)member;
				fast++;
			}
			/* ENUMERATING TWICE MUST NOT DOUBLE-COUNT: the snapshot is rebuilt only when the table has
			 * changed, and the state machine has to restart for each new walk. */
			for (id member in table) {
				(void)member;
				viaSnapshot++;
			}
			check("hash-table-enumeration",
			      fast == 3 && viaSnapshot == 3 && [table count] == 3,
			      [NSString stringWithFormat:@"fast=%lu second=%lu count=%lu",
				(unsigned long)fast, (unsigned long)viaSnapshot,
				(unsigned long)[table count]]);
		}

		printf("FOUNDATION-POINTERS RESULT ok=%d fail=%d\n", okc, failc);
		printf("FOUNDATION-POINTERS-STATUS=%d\n", failc ? 1 : 0);
		printf("FOUNDATION-POINTERS DONE\n");
	}
	return failc ? 1 : 0;
}
