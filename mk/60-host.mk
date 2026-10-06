# The Foundation built for the HOST (mk/60-host.mk).
#
# Why this exists: a Foundation change is a string, a collection, a formatter, a parse or an undo
# operation - and the guest route costs an image rebuild plus a FULL QEMU BOOT PER CASE. The library
# is plain Objective-C over libobjc2 and libc: no kernel, no Xfb, no /dev, no FSH, no px/pt factor.
# The runtime for THIS machine is already built (libobjc2-host-prefix), so the SAME sources compile
# here and run in seconds.
#
# WHAT A HOST RUN IS *NOT*. It never touches the kernel, Xfb, the framebuffer, /dev input, USB HID,
# the FSH config domains, or the guest filesystem - and it links the HOST's GLIBC and ICU rather
# than the guest's musl. A host run says "the library does this"; it cannot say "the OS does this".
# The guest gates (make test / test-all) stay the verification of record.
#
#   make host-foundation        build the host library and the host-clean probes
#   make host-foundation-run    ... and run them (seconds, no QEMU)
#
# This is the FOUNDATION half of the fragment that built the C++ UIKit for the host, removed
# 2026-09-17 when the toolkit was parked. ONLY THE PURE-COMPUTATION PROBES ARE BUILT: a probe that
# reads the guest's filesystem or an FSH path is deliberately absent, because it would fail here
# for a reason that is not a bug.
#
# WHAT DIVERGED, WHY, AND THE TRAP WORTH KEEPING (2026-09-19).
#   * RESOLVED - arc-pool. THE TRAP: ONE CLANG INVOCATION WITH TWO SOURCES DOES NOT APPLY
#     -fobjc-arc AND -fno-objc-arc PER FILE. The probe rule passed both sources in a single command,
#     so the PROBE MAIN WAS COMPILED MRC - and in MRC `c = nil` releases nothing, so the object was
#     never freed and arc-pool failed in a probe that was supposed to be ARC. Compiling each unit
#     separately (which is what the guest mk does, per file) fixed it: 47 -> 48 checks.
#     The long hunt for this is worth reading as a warning: SIX other explanations were tested and
#     all were wrong (runtime configuration, optimisation, the forwarding path, a stale runtime, the
#     compiled pool path, the ARC marker), and the answer came from BISECTION - the probe's own
#     support unit in a small ARC file deallocated correctly, the probe file did not, and an A/B of
#     the build shapes named it in one step.
#   * RESOLVED - the redo pair, and THIS IS THE LOOP PAYING FOR ITSELF: a USE-AFTER-FREE in
#     -[NSUndoManager performFromStack:toStack:], which the guest gate PASSED WITH and the host run
#     caught. The method assigns `_group = inverse` (a BORROWED reference) and then released the
#     array TWICE - once through `_group` and once as `inverse`, which it owns from its alloc. The
#     array was therefore freed while `to` still held it, so the NEXT pass's group contained dangling
#     actions: glibc reuses that memory and the class lookup answers nil, while musl leaves it intact
#     and the redo merely APPEARS to work. Fixed by dropping the release through `_group`.
#     HOW IT WAS FOUND, because the sequence is the point: the guest answered 50/50 and the host
#     47/50, and every explanation I invented was wrong - runtime configuration, optimisation, the
#     ARC flag, the forwarding path, a stale runtime, the trampoline. What worked was a FIVE-LINE
#     REPRODUCER plus gdb: it printed the crashing receiver, which was NIL, and the selector, which
#     was `invoke` - so the group had yielded a nil element. Instrumenting the library's own loop
#     then showed the element's class lookup returning nil at i=2, i.e. freed memory.
#     THE LESSON WORTH KEEPING: musl and glibc differ in what freed memory still looks like, so a
#     use-after-free can be INVISIBLE on the guest and fatal on the host. A host run is not merely
#     faster here - it can see a whole class of bug the guest hides.
#   * REFUTED EARLIER AND STILL REFUTED: the runtime prefix (rebuilt with -DGNUSTEP and
#     OLDABI_COMPAT=OFF to mirror the guest), -O (the guest carries none), <objc/runtime.h>, symbol
#     interposition, and the ARC marker (present on both sides). Do not spend those builds again.
# UNTIL THAT IS UNDERSTOOD, THIS IS A FAST ITERATION LOOP AND NOT A SUBSTITUTE: a host pass is
# evidence, and a host failure on one of those three proves nothing about the guest.

