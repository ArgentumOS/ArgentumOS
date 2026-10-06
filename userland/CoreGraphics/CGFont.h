/*
 * CGFont — a font, its metrics, and its glyphs.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE FONT OBJECT IS APPLE'S API; THE FACE IS FREETYPE'S. This is the first half of Core
 * Graphics' text surface, and it is deliberately the half that rasterises nothing: a font is
 * metrics and glyph identity, and everything a caller can ask about a font in font units is
 * answered here, from the engine's face, offline.
 *
 * WHAT IS NOT HERE YET, AND WHY THAT IS NOT AN OVERSIGHT. The rows this header leaves undeclared
 * are owed, not forgotten, and they fall into three groups with three different reasons:
 *
 *   - `CGFontCreateWithFontName` and `CGFontCreateWithPlatformFont` need a FONT REGISTRY: a name
 *     has to resolve to a file, which is a decision about where fonts live and how names map to
 *     them (this tree has `system.fonts.conf` and a staged `/System/Fonts`, and the X side uses
 *     fontconfig). That decision is not a font decision, so it is not made here by accident.
 *   - `CGFontGetTypeID` needs the CF type-identity question answered for the WHOLE library: this
 *     tree's CoreGraphics has no type registry, and inventing one in the font's header would
 *     make a font's answer the library's answer.
 *   - the tables, variations and PostScript-subset families (`CGFontCopyTableTags`,
 *     `CGFontCreateCopyWithVariations`, `CGFontCreatePostScriptSubset`, ...) are a printing and
 *     embedding surface, and the half of the system that would call them is the parked PDF one.
 *
 * THE FILE IS OPENED ONCE PER FONT AND NEVER PER SIZE — a measured rule, not a style preference.
 * The guest's second concurrent open of one file failed with FreeType error 2
 * ("Unknown_File_Format") on a byte-perfect font, because FreeType reports a failed READ as a bad
 * format; the toolkit reached that wall by keeping a face per (family, size, bold). A `CGFont`
 * therefore holds ONE face and sizes it per use, and `CGFontCreateWithDataProvider` opens no file
 * at all.
 *
 * THE BYTES STAY THE PROVIDER'S. Freetype's memory face POINTS INTO the provider's buffer, so the
 * font retains the provider and the provider's own contract — the bytes stay valid until it is
 * released — becomes the font's. Nothing is copied.
 *
 * THE STRING DOORS RETURN `NSString *` WHERE APPLE'S SAY `CFStringRef`, which is this tree's
 * established spelling for the CF-typed doors (see CGDataProvider.h's argument about
 * `CGDataProviderCreateWithCFData`). The two are toll-free in Apple's API and one type here.
 */
#ifndef CORE_GRAPHICS_CGFONT_H
#define CORE_GRAPHICS_CGFONT_H

#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGDataProvider.h>

#include <stdbool.h>
#include <stddef.h>

/* THE OPAQUE STRING SPELLING, COPIED FROM CGColorSpace.h WHERE IT IS ALREADY USED: a C caller
 * needs the TYPE to write the name, not the implementation, and this keeps `CGFont.h` includable
 * from a translation unit with no Objective-C runtime in it. */
#ifdef __OBJC__
@class NSString;
#else
typedef struct objc_object NSString;
#endif

typedef unsigned short CGFontIndex;
typedef CGFontIndex CGGlyph;
typedef struct CGFont *CGFontRef;

/* APPLE'S VALUES, AND THE LAST TWO ARE THE REASON THIS ENUM EXISTS: `kCGFontIndexMax` is the
 * greatest VALID glyph index and `kCGFontIndexInvalid` is the value a lookup returns when it
 * finds nothing — a distinction that would be lost if a caller used 0 for "not found", since 0 is
 * `.notdef` and is a perfectly good answer. */
enum {
	kCGFontIndexMax = ((1 << 16) - 2),
	kCGFontIndexInvalid = ((1 << 16) - 1),
	kCGGlyphMax = kCGFontIndexMax
};

/* A FONT FROM THE BYTES A PROVIDER HOLDS. NULL for a NULL provider, or for bytes FreeType cannot
 * open as a font: this door does NOT guess a format or fall back to a system font, because a font
 * that is not the one the caller asked for is a different font. */
CGFontRef CGFontCreateWithDataProvider(CGDataProviderRef provider);

CGFontRef CGFontRetain(CGFontRef font);
void CGFontRelease(CGFontRef font);

/* THE FACE'S OWN NUMBERS, IN FONT UNITS — the units `CGFontGetUnitsPerEm` reports. A font unit is
 * what a metric is expressed in before any size is chosen, which is why nothing here takes a
 * size and why nothing here rasterises. */
size_t CGFontGetNumberOfGlyphs(CGFontRef font);
int CGFontGetUnitsPerEm(CGFontRef font);
int CGFontGetAscent(CGFontRef font);
int CGFontGetDescent(CGFontRef font);
int CGFontGetLeading(CGFontRef font);
int CGFontGetCapHeight(CGFontRef font);
int CGFontGetXHeight(CGFontRef font);
CGFloat CGFontGetStemV(CGFontRef font);
CGFloat CGFontGetItalicAngle(CGFontRef font);
CGRect CGFontGetFontBBox(CGFontRef font);

/* THE TWO ARRAY FORMS, and they answer FALSE rather than partially: a caller that asked for
 * twenty advances and got nineteen cannot tell which one is missing, so a font that cannot
 * measure every glyph it was asked about says so and writes nothing it cannot stand behind. */
bool CGFontGetGlyphAdvances(CGFontRef font, const CGGlyph glyphs[], size_t count, int advances[]);
bool CGFontGetGlyphBBoxes(CGFontRef font, const CGGlyph glyphs[], size_t count, CGRect bboxes[]);

/* BY NAME, IN BOTH DIRECTIONS. A font without glyph names answers `kCGFontIndexInvalid` and NULL
 * — that is a font fact, not a failure. */
CGGlyph CGFontGetGlyphWithGlyphName(CGFontRef font, NSString *name);
NSString *CGFontCopyGlyphNameForGlyph(CGFontRef font, CGGlyph glyph);

/* BOTH RETURN +1, which is what `Copy` means; NULL when the face carries no such name. */
NSString *CGFontCopyPostScriptName(CGFontRef font);
NSString *CGFontCopyFullName(CGFontRef font);

#endif /* CORE_GRAPHICS_CGFONT_H */
