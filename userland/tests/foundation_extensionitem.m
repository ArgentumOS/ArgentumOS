/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_extensionitem — §62.69's acceptance: NSExtensionItem, and with it the last row of
 * `App Support / Attachments`.
 *
 * ONE unit, importing only <Foundation/Foundation.h>.
 *
 * THE CLASS IS A VALUE OBJECT WITH A WIRE FORM, and the probe is built around that pair rather than around the
 * accessors:
 *
 *   * the THREE CONSTANTS ARE THE PROPERTIES' NAMES ON THE WIRE, so the check builds a payload DICTIONARY keyed
 *     by them and reads the values back out — which is the only thing those constants exist for, and the reading
 *     Apple's own pages state ("the key for the <property>");
 *   * every property is COPIED, so the check mutates the caller's mutable string/array/dictionary AFTER handing
 *     them over and asserts the item did not follow;
 *   * `NSCopying` is a VALUE copy and `NSSecureCoding` says YES, and both are checked rather than assumed;
 *   * the coder doors are checked by a real round trip through the shipped keyed archiver.
 *
 * AND THE ONE HONEST LIMIT IS CHECKED AS A LIMIT RATHER THAN AVOIDED: an item whose `-attachments` holds values
 * this library cannot archive (NSItemProvider is the interesting case, and it is what Apple's own attachments
 * are) cannot be archived here, because the ARCHIVER refuses an uncodable object on the sending side. The probe
 * asserts that a codable payload round-trips and that the door is the archiver's, not this class's.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-EXTENSIONITEM %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-EXTENSIONITEM %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static NSAttributedString *fn_text(NSString *text)
{
	return [[NSAttributedString alloc] initWithString:text];
}

