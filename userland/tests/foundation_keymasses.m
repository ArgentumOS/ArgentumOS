/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_keymasses, unit of 1 — W8p slice 6f's acceptance: THE KEYS WHOSE SUBJECT THIS SYSTEM DOES NOT
 * HAVE. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE: the probe asks about the machine it is on.
 *
 * THE DISTINCTION THIS PROBE EXISTS FOR is the one Apple's own getter page draws: "If this method returns
 * [YES] and the value is populated with [nil], it means that the resource property is NOT AVAILABLE for the
 * specified resource, and that no errors occurred when determining that the resource property was
 * unavailable." An unavailable property is NOT an error and NOT a fabricated value, so every check below
 * asserts the PAIR (answered YES + answered nil + NO error) rather than merely a nil.
 *
 *   masses-the-ubiquitous-item-answer-is-false  the ONE plain fact among the masses: nothing here is a
 *                                     ubiquitous item, because there is no cloud to be ubiquitous with;
 *   masses-a-thumbnail-key-is-unavailable-not-an-error  YES, nil, and no error - the triple;
 *   masses-a-protection-key-is-unavailable-too  file protection is a thing this system does not have;
 *   masses-an-icon-or-label-key-is-unavailable  so are icons and labels;
 *   masses-the-values-are-their-own-names  the value constants follow this library's standing spelling
 *                                     (§11.6.1 D2: the name is Apple's, the string is ours).
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-KEYMASSES %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-KEYMASSES %s FAIL %s\n", name, detail != nil ? [detail UTF8String] : "");
	}
}

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

/* THE TRIPLE, which is what "unavailable" means: the door ANSWERS, the value is NIL, and there is NO error. */
static BOOL fn_is_unavailable(NSURL *url, NSURLResourceKey key, NSString **detail)
{
	id value = nil;
	NSError *error = nil;
	BOOL answered = [url getResourceValue:&value forKey:key error:&error];

	if (detail != NULL) {
		*detail = [NSString stringWithFormat:@"%@: answered=%d value=%@ error=%@", key, (int)answered,
			value, error];
	}
	return answered && value == nil && error == nil;
}

int main(void)
{
	NSURL *item = fn_url(@"/System");

	{
		id value = nil;
		NSError *error = nil;
		BOOL answered = [item getResourceValue:&value forKey:NSURLIsUbiquitousItemKey error:&error];

		check("masses-the-ubiquitous-item-answer-is-false",
		      answered && value != nil && [value boolValue] == NO && error == nil,
		      [NSString stringWithFormat:@"answered=%d value=%@ error=%@", (int)answered, value, error]);
	}

	{
		NSString *detail = nil;

		check("masses-a-thumbnail-key-is-unavailable-not-an-error",
		      fn_is_unavailable(item, NSURLThumbnailDictionaryKey, &detail), detail);
	}

	{
		NSString *detail = nil;

		check("masses-a-protection-key-is-unavailable-too",
		      fn_is_unavailable(item, NSURLFileProtectionKey, &detail), detail);
	}

	{
		NSString *first = nil;
		NSString *second = nil;

		check("masses-an-icon-or-label-key-is-unavailable",
		      fn_is_unavailable(item, NSURLEffectiveIconKey, &first) &&
		      fn_is_unavailable(item, NSURLLabelNumberKey, &second),
		      [NSString stringWithFormat:@"%@ | %@", first, second]);
	}

	{
		check("masses-the-values-are-their-own-names",
		      [NSURLFileProtectionComplete isEqual:@"NSURLFileProtectionComplete"] &&
		      [NSURLUbiquitousItemDownloadingStatusCurrent
			isEqual:@"NSURLUbiquitousItemDownloadingStatusCurrent"] &&
		      [NSURLUbiquitousSharedItemRoleOwner isEqual:@"NSURLUbiquitousSharedItemRoleOwner"] &&
		      [NSURLUbiquitousSharedItemPermissionsReadWrite
			isEqual:@"NSURLUbiquitousSharedItemPermissionsReadWrite"],
		      @"the value constants are named by their own names (D2)");
	}

	printf("FOUNDATION-KEYMASSES RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-KEYMASSES DONE\n");
	return failc == 0 ? 0 : 1;
}
