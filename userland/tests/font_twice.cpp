/*
 * A DIAGNOSTIC THAT PINS A GUEST LIMIT: how many times can ONE file be open
 * at once?
 *
 * The toolkit keeps an FT face per (family, size, bold), so a second SIZE is
 * a second FT_New_Face on the same file while the first face still holds it
 * open. In the guest the second *distinct* size failed with FT error 2 - "not
 * a font at all" - on a file that is byte-perfect, whose nested open/read the
 * filesystem handles correctly (measured), and which renders fine on the host.
 *
 * This asks FreeType directly, with no toolkit and no fontconfig in the
 * picture. What it measured on the guest:
 *
 *   FONT2 A(12px)            new=0   <- the first face
 *   FONT2 B(13px)            new=0   <- a SECOND concurrent open works
 *   FONT2 C(13px-again)      new=2   <- the THIRD FAILS, and 2 is what it
 *                                       reports for a file it cannot read
 *   FONT2 D(12px-after-done) new=0   <- FT_Done_Face frees the slot again
 *
 * So the guest allows TWO concurrent opens of one file, not three, and
 * FreeType reports a failed READ as Unknown_File_Format - which is why a
 * perfect font looked corrupt, and why "the font is wrong" was the wrong
 * trail for so long. The toolkit already holds fontconfig's handles, so its
 * second size is the third open. The fix is to stop opening a face PER SIZE
 * (one face per family+bold, sized with FT_Set_Pixel_Sizes per use, which
 * text.cpp already does); this probe is how we learn whether that is still
 * needed once the guest's limit changes.
 *
 *   FONT2 <label> new=<ft-error> [set=<ft-error> glyphs=<n> A=<index>]
 */
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>

#define FONT "/System/Shared/Fonts/DejaVuSans.ttf"

static void
openFace(const char *what, FT_Library lib, FT_Face *out, unsigned px)
{
	FT_Error e = FT_New_Face(lib, FONT, 0, out);

	std::printf("FONT2 %s new=%d", what, (int) e);
	if (e == 0 && *out) {
		FT_Error s = FT_Set_Char_Size(*out, 0, (FT_F26Dot6) (px * 64), 0, 0);

		std::printf(" set=%d glyphs=%ld", (int) s,
			    (long) (*out)->num_glyphs);
		std::printf(" A=%u", (unsigned) FT_Get_Char_Index(*out, 'A'));
	}
	std::printf("\n");
	std::fflush(stdout);
}

int
main()
{
	FT_Library lib;
	FT_Error e = FT_Init_FreeType(&lib);
	FT_Face a = nullptr, b = nullptr, c = nullptr;

	std::printf("FONT2 init=%d\n", (int) e);
	openFace("A(12px)", lib, &a, 12);
	openFace("B(13px)", lib, &b, 13);
	openFace("C(13px-again)", lib, &c, 13);
	if (a) {
		FT_Done_Face(a);
		a = nullptr;
	}
	openFace("D(12px-after-done)", lib, &a, 12);
	std::printf("FONT2 done\n");
	std::fflush(stdout);
	return 0;
}
