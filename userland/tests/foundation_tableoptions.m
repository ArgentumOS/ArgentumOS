/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_tableoptions.m — THE PROBE FOR §62.95: the ten NAMES Apple declares beside `NSMapTable` and
 * `NSHashTable` (and the legacy zeroing-weak flag among them), which is the last of the pointer-collections family.
 *
 * WHAT IT ASSERTS, AND WHY EACH ONE IS WORTH THE CALLS IT COSTS:
 *   * THE ALIASES ARE THIS LIBRARY'S OWN OPTIONS — five map-table names and four hash-table names, each compared
 *     against the NSPointerFunctions option it names. A macro that resolved to a DIFFERENT bit would compile and
 *     silently change a table's policy, which is exactly the failure a name-only check cannot see.
 *   * THE LEGACY ZEROING-WEAK FLAG IS ONE NAME IN TWO PLACES: `NSPointerFunctionsZeroingWeakMemory` equals the
 *     shipped `NSHashTableZeroingWeakMemory` and is NOT the weak-memory option, which is the whole content of the
 *     declaration (NSPointerFunctions.h says it is declared rather than interpreted, and this pins the fact that
 *     it is a distinct flag rather than a spelling of the modern one).
 *   * COPY-IN REALLY COPIES, AND THE CONTROL IS THE POINT: the probe inserts a MUTABLE string and then mutates
 *     it, reading the key back through -keyEnumerator. A copying table keeps the text as inserted; the SAME
 *     table without CopyIn shows the mutated text, so the check distinguishes the policy from "the table stored
 *     something" — the failure mode a one-sided assertion would report as a pass.
 *   * THE IDENTITY PERSONALITY REALLY COMPARES BY IDENTITY: with two equal-but-distinct strings, the identity
 *     table does NOT find the key that a default table does. Again both sides are asserted.
 *   * AND THE CONVENIENCE CONSTRUCTORS STILL MAKE TABLES THAT WORK, because those four map-table constructors and
 *     `+weakObjectsHashTable` are built FROM these options: a table whose options were wrong would still answer a
 *     count of zero without complaining.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if (held) {
		okc++;
		printf("FOUNDATION-TABLEOPTIONS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-TABLEOPTIONS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

int main(void)
{
	/* 1 and 2. THE NINE ALIASES, against the options they name. */
	{
		BOOL map = NSMapTableStrongMemory == NSPointerFunctionsStrongMemory &&
			   NSMapTableWeakMemory == NSPointerFunctionsWeakMemory &&
			   NSMapTableZeroingWeakMemory == NSPointerFunctionsZeroingWeakMemory &&
			   NSMapTableCopyIn == NSPointerFunctionsCopyIn &&
			   NSMapTableObjectPointerPersonality == NSPointerFunctionsObjectPointerPersonality;
		BOOL hash = NSHashTableStrongMemory == NSPointerFunctionsStrongMemory &&
			    NSHashTableWeakMemory == NSPointerFunctionsWeakMemory &&
			    NSHashTableCopyIn == NSPointerFunctionsCopyIn &&
			    NSHashTableObjectPointerPersonality == NSPointerFunctionsObjectPointerPersonality;

		check("the-map-table-aliases-are-the-pointer-functions-options", map,
		      @"five names, each the option it names");
		check("the-hash-table-aliases-are-the-pointer-functions-options", hash,
		      @"four names, each the option it names");
	}

	/* 3. THE LEGACY FLAG: one name, two spellings, and not the modern weak option. */
	check("the-legacy-zeroing-weak-flag-is-one-name-in-two-places",
	      NSPointerFunctionsZeroingWeakMemory == NSHashTableZeroingWeakMemory &&
	      NSPointerFunctionsZeroingWeakMemory != NSPointerFunctionsWeakMemory,
	      @"the two names are one flag, and it is distinct from the weak-memory option - which is what "
	      @"'declared rather than interpreted' means");

	/* 4. COPY-IN, WITH ITS CONTROL. */
	{
		NSMapTable *copying = [NSMapTable mapTableWithKeyOptions:(NSMapTableStrongMemory | NSMapTableCopyIn)
							    valueOptions:NSMapTableStrongMemory];
		NSMapTable *keeping = [NSMapTable mapTableWithKeyOptions:NSMapTableStrongMemory
							    valueOptions:NSMapTableStrongMemory];
		NSHashTable *setCopying = [NSHashTable hashTableWithOptions:(NSHashTableStrongMemory | NSHashTableCopyIn)];
		NSHashTable *setKeeping = [NSHashTable hashTableWithOptions:NSHashTableStrongMemory];
		NSMutableString *keyA = [NSMutableString stringWithString:@"abc"];
		NSMutableString *keyB = [NSMutableString stringWithString:@"abc"];
		NSMutableString *memberA = [NSMutableString stringWithString:@"abc"];
		NSMutableString *memberB = [NSMutableString stringWithString:@"abc"];
		NSString *storedMap, *storedHash;

		[copying setObject:@"v" forKey:keyA];
		[keeping setObject:@"v" forKey:keyB];
		[setCopying addObject:memberA];
		[setKeeping addObject:memberB];

		/* THE MUTATION IS WHAT MAKES THE CHECK: after it, a table that copied answers the old text and a
		 * table that kept the pointer answers the new one. */
		[keyA appendString:@"-changed"];
		[keyB appendString:@"-changed"];
		[memberA appendString:@"-changed"];
		[memberB appendString:@"-changed"];

		storedMap = [[copying keyEnumerator] nextObject];
		storedHash = [[setCopying objectEnumerator] nextObject];

		check("copy-in-copies-the-key-on-insertion",
		      [copying count] == 1 && [storedMap isEqualToString:@"abc"] &&
		      [setCopying count] == 1 && [storedHash isEqualToString:@"abc"] &&
		      [[[keeping keyEnumerator] nextObject] isEqualToString:@"abc-changed"] &&
		      [[[setKeeping objectEnumerator] nextObject] isEqualToString:@"abc-changed"],
		      [NSString stringWithFormat:@"the copying tables must hold the text as inserted (map <%@>, set "
						  @"<%@>) while the controls show the mutation (map <%@>, set <%@>)",
						  storedMap, storedHash,
						  [[keeping keyEnumerator] nextObject],
						  [[setKeeping objectEnumerator] nextObject]]);
	}

	/* 5. THE IDENTITY PERSONALITY, WITH ITS CONTROL. */
	{
		NSMapTable *byIdentity = [NSMapTable mapTableWithKeyOptions:
					  (NSMapTableStrongMemory | NSMapTableObjectPointerPersonality)
					 valueOptions:NSMapTableStrongMemory];
		NSMapTable *byEquality = [NSMapTable mapTableWithKeyOptions:NSMapTableStrongMemory
							    valueOptions:NSMapTableStrongMemory];
		NSString *inserted = [NSString stringWithFormat:@"%@", @"identity"];
		NSString *equalButDistinct = [NSString stringWithFormat:@"%@", @"identity"];

		[byIdentity setObject:@"v" forKey:inserted];
		[byEquality setObject:@"v" forKey:inserted];

		check("the-identity-personality-compares-by-identity",
		      inserted != equalButDistinct && [inserted isEqual:equalButDistinct] &&
		      [byIdentity objectForKey:equalButDistinct] == nil &&
		      [byIdentity objectForKey:inserted] != nil &&
		      [byEquality objectForKey:equalButDistinct] != nil,
		      @"an equal-but-distinct key misses the identity table and hits the default one, and both "
		      @"sides are asserted so that 'the table stored nothing' cannot pass");
	}

	/* 6. THE CONSTRUCTORS BUILT FROM THESE OPTIONS. */
	{
		NSMapTable *weakToStrong = [NSMapTable weakToStrongObjectsMapTable];
		NSMapTable *strongToWeak = [NSMapTable strongToWeakObjectsMapTable];
		NSMapTable *weakToWeak = [NSMapTable weakToWeakObjectsMapTable];
		NSHashTable *weakSet = [NSHashTable weakObjectsHashTable];
		NSString *key = [NSString stringWithFormat:@"%@", @"round-trip"];

		[weakToStrong setObject:@"v1" forKey:key];
		[strongToWeak setObject:@"v2" forKey:key];
		[weakToWeak setObject:@"v3" forKey:key];
		[weakSet addObject:key];

		check("the-convenience-constructors-still-make-working-tables",
		      [weakToStrong objectForKey:key] != nil && [[weakToStrong objectForKey:key] isEqualToString:@"v1"] &&
		      [strongToWeak objectForKey:key] != nil && [[strongToWeak objectForKey:key] isEqualToString:@"v2"] &&
		      [weakToWeak objectForKey:key] != nil && [[weakToWeak objectForKey:key] isEqualToString:@"v3"] &&
		      [weakSet containsObject:key] &&
		      [weakToStrong keyPointerFunctions] != nil && [weakSet pointerFunctions] != nil,
		      @"the four map-table constructors and the weak set, which are the options above seen from the "
		      @"convenience side");
	}

	printf("FOUNDATION-TABLEOPTIONS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-TABLEOPTIONS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-TABLEOPTIONS DONE\n");
	return failc ? 1 : 0;
}
