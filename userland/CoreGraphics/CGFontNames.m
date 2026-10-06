/*
 * CGFontNames — the three name doors, where a name has to become a Foundation string.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE SPLIT IS THE COLOUR SPACE'S, FOR THE SAME REASON: `CGFont.c` is C and does not know what an
 * `NSString` is (it hands out C strings, see CGFont_internal.h), and the doors whose Apple
 * signatures return a string are answered here. Whoever changes one half must read the other —
 * that is the cost of not making the whole library Objective-C, and it was paid deliberately.
 *
 * EVERY STRING IS CONVERTED IN ONE DIRECTION ONLY: FreeType hands out UTF-8 or the name table's
 * UTF-16BE (already converted by the C half), and this file's only job is `initWithUTF8String:`
 * with `+1` ownership, because Apple's Copy doors return a retained string. A name that is not
 * there is NULL, not an empty string: the difference between "no such name" and "a name of zero
 * length" is the difference between a caller testing for nil and a caller testing for length.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGFont_internal.h>

/* ONE BUFFER SIZE FOR EVERY NAME, and it is generous on purpose: a glyph name in a real font is
 * a handful of bytes, a full name can be long, and the C half REFUSES rather than truncates --
 * it answers 0 when the name does not fit, so a too-small buffer is a missing name rather than a
 * wrong one. 1024 is over a full name by an order of magnitude. */
#define FN_FONT_NAME_MAX 1024

/* NO CLASS HERE, AND THAT IS THE POINT: the four doors below are C functions
 * over Foundation VALUES, so this file is Objective-C because of the two string
 * conversions and for no other reason. */

NSString *CGFontCopyPostScriptName(CGFontRef font)
{
	const char *name = cg_font_postscript_name(font);

	if (name == NULL) {
		return NULL;
	}
	return [[NSString alloc] initWithUTF8String:name];
}

NSString *CGFontCopyFullName(CGFontRef font)
{
	char buf[FN_FONT_NAME_MAX];

	if (cg_font_full_name(font, buf, sizeof(buf)) <= 0) {
		return NULL;
	}
	return [[NSString alloc] initWithUTF8String:buf];
}

NSString *CGFontCopyGlyphNameForGlyph(CGFontRef font, CGGlyph glyph)
{
	char buf[FN_FONT_NAME_MAX];

	if (cg_font_glyph_name(font, glyph, buf, sizeof(buf)) <= 0) {
		return NULL;
	}
	return [[NSString alloc] initWithUTF8String:buf];
}

CGGlyph CGFontGetGlyphWithGlyphName(CGFontRef font, NSString *name)
{
	if (name == nil) {
		return (CGGlyph)kCGFontIndexInvalid;
	}
	return cg_font_glyph_with_name(font, [name UTF8String]);
}
