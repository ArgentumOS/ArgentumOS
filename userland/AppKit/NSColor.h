/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSColor.h — a colour as a value, and the first drawing class (C8.4).
 *
 * AN NSCOLOR IS A CGCOLOR, AND THAT IS THE WHOLE DESIGN. Apple's NSColor wraps `CGColorRef` on
 * macOS the same way, and this tree already has the Core Graphics colour: the spaces (device RGB,
 * device gray, sRGB, Display P3, generic gray gamma 2.2), the component accessors, the alpha, and
 * `CGColorCreateCopyByMatchingToColorSpace` — the ENGINE, lcms2, which is what makes conversion
 * below possible at all. So this class OWNS ONE PIECE OF STATE (a retained `CGColorRef`) and every
 * member is a mapping onto it; there is no second colour model hiding in here, which is what keeps
 * `-CGColor` from being a lossy door.
 *
 * THE SURFACE IS PINNED, NOT RECALLED, and the selectors came from Apple's navigator tree
 * (`tools/appkit-sweep.py`), not from memory: the ledger reduces a method to its FIRST selector
 * component, so a header written from the ledger alone would have guessed the tails. Everything
 * Apple documents as DEPRECATED is absent, and the refusals below are named with their reason.
 *
 * WHAT IS NOT HERE, IN FOUR GROUPS:
 *
 *   NOT IN THIS SLICE, AND EACH HAS A HOME: `-colorSpace` and `-colorUsingColorSpace:` need
 *   `NSColorSpace`; `-colorSpaceName` and `-colorUsingColorSpaceName:` need the
 *   `NSColorSpaceName` string constants; `-drawSwatchInRect:` needs `NSRectFill` and the AppKit
 *   rect helpers; `-colorWithSystemEffect:`, `-highlightWithLevel:`, `-blendedColorWithFraction:`
 *   and the `NSColor` SYSTEM colours (`labelColor`, `controlAccentColor`, the 20-odd semantic
 *   colours) need an appearance system this tree does not have. The `HSB` and `CMYK` constructors
 *   and getters are absent too, and for the ordinary reason: this library's colour spaces are RGB
 *   and gray (CMYK is refused by the engine, as C4 records).
 *
 *   NO SUBSTRATE: the pattern, `CIColor`, pasteboard, catalog and named-colour constructors
 *   (`+colorWithPatternImage:`, `+colorWithCIColor:`, `+colorFromPasteboard:`,
 *   `+colorWithCatalogName:`, `+colorNamed:`, `+colorWithName:`) each need a subsystem — an image
 *   class, Core Image, a pasteboard, a colour catalog — that does not exist here yet.
 *
 *   AND TWO THAT ARE HDR AND OUT BY THE SAME REASON AS THE `ContentHeadroom` ROWS:
 *   `+colorWithRed:green:blue:alpha:exposure:` and `…:linearExposure:` ask for an extended-range
 *   pipeline this layer does not have.
 *
 * A DEVIATION, STATED BECAUSE IT IS A CHOICE AND NOT AN OVERSIGHT. Apple has three DISTINCT RGB
 * spaces behind these constructors — device, "calibrated" (the generic RGB profile) and generic —
 * and this library has one RGB story: device RGB, which here IS sRGB (C4 says so). So
 * `+colorWithDeviceRed:`, `+colorWithCalibratedRed:` and `+colorWithRed:` all resolve to it, and the
 * white forms likewise. The CONSEQUENCE is that two colours made by different constructors with the
 * same numbers compare equal, which is the reading this library already takes of a device space
 * ("the numbers are the ones to blend"), and `-colorSpace` — deferred — would not tell them apart.
 * The named spaces that DO carve out a real distinction are used where Apple names them:
 * `+colorWithSRGBRed:` uses the sRGB profile, `+colorWithDisplayP3Red:` Display P3, and
 * `+colorWithGenericGamma22White:` the gamma-2.2 gray profile.
 *
 * AND THE COMPONENT GETTERS CONVERT RATHER THAN RAISING, WHICH IS THE SECOND DEVIATION. Apple's
 * `-redComponent` documents an `NSInternalInconsistencyException` for a colour outside an RGB space;
 * this library asks the ENGINE for the value in the space the getter needs instead, so a white
 * colour has a red component (the same colour in RGB, and NOT the same number: the ENGINE is converting between gamma-2.2 gray
 * against D50 and sRGB, so 0.5 in comes back as 0.5039 — a real transform, which the probe measures) and an RGB colour has a white one (its luminance
 * through the profile). A raise is only left for the case the engine genuinely cannot answer. The
 * reading is that a library with a colour engine and a defined answer should give it.
 *
 * AND THE CONVERSION IS A REAL ONE, WHICH IS WORTH SAYING BECAUSE THE FIRST VERSION OF THIS HEADER
 * SAID THE OPPOSITE. It claimed the gray-to-RGB trip was "the gray value" — i.e. an identity — and the
 * probe measured 0.5039 for a 0.5 gray. Device gray here is gamma-2.2 against D50 and device RGB is
 * sRGB (CoreGraphics' own conversion table says so), so the two spaces disagree by a gamma and the
 * engine is doing the work a colour engine exists to do. Every check that crosses the pair therefore
 * carries a colour-transform tolerance and not an exact one.
 */
#ifndef APPKIT_NSCOLOR_H
#define APPKIT_NSCOLOR_H

#import <CoreGraphics/CGColor.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSColor : NSObject <NSCopying>
{
	CGColorRef _cgColor;    /* RETAINED, and the only state this class has */
}

/* THE BRIDGE, IN BOTH DIRECTIONS. `-CGColor` returns the retained colour itself rather than a copy,
 * because there is only one representation to hand back; `+colorWithCGColor:` answers nil for a NULL
 * colour rather than wrapping nothing.
 *
 * AND TAKING A NULLABLE COLOUR IS A DELIBERATE DIFFERENCE FROM APPLE: under an assume-non-null region
 * Apple's parameter is non-null, but a CGColorRef in a caller's hand is very often one that came from
 * something which may have failed (`[... CGColor]` on a nil object IS NULL), so the choice here is to
 * answer nil rather than make a caller unwrap an optional a second time. The declaration says
 * `nullable`, so the contract and the behaviour are one sentence — the compiler pointed at the gap
 * when the probe first passed NULL to what the header had called non-null. */
+ (nullable NSColor *)colorWithCGColor:(nullable CGColorRef)cgColor;
@property (nullable, readonly) CGColorRef CGColor;

/* THE COMPONENT CONSTRUCTORS, over the spaces this library has. Each returns nil only if Core
 * Graphics refused to build the colour, which for these spaces means a NULL space — it cannot
 * happen through this header, and returning nil beats returning a colour in no space. */
+ (nullable NSColor *)colorWithDeviceRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				   alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithDeviceWhite:(CGFloat)white alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithCalibratedRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				       alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithCalibratedWhite:(CGFloat)white alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
			     alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithWhite:(CGFloat)white alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithSRGBRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				 alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithDisplayP3Red:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				      alpha:(CGFloat)alpha;
+ (nullable NSColor *)colorWithGenericGamma22White:(CGFloat)white alpha:(CGFloat)alpha;

/* THE COMPONENTS IN THE COLOUR'S OWN TERMS, converting through the engine when the colour is not
 * already in the space the getter needs — see the header note above. `numberOfComponents` counts
 * ALPHA, which is Apple's reading and this library's (`CGColorGetNumberOfComponents` says the same).
 * `getRed:green:blue:alpha:` and `getWhite:alpha:` are the pointer forms of the same values. */
@property (readonly) NSUInteger numberOfComponents;
@property (readonly) CGFloat alphaComponent;
@property (readonly) CGFloat redComponent;
@property (readonly) CGFloat greenComponent;
@property (readonly) CGFloat blueComponent;
@property (readonly) CGFloat whiteComponent;
- (void)getRed:(nullable CGFloat *)red green:(nullable CGFloat *)green blue:(nullable CGFloat *)blue
	 alpha:(nullable CGFloat *)alpha;
- (void)getWhite:(nullable CGFloat *)white alpha:(nullable CGFloat *)alpha;
- (void)getComponents:(CGFloat *)components;

/* COPIES THE COLOUR WITH A DIFFERENT ALPHA, which Core Graphics does in one call and which is the
 * only mutation a value class needs. */
- (NSColor *)colorWithAlphaComponent:(CGFloat)alpha;

/* THE FIFTEEN CLASS COLOURS, and they are spelled as PROPERTIES because that is how Apple's index
 * files them (`NSColor.blackColor` rather than `+blackColor`) — the Objective-C accessors are
 * `+blackColor` and friends either way, which is what a caller writes. */
+ (NSColor *)blackColor;
+ (NSColor *)whiteColor;
+ (NSColor *)redColor;
+ (NSColor *)greenColor;
+ (NSColor *)blueColor;
+ (NSColor *)cyanColor;
+ (NSColor *)yellowColor;
+ (NSColor *)magentaColor;
+ (NSColor *)orangeColor;
+ (NSColor *)purpleColor;
+ (NSColor *)brownColor;
+ (NSColor *)grayColor;
+ (NSColor *)darkGrayColor;
+ (NSColor *)lightGrayColor;
+ (NSColor *)clearColor;

@end

NS_ASSUME_NONNULL_END

#endif /* APPKIT_NSCOLOR_H */
