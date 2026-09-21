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
				printf("P3 archived %lu\n", (unsigned long)[bytes length]);
				@try {
					pointees = [NSKeyedUnarchiver unarchiveObjectWithData:bytes];
					printf("P4 unarchived %p\n", (__bridge void *)pointees);
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

		printf("FOUNDATION-POINTERS RESULT ok=%d fail=%d\n", okc, failc);
		printf("FOUNDATION-POINTERS-STATUS=%d\n", failc ? 1 : 0);
		printf("FOUNDATION-POINTERS DONE\n");
	}
	return failc ? 1 : 0;
}
