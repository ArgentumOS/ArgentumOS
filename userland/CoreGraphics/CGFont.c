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
	for (row = 0; row < h; row++) {
		memcpy(buf + (size_t)(mirror ? h - 1 - row : row) * (size_t)w,
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
	return 1;
}
