/*
 * NSColor — one retained CGColorRef, and every member a mapping onto it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE ENGINE IS WHAT MAKES THE GETTERS POSSIBLE, and it is also what makes them a stated deviation:
 * Apple raises from `-redComponent` for a colour outside an RGB space, while this file asks
 * `CGColorCreateCopyByMatchingToColorSpace` for the value in the space the getter needs. A raise is
 * left for the one case the engine cannot answer, so a caller who would have crashed gets a number
 * and a caller who asked for the impossible still hears about it.
 *
 * AND THE HDR CONSTRUCTORS ARE ABSENT, not forgotten: `+colorWithRed:green:blue:alpha:exposure:` and
 * its `…:linearExposure:` sibling are extended-range entry points, and this layer has no such
 * pipeline — the same reason CoreGraphics' `ContentHeadroom` rows are unimplemented.
 */
#import <AppKit/NSColor.h>
#import <CoreGraphics/CGColor.h>
#import <CoreGraphics/CGColorSpace.h>
#import <Foundation/NSException.h>

/* THE FIELD IS SET FROM INSIDE THE CLASS, which is not fastidiousness: the ivar is PROTECTED, and a
 * free function cannot touch it — the compiler said exactly that when the first version of this file
 * set `c->_cgColor` from `fn_color` below. A private initialiser is the way in for a helper that is a
 * function rather than a method. */
@interface NSColor ()
- (instancetype)initWithOwnedCGColor:(CGColorRef)cg;
@end

/* THE COLOUR OBJECT, TAKING OWNERSHIP OF A +1 CGColor — or nil for a NULL one, which is what
 * `+colorWithCGColor:` documents rather than an object wrapping nothing. */
static NSColor *fn_color(CGColorRef cg)
{
	NSColor *c;

	if (cg == NULL) {
		return nil;
	}
	c = [[NSColor alloc] initWithOwnedCGColor:cg];
	if (c == nil) {
		/* THE OWNERSHIP WAS HANDED OVER, so a failed allocation is the one path where this function
		 * still owns it. */
		CGColorRelease(cg);
	}
	return c;
}

/* A COLOUR IN A NAMED SPACE, which is where the three spaces that are NOT device spaces come from:
 * `CGColorSpaceCreateWithName` answers the shared object for a name it knows and NULL otherwise, so
 * there is no second check to write. */
static CGColorRef fn_named(NSString *name, CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
	CGColorSpaceRef space = CGColorSpaceCreateWithName(name);
	CGFloat comp[4];
	CGColorRef cg;

	if (space == NULL) {
		return NULL;
	}
	comp[0] = r;
	comp[1] = g;
	comp[2] = b;
	comp[3] = a;
	cg = CGColorCreate(space, comp);
	CGColorSpaceRelease(space);
	return cg;
}

/* THE FIFTEEN SINGLETONS, BUILT ONCE AND HELD FOREVER — the shape the device colour spaces already
 * have in CoreGraphics, and for the same reason: these are VALUES, not resources, so a caller who
 * asks twice should get the same object. `+colorWithDeviceRed:` answers +1, which the static keeps. */
static NSColor *fn_singleton(NSColor **slot, CGFloat r, CGFloat g, CGFloat b, CGFloat a)
{
	if (*slot == nil) {
		*slot = [NSColor colorWithDeviceRed:r green:g blue:b alpha:a];
	}
	return *slot;
}

/* THE COMPONENTS IN A GIVEN KIND OF SPACE, converting when the colour is not already there. Returns
 * the count written, or 0 when even the engine cannot make the trip; `out` receives them with ALPHA
 * LAST, the order both of CoreGraphics' arrays and of the pointer forms below use. */
