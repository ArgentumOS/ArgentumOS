/*
 * foundation_legacyhashtable.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * `NSHashTable`'S LEGACY C API (§62.45) — the same shape as `NSMapTable`'s, with no values, and the same design:
 * a private subclass that carries the caller's call-backs and HANDS ITSELF TO EVERY ONE OF THEM, because Apple's
 * call-backs are promised the table and a C function pointer cannot close over one.
 *
 * THE TWO ARE NOT TWO IMPLEMENTATIONS: the scan lives in `FNLegacyMapTable`, and this class's legacy mode is a
 * thin wrapper that stores each element as the inner table's KEY and passes no value call-backs at all. That is why
 * the probe checks the same hand here as there — if the wrapper lost the table anywhere, it would be this check.
 *
 * TWO OF THE FAMILY'S TWENTY-EIGHT NAMES ARE NOT HERE: the two that take an `NSZone`, which this library removed
 * on purpose (NSObjCRuntime.h records the sequence). Everything else is: the call-back STRUCT, the enumerator, the
 * eight pre-built sets, the legacy option constant, and fifteen functions.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-LEGACYHASHTABLE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-LEGACYHASHTABLE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* --- A CUSTOM SET, SO THAT "THE TABLE WAS HANDED TO A CALL-BACK" IS A MEASUREMENT ------------------- */

static NSHashTable *fn_seen_table = nil;
static int fn_custom_hash_calls = 0;
static int fn_custom_equal_calls = 0;
static int fn_custom_release_calls = 0;

static unsigned fn_custom_hash(NSHashTable *table, const void *pointer)
{
	fn_seen_table = table;
	fn_custom_hash_calls++;
	return (unsigned)((uintptr_t)pointer >> 2);
}

static BOOL fn_custom_equal(NSHashTable *table, const void *a, const void *b)
{
	fn_seen_table = table;
	fn_custom_equal_calls++;
	return a == b;
}

