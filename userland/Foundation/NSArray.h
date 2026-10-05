/*
 * NSArray.h — an ordered collection that IS a CFArray.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS CLASS DECLARES NO STORAGE IVARS, AND WHY THAT IS THE WHOLE POINT. This library's claim is that an
 * NS object IS a CF object, not a copy of one, and a container is where that claim is easiest to cheat on.
 * Two shapes were possible, and the wrong one is instructive: a class holding a CFArrayRef in an ivar
 * satisfies every signature while the two worlds hold DIFFERENT objects — and CF cannot be fooled about it,
 * because CF's own doors read a CFRuntimeBase header out of the object's first words. With an ivar in the
 * way, `CFArrayGetCount((CFArrayRef)anArray)` reads the object's SECOND word, which for a non-CF-shaped
 * object is not a count at all (measured: it answered 1 — the object's retain count).
 *
 * So the storage is not "CF's rather than ours" — THERE IS NO SEPARATE STORAGE. Self IS the CFArray: the
 * object CFArrayCreate returns, with this class as its isa. `(CFArrayRef)array` and `(NSArray *)cfArray` are
 * then both free, which is the only reason the cast this campaign is built on can be written at all.
 *
 * AND IT IS A ROOT CLASS FOR THE SAME REASON, WHICH IS THE SHARPER HALF. NSObject carries three words —
 * `Class isa`, `unsigned long long _cfinfoa`, `unsigned int _refcount` — and the third one sits at OFFSET 16.
 * CFArray's `_count` sits at offset 16 too:
 *
 *     struct __CFArray { CFRuntimeBase _base;  // _cfisa + _cfinfoa  = 16 bytes
 *                        CFIndex _count;        // <- offset 16: the same word
 *                        ... };
 *
 * So an NSObject SUBCLASS would shadow CF's first storage field with a refcount, and every NSObject method
 * that reads `_refcount` would read CF's element count instead — `-retainCount` would answer 3 for a
 * three-element array. That is exactly the "two things sharing one word" failure this re-base exists to end,
 * and it is why NSString is a root class as well. Declaring `Class isa` and nothing else makes the object's
 * words CF's words.
 *
 * WHAT A ROOT CLASS OWES, THEREFORE: it inherits NOTHING, so the NSObject protocol's doors — -retain,
 * -release, -class, -isKindOfClass:, -isEqual:, -hash, -description — and +alloc are all answered here, and
 * each is CF's answer wherever CF has one. (An NSObject SUBCLASS conforms to that protocol by inheritance and
 * needs no clause; a root class must name it, which is the spelling difference this header used to lack.)
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

/* A ROOT CLASS — objc_root_class is how libobjc2 is told the absent superclass is deliberate — whose only
 * field is CF's own first word. */
__attribute__((objc_root_class))
@interface NSArray <NSObject>
{
	Class isa;		/* the CF object's own first word: CF's header IS the object here */
}

/* ALLOCATING AN NSArray MAKES AN EMPTY CF ARRAY, which is the only kind of object this class can be. It must
 * be +alloc rather than a "shell" for one measured reason: a receiver with no CF storage would be an NSArray
 * whose words are not CF's, and the +alloc every object would otherwise inherit is NSObject's, which writes a
 * refcount into a class that has nowhere to put one (offset 16 is CF's `_count`, and a root class with only
 * `isa` is 8 bytes wide). There is no shell in this design, so there is nothing to size.
 *
 * THE TWO ARE ANNOTATED FROM THE IMPLEMENTATION RATHER THAN BY CONVENTION, which is this tree's standing
 * practice and here it also silences a real -Wnullability-completeness warning: +alloc CAN answer nil (it is
 * whatever CFArrayCreate returned), so it is _Nullable, while -init answers the receiver and never nil. */
+ (id _Nullable)alloc;

- (id _Nonnull)init;

/* THE ONE DOOR THAT FILLS AN ARRAY, and it cannot answer with `self` — see the implementation. That is why the
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