HOST_LLVM       ?= /usr/lib/llvm-19/bin
HOST_CC         ?= $(HOST_LLVM)/clang
HOST_CXX        ?= $(HOST_LLVM)/clang++
HOST_OBJCPFX     = .build/libobjc2-host-prefix
HOST_BUILD       = .build/host
HOST_LIBDIR      = $(HOST_BUILD)/lib
HOST_BINDIR      = $(HOST_BUILD)/bin
HOST_OBJDIR      = $(HOST_BUILD)/obj
HOST_FOUNDATION_LIB = $(HOST_LIBDIR)/libfoundation.so

# The same runtime ABI the guest uses; only the C library underneath differs.
HOST_OBJCFLAGS   = -fobjc-runtime=gnustep-2.0 -fblocks \
                   -fconstant-string-class=NSConstantString -I$(HOST_OBJCPFX)/include
HOST_ICU_CFLAGS  = $(shell pkg-config --cflags icu-i18n 2>/dev/null)
HOST_ICU_LIBS    = $(shell pkg-config --libs icu-i18n 2>/dev/null)
# THE LIBRARY IS MRC, exactly as it is on the guest: FOUNDATION_CFLAGS there carries no
# -fobjc-arc (measured at F13.21 - clang refuses it against this system's runtime), so a library file
# using -release compiles there and must compile here. The per-file -fno-objc-arc entries the guest
# block carries are therefore redundant, and this fragment does not repeat them.
# NO -O FLAG, DELIBERATELY: the guest's FOUNDATION_CFLAGS carries none either, and this project has
# a documented history of optimisation-dependent miscompiles. Matching the guest's flags exactly is
# the difference between a host run that predicts the guest and one that invents failures.
# NO ARC FLAG HERE, AND THAT IS LOAD-BEARING. The library is MRC (the guest's FOUNDATION_CFLAGS
# carries no -fobjc-arc either), so -fno-objc-arc belongs on the LIBRARY rule - not here. It was here
# first, and the probe rule then appended -fobjc-arc after it; clang takes the FIRST of a conflicting
# pair, so THE PROBES WERE COMPILED MRC ALL ALONG. That is what the arc-pool "leak" was: compiled MRC,
# `c = nil` releases nothing, so the object was never freed - and the same sequence in a hand-built
# test file deallocated correctly every time, because that file had no such flag.
HOST_CFLAGS      = -fPIC -g -Iinclude -Iuserland $(HOST_OBJCFLAGS)
HOST_RPATH       = -Wl,-rpath,$(CURDIR)/$(HOST_LIBDIR) -Wl,-rpath,$(CURDIR)/$(HOST_OBJCPFX)/lib
# -lcurl -lssl -lcrypto: THE HOST HALF OF THE CURL BRIDGE (2026-09-26). `FN_HOST_SRCS` is a wildcard, so
# the host build compiles `FNCURLURLProtocol.m` - which includes <curl/curl.h> and calls the LibreSSL API -
# and a probe link then fails on undefined `curl_*`/`SSL_*` unless the host libraries are named here. This
# is the change tools/curl-build.sh's header anticipated ("It gets added when something on the host links
# libcurl, which is a one-line change and not a redesign"); the HOST's curl and OpenSSL are used, exactly
# as the host run uses the host's glibc and ICU rather than the guest's musl and staged prefixes.
HOST_LDFLAGS     = -L$(HOST_LIBDIR) -L$(HOST_OBJCPFX)/lib -lobjc -lcurl -lssl -lcrypto

