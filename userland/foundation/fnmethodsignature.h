/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * fnmethodsignature.h — PRIVATE additions to NSMethodSignature, for NSInvocation's
 * marshalling and for nothing else. NOT installed: the public headers are audited
 * against Cocoa and this is not Cocoa's business.
 *
 * WHY THE SIZES LIVE HERE and not in ninvocation.m: the widths and the
 * float-vs-integer class of a type are the PARSER's knowledge (nmethodsignature.m
 * already computes them), and a second copy would be two sources of truth for the
 * same table. The accessors are the one honest way to share it without widening
 * the audited public surface.
 */
#ifndef FOUNDATION_FNMETHODSIGNATURE_H
#define FOUNDATION_FNMETHODSIGNATURE_H

#import <foundation/NSMethodSignature.h>

@interface NSMethodSignature (FNPrivate)

/* One argument's width in bytes, 0 when the type cannot be measured (or is
 * absent); and whether it travels in an SSE register rather than an integer one. */
- (NSUInteger)fnSizeOfArgumentAtIndex:(NSUInteger)index;
- (BOOL)fnArgumentIsFloatingAtIndex:(NSUInteger)index;

/* The same two questions for the return value. */
- (NSUInteger)fnSizeOfReturnValue;
- (BOOL)fnReturnIsFloating;

@end

#endif /* FOUNDATION_FNMETHODSIGNATURE_H */
