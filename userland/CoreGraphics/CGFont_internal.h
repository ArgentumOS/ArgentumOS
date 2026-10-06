/*
 * CGFont_internal — the seam between a font and the engine that owns it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, and named the way CGColorSpace_internal.h is
 * for the same reason: a `CGFont` is FreeType's face plus the bookkeeping Apple's API needs, and
 * whoever draws a glyph should be able to ask for the face WITHOUT INCLUDING `<ft2build.h>`.
 *
 * THE HANDLE IS `void *` AND NOT `FT_Face`, DELIBERATELY — the same rule the colour space's
 * `cmsHPROFILE` accessor follows: the value passes through both ways without a cast at the seam,
 * and no header that includes this one inherits FreeType's include layout (the engine's headers
 * live under an `freetype2/` subdirectory of the X prefix, which is exactly the kind of detail a
 * public header may not force on its includers).
 *
 * THE STRING HELPERS ARE C STRINGS, NOT `NSString *`, for the same reason the colour space names
 * are: the C half of this library does not know what a Foundation string is. The Objective-C half
 * (CGFontNames.m) turns them into `NSString *` at the three doors that return one.
 */
#ifndef CORE_GRAPHICS_CGFONT_INTERNAL_H
#define CORE_GRAPHICS_CGFONT_INTERNAL_H

#include <CoreGraphics/CGFont.h>
#include <CoreGraphics/CGAffineTransform.h>
#include <stddef.h>

/* THE ENGINE'S FACE, OR NULL. NULL is not a gap: it is what a font with no engine representation
 * answers, and today the only `CGFont` that exists at all is one FreeType opened. A caller that
 * gets NULL has nothing to draw and must skip. */
void *cg_font_face(CGFontRef font);

/* THE POSTSCRIPT NAME, OR NULL — FreeType's own string, borrowed from the face, valid while the
 * font lives. Nullable because a face is allowed not to have one. */
const char *cg_font_postscript_name(CGFontRef font);

/* THE FULL NAME INTO `buf` (UTF-8), FROM THE `name` TABLE: the answer is the number of bytes
 * written without the terminator, 0 when the face has no such name. A caller-provided buffer
 * rather than a borrowed pointer because the name table's string is UTF-16BE and has to be
 * converted, and this library does not hand out a static buffer. */
int cg_font_full_name(CGFontRef font, char *buf, size_t size);

/* THE GLYPH'S NAME INTO `buf`, same contract, 0 when the face carries no glyph names at all —
 * which is the common case for a subset or a CFF face. */
int cg_font_glyph_name(CGFontRef font, CGGlyph glyph, char *buf, size_t size);

/* AND THE OTHER WAY: the glyph index for a name, which is how a caller with a name (or the
 * PostScript encoding) reaches a glyph without a character map. `kCGFontIndexInvalid` = not found. */
CGGlyph cg_font_glyph_with_name(CGFontRef font, const char *name);

/* A BYTE TO A GLYPH THROUGH THE FONT'S OWN CHARACTER MAP, which is what the string-drawing doors
 * need and what a caller without a character map cannot do for itself. THE BYTE IS READ AS A
 * CHARACTER CODE, not as a glyph index: this library implements the font-specific encoding that way
 * and SAYS SO (see CGContext.h's note on CGTextEncoding), because the alternative — byte as glyph
 * index — gives garbage for every font whose glyph order is not ASCII-aligned, which is all of
 * them. A byte this font has no glyph for answers kCGFontIndexInvalid, which the caller SKIPS
 * rather than drawing .notdef for. */
CGGlyph cg_font_glyph_for_byte(CGFontRef font, unsigned char code, int macroman);

/* A FONT BY NAME, FROM THE CONFIGURED FONT DIRECTORIES: the door Apple calls
 * `CGFontCreateWithFontName` needs a REGISTRY — a name has to resolve to a file — and this is this
 * library's: the directories are the FSH ones below, a name matches the face's POSTSCRIPT name, its
 * FULL name or `family style` (case-insensitively, which is a kindness Apple's documentation does not
 * promise), and a name that matches nothing ANSWERS NULL rather than substituting a font the caller did
 * not ask for. `FN_FONT_PATH` overrides the directory list with a colon-separated one, which is how the
 * host probe reaches the tree's own directory. */
CGFontRef cg_font_create_with_name(const char *name);

/* ONE GLYPH RASTERISED ... see CGContext.c for how it is placed. */
/* `delta` IS THE SUBPIXEL FRACTION OF THE PEN, IN DEVICE UNITS (x right, y down — the engine's own
 * frame is handled inside). A whole-pixel caller passes (0,0) and gets the old behaviour; a caller
 * that has been asked not to subpixel-position passes (0,0) too, because rounding the pen IS that
 * setting. */
int cg_font_render_glyph(CGFontRef font, CGGlyph glyph, CGFloat pixel_size,
                         CGAffineTransform matrix, CGPoint delta, unsigned char **coverage,
                         int *width, int *height, int *left, int *top, double *advance);

#endif /* CORE_GRAPHICS_CGFONT_INTERNAL_H */
