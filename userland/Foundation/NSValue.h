/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSValue — a box for anything that is not an object. docs/design/foundation-plan.md §10 (the
 * un-refusal program), F13.8c.
 *
 * WHY A LIBRARY THAT ALREADY BOXES SCALARS INTO NSNumber NEEDS THIS: NSNumber is for NUMBERS —
 * it answers -intValue, -doubleValue, -compare:, and it is what arithmetic and sorting want. A
 * value is for a STRUCTURE: bytes with a type encoding, which no number can carry. That is also
 * what makes it load-bearing for the coders (`NSKeyedArchiver` writes structs as values) and for
 * the geometry types a UI layer passes around.
 *
 * THE TWO DOORS THAT MATTER, and why the signature looks like C:
 *   +valueWithBytes:objCType:  copies sizeof(type) bytes, so the caller keeps its own storage;
 *   -getValue:                 copies them back, and the caller is responsible for the buffer
 *                              being large enough — exactly as in Cocoa.
 *
 * THE SIZE COMES FROM THE ENCODING, which is the one piece of real work here: this library WALKS it —
 * scalars, pointers, objects, C arrays and structs/unions, recursively — and RAISES on an encoding it
 * cannot size rather than guessing a length. Named limits: no bitfields, and no Objective-C++ encodings.
 *
 * ⚠ AND IT WALKS IT AS `NSGetSizeAndAlignment` ITSELF, WHICH THIS COMMENT USED TO DENY (§63.23). The sentence
 * here read "there is no NSGetSizeAndAlignment in this library, so `fn_value_size` walks the encoding", and
 * that was wrong twice over: the function has existed since W2e — declared in NSObjCRuntime.h and DEFINED at
 * the end of this header's implementation, on the very walker the size comes from, so the two cannot
 * disagree — and `fn_value_size` was that walker's OLD NAME (it is `fn_measure` now). A header that denies a
 * function its own implementation ships is exactly the kind of sentence a reader has no reason to doubt.
 *
 * THE FOUNDATION-GEOMETRY DOORS SHIP HERE, AND THIS COMMENT USED TO DENY THEM. It read
 * "+valueWithPoint:/+valueWithSize:/+valueWithRect: are declared in Cocoa's NSValue.h, and this library has no
 * NSPoint/NSSize/NSRect to build them from" — and THAT was FALSE the day it was written: NSGeometry.h (W2b)
 * has typedef'd NSPoint/NSSize/NSRect to the CG value types since the CG types landed (NSGeometry.h:40-42,
 * with NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES at :44), so the types these three doors name are in the tree.
 * It is the SAME defect corrected two paragraphs up for NSGetSizeAndAlignment (§63.23) — a header denying what
 * the tree ships, in a sentence a reader has no reason to doubt — and the doors are declared below. NSRange,
 * which was already here, stays supported.
 */

#ifndef FOUNDATION_NSVALUE_H
#define FOUNDATION_NSVALUE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSObjCRuntime.h>
/* FOR `NSCoding`: ABOVE THE ASSUME-NONNULL REGION, which is where NSArray.h puts it too — a `#include` inside a
 * nullability region is refused by the compiler, because the imported header's declarations would be dragged
 * into somebody else's region. */
#import <Foundation/NSCoding.h>
/* FOR THE SIX FOUNDATION-GEOMETRY DOORS (NSPoint/NSSize/NSRect), and ABOVE THE ASSUME-NONNULL REGION for the same
 * reason as NSCoding just above: a `#import` inside a nullability region is refused by the compiler, because the
 * imported header's own declarations would be dragged into this file's region. This is what makes the three
 * +valueWith…: doors and their three readers declarable at all — the types they name are NSGeometry.h's, and
 * (NSGeometry.h:44) those ARE the CG value types. */
#import <Foundation/NSGeometry.h>
/* FOR THE COREGRAPHICS-GEOMETRY DOORS (CGPoint/CGSize/CGRect/CGVector and CGAffineTransform), also ABOVE the
 * region. CGGeometry.h is reached through NSGeometry.h already, but it is named here for the same reason the
 * implementation names its imports: a reader should not have to follow a typedef to learn why a name is in
 * scope. */
#import <CoreGraphics/CGGeometry.h>
#import <CoreGraphics/CGAffineTransform.h>

NS_ASSUME_NONNULL_BEGIN

/* ===================================================================================================
 * NSVALUE AND §C.3 (2026-09-29, M8). A FRONT holding a payload - the bytes, their size and the ObjC type that
 * describes them - with the doors over it.
 *
 * THE PRIMITIVES ARE THE PAYLOAD PAIR PLUS ITS SIZE:
 *
 *     -fnBytes      -fnSize      -objCType
 *
 * -getValue:, -pointerValue, -rangeValue, -isEqualToValue: (and so -isEqual:), -hash and -description are written
 * over those three, so a class answering them is an NSValue as far as this family is concerned. WHAT IS NOT: the
 * construction paths (+value:withObjCType:, +valueWithPointer:, +valueWithRange:) and -copy, which are the
 * storage implementation.
 *
 * THE PLAN SAYS "the payload into concrete classes" AND THIS IS THAT CONTRACT WITHOUT THE SPLIT: the payload is
 * what the primitives hand out, the doors read nothing else, and a class that holds a payload differently is
 * correct through all of them. Splitting the payload into a class per kind (the NSNumber shape) is a later step
 * and needs its own reason.
 * =================================================================================================== */