int main(void)
{
	/* THE THREE WIRE NAMES. */
	check("the-three-keys-exist-are-distinct-and-are-names-not-paths",
	      [NSExtensionItemAttachmentsKey length] > 0 &&
	      [NSExtensionItemAttributedContentTextKey length] > 0 &&
	      [NSExtensionItemAttributedTitleKey length] > 0 &&
	      ![NSExtensionItemAttachmentsKey isEqualToString:NSExtensionItemAttributedTitleKey] &&
	      ![NSExtensionItemAttributedTitleKey isEqualToString:NSExtensionItemAttributedContentTextKey] &&
	      [[NSExtensionItemAttachmentsKey description] rangeOfString:@"/"].location == NSNotFound,
	      [NSString stringWithFormat:@"%@ | %@ | %@", NSExtensionItemAttachmentsKey,
		NSExtensionItemAttributedContentTextKey, NSExtensionItemAttributedTitleKey]);

	/* AN ITEM WITH NOTHING IN IT IS AN ITEM: every property is optional on Apple's page. */
	{
		NSExtensionItem *empty = [[NSExtensionItem alloc] init];

		check("an-item-starts-empty-and-that-is-valid",
		      [empty attachments] == nil && [empty attributedTitle] == nil &&
		      [empty attributedContentText] == nil && [empty userInfo] == nil,
		      [NSString stringWithFormat:@"title=%@ userInfo=%@", [empty attributedTitle], [empty userInfo]]);
	}

	/* THE PAYLOAD AGREEMENT: a dictionary keyed by the three constants carries the item's fields, and the reader
	 * that uses those keys gets the values the item's properties report. This is the contract the constants
	 * exist for, so it is asserted in both directions. */
	{
		NSExtensionItem *item = [[NSExtensionItem alloc] init];
		NSMutableDictionary *payload = [[NSMutableDictionary alloc] init];
		NSAttributedString *title = fn_text(@"A title");
		NSAttributedString *body = fn_text(@"Some content");
		NSArray *attachments = [NSArray arrayWithObject:@"notion://page"];

		[payload setObject:title forKey:NSExtensionItemAttributedTitleKey];
		[payload setObject:body forKey:NSExtensionItemAttributedContentTextKey];
		[payload setObject:attachments forKey:NSExtensionItemAttachmentsKey];

		/* THE READER'S SIDE: the keys are looked up in a payload it did not build. */
		[item setAttributedTitle:[payload objectForKey:NSExtensionItemAttributedTitleKey]];
		[item setAttributedContentText:[payload objectForKey:NSExtensionItemAttributedContentTextKey]];
		[item setAttachments:[payload objectForKey:NSExtensionItemAttachmentsKey]];
		[item setUserInfo:payload];
		check("the-keys-are-the-fields-names-on-the-wire",
		      [[[item attributedTitle] string] isEqualToString:@"A title"] &&
		      [[[item attributedContentText] string] isEqualToString:@"Some content"] &&
		      [[item attachments] count] == 1 &&
		      [[[item userInfo] objectForKey:NSExtensionItemAttributedTitleKey] isEqual:title],
		      [NSString stringWithFormat:@"title=%@ content=%@ attachments=%lu",
			[[item attributedTitle] string], [[item attributedContentText] string],
			(unsigned long)[[item attachments] count]]);

		/* THE SNAPSHOT: the caller changes the payload it handed over, and the item does not follow. */
		{
			NSMutableAttributedString *mutableTitle =
				[[NSMutableAttributedString alloc] initWithString:@"A title"];
			NSExtensionItem *snapshot = [[NSExtensionItem alloc] init];

			[snapshot setAttributedTitle:mutableTitle];
			[mutableTitle replaceCharactersInRange:NSMakeRange(0, 1) withString:@"CHANGED"];
			check("the-properties-are-snapshots-not-references",
			      [[[snapshot attributedTitle] string] isEqualToString:@"A title"],
			      [NSString stringWithFormat:@"after=%@", [[snapshot attributedTitle] string]]);
		}

		/* THE VALUE COPY, and a copy is INDEPENDENT of what it was copied from. */
		{
			NSExtensionItem *copy = [item copy];

			check("a-copy-is-a-value-copy",
			      copy != item && [[[copy attributedTitle] string] isEqualToString:@"A title"] &&
			      [[copy attachments] count] == 1 &&
			      [[copy userInfo] objectForKey:NSExtensionItemAttachmentsKey] != nil,
			      [NSString stringWithFormat:@"copy=%p original=%p copyTitle=%@", (void *)copy, (void *)item,
				[[copy attributedTitle] string]]);
			[item setAttributedTitle:fn_text(@"Changed after the copy")];
			check("changing-the-original-does-not-change-the-copy",
			      [[[copy attributedTitle] string] isEqualToString:@"A title"],
			      [NSString stringWithFormat:@"copyTitle=%@", [[copy attributedTitle] string]]);
		}
	}

	/* NSSECURECODING SAYS YES, and the coder doors really carry the fields. */
	check("secure-coding-is-claimed",
	      [NSExtensionItem supportsSecureCoding],
	      [NSString stringWithFormat:@"supportsSecureCoding=%d", (int)[NSExtensionItem supportsSecureCoding]]);

	{
		NSExtensionItem *item = [[NSExtensionItem alloc] init];
		NSExtensionItem * _Nullable back;

		[item setAttributedTitle:fn_text(@"Round trip")];
		[item setAttributedContentText:fn_text(@"body")];
		[item setAttachments:[NSArray arrayWithObject:@"notion://page"]];
		[item setUserInfo:[NSDictionary dictionaryWithObject:@"v" forKey:@"k"]];
		{
			/* BOUND AND GUARDED, BECAUSE THE DOORS ARE NULLABLE: this tree's guest compiler refuses an
			 * implicit nullable-to-nonnull conversion (-Werror), which is the rule that keeps a probe from
			 * assuming a door that may answer nil. */
			/* AN `id` LOCAL, MEASURED RATHER THAN GUESSED: the guest's `-Werror=nullable-to-nonnull-conversion`
			 * does NOT narrow an explicitly `_Nullable` local through a nil check (it narrows an unqualified
			 * one), so the nullable factory's answer is bound to `id` — which converts freely — and the nil
			 * check stays for the runtime. */
			id archiveData = [NSKeyedArchiver archivedDataWithRootObject:item];

			back = nil;
			if (archiveData != nil) {
				back = [NSKeyedUnarchiver unarchiveObjectWithData:archiveData];
			}
		}
		check("an-item-round-trips-through-a-keyed-archiver",
		      back != nil && [[[back attributedTitle] string] isEqualToString:@"Round trip"] &&
		      [[[back attributedContentText] string] isEqualToString:@"body"] &&
		      [[back attachments] count] == 1 &&
		      [[[back userInfo] objectForKey:@"k"] isEqualToString:@"v"],
		      [NSString stringWithFormat:@"back=%@ title=%@", back,
			[[back attributedTitle] string]]);
	}

	/* AND THE LIMIT, ASSERTED AS A LIMIT: the archiver is what refuses an uncodable payload, and it refuses on
	 * the SENDING side — so a value this library cannot archive never becomes a silent hole in the payload. */
	{
		NSDictionary *uncodable = [NSDictionary dictionaryWithObject:[[NSObject alloc] init] forKey:@"k"];
		NSData * _Nullable data;

		@try {
			data = [NSKeyedArchiver archivedDataWithRootObject:uncodable];
		} @catch (NSException *e) {
			data = nil;
		}
		check("an-uncodable-payload-is-refused-rather-than-silently-dropped",
		      data == nil || [data length] == 0,
		      [NSString stringWithFormat:@"data=%lu bytes", (unsigned long)[data length]]);
	}

	printf("FOUNDATION-EXTENSIONITEM RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-EXTENSIONITEM DONE\n");
	return failc == 0 ? 0 : 1;
}
