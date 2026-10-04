/*
 * NSArray.h — an ordered collection that IS a CFArray.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS CLASS DECLARES NO IVARS, AND WHY THAT IS THE WHOLE POINT. This library's claim is that an NS
 * object IS a CF object, not a copy of one, and a container is where that claim is easiest to cheat on. Two
 * shapes were possible, and the wrong one is instructive: a class holding a CFArrayRef in an ivar satisfies
 * every signature while the two worlds hold DIFFERENT objects — and CF cannot be fooled about it, because
 * CF's own doors read a CFRuntimeBase header out of the object's first words. With an ivar in the way,
 * `CFArrayGetCount((CFArrayRef)anArray)` reads the object's SECOND word, which for a non-CF-shaped object is
 * not a count at all (measured: it answered 1 — the object's retain count).
 *
 * So the storage is not "CF's rather than ours" — THERE IS NO SEPARATE STORAGE. Self IS the CFArray: the
 * object CFArrayCreate returns, with this class as its isa. `(CFArrayRef)array` and `(NSArray *)cfArray` are
 * then both free, which is the only reason the cast this campaign is built on can be written at all. This is
 * NSString's shape exactly, and NSArray follows it for the re-base's own reason: a half-swapped storage that
 * two ownership regimes share is how a class starts reading foreign words.
 *
 * AND THE ITEMS' OWNERSHIP IS CF'S, WHICH WAS MEASURED RATHER THAN ASSUMED. CFArray with CF's own callbacks
 * retains what it is given and releases it when the array goes; two checks in the object probe prove it for
 * objects THIS library builds (a-cf-array-holds-an-object-of-this-library and
 * a-cf-array-releases-it-when-the-array-goes). So there is no hand-retained vector to keep in step, which is
 * the failure mode this re-base was warned about.
 *
 * THE FACTORY METHODS ARE NOT HERE YET, AND THAT IS A STATEMENT RATHER THAN AN OMISSION. +array and its
 * relatives return an AUTORELEASED object under Apple's contract, and this library has no autorelease pool:
 * NSObject declares -retain and -release but not -autorelease. Shipping +array returning a retained object
 * would be a silent ownership deviation, so the factories wait for the pool and this class ships the
 * instance-side doors — exactly as NSString, the slice it follows, did.
 *
 * NULLABILITY IS SPELLED WITH CLANG'S KEYWORDS (_Nullable/_Nonnull) rather than Foundation's macros: this
 * tree's headers use the keyword forms, and NS_ASSUME_NONNULL_BEGIN is not defined anywhere in it.
 */

#ifndef FNX_NSARRAY_H
#define FNX_NSARRAY_H

#import <Foundation/NSObject.h>
#import <CoreFoundation/CFArray.h>

@class NSString;

/* NO IVARS, DELIBERATELY: an NSArray IS a CFArray, and a field here would describe an object that is not one. */
@interface NSArray : NSObject

/* THE ONE DOOR THAT BUILDS AN ARRAY, and it cannot answer with `self`. The receiver it is sent to is the
 * +alloc'd shell, which has this class as its isa but no CF storage, so the door builds the CFArray this class
 * is the face of and returns IT, disposing the shell — the class-cluster initialiser shape. That is why the
 * result is `instancetype` rather than a promise that the address is unchanged. */
- (instancetype _Nonnull)initWithObjects:(const id _Nonnull * _Nullable)objects count:(NSUInteger)count;

- (NSUInteger)count;
- (id _Nonnull)objectAtIndex:(NSUInteger)index;
- (id _Nonnull)objectAtIndexedSubscript:(NSUInteger)index;
- (id _Nullable)firstObject;
- (id _Nullable)lastObject;
- (BOOL)containsObject:(id _Nonnull)anObject;
- (NSUInteger)indexOfObject:(id _Nonnull)anObject;

@end

#endif	/* FNX_NSARRAY_H */
