/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_fileaccessintent, unit of 1 — W8 slice 7a's acceptance: the coordinator family's VOCABULARY.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE AND NO FILE SYSTEM: this slice is the
 * value object and the two option sets that the coordinator's operations are spelled with, and both are
 * pure vocabulary - which is exactly why it is a slice of its own, and why nothing here needs a guest tree.
 *
 * WHAT IT MEASURES, and each one is a rule from Apple's own pages:
 *   fai-a-reading-intent-carries-its-url   +readingIntentWithURL:options: answers an object whose -URL is
 *                                          the URL it was given ("The current URL for this file access
 *                                          intent");
 *   fai-a-writing-intent-carries-its-url   and the writing factory does the same;
 *   fai-the-two-kinds-are-different-objects  the kind is what the two factories differ by, so two intents
 *                                          for one URL are never the same object;
 *   fai-a-nil-url-is-refused               an intent with no item is refused where it is made, rather than
 *                                          carried until the door that would use it;
 *   fai-the-options-are-distinct-bits      the nine published members are non-zero, distinct WITHIN each
 *                                          set and combinable (their VALUES are ours, §11.6.1 D2). The
 *                                          two sets SHARE their low bits, as Apple's own numbering does,
 *                                          and this check's first version demanded otherwise and failed -
 *                                          a demand no correct implementation could meet;
 *   fai-there-is-no-options-accessor       THE BOUNDARY, ASSERTED: Apple publishes TWO FACTORIES AND -URL
 *                                          for this class and NOTHING else - no options accessor, no kind
 *                                          accessor - so none is invented, and the probe asks for one and
 *                                          requires NO.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEACCESSINTENT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEACCESSINTENT %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

int main(void)
{
	NSURL *item = fn_url(@"/System/Temporary Files/an-intent.txt");
	NSFileAccessIntent *reading = [NSFileAccessIntent readingIntentWithURL:item options:0];
	NSFileAccessIntent *writing = [NSFileAccessIntent writingIntentWithURL:item
								      options:NSFileCoordinatorWritingForDeleting];
	NSFileAccessIntent *readingAgain = [NSFileAccessIntent readingIntentWithURL:item options:0];

	check("fai-a-reading-intent-carries-its-url",
	      reading != nil && [[reading URL] isEqual:item],
	      [NSString stringWithFormat:@"reading=%@ URL=%@", reading, reading != nil ? [reading URL] : nil]);

	check("fai-a-writing-intent-carries-its-url",
	      writing != nil && [[writing URL] isEqual:item],
	      [NSString stringWithFormat:@"writing=%@ URL=%@", writing, writing != nil ? [writing URL] : nil]);

	check("fai-the-two-kinds-are-different-objects",
	      reading != nil && writing != nil && reading != writing && readingAgain != reading &&
	      [[readingAgain URL] isEqual:[reading URL]],
	      [NSString stringWithFormat:@"reading=%p writing=%p again=%p",
		(void *)reading, (void *)writing, (void *)readingAgain]);

	check("fai-a-nil-url-is-refused",
	      [NSFileAccessIntent readingIntentWithURL:nil options:0] == nil &&
	      [NSFileAccessIntent writingIntentWithURL:nil options:0] == nil,
	      @"both factories answer nil for a nil URL");

	{
		NSUInteger readingBits[] = {
			NSFileCoordinatorReadingWithoutChanges,
			NSFileCoordinatorReadingResolvesSymbolicLink,
			NSFileCoordinatorReadingImmediatelyAvailableMetadataOnly,
			NSFileCoordinatorReadingForUploading,
		};
		NSUInteger writingBits[] = {
			NSFileCoordinatorWritingForDeleting,
			NSFileCoordinatorWritingForMoving,
			NSFileCoordinatorWritingForMerging,
			NSFileCoordinatorWritingForReplacing,
			NSFileCoordinatorWritingContentIndependentMetadataOnly,
		};
		NSUInteger all[9];
		BOOL distinct = YES;
		BOOL nonZero = YES;
		NSUInteger i, j;

		for (i = 0; i < 4; i++) {
			all[i] = readingBits[i];
		}
		for (i = 0; i < 5; i++) {
			all[4 + i] = writingBits[i];
		}
		/* DISTINCTNESS IS WITHIN A SET AND NOT ACROSS THE TWO, which is the distinction this check
		 * TAUGHT ME BY FAILING: the reading and writing sets share their low bits - as APPLE'S OWN
		 * NUMBERING DOES (each set starts at 1 << 0) - and the two are never compared to each other
		 * because they are the parameters of different doors. Demanding nine pairwise-distinct values
		 * would have been a demand no correct implementation could meet. */
		for (i = 0; i < 9; i++) {
			if (all[i] == 0) {
				nonZero = NO;
			}
		}
		for (i = 0; i < 4; i++) {
			for (j = i + 1; j < 4; j++) {
				if (readingBits[i] == readingBits[j]) {
					distinct = NO;
				}
			}
		}
		for (i = 0; i < 5; i++) {
			for (j = i + 1; j < 5; j++) {
				if (writingBits[i] == writingBits[j]) {
					distinct = NO;
				}
			}
		}
		/* AND THEY COMBINE, which is what an option set is for: two reading options together are a
		 * different number from either alone. */
		check("fai-the-options-are-distinct-bits",
		      nonZero && distinct &&
		      (NSFileCoordinatorReadingWithoutChanges | NSFileCoordinatorReadingForUploading) !=
			NSFileCoordinatorReadingWithoutChanges,
		      [NSString stringWithFormat:@"nonZero=%d distinct=%d", (int)nonZero, (int)distinct]);
	}

	{
		/* APPLE'S PUBLISHED SURFACE FOR THIS CLASS IS TWO FACTORIES AND -URL, and this asserts the
		 * absence rather than leaving it to be discovered: no options accessor, no kind accessor. */
		NSMutableArray *present = [NSMutableArray array];
		NSArray *names = [NSArray arrayWithObjects:@"options", @"setOptions:", @"writing",
				  @"isWriting", @"kind", nil];
		NSUInteger i;

		for (i = 0; i < [names count]; i++) {
			NSString *name = [names objectAtIndex:i];

			if ([reading respondsToSelector:NSSelectorFromString(name)]) {
				[present addObject:name];
			}
		}
		check("fai-there-is-no-options-accessor",
		      [present count] == 0,
		      [NSString stringWithFormat:@"this class answers for %lu of the accessors Apple does not "
			@"publish: %@", (unsigned long)[present count],
			[present componentsJoinedByString:@", "]]);
	}

	printf("FOUNDATION-FILEACCESSINTENT RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEACCESSINTENT DONE\n");
	return failc == 0 ? 0 : 1;
}
