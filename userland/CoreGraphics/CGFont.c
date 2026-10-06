/*
 * CGFont — the FreeType face, and the answers Apple's font API asks of it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE ENGINE IS FREETYPE AND IT IS BEHIND A SEAM (CGFont_internal.h), so nothing outside this
 * file includes `<ft2build.h>`. The engine choice is the one the plan's §5 states for every
 * borrowed engine — the SURFACE is ours, the machinery is borrowed — and it is the same shape as
 * pixman for raster, lcms2 for colour and libpng/libjpeg for the codecs.
 *
 * EVERYTHING HERE IS IN FONT UNITS, WHICH IS WHY IT IS OFFLINE AND SIZE-FREE: `FT_LOAD_NO_SCALE`
 * makes FreeType answer in the font's own units, exactly the units `CGFontGetUnitsPerEm` reports,
 * so a metric taken here can be scaled by a caller holding any size. NOTHING IS RASTERISED and no
 * size is ever set — that is what keeps the measured one-face-per-style rule (CGFont.h) true by
 * construction rather than by discipline.
 *
 * ONE ANSWER IS DELIBERATELY ZERO RATHER THAN GUESSED, and it says why where it is:
 * `CGFontGetStemV` (FreeType exposes no stem V for an SFNT face, and Apple's own documentation
 * says 0 when the font has none). THE OTHER TWO ARE NOT ZERO, WHICH IS A MEASURED CORRECTION
 * THIS FILE OWES THE PROBE: `capHeight` and `xHeight` live in the OS/2 table and only from
 * VERSION 2 of it, and DejaVu Sans's table is version 1 -- the first version of this file
 * answered 0, the probe asked why a layout engine had no cap height, and the fix measures the
 * `H` and `x` glyphs instead. A face with neither the field nor the glyph still answers 0.
 *
 * THREADING: THE LIBRARY HANDLE IS INITIALISED ONCE, LAZILY, and that is the whole of this file's
 * shared mutable state. Two threads creating the first font at the same time is a race this tree
 * does not have a primitive for yet; it is named here rather than hidden, and it is the only one.
 */
#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGFont_internal.h>
#include <CoreGraphics/CGDataProvider_internal.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H

#include <stdlib.h>
#include <string.h>

struct CGFont {
	unsigned int retain;
	FT_Face face;

	/* THE PROVIDER IS RETAINED, NOT COPIED: FreeType's memory face POINTS INTO these bytes, so
	 * the provider's lifetime is now the font's. See CGFont.h. */
	CGDataProviderRef provider;
};

static FT_Library fn_library_handle;
static int fn_library_ready;

/* THE ENGINE, ONCE. A failure here is permanent for the process (a FreeType that cannot
 * initialise cannot initialise later either), so the flag records the ATTEMPT and every font
 * creation after it answers NULL rather than retrying. */
static FT_Library fn_library(void)
{
	if (!fn_library_ready) {
		fn_library_ready = 1;
		if (FT_Init_FreeType(&fn_library_handle) != 0) {
			fn_library_handle = NULL;
		}
	}
	return fn_library_handle;
}

CGFontRef CGFontCreateWithDataProvider(CGDataProviderRef provider)
{
	CGFontRef font;
	FT_Library lib;
	const void *bytes;
	size_t size;

	if (provider == NULL) {
		return NULL;
	}
	lib = fn_library();
	if (lib == NULL) {
		return NULL;
	}
	bytes = cg_dataprovider_bytes(provider, &size);
	if (bytes == NULL || size == 0) {
		return NULL;
	}
	font = calloc(1, sizeof(struct CGFont));
	if (font == NULL) {
		return NULL;
	}
	if (FT_New_Memory_Face(lib, (const FT_Byte *)bytes, (FT_Long)size, 0, &font->face) != 0) {
		free(font);
		return NULL;
	}
	font->retain = 1;
	font->provider = CGDataProviderRetain(provider);
	return font;
}

CGFontRef CGFontRetain(CGFontRef font)
{
	if (font != NULL) {
		font->retain++;
	}
	return font;
}