# THE PER-FILE TABLES THAT STILL MATTER: the sources that include <unicode/...>, one that includes <zlib.h>,
# and one that is a root class. THE COUNT IS DELIBERATELY NOT WRITTEN HERE (§63.24): the line above said "six
# sources" and there were already seven. (The guest block's MRC list is NOT repeated - the whole library is MRC.)
FN_HOST_SRCS     = $(notdir $(wildcard $(FOUNDATION_SRC)/*.m))
FN_HOST_ICU      = NSCalendar.m NSDateFormatter.m NSNumberFormatter.m NSPredicate.m NSTimeZone.m \
                   NSCharacterSet.m NSLocale.m NSString.m
FN_HOST_X11      = NSDataCodec.m
FN_HOST_ROOT     = NSProxy.m
FN_HOST_OBJS     = $(addprefix $(HOST_OBJDIR)/,$(FN_HOST_SRCS:.m=.o)) $(HOST_OBJDIR)/plist.o \
                   $(HOST_OBJDIR)/ninvoke-asm.o

# $(1) is the source file name.
define FN_HOST_rule
$(HOST_OBJDIR)/$(1:.m=.o): $(FOUNDATION_SRC)/$(1)
	@mkdir -p $(HOST_OBJDIR)
	$$(HOST_CC) -c $$(HOST_CFLAGS) -fno-objc-arc $$(HOST_ICU_CFLAGS) \
		$(if $(filter $(1),$(FN_HOST_ROOT)),-Wno-objc-root-class) \
		$$< -o $$@
endef
$(foreach f,$(FN_HOST_SRCS),$(eval $(call FN_HOST_rule,$(f))))

# The two sources that are not .m at all: the plist reader the library links, and the hand-written
# trampoline for -forwardInvocation:'s two paths.
$(HOST_OBJDIR)/plist.o: userland/plist.c
	@mkdir -p $(HOST_OBJDIR)
	$(HOST_CC) -c $(HOST_CFLAGS) $< -o $@

$(HOST_OBJDIR)/ninvoke-asm.o: $(FOUNDATION_SRC)/NSInvocation_amd64.S
	@mkdir -p $(HOST_OBJDIR)
	$(HOST_CC) -c -fPIC $< -o $@

$(HOST_FOUNDATION_LIB): $(FN_HOST_OBJS)
	@mkdir -p $(HOST_LIBDIR)
	$(HOST_CC) -shared -Wl,-soname,libfoundation.so $(FN_HOST_OBJS) \
		$(HOST_LDFLAGS) $(HOST_ICU_LIBS) -lz -o $@

# THE HOST-CLEAN PROBES. Each is built from its own translation unit plus its `_support` half, which
# is MRC - the same two-file split the guest rules use, and ARC is a per-file choice here exactly as
# it is there. A probe is listed ONLY once it has been shown to pass on the host; anything reading
# the guest's filesystem stays out.
# WHAT THE FIRST FULL RUN FOUND (2026-09-19): 22 probes built, and NINETEEN answered completely -
# including core 50/50, predicate 28/28, kvc 16/16, dateformatter 16/16, codecs 11/11. THREE did not,
# and they are the next work rather than hidden:
#   * foundation_calendar 11 ok, 3 FAIL - RESOLVED 2026-09-20: A DATA PATH, and the fix is the
#     RUNNER, not the probe. All three failures are the HOST'S LOCAL TIME ZONE leaking in - the same
#     probe answers 14/14 under `TZ=UTC` with no source change - so `host-foundation-run` now sets
#     `TZ=UTC` and the whole suite answers 270/270. The mechanism and the three printed failures are
#     written out at that recipe. THE OLD GUESS WAS HALF RIGHT AND NAMED THE WRONG SIDE: it said the
#     host's zone DATABASE is ICU's rather than the guest's, and the guest has no zone data to differ
#     over at all - it has no `TZ` and no `/etc/localtime` (this system has no `/etc`), so ICU's
#     default zone answers GMT, while the HOST inherits `/etc/localtime`. Nothing was excluded and no
#     assertion was weakened.
#   * foundation_nsvalue and foundation_operation PRINT NO RESULT LINE AT ALL, i.e. they CRASH. A
#     crash is a strong signal - the use-after-free above was found exactly this way - so these are
#     the first thing to reproduce in a small file.
# NONE OF THE THREE IS IN HOST_CLEAN until it is understood, so the runner cannot claim them.
#
# THE HOST-CLEAN PROBES: every one EXCEPT the five that read the guest's filesystem or an FSH path
# (foundation_collection, foundation_filemanager, foundation_string, foundation_url, foundation_value -
# they would fail here for a reason that is not a bug). Those five stay guest-only.
# THE PROBES FOR THE CUT PREDICATE FAMILY ARE NOT HERE (§63.161, d33b508a): NSPredicate, NSExpression,
# NSCompoundPredicate, NSComparisonPredicate and the format grammar were REMOVED from the tree by the user's
# decision (dec-6e026f163bcec154), and their probes went with them. Naming a probe whose source is gone makes
# the whole tier fail at the FIRST one (`clang: error: no input files`), which is how this was found: the tier
# could not run at all.
HOST_PROBES ?= foundation_backgroundactivity foundation_attributedstring foundation_pointers foundation_calendar foundation_calendardate foundation_codecs foundation_decimal foundation_decimalnumber foundation_notification foundation_notificationqueue foundation_coder foundation_core foundation_dateformatter foundation_error foundation_formatters foundation_host foundation_kvc foundation_kvo foundation_numberformatter foundation_nsvalue foundation_operation foundation_orderedset foundation_processinfo foundation_regex foundation_runloop foundation_set foundation_sort foundation_thread foundation_distributednotification foundation_spellserver foundation_archiver foundation_ubiquitousstore foundation_useractivity foundation_distantobjectrequest foundation_json foundation_jsonwrite foundation_tableoptions foundation_transformers foundation_dataoptions foundation_constants
define FN_HOST_PROBE_rule
$(HOST_BINDIR)/$(1): $(HOST_FOUNDATION_LIB) $(wildcard userland/tests/$(1).m) $(wildcard userland/tests/$(1)_support.m)
	@mkdir -p $(HOST_BINDIR) $(HOST_OBJDIR)
# PER-UNIT COMPILES, WHICH IS NOT COSMETIC: one driver invocation with both sources does NOT
# apply -fobjc-arc and -fno-objc-arc per file, so the probe main came out MRC and `c = nil`
# released nothing. The guest mk compiles per file too. THE SUPPORT HALF IS OPTIONAL: 21 of
# the 28 probes are a single translation unit (measured 2026-09-26).
	$$(HOST_CC) $$(HOST_CFLAGS) -Iuserland/tests -fobjc-arc -c $$(wildcard userland/tests/$(1).m) -o $(HOST_OBJDIR)/probe-$(1).o
	@if [ -f userland/tests/$(1)_support.m ]; then \
		$$(HOST_CC) $$(HOST_CFLAGS) -Iuserland/tests -fno-objc-arc -c userland/tests/$(1)_support.m \
			-o $(HOST_OBJDIR)/probe-$(1)-support.o; \
	fi
# WHETHER THERE IS A SUPPORT OBJECT IS DECIDED AT PARSE TIME, on the SOURCE (which exists by then),
# not on the object (which does not) and not by a shell variable: `$$support` reaches make as
# `$support`, whose `$s` is an empty make variable, leaving the literal `upport` as an argument.
	$$(HOST_CC) $$(HOST_RPATH) $$(HOST_LDFLAGS) -o $$@ $(HOST_OBJDIR)/probe-$(1).o \
		$(if $(wildcard userland/tests/$(1)_support.m),$(HOST_OBJDIR)/probe-$(1)-support.o) \
		-lfoundation -lcoregraphics $$(HOST_ICU_LIBS) -lz -lm
# `-lm` IS HERE FOR THE PROBES THAT COMPUTE A TRIG-OR-ROOT RELATION rather than quoting a decimal:
# foundation_formatters takes `sqrt` for the crossing NSUnitFuelEfficiency is anchored at, and glibc keeps
# libm SEPARATE from libc, so the link failed with "DSO missing from command line" while the same probe is
# fine on the guest (musl carries the maths in libc). The alternative was to quote the anchor as a literal,
# which would have weakened the check from a relation to a number.
endef
$(foreach p,$(HOST_PROBES),$(eval $(call FN_HOST_PROBE_rule,$(p))))

HOST_PROBE_BINS = $(addprefix $(HOST_BINDIR)/,$(HOST_PROBES))

.PHONY: host-foundation host-foundation-run
host-foundation: $(HOST_FOUNDATION_LIB) $(HOST_PROBE_BINS)
	@echo "host-foundation: $(words $(FN_HOST_SRCS)) library source(s), $(words $(HOST_PROBES)) probe(s) in $(HOST_BINDIR)"

# TZ=UTC IS PART OF MIRRORING THE GUEST, NOT A CONVENIENCE (2026-09-20). `foundation_calendar`
# asserts `[[NSTimeZone systemTimeZone] secondsFromGMT] == 0`, and that claim is TRUE ABOUT THIS OS:
# `+systemTimeZone` delegates to ICU's default zone (NSTimeZone.m:95), which on the guest answers GMT
# because there is no `TZ` and no `/etc/localtime` (this system has no `/etc`). A host run inherits
# the HOST's zone instead, so on a machine set to America/Toronto the same probe answers 11 ok / 3
# FAIL - `tz-offset`, `calendar-convert` (the epoch renders 1969-12-31 19:00:00) and `cross-tu` - and
# `TZ=UTC` answers 270/270 with NO source change, which is how the three were attributed to the
# environment rather than to the library. So the probe keeps its claim about the OS and the runner
# reproduces the OS it is standing in; nothing is excluded and no assertion is weakened.
host-foundation-run: host-foundation
	@for p in $(HOST_PROBES); do \
		echo "== $$p =="; \
		TZ=UTC $(HOST_BINDIR)/$$p || echo "   ($$p exited $$?)"; \
	done

# ---------------------------------------------------------------------------
# CoreGraphics: a C library over a bitmap, and the five probes that hold it up
# ---------------------------------------------------------------------------
# THESE ARE C AND NOT OBJECTIVE-C, and they never touch Foundation: CoreGraphics here is a C
# library drawing into a pixman bitmap. So this block is BESIDE the Foundation probes and not
# inside them, and it uses ITS OWN COMPILE AND LINK FLAGS rather than HOST_CFLAGS/HOST_LDFLAGS
# - those carry the Objective-C runtime options and `-lobjc`, and a C probe handed an unused
# `-fobjc-*` flag warns under -Wextra while linking a runtime it does not call. The flags
# below are the ones the probe runs were verified with (0 warnings, 144 checks).
HOST_LCMS2_PREFIX ?= .build/lcms2-host-prefix
HOST_CG_CFLAGS  ?= -std=gnu11 -fPIC -g -Wall -Wextra -Iuserland -I/usr/include/pixman-1 \
		   -I/usr/include/freetype2 \
		   -I$(CURDIR)/$(HOST_LCMS2_PREFIX)/include
# THE HOST DOES NEED THE RPATH — the opposite of the guest, and for the same reason: there is
# no system lcms2 to fall back on here (measured: the runtime .so exists, the header does not),
# so the only copy the probe can load is the one the build script installed.
#
# AND libfoundation IS ON THIS LINE because ONE file of the library is Objective-C: the
# system-defined colour spaces are named, and comparing a name is a message send. The probes
# themselves stay C and inherit it through `-lcoregraphics`, which they resolve because
# HOST_RPATH already names $(HOST_LIBDIR).
# AND THE HOST HAS ITS OWN LIBPNG — 1.6.48 against the guest prefix's 1.6.47, a patch apart and both
# well past the 1.6.36 boundary where the current licence begins — reached by the SYSTEM path with no
# `-L`, which is how these rules already take pixman. Its `png.h` lives at /usr/include/png.h, so no
# include flag is needed either.
# LIBJPEG IS THE SAME ARRANGEMENT: the host has /usr/include/jpeglib.h and libjpeg.so.62, and the IJG
# header's API is stable across implementations of it, so the host probes use the system library while
# the GUEST library links the vendored 3.2.0 that tools/libjpeg-build.sh installs. Two builds would
# be waste here — the reason lcms2 needed two was that this host had no lcms2 DEVELOPMENT HEADER.
HOST_CG_LDFLAGS ?= -lpixman-1 -lpng16 -ljpeg -lfreetype -lm \
		   -L$(CURDIR)/$(HOST_LCMS2_PREFIX)/lib -llcms2 \
		   -L$(HOST_LIBDIR) -lfoundation -L$(CURDIR)/$(HOST_OBJCPFX)/lib -lobjc $(HOST_ICU_LIBS) \
		   -Wl,-rpath,$(CURDIR)/$(HOST_LCMS2_PREFIX)/lib
HOST_CG_SRCS    := $(wildcard userland/CoreGraphics/*.c)
# THE OBJECTIVE-C HALF. IT CANNOT BE ADDED TO THE C SOURCES' ONE-COMMAND LINK: a single clang
# invocation over two sources does NOT apply per-file options, which this tree already knows from
# the Foundation probes — so the `.m` is compiled by a rule of its own and only the OBJECT joins
# the link. The probes need none of this: they pass the names through as opaque pointers, which is
# what the C-side spelling in CGColorSpace.h is for.
HOST_CG_MSRCS   := $(wildcard userland/CoreGraphics/*.m)
HOST_CG_LIB     ?= $(HOST_LIBDIR)/libcoregraphics.so
HOST_CG_OBJDIR  := $(HOST_OBJDIR)/coregraphics
# `=` AND NOT `:=`, WHICH IS NOT A STYLE CHOICE: the object list needs $(HOST_CG_OBJDIR), and an
# immediately-expanded assignment made BEFORE that variable is set expands to nothing — which
# builds an object path with a leading slash and make answers "No rule to make target
# /coregraphics-….o". Deferred expansion is what keeps the order from mattering.
HOST_CG_MOBJS    = $(patsubst userland/CoreGraphics/%.m,$(HOST_CG_OBJDIR)/coregraphics-%.o,$(HOST_CG_MSRCS))
# PIXMAN'S INCLUDE PATH NEEDS THE `pixman-1` SUBDIRECTORY NAMED: `pixman.h` includes
# `pixman-version.h` from its own directory and is NOT self-contained.
HOST_CG_PROBES  ?= coregraphics_context coregraphics_stroke coregraphics_stroke_context coregraphics_curve coregraphics_arc coregraphics_color coregraphics_color_foundation coregraphics_image coregraphics_image_png coregraphics_image_jpeg coregraphics_gradient coregraphics_gradient_colors coregraphics_shading coregraphics_pattern coregraphics_nsvalue coregraphics_font coregraphics_text coregraphics_textshot coregraphics_dataprovider coregraphics_imagederive coregraphics_pathquery coregraphics_layer coregraphics_geometry coregraphics_contextconv coregraphics_colorstate coregraphics_bitmapctx coregraphics_icc










$(HOST_CG_OBJDIR)/coregraphics-%.o: userland/CoreGraphics/%.m
	@mkdir -p $(HOST_CG_OBJDIR)
	$(HOST_CC) $(HOST_CG_CFLAGS) $(HOST_OBJCFLAGS) -I$(CURDIR)/$(HOST_OBJCPFX)/include \
		-c $< -o $@

# THE LIBRARY IS BUILT ONCE AND LINKED FIVE TIMES, the same shape as Foundation's: compiling
# the eight sources into each probe would also work and would take five times as long.
#
# `$` AND NOT `$$` IN THIS RECIPE, WHICH IS NOT A STYLE CHOICE. A directly-written recipe is
# expanded by make, so `$(HOST_CC)` is the compiler; `$$(HOST_CC)` would survive as a literal
# `$(HOST_CC)` for the SHELL, which reads it as a command substitution and answers
# "HOST_CC: not found". The probe rule below is inside `define` + `eval`, where `$$` is exactly
# right — the double dollar is what survives the `eval` and becomes a make expansion - so the
# two spellings differ because the two rules are reached differently, and copying one into the
# other fails at the shell rather than at make.
$(HOST_CG_LIB): $(HOST_CG_SRCS) $(HOST_CG_MOBJS)
	@mkdir -p $(HOST_LIBDIR) $(HOST_CG_OBJDIR)
	$(HOST_CC) $(HOST_CG_CFLAGS) -shared -o $@ $(HOST_CG_SRCS) $(HOST_CG_MOBJS) $(HOST_CG_LDFLAGS)

define CG_HOST_PROBE_rule
$(HOST_BINDIR)/$(1): $(HOST_CG_LIB) $(wildcard userland/tests/$(1).c) $(wildcard userland/tests/$(1).m)
	@mkdir -p $(HOST_BINDIR)
# WHICH LANGUAGE IS DECIDED AT PARSE TIME, ON THE SOURCE, exactly as the Foundation probes decide
# whether they have a `_support` half: a probe whose file is `.m` is Objective-C, and it is built
# MRC (`-fno-objc-arc`) for the reason the tree's ARC rule gives — ARC is for everything that USES
# Foundation, and a check that has to RELEASE an object a C-style function returned with +1 cannot
# be written under ARC at all, because `-release` is forbidden there.
	$$(HOST_CC) $$(HOST_RPATH) $$(HOST_CG_CFLAGS) $(if $(wildcard userland/tests/$(1).m),$$(HOST_OBJCFLAGS) -I$(CURDIR)/$(HOST_OBJCPFX)/include -fno-objc-arc) \
		$(if $(wildcard userland/tests/$(1).m),userland/tests/$(1).m,userland/tests/$(1).c) \
		-L$(HOST_LIBDIR) -lcoregraphics $$(HOST_CG_LDFLAGS) -o $$@
endef
$(foreach p,$(HOST_CG_PROBES),$(eval $(call CG_HOST_PROBE_rule,$(p))))

.PHONY: host-coregraphics host-coregraphics-run
host-coregraphics: $(HOST_CG_LIB) $(addprefix $(HOST_BINDIR)/,$(HOST_CG_PROBES))
	@echo "host-coregraphics: $(words $(HOST_CG_SRCS)) library source(s), $(words $(HOST_CG_PROBES)) probe(s) in $(HOST_BINDIR)"

# A PROBE THAT EXITS NON-ZERO IS A FAILURE AND THIS TARGET MUST NOT SAY OTHERWISE. The
# Foundation runner above reports and carries on; this one keeps the status and fails at the
# end, so it can be used as a GATE rather than read as a report - which is the difference
# between a run that found something and a run that claimed nothing was there.
host-coregraphics-run: host-coregraphics
	@rc=0; for p in $(HOST_CG_PROBES); do \
		echo "== $$p =="; \
		$(HOST_BINDIR)/$$p || { echo "   ($$p exited $$?)"; rc=1; }; \
	done; \
	if [ $$rc -ne 0 ]; then echo "host-coregraphics-run: FAILED"; exit 1; fi; \
	echo "host-coregraphics-run: all $(words $(HOST_CG_PROBES)) probes passed"

# --- THE APPKIT (docs/design/coregraphics-plan.md C8), built the way CoreGraphics is ---------
# ONE OBJECT PREFIX (`appkit-`) AND IT IS NOBODY ELSE'S, the same rule the coregraphics one states.
# THE HOST DOES NEED `-lobjc` EXPLICITLY WHERE THE GUEST DOES NOT: the guest resolves it through
# libfoundation's NEEDED entry, while this link has no such transit and the runtime's own symbols
# (`objc_msgSend`, the class metadata) would otherwise be undefined.
HOST_APPKIT_LIB    ?= $(HOST_LIBDIR)/libappkit.so
HOST_APPKIT_CFLAGS ?= -std=gnu11 -fPIC -g -Wall -Wextra -Iuserland
HOST_APPKIT_MSRCS  := $(wildcard userland/AppKit/*.m)
HOST_APPKIT_OBJDIR := $(HOST_OBJDIR)/appkit
HOST_APPKIT_MOBJS   = $(patsubst userland/AppKit/%.m,$(HOST_APPKIT_OBJDIR)/appkit-%.o,$(HOST_APPKIT_MSRCS))
HOST_APPKIT_PROBES ?= appkit_graphicscontext appkit_color appkit_bezierpath appkit_bitmapimagerep appkit_image appkit_view
HOST_APPKIT_LDFLAGS = -L$(HOST_LIBDIR) -lappkit -lcoregraphics -lfoundation \
		      -L$(CURDIR)/$(HOST_OBJCPFX)/lib -lobjc $(HOST_ICU_LIBS)

$(HOST_APPKIT_OBJDIR)/appkit-%.o: userland/AppKit/%.m
	@mkdir -p $(HOST_APPKIT_OBJDIR)
	$(HOST_CC) $(HOST_APPKIT_CFLAGS) $(HOST_OBJCFLAGS) -I$(CURDIR)/$(HOST_OBJCPFX)/include \
		-c $< -o $@

$(HOST_APPKIT_LIB): $(HOST_APPKIT_MOBJS) $(HOST_CG_LIB)
	@mkdir -p $(HOST_LIBDIR)
	$(HOST_CC) $(HOST_APPKIT_CFLAGS) $(HOST_OBJCFLAGS) -I$(CURDIR)/$(HOST_OBJCPFX)/include \
		-shared -o $@ $(HOST_APPKIT_MOBJS) $(HOST_APPKIT_LDFLAGS)

define APPKIT_HOST_PROBE_rule
$(HOST_BINDIR)/$(1): $(HOST_APPKIT_LIB) $$(wildcard userland/tests/$(1).m)
	@mkdir -p $(HOST_BINDIR)
	$$(HOST_CC) $$(HOST_RPATH) $$(HOST_APPKIT_CFLAGS) $$(HOST_OBJCFLAGS) -I$(CURDIR)/$(HOST_OBJCPFX)/include -fno-objc-arc \
		userland/tests/$(1).m -L$(HOST_LIBDIR) -lappkit $$(HOST_APPKIT_LDFLAGS) -o $$@
endef
$(foreach p,$(HOST_APPKIT_PROBES),$(eval $(call APPKIT_HOST_PROBE_rule,$(p))))

.PHONY: host-appkit host-appkit-run
host-appkit: $(HOST_APPKIT_LIB) $(addprefix $(HOST_BINDIR)/,$(HOST_APPKIT_PROBES))
	@echo "host-appkit: $(words $(HOST_APPKIT_MSRCS)) library source(s), $(words $(HOST_APPKIT_PROBES)) probe(s) in $(HOST_BINDIR)"

# A PROBE THAT EXITS NON-ZERO IS A FAILURE, the same rule host-coregraphics-run states: this is a
# GATE, not a report.
host-appkit-run: host-appkit
	@rc=0; for p in $(HOST_APPKIT_PROBES); do \
		echo "== $$p =="; \
		$(HOST_BINDIR)/$$p || { echo "   ($$p exited $$?)"; rc=1; }; \
	done; \
	if [ $$rc -ne 0 ]; then echo "host-appkit-run: FAILED"; exit 1; fi; \
	echo "host-appkit-run: all $(words $(HOST_APPKIT_PROBES)) probes passed"

# THE HOST RUNTIME ITSELF, and why this target exists. The prefix that was here had been configured
# WITHOUT -DGNUSTEP and WITH OLDABI_COMPAT=ON, while the guest runtime is built with BOTH THE OTHER
# WAY. Those are BEHAVIOURAL switches inside libobjc2, and the host run diverged on exactly the checks
# that would show a runtime difference (arc-pool is a runtime test; the redo path goes through
# NSInvocation). The options below MIRROR the guest's configure line - including that the FNX patch is
# NOT applied here: it exists to make the musl C wrapper link the runtime with the C++ driver, which
# the host's glibc link does not need.
.PHONY: host-libobjc
host-libobjc:
	cmake -G "Unix Makefiles" -S $(OBJC_SRC) -B .build/libobjc2-host \
	  -DCMAKE_C_COMPILER=$(HOST_CC) -DCMAKE_CXX_COMPILER=$(HOST_CXX) \
	  -DCMAKE_OBJC_COMPILER=$(HOST_CC) -DCMAKE_OBJCXX_COMPILER=$(HOST_CXX) \
	  -DCMAKE_INSTALL_PREFIX=$(CURDIR)/$(HOST_OBJCPFX) \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	  -DGNUSTEP_INSTALL_TYPE=NONE -DCMAKE_INSTALL_LIBDIR=lib \
	  -DTESTS=OFF -DOLDABI_COMPAT=OFF -DLLVM_OPTS=OFF \
	  -DBUILD_STATIC_LIBOBJC=OFF \
	  -DCMAKE_C_FLAGS="-DGNUSTEP" -DCMAKE_OBJC_FLAGS="-DGNUSTEP"
	cmake --build .build/libobjc2-host -j$$(nproc)
	cmake --install .build/libobjc2-host
