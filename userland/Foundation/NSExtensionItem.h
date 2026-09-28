/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSExtensionItem — WHAT AN EXTENSION HANDS ACROSS ITS BOUNDARY (plan §62.69), and the last row of
 * `App Support / Attachments` (§62.23 took that family from four rows to one).
 *
 * THE SHAPE IS A VALUE OBJECT WITH A WIRE FORM, which is what separates it from the images and views it carries:
 * four properties, every one of them COPIED, and THREE CONSTANTS THAT ARE THEIR NAMES ON THE WIRE. The constants
 * are not decoration and the probe checks what they are for: an extension's payload is a DICTIONARY, and these
 * three keys are the only way its reader knows which entry is the title. Apple's own pages describe each one as
 * "the key for the <property>" — so the property and the key are two spellings of one field, and this header
 * says so in both directions.
 *
 * IT CONFORMS TO THE TWO PROTOCOLS APPLE LISTS, and each is a real promise rather than a label:
 *
 *   * `NSCopying` — a copy is a VALUE copy, so an item can be handed on without its sender's later changes
 *     following it;
 *   * `NSSecureCoding` — the coder doors are implemented and `+supportsSecureCoding` answers YES, which is what
 *     a class that crosses a process boundary must say.
 *
 * ONE CONSEQUENCE IS STATED RATHER THAN HIDDEN: `-attachments` is an array of `NSItemProvider` values on Apple's
 * system, and NSItemProvider is NOT codable in this library (§62.23 landed it with `NSCopying` alone) — so an
 * item whose attachments are providers cannot be archived here. The coder is honest about it: it encodes what it
 * is given, and the archiver refuses an uncodable value on the SENDING side, which is this library's stated rule
 * for the keyed archiver rather than a silence (§62.53).
 */

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

NS_ASSUME_NONNULL_BEGIN

@class NSArray;
@class NSAttributedString;
@class NSDictionary;

/* THE THREE NAMES THIS VALUE TRAVELS UNDER in an extension payload. Apple publishes the names and not the text,
 * so the text is ours (§11.6.1 D2) — and the NAME is the value, because the constant's whole job is to be
 * readable in a payload a person has to inspect. */
extern NSString *const NSExtensionItemAttachmentsKey;
extern NSString *const NSExtensionItemAttributedContentTextKey;
extern NSString *const NSExtensionItemAttributedTitleKey;

@interface NSExtensionItem : NSObject <NSCopying, NSSecureCoding>

/* AN ITEM WITH NOTHING IN IT IS VALID — every property is optional on Apple's page, and an extension that has
 * only a title or only attachments is an ordinary extension. */
- (nullable NSArray *)attachments;				/* NSItemProvider values on Apple's system */
- (void)setAttachments:(nullable NSArray *)attachments;
- (nullable NSAttributedString *)attributedContentText;
- (void)setAttributedContentText:(nullable NSAttributedString *)attributedContentText;
- (nullable NSAttributedString *)attributedTitle;
- (void)setAttributedTitle:(nullable NSAttributedString *)attributedTitle;

/* THE CALLER'S OWN BAG, which this class carries without reading: Apple calls it "a dictionary of keys and
 * values corresponding to the extension item's properties", and the three constants above are the keys it
 * normally holds. */
- (nullable NSDictionary *)userInfo;
- (void)setUserInfo:(nullable NSDictionary *)userInfo;

@end

NS_ASSUME_NONNULL_END