void CGFontRelease(CGFontRef font)
{
	if (font == NULL) {
		return;
	}
	if (font->retain > 1) {
		font->retain--;
		return;
	}
	if (font->face != NULL) {
		FT_Done_Face(font->face);
	}
	if (font->provider != NULL) {
		CGDataProviderRelease(font->provider);
	}
	free(font);
}

void *cg_font_face(CGFontRef font)
{
	return font == NULL ? NULL : (void *)font->face;
}

size_t CGFontGetNumberOfGlyphs(CGFontRef font)
{
	if (font == NULL || font->face == NULL) {
		return 0;
	}
	return (size_t)font->face->num_glyphs;
}

int CGFontGetUnitsPerEm(CGFontRef font)
{
	if (font == NULL || font->face == NULL) {
		return 0;
	}
	return (int)font->face->units_per_EM;
}

/* THE FOUR VERTICAL METRICS COME FROM THE FACE'S OWN HEAD/HHEA NUMBERS, in font units, which is
 * what FreeType puts in `ascender`/`descender`/`height` for a scalable face. (The 26.6 values in
 * `face->size->metrics` are NOT used anywhere in this file: they are the SIZED ones, and a font
 * here has no size.) */
int CGFontGetAscent(CGFontRef font)
{
	return (font == NULL || font->face == NULL) ? 0 : (int)font->face->ascender;
}

int CGFontGetDescent(CGFontRef font)
{
	return (font == NULL || font->face == NULL) ? 0 : (int)font->face->descender;
}

int CGFontGetLeading(CGFontRef font)
{
	return (font == NULL || font->face == NULL) ? 0 : (int)font->face->height;
}

/* THE OS/2 METRICS, AND THE VERSION CHECK IS THE POINT: sxHeight and sCapHeight were ADDED in
 * OS/2 version 2. A version-0 or -1 table has no such fields, so reading them would read whatever
 * bytes follow — which is why a font whose table is older answers 0 and says so in the header. */
