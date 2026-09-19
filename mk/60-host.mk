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
HOST_LDFLAGS     = -L$(HOST_LIBDIR) -L$(HOST_OBJCPFX)/lib -lobjc

# THE PER-FILE TABLES THAT STILL MATTER: five sources include <unicode/...>, one includes <zlib.h>,
# and one is a root class. (The guest block's MRC list is NOT repeated - the whole library is MRC.)
FN_HOST_SRCS     = $(notdir $(wildcard $(FOUNDATION_SRC)/*.m))
FN_HOST_ICU      = nscalendar.m nsdateformatter.m nsnumberformatter.m nspredicate.m nstimezone.m
FN_HOST_X11      = ncodec.m
FN_HOST_ROOT     = nsproxy.m
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

$(HOST_OBJDIR)/ninvoke-asm.o: $(FOUNDATION_SRC)/ninvoke_amd64.S
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
HOST_PROBES ?= foundation_core
define FN_HOST_PROBE_rule
$(HOST_BINDIR)/$(1): $(HOST_FOUNDATION_LIB) $(wildcard userland/tests/$(1).m) $(wildcard userland/tests/$(1)_support.m)
	@mkdir -p $(HOST_BINDIR) $(HOST_OBJDIR)
# PER-UNIT COMPILES, WHICH IS NOT COSMETIC: one driver invocation with both sources does NOT
# apply -fobjc-arc and -fno-objc-arc per file, so the probe main came out MRC and `c = nil`
# released nothing. That was the whole arc-pool divergence. The guest mk compiles per file too.
	$$(HOST_CC) $$(HOST_CFLAGS) -Iuserland/tests -fobjc-arc -c $$(wildcard userland/tests/$(1).m) -o $(HOST_OBJDIR)/probe-$(1).o
	$$(HOST_CC) $$(HOST_CFLAGS) -Iuserland/tests -fno-objc-arc -c $$(wildcard userland/tests/$(1)_support.m) -o $(HOST_OBJDIR)/probe-$(1)-support.o
	$$(HOST_CC) $$(HOST_RPATH) $$(HOST_LDFLAGS) -o $$@ $(HOST_OBJDIR)/probe-$(1).o $(HOST_OBJDIR)/probe-$(1)-support.o -lfoundation $$(HOST_ICU_LIBS) -lz
endef
$(foreach p,$(HOST_PROBES),$(eval $(call FN_HOST_PROBE_rule,$(p))))

HOST_PROBE_BINS = $(addprefix $(HOST_BINDIR)/,$(HOST_PROBES))

.PHONY: host-foundation host-foundation-run
host-foundation: $(HOST_FOUNDATION_LIB) $(HOST_PROBE_BINS)
	@echo "host-foundation: $(words $(FN_HOST_SRCS)) library source(s), $(words $(HOST_PROBES)) probe(s) in $(HOST_BINDIR)"

host-foundation-run: host-foundation
	@for p in $(HOST_PROBES); do \
		echo "== $$p =="; \
		$(HOST_BINDIR)/$$p || echo "   ($$p exited $$?)"; \
	done

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
