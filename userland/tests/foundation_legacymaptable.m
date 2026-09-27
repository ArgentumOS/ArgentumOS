/*
 * foundation_maptable_legacy.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE LEGACY C API OF `NSMapTable` — §62.44's acceptance, and the largest family left on §62.24's work list: the
 * pre-10.5 functions and call-back STRUCTS Apple deprecated when `NSPointerFunctions` arrived.
 *
 * THE CHECK THIS PROBE EXISTS FOR IS `the-call-backs-are-handed-their-table`, AND IT IS THE DESIGN'S WHOLE POINT.
 * The obvious implementation would have bridged these call-backs onto `NSPointerFunctions` — the internal engine's
 * own shape — and it CANNOT BE DONE: the engine's function pointers take `(const void *item, …)` while Apple's
 * take `(NSMapTable *table, const void *key)`, and a C function pointer cannot close over its table. So the
 * implementation is a private subclass that hands `self` to every call-back, and this probe REGISTERS A CALL-BACK
 * THAT RECORDS WHAT IT WAS HANDED: if the table were lost anywhere between the caller and the engine, that check
 * is where it would show.
 *
 * THE REST IS THE VOCABULARY AND THE SEMANTICS: thirteen pre-built call-back sets (pinned by the ownership they
 * promise — object sets retain, non-owned sets do not), the three sentinels, and the functions themselves over a
 * real table: get, insert, insert-if-absent, member, remove, count, reset, enumerate, keys and values, compare,
 * copy.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-LEGACYMAPTABLE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-LEGACYMAPTABLE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* --- A CUSTOM PERSONALITY, WHICH IS HOW THE TABLE HANDED TO A CALL-BACK BECOMES OBSERVABLE ---------- */

static NSMapTable *fn_seen_table = nil;
static int fn_custom_hash_calls = 0;
static int fn_custom_equal_calls = 0;
static int fn_custom_release_calls = 0;

static unsigned fn_custom_hash(NSMapTable *table, const void *key)
{
	fn_seen_table = table;
	fn_custom_hash_calls++;
	return (unsigned)((uintptr_t)key >> 2);
}

static BOOL fn_custom_is_equal(NSMapTable *table, const void *a, const void *b)
{
	fn_seen_table = table;
	fn_custom_equal_calls++;
	return a == b;
}