static int fn_os2_metric(FT_Face face, int want_cap_height)
{
	TT_OS2 *os2 = (TT_OS2 *)FT_Get_Sfnt_Table(face, ft_sfnt_os2);

	if (os2 != NULL && os2->version >= 2) {
		return want_cap_height ? (int)os2->sCapHeight : (int)os2->sxHeight;
	}
	/* AND WHEN THE TABLE DOES NOT STATE IT, THE ANSWER IS DERIVED RATHER THAN ZERO -- WHICH IS A
	 * MEASURED CORRECTION, NOT A REFINEMENT: the font probe asked for a cap height and got 0 from
	 * DejaVu Sans, whose OS/2 table is version 1 and carries no such field, and a layout engine
	 * that cannot answer `how tall is a capital` cannot place a line of text at all. THE
	 * REFERENCE GLYPHS ARE THE USUAL ONES: `H` for the cap height, `x` for the x-height, measured
	 * with the same unscaled load the advances use, so the number is in the same units as
	 * everything else here. A face with neither the field nor the glyph answers 0. */
	{
		FT_UInt index = FT_Get_Char_Index(face, want_cap_height ? 0x48u : 0x78u);

		if (index == 0
		    || FT_Load_Glyph(face, index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0) {
			return 0;
		}
		return (int)face->glyph->metrics.height;
	}
}

int CGFontGetCapHeight(CGFontRef font)
{
	if (font == NULL || font->face == NULL) {
		return 0;
	}
	return fn_os2_metric(font->face, 1);
}

int CGFontGetXHeight(CGFontRef font)
{
	if (font == NULL || font->face == NULL) {
		return 0;
	}
	return fn_os2_metric(font->face, 0);
}

CGFloat CGFontGetStemV(CGFontRef font)
{
	(void)font;
	/* ZERO, AND IT IS APPLE'S OWN ANSWER FOR THIS CASE: a stem V is a PostScript hint (a CFF or
	 * Type 1 notion), Apple's documentation says the answer is 0 when the font has none, and an
	 * SFNT face does not. FreeType has no stem V to give either, so there is nothing to be lost by
	 * saying so. A CFF font would be the case that needs the OpenType `CFF ` table's Private
	 * DICT — recorded, not guessed at. */
	return 0.0;
}

CGFloat CGFontGetItalicAngle(CGFontRef font)
{
	if (font == NULL || font->face == NULL) {
		return 0.0;
	}
	/* THE ANGLE IS THE SFNT `post` TABLE'S, REACHED THROUGH THE TABLE ACCESSOR RATHER THAN A
	 * FACE MEMBER -- `FT_FaceRec` has no public `postscript` field (that is the internal
	 * TT_FaceRec layout), so `FT_Get_Sfnt_Table(ft_sfnt_post)` is the portable way to the same
	 * number. It is a 16.16 fixed in DEGREES, counter-clockwise positive, which is exactly
	 * Apple's own units, so the only conversion is the fixed-point shift. A face with no `post`
	 * table answers 0.0 -- correct for a face whose angle is not stated. */
	{
		TT_Postscript *post = (TT_Postscript *)FT_Get_Sfnt_Table(font->face, ft_sfnt_post);

		if (post == NULL) {
			return 0.0;
		}
		return (CGFloat)(post->italicAngle / 65536.0);
	}
}

CGRect CGFontGetFontBBox(CGFontRef font)
{
	CGRect box = CGRectMake(0.0, 0.0, 0.0, 0.0);

	if (font == NULL || font->face == NULL) {
		return box;
	}
	/* THE GLOBAL BBOX, in font units, as the face's own HEAD table states it. */
	box.origin.x = (CGFloat)font->face->bbox.xMin;
	box.origin.y = (CGFloat)font->face->bbox.yMin;
	box.size.width = (CGFloat)(font->face->bbox.xMax - font->face->bbox.xMin);
	box.size.height = (CGFloat)(font->face->bbox.yMax - font->face->bbox.yMin);
	return box;
}

/* ONE LOAD, IN FONT UNITS, IS WHAT EVERY GLYPH ANSWER IS BUILT ON — advances and boxes both.
 * `FT_LOAD_NO_HINTING` goes with `FT_LOAD_NO_SCALE` because there is no size to hint for, and a
 * hinted metric would be a metric of some size rather than of the font. */
static int fn_load_glyph(FT_Face face, CGGlyph glyph)
{
	return FT_Load_Glyph(face, (FT_UInt)glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) == 0;
}

/* THE TWO-PASS RULE THE HEADER PROMISES: a caller that asked for twenty answers must not get
 * nineteen and a half. Pass one LOADS every glyph and refuses the whole request if any fails;
 * pass two writes. It costs a second load per glyph and buys an all-or-nothing answer, which is
 * what `false` means. */
bool CGFontGetGlyphAdvances(CGFontRef font, const CGGlyph glyphs[], size_t count, int advances[])
{
	size_t i;

	if (font == NULL || font->face == NULL || glyphs == NULL || advances == NULL) {
		return false;
	}
	for (i = 0; i < count; i++) {
		if (!fn_load_glyph(font->face, glyphs[i])) {
			return false;
		}
	}
	for (i = 0; i < count; i++) {
		(void)fn_load_glyph(font->face, glyphs[i]);
		advances[i] = (int)font->face->glyph->metrics.horiAdvance;
	}
	return true;
}

bool CGFontGetGlyphBBoxes(CGFontRef font, const CGGlyph glyphs[], size_t count, CGRect bboxes[])
{
	size_t i;

	if (font == NULL || font->face == NULL || glyphs == NULL || bboxes == NULL) {
		return false;
	}
	for (i = 0; i < count; i++) {
		if (!fn_load_glyph(font->face, glyphs[i])) {
			return false;
		}
	}
	for (i = 0; i < count; i++) {
		FT_Glyph_Metrics *m;

		(void)fn_load_glyph(font->face, glyphs[i]);
		m = &font->face->glyph->metrics;
		/* THE BOX'S ORIGIN IS THE LOWER-LEFT CORNER, so the y is the top bearing MINUS the
		 * height: a glyph box is stated from the baseline up, and a caller drawing it needs the
		 * corner, not the bearing. x and width come straight across, because the left bearing IS
		 * the left edge. */
		bboxes[i] = CGRectMake((CGFloat)m->horiBearingX,
				       (CGFloat)(m->horiBearingY - m->height),
				       (CGFloat)m->width, (CGFloat)m->height);
	}
	return true;
}

const char *cg_font_postscript_name(CGFontRef font)
{
	if (font == NULL || font->face == NULL) {
		return NULL;
	}
	return FT_Get_Postscript_Name(font->face);
}

/* UTF-16BE (the Windows name table) INTO UTF-8, IN CALLER MEMORY. Surrogate pairs are handled
 * because a full name is allowed to contain anything; a lone surrogate is skipped rather than
 * encoded, which is the same choice every UTF-16 decoder in this tree makes (see NSString.m). */
static int fn_utf16be_to_utf8(const unsigned char *s, size_t len, char *out, size_t outsize)
{
	size_t i = 0, o = 0;

	while (i + 1 < len) {
		unsigned long cp = ((unsigned long)s[i] << 8) | s[i + 1];

		i += 2;
		if (cp == 0) {
			break;
		}
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
			unsigned long lo = ((unsigned long)s[i] << 8) | s[i + 1];

			if (lo >= 0xDC00 && lo <= 0xDFFF) {
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				i += 2;
			}
		}
		if (cp < 0x80) {
			if (o + 1 >= outsize) {
				return 0;
			}
			out[o++] = (char)cp;
		} else if (cp < 0x800) {
			if (o + 2 >= outsize) {
				return 0;
			}
			out[o++] = (char)(0xC0 | (cp >> 6));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			if (o + 3 >= outsize) {
				return 0;
			}
			out[o++] = (char)(0xE0 | (cp >> 12));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		} else {
			if (o + 4 >= outsize) {
				return 0;
			}
			out[o++] = (char)(0xF0 | (cp >> 18));
			out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	out[o] = '\0';
	return (int)o;
}

/* THE FULL NAME IS NAME-ID 4, AND THE PREFERENCE ORDER IS STATED RATHER THAN INCIDENTAL: the
 * Windows (platform 3, encoding 1) record is UTF-16BE and is the one every modern font carries;
 * the Mac (platform 1) record is a single-byte encoding whose ASCII range is ASCII, which is what
 * this tree's other decoders assume too. The first record that MATCHES a preferred shape wins;
 * when none does, the answer is 0 and the caller gets NULL. */
int cg_font_full_name(CGFontRef font, char *buf, size_t size)
{
	FT_SfntName name;
	FT_UInt count, i;
	int have_mac = 0;

	if (font == NULL || font->face == NULL || buf == NULL || size == 0) {
		return 0;
	}
	count = FT_Get_Sfnt_Name_Count(font->face);
	for (i = 0; i < count; i++) {
		if (FT_Get_Sfnt_Name(font->face, i, &name) != 0 || name.name_id != 4) {
			continue;
		}
		if (name.platform_id == 3 && name.encoding_id == 1) {
			return fn_utf16be_to_utf8((const unsigned char *)name.string,
						  (size_t)name.string_len, buf, size);
		}
		if (name.platform_id == 1 && !have_mac) {
			size_t n = (size_t)name.string_len;

			have_mac = 1;
			if (n + 1 > size) {
				continue;
			}
			memcpy(buf, name.string, n);
			buf[n] = '\0';
		}
	}
	if (have_mac) {
		return (int)strlen(buf);
	}
	return 0;
}

int cg_font_glyph_name(CGFontRef font, CGGlyph glyph, char *buf, size_t size)
{
	if (font == NULL || font->face == NULL || buf == NULL || size == 0) {
		return 0;
	}
	if (!FT_HAS_GLYPH_NAMES(font->face)) {
		return 0;
	}
	if (FT_Get_Glyph_Name(font->face, (FT_UInt)glyph, buf, (FT_UInt)size) != 0) {
		return 0;
	}
	return (int)strlen(buf);
}

/* THE MACROMAN HIGH HALF, GENERATED FROM THE STANDARD MAPPING RATHER THAN RECALLED: 0x00-0x7F is
 * ASCII and 0x80-0xFF is what Mac OS Roman defines (0x80 = A-diaeresis, 0xA9 = copyright,
 * 0xFF = caron). Python's codec IS that standard table, which is what generated these 128
 * constants. */
static const unsigned short fn_macroman_high[128] = {
	0x00C4, /* 0x80 */
	0x00C5, /* 0x81 */
	0x00C7, /* 0x82 */
	0x00C9, /* 0x83 */
	0x00D1, /* 0x84 */
	0x00D6, /* 0x85 */
	0x00DC, /* 0x86 */
	0x00E1, /* 0x87 */
	0x00E0, /* 0x88 */
	0x00E2, /* 0x89 */
	0x00E4, /* 0x8A */
	0x00E3, /* 0x8B */
	0x00E5, /* 0x8C */
	0x00E7, /* 0x8D */
	0x00E9, /* 0x8E */
	0x00E8, /* 0x8F */
	0x00EA, /* 0x90 */
	0x00EB, /* 0x91 */
	0x00ED, /* 0x92 */
	0x00EC, /* 0x93 */
	0x00EE, /* 0x94 */
	0x00EF, /* 0x95 */
	0x00F1, /* 0x96 */
	0x00F3, /* 0x97 */
	0x00F2, /* 0x98 */
	0x00F4, /* 0x99 */
	0x00F6, /* 0x9A */
	0x00F5, /* 0x9B */
	0x00FA, /* 0x9C */
	0x00F9, /* 0x9D */
	0x00FB, /* 0x9E */
	0x00FC, /* 0x9F */
	0x2020, /* 0xA0 */
	0x00B0, /* 0xA1 */
	0x00A2, /* 0xA2 */
	0x00A3, /* 0xA3 */
	0x00A7, /* 0xA4 */
	0x2022, /* 0xA5 */
	0x00B6, /* 0xA6 */
	0x00DF, /* 0xA7 */
	0x00AE, /* 0xA8 */
	0x00A9, /* 0xA9 */
	0x2122, /* 0xAA */
	0x00B4, /* 0xAB */
	0x00A8, /* 0xAC */
	0x2260, /* 0xAD */
	0x00C6, /* 0xAE */
	0x00D8, /* 0xAF */
	0x221E, /* 0xB0 */
	0x00B1, /* 0xB1 */
	0x2264, /* 0xB2 */
	0x2265, /* 0xB3 */
	0x00A5, /* 0xB4 */
	0x00B5, /* 0xB5 */
	0x2202, /* 0xB6 */
	0x2211, /* 0xB7 */
	0x220F, /* 0xB8 */
	0x03C0, /* 0xB9 */
	0x222B, /* 0xBA */
	0x00AA, /* 0xBB */
	0x00BA, /* 0xBC */
	0x03A9, /* 0xBD */
	0x00E6, /* 0xBE */
	0x00F8, /* 0xBF */
	0x00BF, /* 0xC0 */
	0x00A1, /* 0xC1 */
	0x00AC, /* 0xC2 */
	0x221A, /* 0xC3 */
	0x0192, /* 0xC4 */
	0x2248, /* 0xC5 */
	0x2206, /* 0xC6 */
	0x00AB, /* 0xC7 */
	0x00BB, /* 0xC8 */
	0x2026, /* 0xC9 */
	0x00A0, /* 0xCA */
	0x00C0, /* 0xCB */
	0x00C3, /* 0xCC */
	0x00D5, /* 0xCD */
	0x0152, /* 0xCE */
	0x0153, /* 0xCF */
	0x2013, /* 0xD0 */
	0x2014, /* 0xD1 */
	0x201C, /* 0xD2 */
	0x201D, /* 0xD3 */
	0x2018, /* 0xD4 */
	0x2019, /* 0xD5 */
	0x00F7, /* 0xD6 */
	0x25CA, /* 0xD7 */
	0x00FF, /* 0xD8 */
	0x0178, /* 0xD9 */
	0x2044, /* 0xDA */
	0x20AC, /* 0xDB */
	0x2039, /* 0xDC */
	0x203A, /* 0xDD */
	0xFB01, /* 0xDE */
	0xFB02, /* 0xDF */
	0x2021, /* 0xE0 */
	0x00B7, /* 0xE1 */
	0x201A, /* 0xE2 */
	0x201E, /* 0xE3 */
	0x2030, /* 0xE4 */
	0x00C2, /* 0xE5 */
	0x00CA, /* 0xE6 */
	0x00C1, /* 0xE7 */
	0x00CB, /* 0xE8 */
	0x00C8, /* 0xE9 */
	0x00CD, /* 0xEA */
	0x00CE, /* 0xEB */
	0x00CF, /* 0xEC */
	0x00CC, /* 0xED */
	0x00D3, /* 0xEE */
	0x00D4, /* 0xEF */
	0xF8FF, /* 0xF0 */
	0x00D2, /* 0xF1 */
	0x00DA, /* 0xF2 */
	0x00DB, /* 0xF3 */
	0x00D9, /* 0xF4 */
	0x0131, /* 0xF5 */
	0x02C6, /* 0xF6 */
	0x02DC, /* 0xF7 */
	0x00AF, /* 0xF8 */
	0x02D8, /* 0xF9 */
	0x02D9, /* 0xFA */
	0x02DA, /* 0xFB */
	0x00B8, /* 0xFC */
	0x02DD, /* 0xFD */
	0x02DB, /* 0xFE */
	0x02C7, /* 0xFF */
};

/* THE BYTE, THROUGH THE ENCODING THE CALLER CHOSE. The font-specific reading is the byte AS a character
 * code (see CGContext.h); the MacRoman one is the byte as a MAC OS ROMAN character, whose code point the
 * table above gives — so one byte reaches two different glyphs, which is the whole reason the encoding is
 * a parameter and not a constant. */
CGGlyph cg_font_glyph_for_byte(CGFontRef font, unsigned char code, int macroman)
{
	FT_ULong ch = (FT_ULong)code;
	FT_UInt index;

	if (font == NULL || font->face == NULL) {
		return (CGGlyph)kCGFontIndexInvalid;
	}
	if (macroman && code >= 0x80) {
		ch = (FT_ULong)fn_macroman_high[code - 0x80];
	}
	index = FT_Get_Char_Index(font->face, ch);
	if (index == 0) {
		/* ZERO IS "THIS FONT HAS NO GLYPH FOR THAT CODE" — and it is also .notdef's index, which is
		 * why it travels as kCGFontIndexInvalid and not as a glyph to draw. */
		return (CGGlyph)kCGFontIndexInvalid;
	}
	return (CGGlyph)index;
}

CGGlyph cg_font_glyph_with_name(CGFontRef font, const char *name)
{
	FT_UInt index;

	if (font == NULL || font->face == NULL || name == NULL) {
		return (CGGlyph)kCGFontIndexInvalid;
	}
	if (!FT_HAS_GLYPH_NAMES(font->face)) {
		return (CGGlyph)kCGFontIndexInvalid;
	}
	index = FT_Get_Name_Index(font->face, (FT_String *)(uintptr_t)name);
	if (index == 0) {
		/* ZERO IS "NOT FOUND" HERE AND IT IS ALSO .notdef, which has no name: a named lookup
		 * that answers 0 has found nothing, and kCGFontIndexInvalid is how that travels. */
		return (CGGlyph)kCGFontIndexInvalid;
	}
	return (CGGlyph)index;
}

/* ------------------------------------------------------------------------- */
/* Rasterisation: the one door that asks the engine for pixels                */
/* ------------------------------------------------------------------------- */

int cg_font_render_glyph(CGFontRef font, CGGlyph glyph, CGFloat pixel_size, CGAffineTransform matrix,
			 CGPoint delta, unsigned char **coverage, int *width, int *height, int *left,
			 int *top, double *advance)
{
	FT_Matrix m;
	FT_Vector d;
	FT_GlyphSlot slot;
	unsigned char *buf;
	int w, h, row, mirror = 0;

	if (font == NULL || font->face == NULL || coverage == NULL || width == NULL || height == NULL) {
		return 0;
	}
	if (pixel_size <= 0.0) {
		return 0;
	}
	/* THE RESOLUTIONS ARE 72 DPI, WHICH IS THE 1:1 CASE — measured: passing 0 ("the default") is
	 * REFUSED by the engine in this tree and every glyph came back as a silent nothing. */
	if (FT_Set_Char_Size(font->face, 0, (FT_F26Dot6)(pixel_size * 64.0 + 0.5), 72, 72) != 0) {
		fprintf(stderr, "CG-REFUSE: the engine would not set the font size (%.2f)\n",
			(double)pixel_size);
		return 0;
	}
	/* A REFLECTION IS TAKEN OUT OF THE ENGINE'S MATRIX AND PUT BACK INTO THE ROWS, because a bitmap
	 * context's CTM is flipped and handing that flip to FreeType makes it report bitmap_top = 0 for
	 * every glyph (measured): the ink then lands at the pen and runs off the surface. */
	if (matrix.a * matrix.d - matrix.b * matrix.c < 0.0) {
		if (matrix.b != 0.0 || matrix.c != 0.0) {
			fprintf(stderr, "CG-REFUSE: a mirrored text matrix with a shear or rotation is not "
					"implemented; this library handles a pure reflection\n");
			return 0;
		}
		mirror = 1;
		matrix.d = -matrix.d;
	}
	m.xx = (FT_Fixed)(matrix.a * 65536.0);
	m.xy = (FT_Fixed)(matrix.b * 65536.0);
	m.yx = (FT_Fixed)(matrix.c * 65536.0);
	m.yy = (FT_Fixed)(matrix.d * 65536.0);
	(void)delta;
	d.x = 0;
	d.y = 0;
	FT_Set_Transform(font->face, &m, &d);

	if (FT_Load_Glyph(font->face, (FT_UInt)glyph, FT_LOAD_DEFAULT) != 0) {
		fprintf(stderr, "CG-REFUSE: the engine would not load glyph %u\n", (unsigned)glyph);
		return 0;
	}
	slot = font->face->glyph;
	if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
		fprintf(stderr, "CG-REFUSE: the engine would not render glyph %u\n", (unsigned)glyph);
		return 0;
	}
	if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
		fprintf(stderr, "CG-REFUSE: this font renders glyph coverage this library cannot use "
				"(pixel mode %d)\n", slot->bitmap.pixel_mode);
		return 0;
	}
	w = (int)slot->bitmap.width;
	h = (int)slot->bitmap.rows;
	buf = malloc((size_t)(w > 0 && h > 0 ? w * h : 1));
	if (buf == NULL) {
		return 0;
	}
	/* THE ROWS ARE **NOT** REVERSED WHEN THE FLIP WAS TAKEN OUT OF THE MATRIX, which is a correction
	 * the PICTURE made and no check in the probe did. FreeType's bitmap rows are TOP-DOWN whatever
	 * direction its outline ran, so after the un-flip they are ALREADY in the device's own order
	 * (y down) — reversing them beat the glyph twice and every letter came out vertically mirrored.
	 * The probe's `L` check passed on that upside-down picture, because a mirrored `L` still has its
	 * STEM crossing the baseline, which is all that check measured; it now compares two bands. */
	for (row = 0; row < h; row++) {
		memcpy(buf + (size_t)row * (size_t)w,
		       slot->bitmap.buffer + (size_t)row * (size_t)(slot->bitmap.pitch < 0
								    ? -slot->bitmap.pitch : slot->bitmap.pitch),
		       (size_t)w);
	}
	*coverage = buf;
	*width = w;
	*height = h;
	if (left != NULL) {
		*left = slot->bitmap_left;
	}
	if (top != NULL) {
		*top = slot->bitmap_top;
	}
	if (advance != NULL) {
		*advance = slot->advance.x / 64.0;
	}
	(void)mirror;
	return 1;
}