static void fn_custom_release(NSHashTable *table, const void *pointer)
{
	(void)table;
	(void)pointer;
	fn_custom_release_calls++;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE VOCABULARY ----------------------------------------------------------------------------- */
	{
		check("the-eight-call-back-sets-promise-what-their-names-say",
		      NSObjectHashCallBacks.hash != NULL && NSObjectHashCallBacks.isEqual != NULL &&
		      NSObjectHashCallBacks.retain != NULL && NSObjectHashCallBacks.release != NULL &&
		      NSNonOwnedPointerHashCallBacks.retain == NULL &&
		      NSNonOwnedPointerHashCallBacks.release == NULL &&
		      NSNonOwnedPointerHashCallBacks.hash == NSPointerToStructHashCallBacks.hash &&
		      NSNonRetainedObjectHashCallBacks.retain == NULL &&
		      NSNonRetainedObjectHashCallBacks.hash == NSObjectHashCallBacks.hash &&
		      NSOwnedObjectIdentityHashCallBacks.retain != NULL &&
		      NSOwnedObjectIdentityHashCallBacks.hash != NSObjectHashCallBacks.hash &&
		      NSOwnedPointerHashCallBacks.release != NULL &&
		      NSIntHashCallBacks.hash != NULL && NSIntegerHashCallBacks.hash != NULL &&
		      NSHashTableZeroingWeakMemory != 0,
		      @"an OBJECT set hashes and compares by value, an IDENTITY set hashes the pointer, a NON-OWNED set "
		      @"does not retain, and the legacy option constant exists");
	}

	/* --- A TABLE FROM THE C API, AND THE CALL-BACKS THAT ARE HANDED IT ------------------------------ */
	{
		NSHashTableCallBacks callBacks = {
			fn_custom_hash, fn_custom_equal, NULL, fn_custom_release, NULL, (void *)-1
		};
		NSHashTable *table = NSCreateHashTable(callBacks, 4);

		check("the-table-exists-and-is-empty",
		      table != nil && NSCountHashTable(table) == 0 && [table anyObject] == nil,
		      @"NSCreateHashTable answers an empty table, and an empty one has no member");

		NSHashInsert(table, (const void *)0x1000);
		NSHashInsert(table, (const void *)0x2000);

		check("the-table-is-handed-to-a-call-back-and-the-scan-compares",
		      fn_custom_equal_calls > 0 && fn_seen_table == table && NSCountHashTable(table) == 2 &&
		      NSHashGet(table, (const void *)0x1000) == (void *)0x1000 &&
		      NSHashGet(table, (const void *)0x2000) == (void *)0x2000 &&
		      NSHashGet(table, (const void *)0x9999) == NULL,
		      [NSString stringWithFormat:@"%d comparison(s) through the caller's isEqual (%d hash call(s) - a "
			@"SCAN consults the comparison and not the hash), and the table they were handed was %@",
			fn_custom_equal_calls, fn_custom_hash_calls,
			fn_seen_table == table ? @"THE TABLE" : @"NOT the table"]);

		check("insert-if-absent-answers-the-member-that-was-there",
		      NSHashInsertIfAbsent(table, (const void *)0x1000) == (void *)0x1000 &&
		      NSCountHashTable(table) == 2 &&
		      NSHashInsertIfAbsent(table, (const void *)0x3000) == NULL &&
		      NSCountHashTable(table) == 3,
		      @"an absent insert answers NULL and adds; a present one answers the member");

		/* THE CONTRACT FORM IS A CALL, NOT AN EXPRESSION: `NSHashInsertKnownAbsent` answers nothing, so the probe
		 * calls it and then reads the table. (The line this replaces passed a `void` call as a check's BOOLEAN,
		 * which is the sort of thing that only looks like a test.) */
		NSHashInsertKnownAbsent(table, (const void *)0x4000);
		check("the-known-absent-form-added-its-member",
		      NSCountHashTable(table) == 4 && NSHashGet(table, (const void *)0x4000) == (void *)0x4000,
		      @"the contract form is the same insert");

		NSHashRemove(table, (const void *)0x1000);
		check("remove-takes-one-away-and-gives-it-back-through-the-release",
		      NSCountHashTable(table) == 3 && NSHashGet(table, (const void *)0x1000) == NULL &&
		      fn_custom_release_calls == 1,
		      [NSString stringWithFormat:@"the member is gone and the caller's release was called (%d time(s))",
			fn_custom_release_calls]);

		/* THE ENUMERATION AND THE CONVENIENCE WALKS */
		{
			NSHashEnumerator enumerator = NSEnumerateHashTable(table);
			NSArray *all = NSAllHashTableObjects(table);
			void *item = NULL;
			int items = 0;

			while((item = NSNextHashEnumeratorItem(&enumerator)) != NULL) {
				items++;
			}
			NSEndHashTableEnumeration(&enumerator);
			check("the-enumeration-walks-every-object",
			      items == 3 && [all count] == 3,
			      [NSString stringWithFormat:@"%d object(s) through the enumerator and %d through the array",
				items, (int)[all count]]);
		}

		NSResetHashTable(table);
		check("reset-empties-it",
		      NSCountHashTable(table) == 0 && fn_custom_release_calls == 4,
		      @"every member goes back through the caller's release exactly once");
		NSFreeHashTable(table);
	}

	/* --- AN OBJECT SET: THE VALUE-BASED PERSONALITY, AND THE SET RELATIONS ------------------------- */
	{
		NSHashTable *table = NSCreateHashTable(NSObjectHashCallBacks, 4);
		NSHashTable *other = NSCreateHashTable(NSObjectHashCallBacks, 4);
		NSHashTable *copy;
		NSString *one = @"one";
		NSString *oneAgain = [[NSString alloc] initWithString:@"one"];
		NSString *two = @"two";

		NSHashInsert(table, (__bridge const void *)one);
		check("an-equal-but-distinct-object-is-the-same-member",
		      NSCountHashTable(table) == 1 &&
		      NSHashGet(table, (__bridge const void *)oneAgain) == (__bridge void *)one,
		      @"the object set hashes and compares BY VALUE, so a second string equal to the first is not added "
		      @"and the ORIGINAL comes back");

		NSHashInsert(table, (__bridge const void *)two);
		NSHashInsert(other, (__bridge const void *)two);
		/* EACH DOOR IS READ ONCE INTO A NAME. That is not only easier to read: a negated message send inside an
		 * argument list is the one construct in this probe the parser refused, and naming the answers is what a
		 * reader wants anyway. */
		BOOL equalToItself = NSCompareHashTables(table, table);
		BOOL equalToOther = NSCompareHashTables(table, other);
		BOOL otherIsSubset = [other isSubsetOfHashTable:table];
		BOOL intersects = [table intersectsHashTable:other];
		BOOL equalOverall = [table isEqualToHashTable:other];

		check("compare-answers-content-and-the-set-relations-are-answered",
		      NSCountHashTable(table) == 2 && equalToItself && !equalToOther && otherIsSubset &&
		      intersects && !equalOverall,
		      [NSString stringWithFormat:@"counts %d/%d, equalToItself=%d equalToOther=%d otherIsSubset=%d "
			@"intersects=%d equalOverall=%d", (int)NSCountHashTable(table), (int)NSCountHashTable(other),
			(int)equalToItself, (int)equalToOther, (int)otherIsSubset, (int)intersects,
			(int)equalOverall]);

		copy = [table copy];
		NSHashRemove(copy, (__bridge const void *)one);
		check("a-copy-is-its-own-table",
		      NSCountHashTable(copy) == 1 && NSCountHashTable(table) == 2 &&
		      NSHashGet(copy, (__bridge const void *)one) == NULL,
		      @"removing from the copy leaves the original alone");

		[other intersectHashTable:table];
		check("intersect-keeps-what-is-in-both",
		      NSCountHashTable(other) == 1 && NSHashGet(other, (__bridge const void *)two) != NULL,
		      @"the intersection removes what the other table does not have");
		NSFreeHashTable(copy);
		NSFreeHashTable(other);
		NSFreeHashTable(table);
	}

	printf("FOUNDATION-LEGACYHASHTABLE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-LEGACYHASHTABLE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-LEGACYHASHTABLE DONE\n");
	return failc ? 1 : 0;
}
