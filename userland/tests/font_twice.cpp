/*
 * HOW MANY TIMES CAN ONE FILE BE OPEN AT ONCE — and is the limit PER FILE?
 *
 * The toolkit used to keep a font face per (family, size, bold), which is a
 * FILE OPEN per size, and in the guest its second distinct size failed with
 * FT error 2, "Unknown_File_Format", on a byte-perfect font. FreeType reports
 * a failed READ as a bad format, so what looked like a corrupt font was a
 * read that did not come back. (The toolkit no longer opens a face per size -
 * one face per style, sized per use - but the limit itself is a defect, and
 * it would break any application that opens a file twice.)
 *
 * This probe answers three questions in one run, with no toolkit involved:
 *
 *   A. three concurrent RAW opens of ONE file: which step fails, and errno?
 *   B. three concurrent raw opens of THREE DIFFERENT files — a failure here
 *      would mean a GLOBAL limit rather than a per-file one
 *   C. the same shape through FreeType, which is how the toolkit met it
 *
 * Lines:  OPEN3 <label> path=<p> fd=<n> errno=<n>(<text>) read=<n> first=<hex>
 *         FONT2 <label> new=<ft-error> [glyphs=<n> A=<index>]
 */
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define FONT "/System/Shared/Fonts/DejaVuSans.ttf"
#define BOLD "/System/Shared/Fonts/DejaVuSans-Bold.ttf"
#define CONF "/System/Configuration/system.fonts.conf"

static unsigned char g_buf[8];

/* open (or use) `fd`, read eight bytes, and report both outcomes */
static int
step(const char *what, const char *path, int fd)
{
	int err = 0;
	long n = -1;

	if (fd < 0) {
		err = errno;
	} else {
		ssize_t r = read(fd, g_buf, sizeof(g_buf));

		n = (long) r;
		if (r < 0) {
			err = errno;
		}
	}
	std::printf("OPEN3 %-16s path=%s fd=%d errno=%d(%s) read=%ld "
		    "first=%02x%02x\n",
		    what, path, fd, err, err ? std::strerror(err) : "-", n,
		    n > 1 ? g_buf[0] : 0, n > 1 ? g_buf[1] : 0);
	std::fflush(stdout);
	return fd;
}

static void
ftOpen(const char *what, FT_Library lib, FT_Face *out, const char *path)
{
	FT_Error e = FT_New_Face(lib, path, 0, out);

	std::printf("FONT2 %-16s new=%d", what, (int) e);
	if (e == 0 && *out) {
		std::printf(" glyphs=%ld A=%u", (long) (*out)->num_glyphs,
			    (unsigned) FT_Get_Char_Index(*out, 'A'));
	}
	std::printf("\n");
	std::fflush(stdout);
}

