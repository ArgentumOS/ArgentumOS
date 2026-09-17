"""A file may be opened more than twice at once.

THE DEFECT THIS PINS: this case was written for a guest that appeared to allow
TWO concurrent opens of one file and not three — because FreeType reports a
failed READ as "Unknown_File_Format", so the third font face failed with an
error that reads like a corrupt font, and that is how it was chased (the file,
the image, the library, the clipping path) before anything counted the opens.
There was no such limit. The openings were always fine; the MAPPING was not,
and check I below is where that lives now.

It matters beyond fonts: any application that holds a file open twice and then
opens it again — a library loading two copies, a tool reading its own input —
takes a failure that looks like the data is bad.

The probe (/System/Shared/tests/font_twice) does it three ways, so the shape
of the limit is visible whichever way it breaks:

  A. three concurrent RAW opens of ONE file, reporting fd and errno per step
     (an open failure and a read failure are different bugs, and this tells
     them apart);
  B. three concurrent raw opens of THREE DIFFERENT files — a failure here
     would mean a GLOBAL limit, not a per-file one;
  C. the same shape through FreeType, which is how the toolkit met it.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/font_twice"

OPEN3 = re.compile(
    r"OPEN3 (\S+)\s+path=(\S+) fd=(-?\d+) errno=(\d+)\(([^)]*)\) read=(-?\d+)"
    r" first=([0-9a-f]{2})([0-9a-f]{2})")
FONT2 = re.compile(r"FONT2 (\S+)\s+new=(-?\d+)")
MMAP = re.compile(r"OPEN3 mmap#(\d)\s+path=(\S+) base=(\S+) first=([0-9a-f]{8})"
                  r" matches=(\w+)")


class Case(BaseCase):
    title = "fs: one file can be opened more than twice at once"
    tier = "slow"
    timeout = 300

    def run(self, ctx):
        session = ctx.boot(machine="pc,i8042=off",
                           extra=["-device", "qemu-xhci",
                                  "-device", "usb-mouse",
                                  "-device", "usb-kbd"])
        if not session.shell_ready(150):
            self.check("shell-ready", False, "no serial shell")
            return

        mark = len(session.log_text())
        session.run("test -x %s && %s; echo TWICE-EXIT=$?" % (PROBE, PROBE))
        session.wait_for(r"TWICE-EXIT=", 90)
        out = session.output_since(mark)
        raw = "\n".join(l.strip() for l in out.splitlines()
                        if l.startswith(("OPEN3", "FONT2", "STDIO", "MEM2")))

        steps = {m.group(1): m for m in OPEN3.finditer(out)}
        faces = {m.group(1): int(m.group(2)) for m in FONT2.finditer(out)}
        stdio = re.findall(r"STDIO stdio#(\d) fp=(\S+) read=(-?\d+)", out)
        mem = re.findall(r"MEM2 mem#(\d) new=(-?\d+)", out)

        def worked(name):
            """An open that returned a descriptor and a full eight-byte read."""
            m = steps.get(name)

            return (m is not None and int(m.group(3)) >= 0
                    and int(m.group(4)) == 0 and int(m.group(6)) == 8)

        # A. THE SAME FILE, THREE AT ONCE — the case that was broken.
        self.check("three-opens-of-one-file-all-read",
                   worked("same#1") and worked("same#2") and worked("same#3"),
                   "three concurrent opens of one font must each return a "
                   "descriptor and read eight bytes:\n%s" % raw)

        # The bytes are the point: a read that "succeeds" with the wrong
        # content is the failure that looked like a corrupt font.
        third = steps.get("same#3")
        self.check("and-the-third-open-reads-the-right-bytes",
                   third is not None and third.group(7) == "00"
                   and third.group(8) == "01",
                   "the third open's first bytes must be the TrueType magic "
                   "00 01, not whatever a stale offset would give: %s"
                   % (third.group(7) + third.group(8) if third else "no line"))

        # B. DIFFERENT FILES, so a global limit cannot pass for a per-file one.
        self.check("three-different-files-also-open",
                   worked("diff#1-bold") and worked("diff#2-conf")
                   and worked("diff#3-font"),
                   "three different files held open at once must all read:\n%s"
                   % raw)

        # I. THE MAPPING, which is what actually broke, and what no
        # descriptor-level test above could see. FreeType's Unix stream maps a
        # font and reads through the mapping, so a THIRD face of one file came
        # back as a bad format while every open/read/lseek was perfect. Three
        # concurrent mappings of one file must each read the file's bytes.
        maps = {int(m.group(1)): m for m in MMAP.finditer(out)}
        self.check("three-mappings-of-one-file-all-read-it",
                   len(maps) == 3
                   and all(m.group(5) == "yes" for m in maps.values()),
                   "every mapping of one file must show the file's bytes "
                   "(first=00010000 matches=yes): the kernel merged adjacent "
                   "mappings of one file into a single vma and read everything "
                   "past the first from a file offset the file does not have:\n%s"
                   % "\n".join(m.group(0) for m in maps.values()))

        # WHAT THIS CASE IS NOT ABOUT, and what it cost to learn: there is NO
        # two-open limit, in the kernel or the filesystem. Every layer was
        # clean and measured to the byte with errno — open/read/lseek at
        # offsets 0, 300000 and 700000 on three concurrent descriptors, the
        # same interleaved seek/read through three stdio FILE*, three FT faces
        # from one in-memory buffer — while a SECOND file-backed face of one
        # font still failed with new=2. That "2" was never an open count: it
        # was FreeType's stream mapping the font, and the mapping returning
        # zeros (see check I above). The toolkit did not need FreeType's file
        # path to behave differently any more (one face per family+bold, sized
        # per use; the toolkit is parked, 2026-09), and the FONT2 lines stay in
        # this case's evidence, below, where they say exactly what they said
        # while the font was being blamed.
        # exactly what they said while the font was being blamed.

        # D/E say WHERE the fault is, once the kernel and the filesystem are
        # ruled out (which they are, above, to the byte): below FreeType (musl
        # stdio), or inside it.
        self.check("stdio-opens-three",
                   len(stdio) == 3 and all(int(r) == 8 for _, _, r in stdio),
                   "three stdio FILE* on one file must each read eight bytes: "
                   "%s" % stdio)
        self.check("freetype-from-memory-opens-three",
                   len(mem) == 3 and all(int(e) == 0 for _, e in mem),
                   "three FT faces made from ONE in-memory buffer must all "
                   "open (no stream, no FILE*): %s" % mem)

        # F. THE SEEK, which is what the tests above did NOT do and what
        # FreeType does first on a pathname: learn the file's size by seeking
        # to its end. Every open must report the SAME size (759720 for these
        # fonts) and seek back to 0.
        rawseek = re.findall(
            r"SEEK raw#(\d) fd=(-?\d+) end=(-?\d+) back=(-?\d+) errno=(\d+)",
            out)
        stdseek = re.findall(
            r"SEEK stdio#(\d) fp=(\S+) end=(-?\d+) pos=(-?\d+) errno=(\d+)",
            out)
        self.check("every-open-reports-the-right-size",
                   len(rawseek) == 3 and len(stdseek) == 3
                   and all(r[2] == "759720" and r[3] == "0" for r in rawseek)
                   and all(s[2] == "759720" and s[3] == "0" for s in stdseek),
                   "a seek to the end must report the file's size (759720) "
                   "and a seek back must report 0, on every one of three "
                   "concurrent opens — raw: %s stdio: %s" % (rawseek, stdseek))

        # Say the FreeType half in the report WITHOUT failing on it: it is a
        # FreeType-side defect in this guest (see the note above), and the
        # probe's own FONT2 lines carry it.
        self.note("FreeType file-backed faces of one font: %s — a THIRD face "
                  "of the SAME file fails while a different file and an "
                  "in-memory buffer are both fine" % faces)