static size_t fn_components_in(CGColorRef c, int want_rgb, CGFloat *out)
{
	CGColorSpaceRef space;
	CGColorSpaceRef target;
	CGColorRef converted;
	const CGFloat *comp;
	size_t n;
	size_t i;

	if (c == NULL) {
		return 0;
	}
	/* A COLOUR ALREADY IN THE RIGHT KIND OF SPACE IS READ DIRECTLY, so the common case does not pay
	 * for a transform. The test is the COMPONENT COUNT, because this library's RGB spaces are one
	 * story and its gray spaces are another (a device space says the numbers are the ones to blend). */
	space = CGColorGetColorSpace(c);
	if (space != NULL) {
		size_t have = CGColorSpaceGetNumberOfComponents(space);

		if ((want_rgb && have == 3) || (!want_rgb && have == 1)) {
			comp = CGColorGetComponents(c);
			for (i = 0; i < have; i++) {
				out[i] = comp[i];
			}
			out[have] = CGColorGetAlpha(c);
			return have + 1;
		}
	}
	target = want_rgb ? CGColorSpaceCreateDeviceRGB() : CGColorSpaceCreateDeviceGray();
	if (target == NULL) {
		return 0;
	}
	converted = CGColorCreateCopyByMatchingToColorSpace(c, kCGRenderingIntentDefault, target, NULL);
	CGColorSpaceRelease(target);
	if (converted == NULL) {
		return 0;
	}
	n = want_rgb ? 3 : 1;
	comp = CGColorGetComponents(converted);
	for (i = 0; i < n; i++) {
		out[i] = comp[i];
	}
	out[n] = CGColorGetAlpha(converted);
	CGColorRelease(converted);
	return n + 1;
}

/* AND THE RAISE, for the case the engine could not answer — see the file's header note. */
static void fn_raise_for(const char *what)
{
	[NSException raise:@"NSInternalInconsistencyException"
		    format:@"-NSColor %s: this colour cannot be expressed in that space", what];
}

@implementation NSColor

- (instancetype)initWithOwnedCGColor:(CGColorRef)cg
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_cgColor = cg;      /* takes the caller's +1 */
	return self;
}

+ (nullable NSColor *)colorWithCGColor:(CGColorRef)cgColor
{
	/* THE CALLER'S REFERENCE IS RETAINED, which is what makes the returned colour independent of it —
	 * the rule `CGColorCreateCopy` follows, and the reason this is not a borrow. */
	return fn_color(CGColorRetain(cgColor));
}

- (CGColorRef)CGColor
{
	return _cgColor;
}

+ (nullable NSColor *)colorWithDeviceRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				   alpha:(CGFloat)alpha
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	CGFloat comp[4];
	CGColorRef cg;

	if (space == NULL) {
		return nil;
	}
	comp[0] = red;
	comp[1] = green;
	comp[2] = blue;
	comp[3] = alpha;
	cg = CGColorCreate(space, comp);
	CGColorSpaceRelease(space);
	return fn_color(cg);
}

+ (nullable NSColor *)colorWithDeviceWhite:(CGFloat)white alpha:(CGFloat)alpha
{
	CGColorSpaceRef space = CGColorSpaceCreateDeviceGray();
	CGFloat comp[2];
	CGColorRef cg;

	if (space == NULL) {
		return nil;
	}
	comp[0] = white;
	comp[1] = alpha;
	cg = CGColorCreate(space, comp);
	CGColorSpaceRelease(space);
	return fn_color(cg);
}

/* THE THREE RGB STORIES ARE ONE STORY HERE — see NSColor.h's deviation note. `Calibrated` and the
 * generic `colorWithRed:` route through the device constructor rather than pretending to a profile
 * this library does not ship. */
+ (nullable NSColor *)colorWithCalibratedRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				       alpha:(CGFloat)alpha
{
	return [self colorWithDeviceRed:red green:green blue:blue alpha:alpha];
}

+ (nullable NSColor *)colorWithCalibratedWhite:(CGFloat)white alpha:(CGFloat)alpha
{
	return [self colorWithDeviceWhite:white alpha:alpha];
}

