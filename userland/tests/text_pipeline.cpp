// FNX text-stack acceptance (docs/design/shrike-plan.md §4): prove the
// fontconfig -> HarfBuzz -> FreeType pipeline end-to-end in the guest.
//  1. fontconfig init against the FSH config (/System/Configuration/fonts)
//     + a family match (exercises the dir list /System/Shared/Fonts).
//  2. HarfBuzz shapes an Arabic string through the matched face - lam-alef
//     ligation (GSUB) must reduce the glyph count below the cluster count.
//  3. FreeType rasterizes a shaped glyph to a real bitmap.
// Prints TEXT-PIPELINE: all checks OK when all three hold.
//
// Built by the C++ wrapper against .build/x11-prefix and staged under
// System/Shared/tests (a lint carve-out tree).
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <hb.h>
#include <hb-ft.h>

#include <cstdio>
#include <cstring>

int main()
{
	int ok = 0;

	if(!FcInit()) {
		printf("TEXT-PIPELINE: FcInit failed\n");
		return 1;
	}

	/* 1) fontconfig: match an Arabic face from the FSH font dirs */
	FcPattern *pat = FcNameParse((const FcChar8 *)"DejaVu Sans");
	if(!pat) {
		printf("TEXT-PIPELINE: FcNameParse failed\n");
		return 1;
	}
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult res;
	FcPattern *match = FcFontMatch(NULL, pat, &res);
	if(!match || res != FcResultMatch) {
		printf("TEXT-PIPELINE: FcFontMatch failed (res %d) - no Arabic font\n",
		       (int)res);
		return 1;
	}
	FcChar8 *file = NULL;
	int index = 0;
	if(FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch ||
	   FcPatternGetInteger(match, FC_INDEX, 0, &index) != FcResultMatch) {
		printf("TEXT-PIPELINE: no FC_FILE/FC_INDEX in match\n");
		return 1;
	}
	printf("TEXT-PIPELINE: matched '%s' index %d\n", (char *)file, index);

	/* 2) FreeType face from the matched file */
	FT_Library lib;
	FT_Face face;
	if(FT_Init_FreeType(&lib) || FT_New_Face(lib, (char *)file, index, &face)) {
		printf("TEXT-PIPELINE: FT_New_Face failed\n");
		return 1;
	}

	/* 3) HarfBuzz shapes Arabic - lam-alef ligatures must fire */
	hb_font_t *hbf = hb_ft_font_create_referenced(face);
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, "\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
			   -1, 0, -1); /* السلام: 6 code points, RTL */
	hb_buffer_guess_segment_properties(buf);
	printf("TEXT-PIPELINE: dir=%d script=%d lang=%s\n",
	       (int)hb_buffer_get_direction(buf),
	       (int)hb_buffer_get_script(buf),
	       hb_language_to_string(hb_buffer_get_language(buf)));
	hb_shape(hbf, buf, NULL, 0);
	unsigned int nglyphs = hb_buffer_get_length(buf);
	unsigned int nclusters = 0;
	hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, NULL);
	unsigned int i;
	for(i = 0; i < nglyphs; i++) {
		if(i == 0 || info[i].cluster != info[i - 1].cluster) {
			nclusters++;
		}
	}
	printf("TEXT-PIPELINE: shaped 6 code points -> %u glyphs"
	       " (lam-alef ligature)\n", nglyphs);
	for(i = 0; i < nglyphs && i < 10; i++) {
		printf("  glyph %u cluster %u\n", info[i].codepoint,
		       info[i].cluster);
	}
	if(nglyphs == 0 || nglyphs >= 6) {
		printf("TEXT-PIPELINE: shaping FAILED (lam-alef ligation should\n"
		       "drop 6 clusters to 5 glyphs in DejaVu Sans)\n");
		return 1;
	}

	/* 4) FreeType rasterizes the first shaped glyph */
	FT_Set_Pixel_Sizes(face, 0, 24);
	if(FT_Load_Glyph(face, info[0].codepoint, FT_LOAD_RENDER) ||
	   face->glyph->bitmap.rows == 0) {
		printf("TEXT-PIPELINE: FT_Load/Render failed\n");
		return 1;
	}
	printf("TEXT-PIPELINE: rasterized glyph %u -> %ux%u bitmap\n",
	       info[0].codepoint, face->glyph->bitmap.width,
	       face->glyph->bitmap.rows);

	hb_buffer_destroy(buf);
	hb_font_destroy(hbf);
	FT_Done_Face(face);
	FT_Done_FreeType(lib);
	FcPatternDestroy(match);
	FcPatternDestroy(pat);
	FcFini();
	printf("TEXT-PIPELINE: all checks OK\n");
	return 0;
}
