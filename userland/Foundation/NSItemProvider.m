/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSItemProvider.m — the constants only. There is no NSItemProvider class in this system (see the header's note),
 * so this unit defines the keys and the domain and nothing else: the vocabulary is shipped, the door is not.
 */

#import <Foundation/NSItemProvider.h>

/* The domain and every key answer their OWN NAMES - this library's convention for these constants (see
 * NSLocale.h for the rule and its reason). NSItemProviderErrorDomain is Apple's own string for the same. */
NSString *const NSItemProviderErrorDomain = @"NSItemProviderErrorDomain";
NSString *const NSItemProviderPreferredImageSizeKey = @"NSItemProviderPreferredImageSizeKey";
NSString *const NSExtensionJavaScriptPreprocessingResultsKey = @"NSExtensionJavaScriptPreprocessingResultsKey";
NSString *const NSExtensionJavaScriptFinalizeArgumentKey = @"NSExtensionJavaScriptFinalizeArgumentKey";