+ (nullable NSColor *)colorWithRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
			     alpha:(CGFloat)alpha
{
	return [self colorWithDeviceRed:red green:green blue:blue alpha:alpha];
}

/* THE GENERIC WHITE IS THE ONE WHITE THAT HAS A PROFILE OF ITS OWN HERE, so the generic constructor
 * takes it and `+colorWithWhite:` — which Apple defines in terms of the generic space — follows. */
+ (nullable NSColor *)colorWithWhite:(CGFloat)white alpha:(CGFloat)alpha
{
	return [self colorWithGenericGamma22White:white alpha:alpha];
}

+ (nullable NSColor *)colorWithSRGBRed:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				 alpha:(CGFloat)alpha
{
	return fn_color(fn_named(kCGColorSpaceSRGB, red, green, blue, alpha));
}

+ (nullable NSColor *)colorWithDisplayP3Red:(CGFloat)red green:(CGFloat)green blue:(CGFloat)blue
				      alpha:(CGFloat)alpha
{
	return fn_color(fn_named(kCGColorSpaceDisplayP3, red, green, blue, alpha));
}

+ (nullable NSColor *)colorWithGenericGamma22White:(CGFloat)white alpha:(CGFloat)alpha
{
	CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceGenericGrayGamma2_2);
	CGFloat comp[2];
	CGColorRef cg;

	if (space == NULL) {
		return nil;
	}
	comp[0] = white;
	comp[1] = alpha;
	cg = CGColorCreate(space, comp);
	CGColorSpaceRelease(space);
	return fn_color(cg);
}

- (NSUInteger)numberOfComponents
{
	return _cgColor == NULL ? 0 : (NSUInteger)CGColorGetNumberOfComponents(_cgColor);
}

- (CGFloat)alphaComponent
{
	return _cgColor == NULL ? 0.0 : CGColorGetAlpha(_cgColor);
}

- (CGFloat)redComponent
{
	CGFloat comp[4];

	if (fn_components_in(_cgColor, 1, comp) < 4) {
		fn_raise_for("redComponent");
	}
	return comp[0];
}

- (CGFloat)greenComponent
{
	CGFloat comp[4];

	if (fn_components_in(_cgColor, 1, comp) < 4) {
		fn_raise_for("greenComponent");
	}
	return comp[1];
}

- (CGFloat)blueComponent
{
	CGFloat comp[4];

	if (fn_components_in(_cgColor, 1, comp) < 4) {
		fn_raise_for("blueComponent");
	}
	return comp[2];
}

- (CGFloat)whiteComponent
{
	CGFloat comp[2];

	if (fn_components_in(_cgColor, 0, comp) < 2) {
		fn_raise_for("whiteComponent");
	}
	return comp[0];
}

- (void)getRed:(nullable CGFloat *)red green:(nullable CGFloat *)green blue:(nullable CGFloat *)blue
	 alpha:(nullable CGFloat *)alpha
{
	CGFloat comp[4];

	if (fn_components_in(_cgColor, 1, comp) < 4) {
		fn_raise_for("getRed:green:blue:alpha:");
	}
	if (red != NULL) {
		*red = comp[0];
	}
	if (green != NULL) {
		*green = comp[1];
	}
	if (blue != NULL) {
		*blue = comp[2];
	}
	if (alpha != NULL) {
		*alpha = comp[3];
	}
}

- (void)getWhite:(nullable CGFloat *)white alpha:(nullable CGFloat *)alpha
{
	CGFloat comp[2];

	if (fn_components_in(_cgColor, 0, comp) < 2) {
		fn_raise_for("getWhite:alpha:");
	}
	if (white != NULL) {
		*white = comp[0];
	}
	if (alpha != NULL) {
		*alpha = comp[1];
	}
}