int
main()
{
	/* ---- A: ONE file, three concurrent raw opens ---- */
	int a = step("same#1", FONT, open(FONT, O_RDONLY));
	int b = step("same#2", FONT, open(FONT, O_RDONLY));
	int c = step("same#3", FONT, open(FONT, O_RDONLY));

	(void) a;
	(void) b;
	(void) c;

	/* ---- B: different files, every one of them held open ---- */
	int d = step("diff#1-bold", BOLD, open(BOLD, O_RDONLY));
	int e = step("diff#2-conf", CONF, open(CONF, O_RDONLY));
	int f = step("diff#3-font", FONT, open(FONT, O_RDONLY));

	(void) d;
	(void) e;
	(void) f;

	/* ---- C: the shape the toolkit met it in ---- */
	FT_Library lib;
	FT_Error ie = FT_Init_FreeType(&lib);
	FT_Face faces[4] = { nullptr, nullptr, nullptr, nullptr };

	std::printf("FONT2 init=%d\n", (int) ie);
	ftOpen("A(12px)", lib, &faces[0], FONT);
	ftOpen("B(13px)", lib, &faces[1], FONT);
	ftOpen("C(13px-again)", lib, &faces[2], FONT);
	ftOpen("D(bold)", lib, &faces[3], BOLD);

	/* ---- D: musl stdio — three FILE* on ONE file, all held open ----
	 * FreeType's own stream is a FILE* (ft_ansi_stream_io), so if THIS is
	 * what caps at two, then the fault is below FreeType, in the C library. */
	{
		FILE *s[3];

		s[0] = std::fopen(FONT, "rb");
		s[1] = std::fopen(FONT, "rb");
		s[2] = std::fopen(FONT, "rb");
		for (int k = 0; k < 3; k++) {
			size_t n = 0;
			unsigned char b[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

			if (s[k]) {
				n = std::fread(b, 1, sizeof(b), s[k]);
			}
			std::printf("STDIO stdio#%d fp=%p read=%ld first=%02x%02x\n",
				    k + 1, (void *) s[k], (long) n, b[0], b[1]);
		}
		/* AND THE PATTERN A FONT LOADER USES: seek, read eight bytes, seek
		 * again, read again - across the first blocks, the middle and the
		 * end. FreeType does exactly this to walk a font's tables, so if
		 * interleaved seeking is what breaks, this is the failing shape. */
		for (int k = 0; k < 3; k++) {
			static const long offs[3] = { 0, 300000, 700000 };

			for (int o = 0; o < 3; o++) {
				unsigned char b[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
				size_t n = 0;

				if (s[k] && std::fseek(s[k], offs[o], SEEK_SET) == 0) {
					n = std::fread(b, 1, sizeof(b), s[k]);
				}
				std::printf("STDIO stdio#%d@%ld read=%ld first=%02x%02x\n",
					    k + 1, offs[o], (long) n, b[0], b[1]);
			}
		}
		std::fflush(stdout);
	}

	/* ---- E: FreeType from MEMORY — no stream, no FILE* ----
	 * If three in-memory faces work where three pathname faces do not, the
	 * fault is in FreeType's file handling and FT_OPEN_MEMORY is a way round
	 * it. */
	{
		int fd = open(FONT, O_RDONLY);
		long sz = fd >= 0 ? (long) lseek(fd, 0, SEEK_END) : 0;

		if (fd >= 0 && sz > 0) {
			unsigned char *bytes = new unsigned char[sz];
			long got = 0;

			lseek(fd, 0, SEEK_SET);
			while (got < sz) {
				ssize_t r = read(fd, bytes + got,
						 (size_t) (sz - got));

				if (r <= 0) {
					break;
				}
				got += (long) r;
			}
			std::printf("MEM2 loaded=%ld of %ld\n", got, sz);
			for (int k = 0; k < 3; k++) {
				FT_Open_Args args = {};
				FT_Face mf = nullptr;

				args.flags = FT_OPEN_MEMORY;
				args.memory_base = bytes;
				args.memory_size = (FT_Long) got;
				FT_Error me = FT_Open_Face(lib, &args, 0, &mf);
				std::printf("MEM2 mem#%d new=%d glyphs=%ld\n", k + 1,
					    (int) me,
					    mf ? (long) mf->num_glyphs : -1L);
			}
		}
		close(fd);
		std::fflush(stdout);
	}

	/* ---- F: THE PATTERN FREETYPE ACTUALLY USES ----
	 * On a pathname FT seeks to the END of the file to learn its size, and
	 * seeks back, before reading anything. None of the tests above seek, so
	 * if the third open reports the wrong SIZE, that is the failure - and a
	 * different bug from every one ruled out so far. */
	for (int k = 0; k < 3; k++) {
		int fd = open(FONT, O_RDONLY);
		int err = 0;
		long end = -1, back = -1;

		if (fd < 0) {
			err = errno;
		} else {
			end = (long) lseek(fd, 0, SEEK_END);
			if (end < 0) {
				err = errno;
			}
			back = (long) lseek(fd, 0, SEEK_SET);
			close(fd);
		}
		std::printf("SEEK raw#%d fd=%d end=%ld back=%ld errno=%d(%s)\n",
			    k + 1, fd, end, back, err,
			    err ? std::strerror(err) : "-");
		std::fflush(stdout);
	}

	/* the same thing through stdio, with all three FILE* held open */
	{
		FILE *s[3] = { std::fopen(FONT, "rb"), std::fopen(FONT, "rb"),
			       std::fopen(FONT, "rb") };

		for (int k = 0; k < 3; k++) {
			long end = -1, pos = -1;
			int err = 0;

			if (s[k]) {
				if (std::fseek(s[k], 0, SEEK_END) != 0) {
					err = errno;
				}
				end = std::ftell(s[k]);
				std::fseek(s[k], 0, SEEK_SET);
				pos = std::ftell(s[k]);
			}
			std::printf("SEEK stdio#%d fp=%p end=%ld pos=%ld errno=%d(%s)\n",
				    k + 1, (void *) s[k], end, pos, err,
				    err ? std::strerror(err) : "-");
		}
		std::fflush(stdout);
	}

	/* ---- G: READS AT HIGH OFFSETS, on three concurrent opens ----
	 * Everything the tests above do is at offset 0 - one block, no indirect
	 * blocks. FreeType reads a font's tables from ALL OVER the file (this one
	 * is 759720 bytes), so what is left to test is a read that has to walk
	 * the file's extents. The lines carry the OPEN3 prefix so they show up
	 * beside the rest. */
	{
		static const long offs[3] = { 0, 300000, 700000 };
		int fds[3];

		for (int k = 0; k < 3; k++) {
			fds[k] = open(FONT, O_RDONLY);
		}
		for (int k = 0; k < 3; k++) {
			for (int o = 0; o < 3; o++) {
				unsigned char b[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
				long n = -1;
				int err = 0;

				if (fds[k] < 0) {
					err = errno;
				} else {
					if (lseek(fds[k], offs[o], SEEK_SET) < 0) {
						err = errno;
					} else {
						ssize_t r = read(fds[k], b,
								 sizeof(b));

						n = (long) r;
						if (r < 0) {
							err = errno;
						}
					}
				}
				std::printf("OPEN3 high#%d@%ld path=%s fd=%d errno=%d(%s)"
					    " read=%ld first=%02x%02x\n",
					    k + 1, offs[o], FONT, fds[k], err,
					    err ? std::strerror(err) : "-", n,
					    b[0], b[1]);
			}
		}
		std::fflush(stdout);
	}

	/* ---- H: LARGE READS, which is what loading a font's tables IS ----
	 * FreeType reads a whole table in one call - `glyf` in these fonts is
	 * hundreds of kilobytes - so the one read shape still untested is a big
	 * one. A short read here and FreeType rejects the face as a bad format,
	 * which is exactly the error being chased. */
	{
		static unsigned char big[600000];
		int fds[3];

		for (int k = 0; k < 3; k++) {
			fds[k] = open(FONT, O_RDONLY);
		}
		for (int k = 0; k < 3; k++) {
			long n = -1;
			int err = 0;

			if (fds[k] < 0) {
				err = errno;
			} else if (lseek(fds[k], 0, SEEK_SET) < 0) {
				err = errno;
			} else {
				ssize_t r = read(fds[k], big, sizeof(big));

				n = (long) r;
				if (r < 0) {
					err = errno;
				}
			}
			std::printf("OPEN3 big#%d path=%s fd=%d errno=%d(%s) read=%ld "
				    "of=%lu\n", k + 1, FONT, fds[k], err,
				    err ? std::strerror(err) : "-", n,
				    (unsigned long) sizeof(big));
		}
	}

	/* and the same through stdio, with three FILE* held open */
	{
		static unsigned char big[600000];
		FILE *s[3] = { std::fopen(FONT, "rb"), std::fopen(FONT, "rb"),
			       std::fopen(FONT, "rb") };

		for (int k = 0; k < 3; k++) {
			size_t n = 0;
			int err = 0;

			if (!s[k]) {
				err = errno;
			} else {
				std::fseek(s[k], 0, SEEK_SET);
				n = std::fread(big, 1, sizeof(big), s[k]);
				if (n != sizeof(big)) {
					err = std::ferror(s[k]) ? errno : 0;
				}
			}
			std::printf("STDIO big#%d fp=%p errno=%d(%s) read=%ld "
				    "of=%lu\n", k + 1, (void *) s[k], err,
				    err ? std::strerror(err) : "-", (long) n,
				    (unsigned long) sizeof(big));
		}
	}

	/* ---- I: MMAP — the primitive that started all of this ----
	 * FreeType's Unix stream maps a font and reads through the mapping, and
	 * a THIRD face of one file came back as a bad format. Nothing in this
	 * probe had ever mapped anything, which is why every descriptor-level
	 * test above was clean and the fault still looked like the font's.
	 *
	 * Three concurrent mappings of ONE file, each checked against a plain
	 * read of the same bytes. The mapping is MAP_PRIVATE/PROT_READ, exactly
	 * what FreeType asks for. */
	{
		int fd = open(FONT, O_RDONLY);

		if (fd >= 0) {
			static unsigned char truth[8];
			ssize_t t = read(fd, truth, sizeof(truth));

			std::printf("OPEN3 mmap-truth path=%s read=%ld first=%02x%02x"
				    "%02x%02x\n", FONT, (long) t, truth[0],
				    truth[1], truth[2], truth[3]);

			for (int k = 0; k < 3; k++) {
				/* A FRESH DESCRIPTOR PER MAPPING, CLOSED AS SOON AS IT
				 * IS MAPPED — which is what FreeType's stream does, and
				 * the shape that failed. */
				int fd = open(FONT, O_RDONLY);
				void *m = MAP_FAILED;
				int err = 0;

				if (fd < 0) {
					err = errno;
				} else {
					m = mmap(NULL, 759720, PROT_READ,
						 MAP_PRIVATE, fd, 0);
					if (m == MAP_FAILED) {
						err = errno;
					}
					close(fd);
				}
				if (m == MAP_FAILED) {
					std::printf("OPEN3 mmap#%d path=%s errno=%d"
						    "(%s) MAP_FAILED\n", k + 1, FONT,
						    err, std::strerror(err));
				} else {
					unsigned char *p = (unsigned char *) m;
					bool same = (p[0] == truth[0]
						     && p[1] == truth[1]
						     && p[2] == truth[2]
						     && p[3] == truth[3]);

					std::printf("OPEN3 mmap#%d path=%s base=%p "
						    "first=%02x%02x%02x%02x "
						    "matches=%s\n", k + 1, FONT, m,
						    p[0], p[1], p[2], p[3],
						    same ? "yes" : "NO");
				}
				std::fflush(stdout);
			}
		}
	}

	std::printf("OPEN3 done\n");
	std::fflush(stdout);
	return 0;
}
