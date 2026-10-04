/*
 * NSArray.h — an ordered collection whose STORAGE IS A CFArray.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THE STORAGE IS CF'S RATHER THAN OURS. This library's claim is that an NS object IS a CF object, not a
 * copy of one, and a container is where that claim is easiest to cheat on: an array keeping its own item
 * vector and building a CFArray on demand would satisfy every signature while making the two worlds disagree
 * about a single object's identity. Here there is ONE array, CF's, and this class is a face on it.
 *
 * AND THE ITEMS' OWNERSHIP IS CF'S, WHICH WAS MEASURED RATHER THAN ASSUMED. CFArray with CF's own callbacks
 * retains what it is given and releases it when the array goes; two checks in the object probe prove it for
 * objects THIS library builds (a-cf-array-holds-an-object-of-this-library and
 * a-cf-array-releases-it-when-the-array-goes). So there is no hand-retained _items vector to keep in step,
 * which is the failure mode this re-base was warned about.
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

@interface NSArray : NSObject
{
	CFArrayRef _storage;	/* THE array; every door below reads it and nothing else */
}

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