static void fn_custom_release(NSMapTable *table, const void *value)
{
	(void)table;
	fn_custom_release_calls++;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE SENTINELS AND THE THIRTEEN SETS -------------------------------------------------------- */
	{
		check("the-three-sentinels-are-pinned",
		      NSNotAnIntMapKey == (void *)(long)-1 && NSNotAnIntegerMapKey == (void *)(long)-1 &&
		      NSNotAPointerMapKey == (void *)-1,
		      @"the markers a call-back returns when there is no answer");
		check("the-thirteen-call-back-sets-promise-what-their-names-say",
		      NSObjectMapKeyCallBacks.hash != NULL && NSObjectMapKeyCallBacks.isEqual != NULL &&
		      NSObjectMapKeyCallBacks.retain != NULL && NSObjectMapKeyCallBacks.release != NULL &&
		      NSObjectMapKeyCallBacks.notAKeyMarker == NSNotAPointerMapKey &&
		      NSObjectMapValueCallBacks.retain != NULL && NSObjectMapValueCallBacks.release != NULL &&
		      NSNonOwnedPointerMapKeyCallBacks.retain == NULL &&
		      NSNonOwnedPointerMapKeyCallBacks.release == NULL &&
		      NSNonOwnedPointerMapValueCallBacks.release == NULL &&
		      NSNonRetainedObjectMapKeyCallBacks.retain == NULL &&
		      NSNonRetainedObjectMapKeyCallBacks.hash == NSObjectMapKeyCallBacks.hash &&
		      NSOwnedPointerMapKeyCallBacks.release != NULL &&
		      NSOwnedPointerMapValueCallBacks.release != NULL &&
		      NSIntMapKeyCallBacks.notAKeyMarker == NSNotAnIntMapKey &&
		      NSIntegerMapKeyCallBacks.notAKeyMarker == NSNotAnIntegerMapKey &&
		      NSNonOwnedPointerOrNullMapKeyCallBacks.retain == NULL,
		      @"an OBJECT set retains and releases, a NON-OWNED one does not, an OWNED pointer set releases, "
		      @"and each integer set carries its own sentinel");
	}

	/* --- A TABLE FROM THE C API, AND THE CALL-BACKS THAT ARE HANDED IT ------------------------------ */
	{
		NSMapTableKeyCallBacks keyCallBacks = {
			fn_custom_hash, fn_custom_is_equal, NULL, NULL, NULL, NSNotAPointerMapKey
		};
		NSMapTableValueCallBacks valueCallBacks = { NULL, fn_custom_release, NULL };
		NSMapTable *table = NSCreateMapTable(keyCallBacks, valueCallBacks, 4);

		check("the-table-exists-and-is-empty",
		      table != nil && NSCountMapTable(table) == 0,
		      @"NSCreateMapTable answers a table, and an empty one");

		NSMapInsert(table, (const void *)0x1000, (const void *)0x2000);
		NSMapInsert(table, (const void *)0x3000, (const void *)0x4000);

		/* WHAT THE SCAN CONSULTS IS THE CALLER'S `isEqual`, NOT THE HASH — and the first version of this check
		 * asserted hash calls, which is what a HASHED table would make. THE LEGACY MODE IS A LINEAR SCAN, stated
		 * in the implementation and stated here: it is the one implementation of "is this key here" that cannot
		 * disagree with the scan that finds its position, and it means a caller's hash call-back is never
		 * consulted. THE CHECK MEASURES THE DEVIATION RATHER THAN EXPECTING THE OTHER DESIGN. */
		check("get-and-count-and-the-table-handed-to-a-call-back",
		      fn_custom_equal_calls > 0 && fn_seen_table == table &&
		      NSCountMapTable(table) == 2 &&
		      NSMapGet(table, (const void *)0x1000) == (void *)0x2000 &&
		      NSMapGet(table, (const void *)0x3000) == (void *)0x4000 &&
		      NSMapGet(table, (const void *)0x9999) == NULL,
		      [NSString stringWithFormat:@"%d comparison(s) through the caller's isEqual (the hash is not "
			@"consulted by a scan: %d call(s)), the table they were handed was %@, and a missing key "
			@"answers NULL", fn_custom_equal_calls, fn_custom_hash_calls,
			fn_seen_table == table ? @"THE TABLE" : @"NOT the table"]);

		/* AND THE CALL-BACKS ARE HANDED THEIR TABLE ON EVERY DOOR, NOT JUST THE FIRST: the scan that looks a key
		 * up is the call-back's too, and it is called through the same hand. */
		fn_seen_table = nil;
		(void)NSMapGet(table, (const void *)0x3000);
		check("the-table-is-handed-on-every-call",
		      fn_seen_table == table,
		      @"a lookup after the insert is still given the table - the hand is the call's, not the table's "
		      @"lifetime's");

		{
			void *original = NULL;
			void *found = NULL;

			check("member-answers-the-original-key-and-the-value",
			      NSMapMember(table, (const void *)0x1000, &original, &found) &&
			      original == (void *)0x1000 && found == (void *)0x2000 &&
			      !NSMapMember(table, (const void *)0x9999, NULL, NULL),
			      @"NSMapMember answers yes and hands back BOTH the stored key and its value");
		}

		check("insert-if-absent-answers-what-was-there",
		      NSMapInsertIfAbsent(table, (const void *)0x1000, (const void *)0x5555) == (void *)0x2000 &&
		      NSMapGet(table, (const void *)0x1000) == (void *)0x2000 &&
		      NSMapInsertIfAbsent(table, (const void *)0x7777, (const void *)0x8888) == NULL &&
		      NSMapGet(table, (const void *)0x7777) == (void *)0x8888 &&
		      NSCountMapTable(table) == 3,
		      @"an absent insert replaces nothing and answers the OLD value; a new one answers NULL");

		NSMapInsertKnownAbsent(table, (const void *)0xaaaa, (const void *)0xbbbb);
		check("insert-known-absent-adds-once",
		      NSCountMapTable(table) == 4 && NSMapGet(table, (const void *)0xaaaa) == (void *)0xbbbb,
		      @"the known-absent form is the same insert, and the count moves by one");
	}

	/* --- THE ENUMERATION, THE KEYS AND THE VALUES --------------------------------------------------- */
	{
		NSMapTable *table = NSCreateMapTable(NSIntMapKeyCallBacks, NSIntMapValueCallBacks, 4);
		NSMapEnumerator enumerator;
		void *key = NULL;
		void *value = NULL;
		int pairs = 0;
		int seen[4] = { 0, 0, 0, 0 };

		NSMapInsert(table, (const void *)1, (const void *)11);
		NSMapInsert(table, (const void *)2, (const void *)22);
		NSMapInsert(table, (const void *)3, (const void *)33);

		enumerator = NSEnumerateMapTable(table);
		while(NSNextMapEnumeratorPair(&enumerator, &key, &value)) {
			long k = (long)key;

			if(k >= 1 && k <= 3 && (long)value == k * 11) {
				seen[k - 1] = 1;
			}
			pairs++;
		}
		NSEndMapTableEnumeration(&enumerator);
		check("the-enumeration-walks-every-pair-once",
		      pairs == 3 && seen[0] && seen[1] && seen[2],
		      [NSString stringWithFormat:@"%d pair(s), each with the value its key implies", pairs]);

		check("keys-and-values-come-back-as-arrays",
		      [NSAllMapTableKeys(table) count] == 3 && [NSAllMapTableValues(table) count] == 3,
		      @"the two convenience walks answer arrays of the table's own size");

		check("an-integer-key-is-its-own-value-and-absent-answers-null",
		      NSMapGet(table, (const void *)2) == (void *)22 &&
		      NSMapGet(table, (const void *)99) == NULL &&
		      NSIntMapKeyCallBacks.notAKeyMarker == NSNotAnIntMapKey,
		      @"the integer personality compares the POINTER as a number, which is what makes it an integer "
		      @"map at all");
	}

	/* --- REMOVE, RESET, COMPARE, COPY --------------------------------------------------------------- */
	{
		NSMapTable *table = NSCreateMapTable(NSObjectMapKeyCallBacks, NSObjectMapValueCallBacks, 2);
		NSMapTable *same = NSCreateMapTable(NSObjectMapKeyCallBacks, NSObjectMapValueCallBacks, 2);
		NSMapTable *copy;
		NSString *keyOne = @"one";
		NSString *keyTwo = @"two";

		/* THE BRIDGED CASTS ARE THE ARC BOUNDARY, AND IT IS EXACTLY WHERE IT BELONGS: this is a C API that takes
		 * `const void *`, and an object crossing into it is the caller's decision - which is the same thing the
		 * legacy API asks of a caller in Apple's own code. */
		NSMapInsert(table, (__bridge const void *)keyOne, (__bridge const void *)@"first");
		NSMapInsert(table, (__bridge const void *)keyTwo, (__bridge const void *)@"second");
		NSMapInsert(same, (__bridge const void *)keyTwo, (__bridge const void *)@"second");
		NSMapInsert(same, (__bridge const void *)keyOne, (__bridge const void *)@"first");

		check("compare-answers-content-not-order",
		      NSCompareMapTables(table, same) &&
		      NSCountMapTable(table) == 2 &&
		      NSMapGet(table, (__bridge const void *)keyOne) != NULL,
		      @"two tables with the same pairs are equal, whatever order they were filled in");

		/* THE COPY COMES FROM THE OBJECT API, because the legacy function that took a ZONE is not declared: this
		 * library has no `NSZone` type (a decision NSObjCRuntime.h records). The class's own `-copy` is what
		 * `NSCopyMapTableWithZone` was, minus the argument nobody here can pass. */
		copy = [table copy];
		NSMapRemove(copy, (__bridge const void *)keyOne);
		check("a-copy-is-its-own-table",
		      NSCountMapTable(copy) == 1 && NSCountMapTable(table) == 2 &&
		      !NSCompareMapTables(table, copy) &&
		      NSMapGet(copy, (__bridge const void *)keyTwo) != NULL,
		      @"removing from the copy leaves the original alone");

		NSResetMapTable(table);
		check("reset-empties-it",
		      NSCountMapTable(table) == 0 && NSMapGet(table, (__bridge const void *)keyOne) == NULL,
		      @"NSResetMapTable keeps the table and drops its contents");
		NSFreeMapTable(table);
		NSFreeMapTable(same);
		NSFreeMapTable(copy);
	}

	/* --- AND A RELEASE CALL-BACK IS CALLED WHEN A PAIR GOES ----------------------------------------- */
	{
		NSMapTableValueCallBacks releasing = { NULL, fn_custom_release, NULL };
		NSMapTable *table = NSCreateMapTable(NSNonOwnedPointerMapKeyCallBacks, releasing, 2);

		fn_custom_release_calls = 0;
		NSMapInsert(table, (const void *)0x1, (const void *)0x2);
		NSMapRemove(table, (const void *)0x1);
		NSMapInsert(table, (const void *)0x3, (const void *)0x4);
		NSResetMapTable(table);
		check("a-release-call-back-is-called-for-every-pair-that-goes",
		      fn_custom_release_calls == 2,
		      [NSString stringWithFormat:@"removal and reset each gave the pair back through the caller's "
			@"release (%d call(s))", fn_custom_release_calls]);
		NSFreeMapTable(table);
	}

	printf("FOUNDATION-LEGACYMAPTABLE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-LEGACYMAPTABLE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-LEGACYMAPTABLE DONE\n");
	return failc ? 1 : 0;
}