- (void)getComponents:(CGFloat *)components
{
	const CGFloat *comp;
	size_t n;
	size_t i;

	if (components == NULL || _cgColor == NULL) {
		return;
	}
	/* THE COLOUR'S OWN COMPONENTS PLUS ITS ALPHA, and NO conversion: this form is documented as "the
	 * colour's components", and a caller asking for those does not want a space change. */
	comp = CGColorGetComponents(_cgColor);
	n = CGColorSpaceGetNumberOfComponents(CGColorGetColorSpace(_cgColor));
	for (i = 0; i < n; i++) {
		components[i] = comp[i];
	}
	components[n] = CGColorGetAlpha(_cgColor);
}

- (NSColor *)colorWithAlphaComponent:(CGFloat)alpha
{
	return fn_color(CGColorCreateCopyWithAlpha(_cgColor, alpha));
}

/* A VALUE CLASS COMPARES BY VALUE, and `CGColorEqualToColor` is the comparison this library already
 * defines (space model plus components) — which is why two colours made by different constructors
 * with the same numbers ARE equal here. NSColor.h's deviation note says why that follows. */
- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSColor class]]) {
		return NO;
	}
	return CGColorEqualToColor(_cgColor, ((NSColor *)other)->_cgColor) ? YES : NO;
}

- (NSUInteger)hash
{
	const CGFloat *c;
	size_t n;
	size_t i;
	unsigned long h = 0;

	if (_cgColor == NULL) {
		return 0;
	}
	c = CGColorGetComponents(_cgColor);
	n = CGColorGetNumberOfComponents(_cgColor);
	for (i = 0; i < n && i < 8; i++) {
		h = h * 31u + (unsigned long)(c[i] * 1000.0 + 0.5);
	}
	return (NSUInteger)h;
}

/* `-copy` AND NOT `-copyWithZone:`, BECAUSE THIS TREE HAS NO ZONES: `NSZone` is deliberately
 * undeclared here and `NSCopying`'s only member is `-copy` (NSObject.h states the decision and its
 * cost — a Cocoa class implementing `-copyWithZone:` would not conform). A colour is immutable, so
 * the copy IS a retain, which is what a `NSCopying` value class should do. */
- (id)copy
{
	return [self retain];
}

- (void)dealloc
{
	CGColorRelease(_cgColor);
	[super dealloc];
}

+ (NSColor *)blackColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.0, 0.0, 0.0, 1.0);
}

+ (NSColor *)whiteColor
{
	static NSColor *s;
	return fn_singleton(&s, 1.0, 1.0, 1.0, 1.0);
}

+ (NSColor *)redColor
{
	static NSColor *s;
	return fn_singleton(&s, 1.0, 0.0, 0.0, 1.0);
}

+ (NSColor *)greenColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.0, 1.0, 0.0, 1.0);
}

+ (NSColor *)blueColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.0, 0.0, 1.0, 1.0);
}

+ (NSColor *)cyanColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.0, 1.0, 1.0, 1.0);
}

+ (NSColor *)yellowColor
{
	static NSColor *s;
	return fn_singleton(&s, 1.0, 1.0, 0.0, 1.0);
}

+ (NSColor *)magentaColor
{
	static NSColor *s;
	return fn_singleton(&s, 1.0, 0.0, 1.0, 1.0);
}

+ (NSColor *)orangeColor
{
	static NSColor *s;
	return fn_singleton(&s, 1.0, 0.5, 0.0, 1.0);
}

+ (NSColor *)purpleColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.5, 0.0, 0.5, 1.0);
}

+ (NSColor *)brownColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.6, 0.4, 0.2, 1.0);
}

+ (NSColor *)grayColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.5, 0.5, 0.5, 1.0);
}

+ (NSColor *)darkGrayColor
{
	static NSColor *s;
	return fn_singleton(&s, 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0, 1.0);
}

+ (NSColor *)lightGrayColor
{
	static NSColor *s;
	return fn_singleton(&s, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 1.0);
}

+ (NSColor *)clearColor
{
	static NSColor *s;
	return fn_singleton(&s, 0.0, 0.0, 0.0, 0.0);
}

@end