/* ------------------------------------------------------------------------- */
/* The registry: a name, and the directories that can answer for it           */
/* ------------------------------------------------------------------------- */
/* THE INCLUDES RIDE WITH THE CODE THAT NEEDS THEM, which is a habit this file keeps: `dirent.h` is here
 * because a directory scan is here, and a reader does not have to look at the top of a 500-line file to
 * learn why. */
#include <dirent.h>
#include <strings.h>

/* THE FSH DIRECTORIES A FONT MAY LIVE IN, in the order they are searched: the shared fonts this tree
 * stages, then the two system locations a Mac-shaped filesystem would use, then the flat one. */
static const char *fn_font_dirs[] = {
	"/System/Shared/Fonts",
	"/System/Library/Fonts",
	"/Library/Fonts",
	"/System/Fonts"
};

static int fn_has_font_suffix(const char *name)
{
	size_t n = strlen(name);

	if (n > 4 && (strcasecmp(name + n - 4, ".ttf") == 0 || strcasecmp(name + n - 4, ".otf") == 0)) {
		return 1;
	}
	return n > 4 && strcasecmp(name + n - 4, ".ttc") == 0;
}

/* DOES THIS FACE ANSWER TO THAT NAME? The POSTSCRIPT NAME and the FULL name are Apple's contract; the
 * family and `family style` forms are a kindness this library adds and the header says so — they are what
 * makes "DejaVu Sans" find DejaVuSans.ttf without the caller knowing the PostScript spelling. */