@interface NSValue : NSObject <NSCopying, NSCoding>
{
	const char *_objCType;		/* OWNED: a copy of the caller's string */
	void *_bytes;			/* OWNED: _size bytes, the caller's storage copied in */
	NSUInteger _size;
}

/* THE NSCoding DOORS (§63.22). THE WIRE IS OURS (Apple's spelling for this class's coded form is not
 * published in a source this project may read). THE ENCODING TRAVELS WITH THE BYTES, because the encoding is
 * what says how many bytes there are — this class's own point, and the reason the pair cannot drop it. */
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;

/* THE INSTANCE SPELLING OF THE RAW-BYTES DOOR, beside its class twin below: Apple offers
 * -initWithBytes:objCType: as the designated initializer, and both it and +valueWithBytes:objCType: funnel
 * through the same private -fnInitWithBytes:, so the two cannot diverge. */
- (instancetype)initWithBytes:(const void *)value objCType:(const char *)type;

+ (NSValue *)valueWithBytes:(const void *)value objCType:(const char *)type;
/* The older spelling, still present in Cocoa's header, so it is here too. */
+ (NSValue *)value:(const void *)value withObjCType:(const char *)type;
+ (NSValue *)valueWithPointer:(nullable const void *)pointer;
/* A BORROWED reference: Apple documents this door as equivalent to +value:withObjCType: over
 * @encode(void *), so the box keeps NO claim on the object — it holds the address and neither retains nor
 * releases (the implementation states the full contract). */
+ (NSValue *)valueWithNonretainedObject:(nullable id)anObject;
+ (NSValue *)valueWithRange:(NSRange)range;

/* THE FOUNDATION-GEOMETRY DOORS, in Cocoa's shape: the type IS the encoding — @encode(NSPoint) is
 * "{CGPoint=dd}" because NSGeometry.h makes NSPoint a typedef of CGPoint — so each door is +valueWithRange:'s
 * shape over its struct, and each reader (below) is the range reader's shape over the same private primitive. */
+ (NSValue *)valueWithPoint:(NSPoint)point;
+ (NSValue *)valueWithSize:(NSSize)size;
+ (NSValue *)valueWithRect:(NSRect)rect;

/* THE COREGRAPHICS-GEOMETRY DOORS, the same shape again: the CG structs are Apple's published value types
 * (userland/CoreGraphics/), so each door is +valueWithRange:'s shape over its struct and each reader below is
 * the range reader's shape. @encode(CGPoint) is "{CGPoint=dd}"; @encode(CGAffineTransform) is the six doubles
 * of the 3x2 matrix. */
+ (NSValue *)valueWithCGPoint:(CGPoint)point;
+ (NSValue *)valueWithCGSize:(CGSize)size;
+ (NSValue *)valueWithCGRect:(CGRect)rect;
+ (NSValue *)valueWithCGVector:(CGVector)vector;
+ (NSValue *)valueWithCGAffineTransform:(CGAffineTransform)transform;

/* THE EDGE-INSETS DOOR — the boxed type is `NSEdgeInsets`, NOT UIKit's `UIEdgeInsets`. Apple's own
 * documentation gives the Swift spelling init(edgeInsets: NSEdgeInsets), and the UIKit spelling is a
 * DIFFERENT selector (+valueWithUIEdgeInsets:); this tree is macOS-shaped, ships `NSEdgeInsets`
 * (NSGeometry.h:48-53), and does not ship `UIEdgeInsets`.
 *
 *   ⚠ ENCODING DEVIATION, recorded where a reader meets it: @encode(NSEdgeInsets) HERE is
 *   "{_NSEdgeInsets=dddd}" because NSGeometry.h tags the struct `_NSEdgeInsets`, while Apple's tag is
 *   `NSEdgeInsets` — its encoding is "{NSEdgeInsets=dddd}". The LAYOUT is identical (four CGFloat, 32 bytes)
 *   but -objCType EXPOSES the string, so a value boxed here differs from Apple's in that string and in
 *   -isEqualToValue: against an Apple-built one. Retagging the struct is NSGeometry.h's, not this file's. */
+ (NSValue *)valueWithEdgeInsets:(NSEdgeInsets)insets;

- (void)getValue:(void *)value;
/* Copies at most `size` bytes, and the encoding must fit: Cocoa's own door, for a caller with a
 * buffer of a size it computed itself. */
- (void)getValue:(void *)value size:(NSUInteger)size;

- (const char *)objCType;
- (nullable void *)pointerValue;
/* The BORROWED object back — a +0 reference, so ownership never moves to the caller. */
- (nullable id)nonretainedObjectValue;
- (NSRange)rangeValue;
- (NSPoint)pointValue;
- (NSSize)sizeValue;
- (NSRect)rectValue;
- (CGPoint)CGPointValue;
- (CGSize)CGSizeValue;
- (CGRect)CGRectValue;
- (CGVector)CGVectorValue;
- (CGAffineTransform)CGAffineTransformValue;
- (NSEdgeInsets)edgeInsetsValue;

- (BOOL)isEqualToValue:(NSValue *)value;
- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSVALUE_H */
