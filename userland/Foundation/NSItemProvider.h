/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSItemProvider.h — the vocabulary of a drag-and-drop payload: what a provider is asked for, what may see it,
 * and what can go wrong.
 *
 * THE CLASS ITSELF IS NOT HERE, AND THAT IS A RECORDED DECISION rather than an omission: an item provider exists
 * to hand a payload to another process, and this system's interprocess story is its own (see the pasteboard
 * decision). So the ERRORS, the VISIBILITY levels and the option bits ship as the vocabulary a conforming caller
 * compiles against, and the door is absent rather than stubbed. Names Apple's, values ours (§11.6.1 D2).
 */

#ifndef FOUNDATION_NSITEMPROVIDER_H
#define FOUNDATION_NSITEMPROVIDER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSError.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSArray.h>

NS_ASSUME_NONNULL_BEGIN

/* The completion handler a load is answered with: the item, an error, and (for a coercion) the coordinates the
 * loader asked for. The load handler is the block a registration is MADE of. Neither is called anywhere in this
 * system - there is no provider to call them - so they are declared as the signatures they are. */
typedef void (^NSItemProviderCompletionHandler)(id _Nullable item, NSError * _Nullable error);
typedef void (^NSItemProviderLoadHandler)(NSItemProviderCompletionHandler completionHandler,
					  Class expectedValueClass, NSDictionary *options);

/* What a provider hands out and who may see it. Bit sets, as their names say. */
typedef enum {
	NSItemProviderFileOptionOpenInPlace = 1 << 0
} NSItemProviderFileOptions;

typedef enum {
	NSItemProviderRepresentationVisibilityAll = 1 << 0,
	NSItemProviderRepresentationVisibilityTeam = 1 << 1,
	NSItemProviderRepresentationVisibilityGroup = 1 << 2,
	NSItemProviderRepresentationVisibilityOwnProcess = 1 << 3
} NSItemProviderRepresentationVisibility;

/* The error codes, sequential because nothing in this system compares them to Apple's numbers (D2). */
typedef enum {
	NSItemProviderUnknownError = 0,
	NSItemProviderItemUnavailableError = 1,
	NSItemProviderUnexpectedValueClassError = 2,
	NSItemProviderUnavailableCoercionError = 3
} NSItemProviderErrorCode;

extern NSString *const NSItemProviderErrorDomain;
extern NSString *const NSItemProviderPreferredImageSizeKey;
extern NSString *const NSExtensionJavaScriptPreprocessingResultsKey;
extern NSString *const NSExtensionJavaScriptFinalizeArgumentKey;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSITEMPROVIDER_H */