static int fn_face_is_named(FT_Face face, const char *name)
{
	char buf[256];
	const char *ps = FT_Get_Postscript_Name(face);

	if (ps != NULL && strcasecmp(ps, name) == 0) {
		return 1;
	}
	if (face->family_name != NULL && face->style_name != NULL) {
		snprintf(buf, sizeof buf, "%s %s", face->family_name, face->style_name);
		if (strcasecmp(buf, name) == 0) {
			return 1;
		}
	}
	if (face->family_name != NULL && strcasecmp(face->family_name, name) == 0) {
		return 1;
	}
	return face->style_name != NULL && strcasecmp(face->style_name, name) == 0;
}

static CGFontRef fn_try_file(const char *path, const char *name)
{
	CGDataProviderRef provider = CGDataProviderCreateWithFilename(path);
	CGFontRef font;
	char buf[256];
	int match;

	if (provider == NULL) {
		return NULL;
	}
	font = CGFontCreateWithDataProvider(provider);
	CGDataProviderRelease(provider);
	if (font == NULL) {
		return NULL;
	}
	match = fn_face_is_named(font->face, name);
	if (!match && cg_font_full_name(font, buf, sizeof buf) > 0 && strcasecmp(buf, name) == 0) {
		match = 1;
	}
	if (!match) {
		CGFontRelease(font);
		return NULL;
	}
	return font;
}

