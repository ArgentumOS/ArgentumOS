/*
 * NSURLError.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE SIX STRINGS THE ERROR NAMES NEED TO EXIST (the header's design and its provenance note are in
 * NSURLError.h; docs/design/foundation-plan.md §55 and §56 are the measurements behind it).
 *
 * WHY THIS FILE IS ONE DECLARATION PER CONSTANT AND NOTHING ELSE: an `extern` in a header is a promise, and
 * this is where the library keeps it - the codes are enum constants and need no storage at all, while the
 * domain and the five userInfo keys are OBJECTS, so a caller in another translation unit gets a link error
 * rather than a nil without them. THAT IS NOT HYPOTHETICAL: the first build of §56's probe failed exactly
 * there, with `undefined reference to 'NSURLErrorDomain'`.
 *
 * THE DOMAIN'S VALUE IS APPLE'S, because it crosses systems: an NSError serialised by this library is read
 * back elsewhere by domain. THE FIVE KEYS' VALUES ARE OURS (§11.6.1 D2), and they are deliberately each
 * constant's own name, so a dictionary printed anywhere says which key it is without a caller having to look
 * one up.
 */
#import <Foundation/NSURLError.h>

NSString * const NSURLErrorDomain = @"NSURLErrorDomain";

NSString * const NSURLErrorKey = @"NSURLErrorKey";
NSString * const NSURLErrorFailingURLErrorKey = @"NSURLErrorFailingURLErrorKey";
NSString * const NSURLErrorFailingURLPeerTrustErrorKey = @"NSURLErrorFailingURLPeerTrustErrorKey";
NSString * const NSURLErrorNetworkUnavailableReasonKey = @"NSURLErrorNetworkUnavailableReasonKey";
NSString * const NSURLErrorBackgroundTaskCancelledReasonKey = @"NSURLErrorBackgroundTaskCancelledReasonKey";
