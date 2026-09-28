/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSExtensionItem.m — a value object with a wire form (§62.69). MANUAL OWNERSHIP.
 *
 * FOUR FIELDS AND FOUR DOORS EACH, all copied; the two protocols Apple lists; and nothing else. The file exists to
 * make those claims TRUE rather than merely stated, which is why the setters, `-copy` and the two coder doors are
 * written out and the probe checks each of them.
 *
 * THE CODER DOORS USE THE KEYED ARCHIVER'S OWN OBJECT DOORS (`-encodeObject:forKey:`), one key per property, so
 * the payload is the object graph rather than a flattened blob: a value this library cannot archive makes the
 * ARCHIVER refuse, on the SENDING side, which is where §62.53 put that decision.
 */

#import <Foundation/NSExtensionItem.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSAttributedString.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>

/* THE STORAGE, IN A CLASS EXTENSION: the accessors below are written out rather than synthesised, and an
 * explicit accessor brings no ivar with it — nothing declares these four slots unless this file does. They are in
 * the .m rather than in the header because they are this implementation's business and no caller's. */
@interface NSExtensionItem ()
{
@private
	NSArray *_attachments;
	NSAttributedString *_attributedContentText;
	NSAttributedString *_attributedTitle;
	NSDictionary *_userInfo;
}
@end

/* APPLE PUBLISHES THE NAMES AND NOT THE TEXT (§11.6.1 D2): the value is the name, because the constant's job is
 * to label a payload entry that a person may have to read. */
NSString *const NSExtensionItemAttachmentsKey = @"NSExtensionItemAttachmentsKey";
NSString *const NSExtensionItemAttributedContentTextKey = @"NSExtensionItemAttributedContentTextKey";
NSString *const NSExtensionItemAttributedTitleKey = @"NSExtensionItemAttributedTitleKey";

/* THE ONE SHAPE EVERY SETTER HAS: snapshot through the value's own -copy, release what was there, keep the
 * snapshot. Nil is a value here — clearing a field is how an extension says "nothing of this kind". */
static void fn_set_copy(id *slot, id value)
{
	id kept = [value copy];

	[*slot release];
	*slot = kept;
}

@implementation NSExtensionItem

- (nullable NSArray *)attachments { return _attachments; }
- (void)setAttachments:(nullable NSArray *)attachments { fn_set_copy((id *)&_attachments, attachments); }
- (nullable NSAttributedString *)attributedContentText { return _attributedContentText; }
- (void)setAttributedContentText:(nullable NSAttributedString *)text
{
	fn_set_copy((id *)&_attributedContentText, text);
}
- (nullable NSAttributedString *)attributedTitle { return _attributedTitle; }
- (void)setAttributedTitle:(nullable NSAttributedString *)title { fn_set_copy((id *)&_attributedTitle, title); }
- (nullable NSDictionary *)userInfo { return _userInfo; }
- (void)setUserInfo:(nullable NSDictionary *)userInfo { fn_set_copy((id *)&_userInfo, userInfo); }

- (void)dealloc
{
	[_attachments release];
	[_attributedContentText release];
	[_attributedTitle release];
	[_userInfo release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

/* THE VALUE COPY: every field through its own setter, so the copy properties snapshot what they are given rather
 * than sharing the sender's mutable state. */
- (id)copy
{
	NSExtensionItem *copy = [[NSExtensionItem alloc] init];

	[copy setAttachments:_attachments];
	[copy setAttributedContentText:_attributedContentText];
	[copy setAttributedTitle:_attributedTitle];
	[copy setUserInfo:_userInfo];
	return copy;
}

/* NSSecureCoding: a class that crosses a process boundary has to say whether it can be trusted to decode itself,
 * and the honest answer for a value object whose fields it names is YES. */
+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_attachments forKey:@"NSExtensionItemAttachments"];
	[coder encodeObject:_attributedContentText forKey:@"NSExtensionItemAttributedContentText"];
	[coder encodeObject:_attributedTitle forKey:@"NSExtensionItemAttributedTitle"];
	[coder encodeObject:_userInfo forKey:@"NSExtensionItemUserInfo"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		/* THROUGH THE SETTERS, so a decoded item is as snapshot-safe as one that was built by hand. */
		[self setAttachments:[coder decodeObjectForKey:@"NSExtensionItemAttachments"]];
		[self setAttributedContentText:[coder decodeObjectForKey:@"NSExtensionItemAttributedContentText"]];
		[self setAttributedTitle:[coder decodeObjectForKey:@"NSExtensionItemAttributedTitle"]];
		[self setUserInfo:[coder decodeObjectForKey:@"NSExtensionItemUserInfo"]];
	}
	return self;
}

@end