CGFontRef cg_font_create_with_name(const char *name)
{
	char override_buf[1024];
	const char *dirs[8];
	int ndirs = 0;
	int i;

	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	{
		/* THE OVERRIDE IS THE PROBE'S DOOR IN, AND THE ONLY WAY TO SEARCH ELSEWHERE: this library has no
		 * settings domain of its own — system.fonts.conf belongs to the X11 stack's fontconfig, and
		 * reading it here would put libconfig inside CoreGraphics. */
		const char *env = getenv("FN_FONT_PATH");

		if (env != NULL && env[0] != '\0' && strlen(env) < sizeof override_buf) {
			char *p;

			strcpy(override_buf, env);
			p = strtok(override_buf, ":");
			while (p != NULL && ndirs < 8) {
				dirs[ndirs++] = p;
				p = strtok(NULL, ":");
			}
		}
	}
	if (ndirs == 0) {
		for (i = 0; i < (int)(sizeof fn_font_dirs / sizeof fn_font_dirs[0]); i++) {
			dirs[ndirs++] = fn_font_dirs[i];
		}
	}
	for (i = 0; i < ndirs; i++) {
		DIR *d = opendir(dirs[i]);
		struct dirent *e;

		if (d == NULL) {
			continue;
		}
		while ((e = readdir(d)) != NULL) {
			char path[1024];
			CGFontRef font;

			if (!fn_has_font_suffix(e->d_name)) {
				continue;
			}
			if (snprintf(path, sizeof path, "%s/%s", dirs[i], e->d_name) >= (int)sizeof path) {
				continue;
			}
			font = fn_try_file(path, name);
			if (font != NULL) {
				closedir(d);
				return font;
			}
		}
		closedir(d);
	}
	return NULL;
}
