m0clang: userland64 $(COMPILER_RT_BUILTINS) tools/musl-clang64.sh tools/musl-clang64-static.sh
	$(MUSL64_CLANG) userland/tests/hello.c -o $(M0CLANG_DIR)/clang-hello-dl
	$(MUSL64_CLANG_STATIC) userland/tests/hello.c -o $(M0CLANG_DIR)/clang-hello-static
	@echo "m0clang: clang-built hellos staged under System/Shared/tests"

dash64: $(DASH64_BIN)
$(DASH64_BIN): $(MUSL64_LIBC) third_party/dash-fsh.patch
	cd third_party/dash && git apply $(CURDIR)/third_party/dash-fsh.patch && \
		./autogen.sh && \
		CC="$(MUSL64_CC)" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(CURDIR)/$(DASH64_BIN) && \
		git checkout -- .

toybox64: $(TOYBOX64_BIN)
$(TOYBOX64_BIN): $(MUSL64_LIBC) tools/mktoybox.sh $(MUSL64_CC) $(FNXLIB_CONFIG)
	TOYBOX_CC="$(MUSL64_CC)" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $(TOYBOX64_BIN)

# --- the static recovery set (docs/shared-libraries-plan.md §2.4/§3) ---
# Static-by-choice binaries that must run when /System/Libraries is
# corrupt or missing: a static dash (RECOVERY_PROGRAM, exec'd by the
# kernel) + a static toybox (the repair tools; its account applets need
# libconfig, hence the static libconfig.a). Both are built with
# MUSL64_CC_STATIC after the dynamic world (serial make order: dash64 /
# toybox64 above run first, so the in-tree third_party builds are not
# clobbered out from under the dynamic outputs).
RECOVERY64      = .build/recovery64
DASH64_RECOVERY = $(RECOVERY64)/dash-static
TOYBOX64_RECOVERY = $(RECOVERY64)/toybox-static

$(FNXLIB)/libconfig.a: $(FNXLIB_CONFIG) userland/libconfig.c \
	userland/libconfig_plist.c userland/libconfig_internal.h \
	userland/plist.c userland/libconfig.h
	$(MUSL64_CC) -Iinclude -Iuserland -c userland/libconfig.c -o $(FNXLIB)/libconfig.o
	$(MUSL64_CC) -Iinclude -Iuserland -c userland/libconfig_plist.c -o $(FNXLIB)/libconfig-plist.o
	$(MUSL64_CC) -Iinclude -Iuserland -c userland/plist.c -o $(FNXLIB)/libconfig-core.o
	ar rcs $@ $(FNXLIB)/libconfig.o $(FNXLIB)/libconfig-plist.o $(FNXLIB)/libconfig-core.o

$(DASH64_RECOVERY): $(MUSL64_LIBC) third_party/dash-fsh.patch
	@mkdir -p $(RECOVERY64)
	cd third_party/dash && git apply $(CURDIR)/third_party/dash-fsh.patch && \
		./autogen.sh && \
		CC="$(MUSL64_CC_STATIC)" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(CURDIR)/$(DASH64_RECOVERY) && \
		git checkout -- .

$(TOYBOX64_RECOVERY): $(MUSL64_LIBC) tools/mktoybox.sh $(MUSL64_CC_STATIC) $(FNXLIB)/libconfig.a
	@mkdir -p $(RECOVERY64)
	# TOYBOX_STAGE points the static build at its own scratch root so the
	# recovery run cannot clobber .build/toybox-root (the dynamic staging
	# userland64 copies as System/Tools/toybox) with a static toybox.
	TOYBOX_CC="$(MUSL64_CC_STATIC)" TOYBOX_STAGE="$(CURDIR)/$(RECOVERY64)/toybox-stage" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $@

.PHONY: recovery64
recovery64: $(DASH64_RECOVERY) $(TOYBOX64_RECOVERY)
	@echo "recovery64: static recovery set built ($(DASH64_RECOVERY), $(TOYBOX64_RECOVERY))"


# --- Xfb: FNX's native X server (fork of Xvfb; the pristine upstream is
# not vendored - regenerate it on demand, see userland/xfb/README.md).
# xfb64 is phony and always delegates: the inner per-file rules own the
# incremental rebuild (a file-prerequisite here would go stale forever,
# since .build/x11/xfb/Xfb has no source deps at this level).
XFB_SRC = userland/xfb
XFB_OUT = .build/x11/xfb
XFB_BIN = $(XFB_OUT)/Xfb

.PHONY: xfb64
xfb64: $(FNXLIB_CONFIG)
	# Xfb links dynamic (M2): its third-party deps (pixman, xkbfile,
	# Xfont2, Xau) are shared .so in /System/Libraries; the server's own
	# archives stay in the binary. libsha1.a + the server archives are
	# the only static pieces left (single-consumer FNX code).
	$(MAKE) -C $(XFB_SRC) OUT="$(CURDIR)/$(XFB_OUT)" \
		CC="$(MUSL64_CC)" -j8


# ---------------------------------------------------------------------------
# userland64: stage the native x86_64 root in the FNX hierarchy
# (docs/fsh-proposal.md). The root contains exactly the five top-level
# entries: Applications/, Shared/, System/, Users/, Volumes/. Tools live in
# System/Tools, machine config in System/Configuration, device nodes come
# from devfs at /System/Devices (boot-time kernel mount), and the
# virtual-fs mount points (/System/Processes, devpts under Devices) exist
# as directories.
# ---------------------------------------------------------------------------
# toybox installs applets into PREFIX/{bin,sbin,usr/...} per toy flags;
# stage into a scratch root and merge every applet dir into System/Tools.
TOYBOX64_STAGE = .build/toybox-root

# The Foundation (docs/design/foundation-plan.md F0): the root class, as a shared
# library beside libconfig. Declared HERE, not in mk/00-base.mk, because a make
# target's name and prerequisites are expanded when the rule is READ - and
# $(OBJC_STAMP) only exists once mk/10-toolchain.mk has been included.
# The string family (F1) and the tagged-string class (F2 of the build, F1 of the
# plan). The compile flags are per file and each for a measured reason:
#   -fno-objc-arc   nsobject.m and ntinystring.m IMPLEMENT -retain/-release,
#                   which ARC forbids (they are the MRR files);
#   -Wno-objc-missing-super-calls  every ARC -dealloc: clang emits the super
#                   chain itself, so the warning is unactionable noise;
#   -Wno-incomplete-implementation  NSString is ABSTRACT: its primitives are
#                   implemented by the concrete subclasses.
FOUNDATION_SRCS = $(FOUNDATION_SRC)/nsobject.m $(FOUNDATION_SRC)/nstring.m \
	$(FOUNDATION_SRC)/ntinystring.m $(FOUNDATION_SRC)/nnumber.m \
	$(FOUNDATION_SRC)/ndata.m $(FOUNDATION_SRC)/ndate.m \
	$(FOUNDATION_SRC)/nsarray.m $(FOUNDATION_SRC)/nsdictionary.m \
	$(FOUNDATION_SRC)/nerror.m $(FOUNDATION_SRC)/nexception.m \
	$(FOUNDATION_SRC)/ncharacterset.m $(FOUNDATION_SRC)/nindexset.m \
	$(FOUNDATION_SRC)/nindexpath.m \
	$(FOUNDATION_SRC)/nlocale.m \
	$(FOUNDATION_SRC)/nmethodsignature.m \
	$(FOUNDATION_SRC)/ninvocation.m \
	$(FOUNDATION_SRC)/ninvoke_amd64.S \
	$(FOUNDATION_SRC)/fninvoke.h $(FOUNDATION_SRC)/fnmethodsignature.h \
	$(FOUNDATION_SRC)/nenumerator.m $(FOUNDATION_SRC)/npropertylistserialization.m \
	$(FOUNDATION_SRC)/nstimezone.m $(FOUNDATION_SRC)/ndatecomponents.m \
	$(FOUNDATION_SRC)/nscalendar.m \
	$(FOUNDATION_SRC)/nurl.m \
	$(FOUNDATION_SRC)/nskeyvaluecoding.m \
	$(FOUNDATION_SRC)/nssortdescriptor.m \
	$(FOUNDATION_SRC)/nspredicate.m \
	$(FOUNDATION_SRC)/npredicateformat.m \
	$(FOUNDATION_SRC)/fnpredicate.h \
	$(FOUNDATION_SRC)/ncodec.m \
	$(FOUNDATION_SRC)/fncodec.h \
	$(FOUNDATION_SRC)/fncalendar.m \
	$(FOUNDATION_SRC)/fncalendar.h \
	$(FOUNDATION_SRC)/nsformatter.m \
	$(FOUNDATION_SRC)/nsdateformatter.m \
	$(FOUNDATION_SRC)/nsnumberformatter.m \
	$(FOUNDATION_SRC)/nsset.m \
	$(FOUNDATION_SRC)/nsvalue.m \
	$(FOUNDATION_SRC)/nsnull.m \
	$(FOUNDATION_SRC)/nscountedset.m \
	$(FOUNDATION_SRC)/nsorderedset.m \
	$(FOUNDATION_SRC)/nskeyvalueobserving.m \
	$(FOUNDATION_SRC)/nsexpression.m \
	$(FOUNDATION_SRC)/nscomparisonpredicate.m \
	$(FOUNDATION_SRC)/nscoder.m \
	$(FOUNDATION_SRC)/nskeyedarchiver.m \
	$(FOUNDATION_SRC)/nsprocessinfo.m
FOUNDATION_HDRS = $(FOUNDATION_SRC)/NSObjCRuntime.h $(FOUNDATION_SRC)/NSObject.h \
	$(FOUNDATION_SRC)/NSString.h \
	$(FOUNDATION_SRC)/NSTinyString.h $(FOUNDATION_SRC)/NSNumber.h \
	$(FOUNDATION_SRC)/NSData.h $(FOUNDATION_SRC)/NSDate.h \
	$(FOUNDATION_SRC)/NSCoding.h $(FOUNDATION_SRC)/NSCoder.h \
	$(FOUNDATION_SRC)/NSKeyedArchiver.h \
	$(FOUNDATION_SRC)/NSProcessInfo.h \
	$(FOUNDATION_SRC)/NSFastEnumeration.h $(FOUNDATION_SRC)/NSArray.h \
	$(FOUNDATION_SRC)/NSDictionary.h $(FOUNDATION_SRC)/NSError.h \
	$(FOUNDATION_SRC)/NSException.h $(FOUNDATION_SRC)/NSCharacterSet.h \
	$(FOUNDATION_SRC)/NSIndexSet.h $(FOUNDATION_SRC)/NSIndexPath.h \
	$(FOUNDATION_SRC)/NSLocale.h \
	$(FOUNDATION_SRC)/NSMethodSignature.h \
	$(FOUNDATION_SRC)/NSInvocation.h \
	$(FOUNDATION_SRC)/NSFormatter.h \
	$(FOUNDATION_SRC)/NSDateFormatter.h \
	$(FOUNDATION_SRC)/NSNumberFormatter.h \
	$(FOUNDATION_SRC)/NSSet.h \
	$(FOUNDATION_SRC)/NSValue.h \
	$(FOUNDATION_SRC)/NSNull.h \
	$(FOUNDATION_SRC)/NSCountedSet.h \
	$(FOUNDATION_SRC)/NSOrderedSet.h \
	$(FOUNDATION_SRC)/NSMutableOrderedSet.h \
	$(FOUNDATION_SRC)/NSKeyValueObserving.h \
	$(FOUNDATION_SRC)/NSEnumerator.h \
	$(FOUNDATION_SRC)/NSPropertyListSerialization.h \
	$(FOUNDATION_SRC)/NSDateComponents.h \
	$(FOUNDATION_SRC)/NSTimeZone.h \
	$(FOUNDATION_SRC)/NSCalendar.h \
	$(FOUNDATION_SRC)/NSURL.h \
	$(FOUNDATION_SRC)/NSKeyValueCoding.h \
	$(FOUNDATION_SRC)/NSSortDescriptor.h \
	$(FOUNDATION_SRC)/NSPredicate.h \
	$(FOUNDATION_SRC)/Foundation.h
# -Iinclude: the plist CORE (include/plist.h) is shared with libconfig, which
# consumes it from C — see the one-core-two-skins decision in the plan.
# F6 (nullability): a header that carries ANY annotation must carry them ALL —
# clang's -Wnullability-completeness is what says so, and as an ERROR it is the
# sweep's own gate: a file with some annotations and not others fails the build,
# while an untouched file (no annotations at all) stays silent, which is what lets
# the sweep land one slice at a time.
FOUNDATION_CFLAGS = -fPIC -Iinclude -Wno-objc-missing-super-calls -Wno-incomplete-implementation \
	-Werror=nullability-completeness

$(FOUNDATION_LIB): $(FOUNDATION_SRCS) $(FOUNDATION_HDRS) $(OBJC_STAMP)
	@mkdir -p $(FNXLIB)
	$(MUSL64_CC) -c -fPIC -Iinclude userland/plist.c -o .build/plist.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -fno-objc-arc -Iuserland \
		$(FOUNDATION_SRC)/nsobject.m -o .build/foundation-nsobject.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nstring.m -o .build/foundation-nstring.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -fno-objc-arc -Iuserland \
		$(FOUNDATION_SRC)/ntinystring.m -o .build/foundation-ntinystring.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nnumber.m -o .build/foundation-nnumber.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/ndata.m -o .build/foundation-ndata.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/ndate.m -o .build/foundation-ndate.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsarray.m -o .build/foundation-nsarray.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsdictionary.m -o .build/foundation-nsdictionary.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nerror.m -o .build/foundation-nerror.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nexception.m -o .build/foundation-nexception.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/ncharacterset.m -o .build/foundation-ncharacterset.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nindexset.m -o .build/foundation-nindexset.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nindexpath.m -o .build/foundation-nindexpath.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nlocale.m -o .build/foundation-nlocale.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nmethodsignature.m -o .build/foundation-nmethodsignature.o
	$(MUSL64_CC) -c -fPIC $(FOUNDATION_SRC)/ninvoke_amd64.S -o .build/foundation-ninvoke-asm.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/ninvocation.m -o .build/foundation-ninvocation.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nenumerator.m -o .build/foundation-nenumerator.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/npropertylistserialization.m -o .build/foundation-npropertylistserialization.o
	# F7: the calendar family. nscalendar.m and nstimezone.m are ARC; the
	# components bag owns nothing but its fields.
	#
	# F13.7a: nstimezone.m NOW INCLUDES ICU (<unicode/ucal.h>, <unicode/uenum.h>), because the class
	# reads the zone database instead of refusing it — so the ICU prefix is on ITS include path.
	# nscalendar.m and ndatecomponents.m do not include ICU: their arithmetic stays on libc's
	# struct tm and they reach the database only through NSTimeZone.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland -I$(ICUPREFIX)/include \
		$(FOUNDATION_SRC)/nstimezone.m -o .build/foundation-nstimezone.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/ndatecomponents.m -o .build/foundation-ndatecomponents.o
	# F13.7b: nscalendar.m NOW INCLUDES ICU (<unicode/ucal.h>), because the class reads every
	# calendar out of it instead of refusing the ones whose tables it lacked. So the ICU prefix is
	# on ITS include path too; the link needed nothing new (F13.6 already made libfoundation need
	# libicui18n/libicuuc/libicudata).
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland -I$(ICUPREFIX)/include \
		$(FOUNDATION_SRC)/nscalendar.m -o .build/foundation-nscalendar.o
	# F8: the URL value type.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nurl.m -o .build/foundation-nurl.o
	# F9: the key-value coding family — a category on NSObject, so nothing here
	# owns its storage; the lookup goes through the runtime's ivar table.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nskeyvaluecoding.m -o .build/foundation-nskeyvaluecoding.o
	# F10: the sorting family. nssortdescriptor.m resolves its key through KVC and
	# calls a comparison selector through its OWN return type (a scalar, not `id`).
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nssortdescriptor.m -o .build/foundation-nssortdescriptor.o
	# F11a: the predicate object model — an abstract base, two private leaves, the tree
	# node, and the two collection filters. The block leaf is why this file stores a block.
	#
	# F13.7d: nspredicate.m NOW INCLUDES ICU (<unicode/ucol.h>, for the `[d]` collation) and
	# <regex.h> (musl's POSIX engine, which is inside libc — so MATCHES adds no link and no
	# artifact). The ICU include path is therefore on THIS rule; the link needed nothing new,
	# because F13.6 already made libfoundation need libicui18n/libicuuc/libicudata.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland -I$(ICUPREFIX)/include \
		$(FOUNDATION_SRC)/nspredicate.m -o .build/foundation-nspredicate.o
	# F11b: the format grammar. A category on NSPredicate, so the parser lives beside the object
	# model without either file owning the other.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/npredicateformat.m -o .build/foundation-npredicateformat.o
	# F12: the compression binding. ncodec.m is the ONLY file that includes <zlib.h>, so the X11
	# prefix is on ITS include path — and on the LINK line below, because libfoundation now needs
	# libz.so.1. That library is already staged into the guest for the X11 stack, so this adds a
	# dependency and no new artifact (docs/design/foundation-plan.md, F12).
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland -I$(X11PREFIX)/include \
		$(FOUNDATION_SRC)/ncodec.m -o .build/foundation-ncodec.o
	# F13.6: the value-to-text family. nsformatter.m is the abstract base and needs nothing extra;
	# nsdateformatter.m is the file that includes <unicode/udat.h> and <unicode/udatpg.h>, so the
	# ICU prefix is on ITS include path — and on the LINK line below, because libfoundation now
	# needs libicui18n/libicuuc/libicudata. Those libraries and their data package are already
	# staged into the guest (docs/design/foundation-plan.md §10, F13), so this adds a dependency
	# and no new artifact — the same shape as F12's libz.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsformatter.m -o .build/foundation-nsformatter.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland -I$(ICUPREFIX)/include \
		$(FOUNDATION_SRC)/nsdateformatter.m -o .build/foundation-nsdateformatter.o
	# F13.7c: the number formatter. It includes <unicode/unum.h>, so the ICU prefix is on ITS
	# include path; the LINK needs nothing new, because F13.6 already made libfoundation need
	# libicui18n/libicuuc/libicudata.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland -I$(ICUPREFIX)/include \
		$(FOUNDATION_SRC)/nsnumberformatter.m -o .build/foundation-nsnumberformatter.o
	# F13.7e: the shared calendar-keyword bridge (fncalendar.m/.h). It is its OWN translation unit
	# because TWO classes call it — NSCalendar and NSDateFormatter — and it includes only
	# <foundation/...> headers, so it needs no ICU include path of its own.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/fncalendar.m -o .build/foundation-fncalendar.o
	# F13.8: the unordered collection. It includes <foundation/...> headers only — no ICU, no zlib —
	# because a set is RULES rather than data, which is why it was a gap in the plan's refusal table
	# rather than an entry in it.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsset.m -o .build/foundation-nsset.o
	# F13.8c: the box for everything that is not an object, and the object that stands for nothing.
	# Same shape as the set: <foundation/...> headers only, no ICU and no zlib.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsvalue.m -o .build/foundation-nsvalue.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsnull.m -o .build/foundation-nsnull.o
	# F13.8d: the counted set. A SUBCLASS of NSMutableSet, so its initialisers have to reach the
	# counts — see the file's header for why the array form may not go through the superclass's.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nscountedset.m -o .build/foundation-nscountedset.o
	# F13.8e: the ordered set and its mutable half, in ONE translation unit (they share no
	# superclass relation, so there is no NSMutableSet-style reason to split them).
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsorderedset.m -o .build/foundation-nsorderedset.o
	# F13.9: the observer registry. <foundation/...> headers only, like the collections.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nskeyvalueobserving.m -o .build/foundation-nskeyvalueobserving.o
	# F13.10: the expression tree. It reads collections and key paths, so it includes the NSSet
	# header; no ICU and no zlib.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsexpression.m -o .build/foundation-nsexpression.o
	# F13.11: the expression-shaped comparison. It shares the comparison rule with the grammar's
	# leaf through FNCompareValues, so this file states no rule of its own.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nscomparisonpredicate.m -o .build/foundation-nscomparisonpredicate.o
	# F13.12: the coder family. nskeyedarchiver.m includes the plist serialisation and the runtime
	# (it looks a class up BY NAME), so it is the one foundation source with those two dependencies.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nscoder.m -o .build/foundation-nscoder.o
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nskeyedarchiver.m -o .build/foundation-nskeyedarchiver.o
	# F13.13: the process service. It reads /proc and the C library's environ, so it is the one
	# source here that includes <unistd.h> and <stdio.h>.
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) -Iuserland \
		$(FOUNDATION_SRC)/nsprocessinfo.m -o .build/foundation-nsprocessinfo.o
	$(MUSL64_OBJC) -shared -Wl,-soname,libfoundation.so.1 \
		.build/foundation-nsobject.o .build/foundation-nstring.o \
		.build/foundation-ntinystring.o .build/foundation-nnumber.o \
		.build/foundation-ndata.o .build/foundation-ndate.o \
		.build/foundation-nsarray.o .build/foundation-nsdictionary.o \
		.build/foundation-nerror.o .build/foundation-nexception.o .build/foundation-nscoder.o .build/foundation-nskeyedarchiver.o .build/foundation-nsprocessinfo.o \
		.build/foundation-ncharacterset.o .build/foundation-nindexset.o .build/foundation-nindexpath.o .build/foundation-nlocale.o .build/foundation-nmethodsignature.o .build/foundation-ninvocation.o .build/foundation-ninvoke-asm.o .build/foundation-nenumerator.o \
		.build/foundation-npropertylistserialization.o .build/foundation-nstimezone.o .build/foundation-ndatecomponents.o .build/foundation-nscalendar.o .build/foundation-nurl.o .build/foundation-nskeyvaluecoding.o .build/foundation-nssortdescriptor.o .build/foundation-nspredicate.o .build/foundation-npredicateformat.o .build/foundation-ncodec.o .build/foundation-nsformatter.o .build/foundation-nsdateformatter.o .build/foundation-nsnumberformatter.o .build/foundation-fncalendar.o .build/foundation-nsset.o .build/foundation-nsvalue.o .build/foundation-nsnull.o .build/foundation-nscountedset.o .build/foundation-nsorderedset.o .build/foundation-nskeyvalueobserving.o .build/foundation-nsexpression.o .build/foundation-nscomparisonpredicate.o .build/plist.o -L$(X11PREFIX)/lib -lz -L$(ICUPREFIX)/lib -licui18n -licuuc -licudata -o $@
	ln -sf libfoundation.so.1 $(FNXLIB)/libfoundation.so
userland64: toolchain-gate $(MUSL64_LIBC) $(DASH64_BIN) $(TOYBOX64_BIN) $(LLVM_CXX_STAMP) $(OBJC_STAMP) foundation-gate $(FOUNDATION_LIB) $(LVGL64) $(XFB_BIN) $(FNXLIB_CONFIG) $(DASH64_RECOVERY) $(TOYBOX64_RECOVERY)
	rm -rf $(ROOTFS64)
	@mkdir -p $(ROOTFS64)
	# third-party X11 + toolchain tests live under System/Shared
	@mkdir -p "$(ROOTFS64)/System/Shared/X11/bin" "$(ROOTFS64)/System/Shared/tests"
	# --- the FSH skeleton (spaced names verbatim, Q7). No root /tmp: the
	# FSH maps /tmp to /System/Temporary Files (staged below); fshlint
	# bans the /tmp string in System/Tools; fs_repair_tmpdir() recreates
	# /System/Temporary Files at mount if a kill-replay left it non-dir. ---
	# Application Support: behaviour material (scripts, app data) per the
	# config policy carve-out - the same domain key and scope tree as
	# Configuration/, a different payload kind (config-design.md §0).
	@mkdir -p "$(ROOTFS64)/System/Application Support" \
		"$(ROOTFS64)/Shared/Application Support" \
		"$(ROOTFS64)/System/User Template/Application Support" \
	@mkdir -p "$(ROOTFS64)/Applications" "$(ROOTFS64)/Volumes"
	@mkdir -p "$(ROOTFS64)/Shared/Configuration" "$(ROOTFS64)/Shared/Libraries" \
		"$(ROOTFS64)/Shared/Fonts" "$(ROOTFS64)/Shared/Images" \
		"$(ROOTFS64)/Shared/Sounds" "$(ROOTFS64)/Shared/Videos" \
		"$(ROOTFS64)/Shared/Documentation" "$(ROOTFS64)/Shared/Themes"
	@mkdir -p "$(ROOTFS64)/System/Tools" "$(ROOTFS64)/System/Libraries" \
		"$(ROOTFS64)/System/Configuration" "$(ROOTFS64)/System/Devices" \
		"$(ROOTFS64)/System/Devices/pts" "$(ROOTFS64)/System/Processes" \
		"$(ROOTFS64)/System/ESP" "$(ROOTFS64)/System/Documentation/HTML/FNX" \
		"$(ROOTFS64)/System/Documentation/PDF/FNX" \
		"$(ROOTFS64)/System/Source Code" "$(ROOTFS64)/System/Shared/Fonts" \
		"$(ROOTFS64)/System/Shared/Images/Icons" \
		"$(ROOTFS64)/System/Shared/Images/Wallpaper" \
		"$(ROOTFS64)/System/Shared/Sounds" "$(ROOTFS64)/System/Shared/Videos" \
		"$(ROOTFS64)/System/Shared/X11/xkb" \
		"$(ROOTFS64)/System/Temporary Files" \
		"$(ROOTFS64)/System/Variable Data/X11/xkb/compiled" \
		"$(ROOTFS64)/System/User Template/Configuration" \
		"$(ROOTFS64)/System/User Template/Applications" \
		"$(ROOTFS64)/System/User Template/Documents" \
		"$(ROOTFS64)/System/User Template/Desktop" \
		"$(ROOTFS64)/System/User Template/Music" \
		"$(ROOTFS64)/System/User Template/Pictures" \
		"$(ROOTFS64)/System/User Template/Videos" \
		"$(ROOTFS64)/System/User Template/Shared/Libraries" \
		"$(ROOTFS64)/System/User Template/Shared/Fonts" \
		"$(ROOTFS64)/System/User Template/Shared/Images" \
		"$(ROOTFS64)/System/User Template/Shared/Sounds" \
		"$(ROOTFS64)/System/User Template/Shared/Videos" \
		"$(ROOTFS64)/System/User Template/Shared/Documentation" \
		"$(ROOTFS64)/System/User Template/Temporary Files" \
		"$(ROOTFS64)/System/User Template/Variable Data"
	# --- tools (executables) ---
	# toybox is built + installed by mktoybox.sh (fresh config + the
	# libconfig link); userland64 only flattens the staged applet dirs.
	@for d in bin sbin usr/bin usr/sbin; do \
		if [ -d "$(TOYBOX64_STAGE)/$$d" ]; then \
			cp -a $(TOYBOX64_STAGE)/$$d/. "$(ROOTFS64)/System/Tools/"; \
		fi; \
	done
	# toybox install links applets with PREFIX-relative targets that break
	# once bin/sbin/usr/bin are flattened into one Tools dir: point every
	# symlink at the toybox binary sitting next to it.
	@cd "$(ROOTFS64)/System/Tools" && for l in *; do \
		if [ -L "$$l" ]; then ln -sfn toybox "$$l"; fi; \
	done
	# suid root: the kernel honors S_ISUID at exec, and toybox drops to
	# the real uid for every applet except the account tools
	# (TOYFLAG_STAYROOT/ROOTONLY) — that is what lets non-root `su`
	# authenticate and switch users (M4).
	@chmod 4755 "$(ROOTFS64)/System/Tools/toybox"
	@chmod 0755 "$(ROOTFS64)/System/Tools/config" \
		"$(ROOTFS64)/System/Tools/init" 2>/dev/null || true
	# init reads the machine configuration THROUGH LIBCONFIG (P3c-b): one reader
	# for the domains it needs, and the reason a domain written as a plist can no
	# longer strand the boot mounts. -lconfig is the staged shared library, like
	# every other userland tool's.
	$(MUSL64_CC) -Iuserland -L$(FNXLIB) userland/tools/init.c -lconfig \
		-o "$(ROOTFS64)/System/Tools/init"
	$(MUSL64_CXX) userland/tests/cpp_smoke.cpp -o "$(ROOTFS64)/System/Shared/tests/cpp_smoke"
	# objc_smoke: the Objective-C runtime (docs/design/objc-toolchain-plan.md
	# P2/P3). TWO translation units on purpose: the class is implemented in
	# objc_smoke_support.m and its CATEGORY in objc_smoke.m, because cross-TU
	# class registration is the case that was misdiagnosed during P1 - it stays
	# in the acceptance now. The support unit is MRR (a root class cannot be
	# ARC), the other is ARC; they link into one binary.
	# -Wno-objc-root-class: SmokeObject IS a root class, deliberately.
	$(MUSL64_OBJC) -c -Wno-objc-root-class -Iuserland/tests \
		userland/tests/objc_smoke_support.m -o .build/objc-smoke-support.o
	$(MUSL64_OBJC) -c -Wno-objc-root-class -fobjc-arc -Iuserland/tests \
		userland/tests/objc_smoke.m -o .build/objc-smoke-main.o
	$(MUSL64_OBJC) .build/objc-smoke-support.o .build/objc-smoke-main.o \
		-o "$(ROOTFS64)/System/Shared/tests/objc_smoke"
	# foundation_core: F0 acceptance (docs/design/foundation-plan.md). Two units
	# AND two ownership regimes: the subclass and the MRR lifetime exercises in
	# the support unit, the checks in the ARC unit. The ARC flag is EXPLICIT -
	# the wrapper never adds it - and without it clang emits no release at all and
	# the pool check fails (measured).
	# THE FORWARDING FIXTURES DECLARE METHODS THEY MUST NOT IMPLEMENT: FastForwarder's -marker and
	# SlowForwarder's -value/-setValue: are the CLAIM this probe tests (the runtime has to forward
	# them to the backing object), so -Wincomplete-implementation is not noise here — it is the
	# compiler correctly observing that the design is incomplete ON PURPOSE. The same shape, and the
	# same justification, as NSString's abstract primitives in FOUNDATION_CFLAGS above.
	$(MUSL64_OBJC) -c -Wno-objc-root-class -Wno-incomplete-implementation -fno-objc-arc \
		-Iuserland -Iuserland/tests \
		userland/tests/foundation_core_support.m -o .build/foundation-core-support.o
	$(MUSL64_OBJC) -c -Wno-objc-root-class -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_core.m -o .build/foundation-core-main.o
	$(MUSL64_OBJC) .build/foundation-core-support.o .build/foundation-core-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_core"
	# foundation_string: F1 acceptance (docs/design/foundation-plan.md). Two
	# units again, and the support unit is where the OTHER constant-string
	# cases live (a 4-character literal is a TAGGED pointer, a 20-character one
	# is an object - both paths have to work).
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_string_support.m -o .build/foundation-string-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_string.m -o .build/foundation-string-main.o
	$(MUSL64_OBJC) .build/foundation-string-support.o .build/foundation-string-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_string"
	# foundation_value: F2 acceptance. The support unit imports ONLY the
	# umbrella header, so a complete <foundation/Foundation.h> is part of the
	# acceptance too.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_value_support.m -o .build/foundation-value-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_value.m -o .build/foundation-value-main.o
	$(MUSL64_OBJC) .build/foundation-value-support.o .build/foundation-value-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_value"
	# foundation_collection: F3 acceptance. The support unit builds a NESTED
	# collection, which is the cheapest check that collections are ordinary
	# objects; the main unit exercises clang's for-in lowering.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_collection_support.m -o .build/foundation-collection-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_collection.m -o .build/foundation-collection-main.o
	$(MUSL64_OBJC) .build/foundation-collection-support.o .build/foundation-collection-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_collection"
	# foundation_error: F4 acceptance. The support unit builds values from
	# another unit; the main unit exercises @try/@catch, so the runtime's throw
	# path is part of the check.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_error_support.m -o .build/foundation-error-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_error.m -o .build/foundation-error-main.o
	$(MUSL64_OBJC) .build/foundation-error-support.o .build/foundation-error-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_error"
	# foundation_calendar: F7 acceptance. The same two-unit shape, and the support
	# unit imports ONLY the umbrella — which is how the three new headers are
	# proved to have reached <foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_calendar_support.m -o .build/foundation-calendar-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_calendar.m -o .build/foundation-calendar-main.o
	$(MUSL64_OBJC) .build/foundation-calendar-support.o .build/foundation-calendar-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_calendar"
	# foundation_url: F8 acceptance. Two units again, and the support unit imports
	# ONLY the umbrella — so this is also the proof that NSURL reached
	# <foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_url_support.m -o .build/foundation-url-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_url.m -o .build/foundation-url-main.o
	$(MUSL64_OBJC) .build/foundation-url-support.o .build/foundation-url-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_url"
	# foundation_kvc: F9 acceptance. Two units again — and here the split is the
	# CLAIM under test: the support unit defines the OBJECTS and imports only the
	# umbrella, so a lookup that crossed translation units by name can only be the
	# runtime's.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_kvc_support.m -o .build/foundation-kvc-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_kvc.m -o .build/foundation-kvc-main.o
	$(MUSL64_OBJC) .build/foundation-kvc-support.o .build/foundation-kvc-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_kvc"
	# foundation_sort: F10 acceptance. Two units, and the split carries the claim again:
	# the support unit builds the OBJECTS and a descriptor from its own side, so a sort
	# whose key was resolved by name can only have gone through KVC.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_sort_support.m -o .build/foundation-sort-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_sort.m -o .build/foundation-sort-main.o
	$(MUSL64_OBJC) .build/foundation-sort-support.o .build/foundation-sort-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_sort"
	# foundation_predicate: F11a acceptance. Two units again: the support unit builds a
	# predicate (and counts a block's calls from its own side), so a predicate that crossed a
	# translation unit can only have gone through the object model.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_predicate_support.m -o .build/foundation-predicate-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_predicate.m -o .build/foundation-predicate-main.o
	$(MUSL64_OBJC) .build/foundation-predicate-support.o .build/foundation-predicate-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_predicate"
	# foundation_codecs: F12 acceptance. Two units, and the SUPPORT unit builds the BYTES — so the
	# codec is exercised on data it did not create.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_codecs_support.m -o .build/foundation-codecs-support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_codecs.m -o .build/foundation-codecs-main.o
	$(MUSL64_OBJC) .build/foundation-codecs-support.o .build/foundation-codecs-main.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_codecs"
	# icu_smoke: F13's acceptance for the ICU bring-up (docs/design/foundation-plan.md §10). The
	# SOURCE is C, because ICU is a C library and this exercises the DATA path rather than the
	# object layer — but the LINK goes through the C++ driver: ICU's libraries are C++ underneath
	# (libicui18n NEEDs libc++.so.1), and musl-clang++64.sh self-bootstraps the C++ runtime's
	# link pieces (-L.build/llvm-cxx/lib -lc++ -lc++abi -lunwind), which the C driver does not add.
	# Measured: with $(MUSL64_CC) the link fails on __cxa_* and std::__1::mutex from libicui18n.
	# Every check asks for an answer that comes from libicudata — three locales' number
	# formatting, a locale's own date pattern, the German vs Swedish collation rules, and the
	# time-zone id set — so a pass cannot come from constants in the probe.
	$(MUSL64_CXX) -I$(ICUPREFIX)/include userland/tests/icu_smoke.c \
		-L$(ICUPREFIX)/lib -licui18n -licuuc -licudata \
		-o "$(ROOTFS64)/System/Shared/tests/icu_smoke"
	# foundation_dateformatter: F13.6 acceptance - the first un-refused DATA family, exercised
	# through FOUNDATION's own API rather than ICU's directly. ONE unit, deliberately: the other
	# Foundation probes are two-unit because their claim is a cross-translation-unit boundary,
	# while this family's claim is data, so the probe includes only <foundation/Foundation.h>
	# (which also proves the umbrella exports the new headers). It links the Foundation library,
	# which is where ICU is now bound.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_dateformatter.m -o .build/foundation-dateformatter.o
	$(MUSL64_OBJC) .build/foundation-dateformatter.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_dateformatter"
	# foundation_set: F13.8 acceptance - the first family the boundary never justified. ONE unit (the
	# claim is VALUE SEMANTICS, not a cross-TU boundary), only <foundation/Foundation.h>, linking the
	# Foundation library.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_set.m -o .build/foundation-set.o
	$(MUSL64_OBJC) .build/foundation-set.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_set"
	# foundation_nsvalue: F13.8c acceptance. ONE unit, only <foundation/Foundation.h>. Named nsvalue
	# and NOT value, because foundation_value is F2/F8's probe for NSNumber/NSData/NSDate.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_nsvalue.m -o .build/foundation-nsvalue.o
	$(MUSL64_OBJC) .build/foundation-nsvalue.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_nsvalue"
	# foundation_orderedset: F13.8e acceptance. ONE unit, only <foundation/Foundation.h>. A separate
	# probe from foundation_set because NSOrderedSet is NOT an NSSet subclass: order is its value.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_orderedset.m -o .build/foundation-orderedset.o
	$(MUSL64_OBJC) .build/foundation-orderedset.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_orderedset"
	# foundation_kvo: F13.9 acceptance. ONE unit, only <foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_kvo.m -o .build/foundation-kvo.o
	$(MUSL64_OBJC) .build/foundation-kvo.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_kvo"
	# foundation_expression: F13.10 acceptance. ONE unit, only <foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_expression.m -o .build/foundation-expression.o
	$(MUSL64_OBJC) .build/foundation-expression.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_expression"
	# foundation_coder: F13.12 acceptance. ONE unit, only <foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_coder.m -o .build/foundation-coder.o
	$(MUSL64_OBJC) .build/foundation-coder.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_coder"
	# foundation_processinfo: F13.13 acceptance. ONE unit, only <foundation/Foundation.h> plus
	# <unistd.h> for the getpid cross-check.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_processinfo.m -o .build/foundation-processinfo.o
	$(MUSL64_OBJC) .build/foundation-processinfo.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_processinfo"
	# foundation_numberformatter: F13.7c acceptance - the second un-refused DATA family, and the
	# same shape as the date one: ONE unit (the claim is data), only <foundation/Foundation.h>, and
	# it links the Foundation library, where ICU is bound.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_numberformatter.m -o .build/foundation-numberformatter.o
	$(MUSL64_OBJC) .build/foundation-numberformatter.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_numberformatter"
	# (The toolkit probes — layout_solve, view_layout, stack_view, scroll_view,
	# collection_view, tab_view, split_view, grid_view, kvc_basic,
	# notification_basic, cell_basic, viewcontroller_basic, window_draw,
	# control_click, text_stack — and the Widget Zoo were removed with the
	# toolkit, 2026-09-17: docs/design/argentum-uikit-plan.md, DEFERRED.)
	# font_twice: TEMPORARY DIAGNOSTIC - asks the guest's own FreeType whether
	# it can open one font file twice (the toolkit keeps a face per size, so a
	# second size is a second FT_New_Face). No toolkit, no fontconfig.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-I$(X11PREFIX)/include/freetype2 -L$(X11PREFIX)/lib \
		userland/tests/font_twice.cpp -lfreetype \
		-o "$(ROOTFS64)/System/Shared/tests/font_twice"
	# plist_test: the C plist CORE's acceptance (include/plist.h + userland/plist.c)
	# — the shared core libconfig will consume from C. Compiled from the same
	# source the library builds, rather than linked out of libfoundation: it is a C
	# probe of a C core, so there is one implementation either way and no Objective-C
	# runtime in the path.
	$(MUSL64_CC) -Iinclude userland/tests/plist_test.c userland/plist.c -lm \
		-o "$(ROOTFS64)/System/Shared/tests/plist_test"
	# config_plist_test: P3b's acceptance (docs/design/plist-config-plan.md) — a
	# .conf reads the SAME in both spellings. It copies each shipped file into a
	# scratch tree, forces a rewrite (the writer emits plists now), and compares
	# every key, every value and the prose, both ways. Linked from the same
	# sources the library builds, like plist_test: one implementation either way.
	$(MUSL64_CC) -Iinclude -Iuserland userland/tests/config_plist_test.c \
		userland/libconfig.c userland/libconfig_plist.c userland/plist.c \
		-o "$(ROOTFS64)/System/Shared/tests/config_plist_test"
	# x_move: raw-Xlib window mover — attributes a window-move wedge
	# between the server (Xfb) and any client, with no toolkit involved.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/x_move.cpp -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/x_move"
	# x_keys: raw-Xlib key reader — proves a typed key reaches the guest's X
	# server with no toolkit in the path (the keyboard's x_move).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/x_keys.cpp -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/x_keys"
	# oom_probe: S4.3d (eats memory until a page cannot be faulted in,
	# to prove the fault path reports it and sends SIGBUS)
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/oom_probe.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/oom_probe"
	# xclick: generic synthetic-click injector for the S2.4 gates
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xclick.c \
		-L$(X11PREFIX)/lib -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xclick"
	# xshm_m0: MIT-SHM M0 acceptance — Xlib client (the UIKit transport)
	# paints a window via a SysV segment + XShmPutImage (libXext); logs
	# SHMM0-DONE for the gate.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xshm_m0.c \
		-L$(X11PREFIX)/lib -lXext -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xshm_m0"
	# xshm_geo: the Xfb short-window SHM measurement (S4.2a open item).
	# XShmPutImage into a matrix of geometries - including the toolkit's
	# real one (window 1920x30 with a 25%-larger backing) - reads each
	# window back with XGetImage and prints a verdict per geometry; run
	# from the console shell with DISPLAY=:0 and read the SHMGEO lines.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xshm_geo.c \
		-L$(X11PREFIX)/lib -lXext -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xshm_geo"
	# xwinprobe: the shadow-vs-screen probe (XGetImage over a screen rect,
	# twice) — written for the menubar strip that never repaints; useful
	# for any "the draw landed but the screen never changed" question.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xwinprobe.c \
		-L$(X11PREFIX)/lib -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xwinprobe"
	# xcomp_probe: de-risking a compositing Kestrel — does Xfb's Composite
	# redirect + hand out a usable pixmap, and what does a screen-sized
	# frame cost. Linked against the xcb bindings deliberately: the Xlib
	# wrappers (libXcomposite/libXdamage/libXrender) are not built here.
	$(MUSL64_CC) -I$(X11PREFIX)/include \
		-I$(X11PREFIX)/include/pixman-1 \
		userland/tests/xcomp_probe.c \
		-L$(X11PREFIX)/lib -lX11 -lX11-xcb -lxcb -lxcb-composite \
		-lpixman-1 -lXrender \
		-o "$(ROOTFS64)/System/Shared/tests/xcomp_probe"
	# xbtn: X11 mouse-leg regression client (window + pointer poll +
	# button print) — the S0.6 mouse gate drives QEMU monitor mouse at
	# it and expects "XBTN: button 1 press/release".
	$(MUSL64_CC) -I$(X11PREFIX)/include -I$(X11PREFIX)/include/X11 \
		-L$(X11PREFIX)/lib \
		userland/tests/xbtn.c -lX11 -o "$(ROOTFS64)/System/Shared/tests/xbtn"
	$(MUSL64_CC) userland/tools/acl.c -o "$(ROOTFS64)/System/Tools/acl"
	$(MUSL64_CC) -Iinclude -Iuserland userland/tools/config.c -L$(CURDIR)/$(FNXLIB) \
		-lconfig -o "$(ROOTFS64)/System/Tools/config"
	$(MUSL64_CC) -Iinclude tools/shm_leak_test.c -o "$(ROOTFS64)/System/Tools/shm_leak_test"
	$(MUSL64_CC) -Iinclude tools/shm_cap_test.c -o "$(ROOTFS64)/System/Tools/shm_cap_test"
	$(MUSL64_CC) tools/config_m3_test.c -o "$(ROOTFS64)/System/Tools/config_m3_test"
	$(MUSL64_CC) userland/tools/pty_test.c -o "$(ROOTFS64)/System/Tools/pty_test"
	# Probes the test harness drives (tests/cases/*): the security battery from
	# the audit rounds, the AGFS metadata/stream probes and the mmap probe.
	# They are committed sources; nothing built them into the image before.
	$(MUSL64_CC) -Iinclude tools/sec_test.c -o "$(ROOTFS64)/System/Shared/tests/sec_test"
	$(MUSL64_CC) userland/tests/test_mmap.c -o "$(ROOTFS64)/System/Shared/tests/test_mmap"
	$(MUSL64_CC) userland/tests/agfsattr.c -o "$(ROOTFS64)/System/Shared/tests/agfsattr"
	$(MUSL64_CC) userland/tests/agfsdir.c -o "$(ROOTFS64)/System/Shared/tests/agfsdir"
	$(MUSL64_CC) userland/tests/agfsxattr.c -o "$(ROOTFS64)/System/Shared/tests/agfsxattr"
	$(MUSL64_CC) userland/tools/agfsquery.c -o "$(ROOTFS64)/System/Tools/agfsquery"
	$(MUSL64_CC) userland/tools/agfsqtest.c -o "$(ROOTFS64)/System/Tools/agfsqtest"
	$(MUSL64_CC) userland/tools/tone.c -o "$(ROOTFS64)/System/Tools/tone" -lm
	$(MUSL64_CC) userland/tools/fbdump.c -o "$(ROOTFS64)/System/Tools/fbdump"
	cp $(DASH64_BIN) "$(ROOTFS64)/System/Tools/sh"
	# --- the static recovery set (docs/shared-libraries-plan.md §2.4/§3):
	# insurance when /System/Libraries is corrupt or missing. The kernel
	# boots these (RECOVERY_PROGRAM) instead of init on a 'recovery'
	# param or a failed NEEDED-closure probe. Static dash + static
	# toybox; the /System/Recovery/bin applet links mirror the dynamic
	# /System/Tools set so repair commands resolve to the static toybox.
	cp $(DASH64_RECOVERY) "$(ROOTFS64)/System/Tools/recovery-sh"
	cp $(TOYBOX64_RECOVERY) "$(ROOTFS64)/System/Tools/recovery-toybox"
	@chmod 0755 "$(ROOTFS64)/System/Tools/recovery-sh" \
		"$(ROOTFS64)/System/Tools/recovery-toybox"
	@mkdir -p "$(ROOTFS64)/System/Recovery/bin"
	@for l in $(CURDIR)/$(ROOTFS64)/System/Tools/*; do \
		if [ -L "$$l" ] && [ "$$(readlink "$$l")" = toybox ]; then \
			ln -sfn /System/Tools/recovery-toybox \
				"$(ROOTFS64)/System/Recovery/bin/$$(basename "$$l")"; \
		fi; \
	done
	# third-party X11 lives under System/Shared/X11 (outside the
	# zero-allow System/Tools lint scope); System/Tools stays first-party
	# + ported toybox only.
	@mkdir -p "$(ROOTFS64)/System/Shared/X11/bin" "$(ROOTFS64)/System/Shared/tests"
	cp $(XFB_BIN) "$(ROOTFS64)/System/Shared/X11/bin/Xfb"
	cp .build/x11-prefix/bin/xkbcomp "$(ROOTFS64)/System/Shared/X11/bin/xkbcomp"
	# xkb data for the runtime XKB compile (Xfb's libxkbfile default is
	# System/Shared/X11/xkb). Host xkeyboard-config is the established
	# source (bundled data mismatches the server's xkbcomp).
	@if [ -d /usr/share/X11/xkb/rules ]; then \
		cp -r /usr/share/X11/xkb/. "$(ROOTFS64)/System/Shared/X11/xkb/"; \
	else \
		echo "WARNING: /usr/share/X11/xkb missing - Xfb keyboard init will fail"; \
	fi
	@cp userland/tests/test_toybox.sh "$(ROOTFS64)/System/Shared/tests/test_toybox.sh" 2>/dev/null || \
		{ mkdir -p "$(ROOTFS64)/System/Shared/tests" && \
		  cp userland/tests/test_toybox.sh "$(ROOTFS64)/System/Shared/tests/test_toybox.sh"; }
	@chmod +x "$(ROOTFS64)/System/Tools/sh" "$(ROOTFS64)/System/Tools/init"
	@mkdir -p "$(ROOTFS64)/System/Shared/scripts/dhcp" && \
		cp userland/scripts/dhcp_script.sh "$(ROOTFS64)/System/Shared/scripts/dhcp/default.script" 2>/dev/null || true
	# --- machine configuration (System/Configuration; Q9 accounts) ---
	# Identity, name resolution and machine identity are record domains
	# shipped in System scope (docs/system-config-files-plan.md M2/M5).
	# No legacy colon/line files exist (passwd, group, shells, hosts).
	@cp userland/configuration/system.passwd.conf "$(ROOTFS64)/System/Configuration/system.passwd.conf"
	@cp userland/configuration/system.group.conf "$(ROOTFS64)/System/Configuration/system.group.conf"
	@cp userland/configuration/system.shells.conf "$(ROOTFS64)/System/Configuration/system.shells.conf"
	@cp userland/configuration/system.hosts.conf "$(ROOTFS64)/System/Configuration/system.hosts.conf"
	@cp userland/configuration/system.network.conf "$(ROOTFS64)/System/Configuration/system.network.conf"
	@cp userland/configuration/system.mounts.conf "$(ROOTFS64)/System/Configuration/system.mounts.conf"
	# --- FSH fonts (text stack): OS fonts in /System/Shared/Fonts; the
	# fontconfig config is the libconfig domain system.fonts.conf (M0,
	# docs/design/fontconfig-config-plan.md) - no XML fonts.conf ships.
	# --- the interim cursor theme (userland/cursors): an Xcursor theme is
	# <dir>/<theme>/cursors/<name>, which is the layout libXcursor searches
	# under XCURSOR_PATH. Kestrel points XCURSOR_PATH at this and defines
	# the cursor on the root window.
	# Idempotent ON PURPOSE: `cp -a src dst` copies src INTO dst when dst
	# is already a directory, so a second make run nested a whole duplicate
	# theme at cursors/cursors/ (~11.7MB, 146 entries) and pushed the tree
	# past the 64MB image. Clear the destination first.
	@mkdir -p "$(ROOTFS64)/System/Shared/Icons/default"
	@rm -rf "$(ROOTFS64)/System/Shared/Icons/default/cursors"
	@cp -a userland/cursors \
		"$(ROOTFS64)/System/Shared/Icons/default/cursors"
	@mkdir -p "$(ROOTFS64)/System/Shared/Fonts"
	@cp userland/fonts/DejaVuSans.ttf userland/fonts/DejaVuSans-Bold.ttf \
		"$(ROOTFS64)/System/Shared/Fonts/"
	@cp userland/configuration/system.fonts.conf \
		"$(ROOTFS64)/System/Configuration/system.fonts.conf"
	# --- shared libc (docs/shared-libraries-plan.md): stage the dynamic
	# linker + libc for the dynamic userland. The interpreter is a
	# hardlink of libc.so (same inode), matching musl's own install, so
	# the loader recognizes libc as itself.
	@cp $(MUSL64_PREFIX)/lib/libc.so "$(ROOTFS64)/System/Libraries/libc.so"
	@ln -f "$(ROOTFS64)/System/Libraries/libc.so" "$(ROOTFS64)/System/Libraries/ld-musl-x86_64.so.1"
	# --- shared X stack (docs/shared-libraries-plan.md §6, M2): the
	# versioned .so files of the X11 dependency prefix. musl's loader
	# resolves each NEEDED soname (libX11.so.6, libxcb.so.1, ...) as an
	# exact filename against the baked /System/Libraries search path; the
	# glob carries the soname symlink + the versioned real file (the bare
	# dev symlink libX11.so is link-time only and skipped). Only the libs
	# today's consumers NEED are staged. libX11-xcb + libxcb-composite are
	# now staged because something DOES link them: xcomp_probe, which
	# de-risks a compositing Kestrel. libXrender is vendored too (the
	# RENDER client the compositor blends through); libXcomposite and
	# libXdamage are still not - the xcb bindings cover Composite.
	@if [ ! -d "$(X11PREFIX)/lib" ]; then \
		echo "X11 prefix missing - run tools/x11-shared-build.sh first"; \
		exit 1; \
	fi
	@for l in libX11.so libxcb.so libXau.so libXdmcp.so libxkbfile.so \
		libpixman-1.so libXfont2.so libfontenc.so libz.so libXext.so \
		libX11-xcb.so libxcb-composite.so libXrender.so \
		libXcursor.so libXfixes.so libXcomposite.so; do \
		cp -a $(X11PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# --- ICU4C (docs/design/foundation-plan.md §10, slice F13): the DATA backend
	# the Foundation's data-driven families bind - the formatters, the time-zone
	# names and DST rules, the non-Gregorian calendars, collation and the [d]
	# fold. Three libraries plus their sonames: the common library, the i18n
	# library, and the DATA PACKAGE, which --with-data-packaging=library makes a
	# real shared library (the Debian arrangement). That is why the guest needs no
	# data path and no ICU_DATA: musl's loader resolves libicudata.so.76 as an
	# ordinary NEEDED entry, exactly like libz. ICU's own NEEDED closure is the
	# C++ stack (libc++/libc++abi/libunwind/libc), staged above.
	@if [ ! -d "$(ICUPREFIX)/lib" ]; then \
		echo "ICU prefix missing - run tools/icu-build.sh first"; \
		exit 1; \
	fi
	@for l in libicuuc.so libicui18n.so libicudata.so; do \
		cp -a $(ICUPREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# FNX's own shared libconfig (first-party, .build/fnxlib): the
	# config tool, toybox account tools and Xfb's configargs all NEEDED it.
	@cp $(FNXLIB_CONFIG) "$(ROOTFS64)/System/Libraries/libconfig.so.1"
	# The Objective-C runtime (docs/design/objc-toolchain-plan.md P3): same
	# rule as libconfig and the C++ stack - the versioned file, whose SONAME
	# ("libobjc.so.4.6") the guest loader resolves. Upstream tries to suppress
	# the soname with a malformed set_property() call, so it has one.
	@cp $(OBJC_PREFIX)/lib/libobjc.so.4.6 "$(ROOTFS64)/System/Libraries/libobjc.so.4.6"
	# The Foundation (docs/design/foundation-plan.md F0): the same staging rule.
	@cp $(FOUNDATION_LIB) "$(ROOTFS64)/System/Libraries/libfoundation.so.1"
	# Its PUBLIC HEADERS, which is what makes an on-guest Objective-C rebuild
	# possible - the gap docs/design/self-hosting-packages.md §6 records for the
	# runtime. Lower-case directory on purpose: <foundation/...>, never Apple's.
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/foundation"
	@cp $(FOUNDATION_SRC)/*.h "$(ROOTFS64)/System/Shared/Headers/foundation/"

	# --- shared C++ stack (dynamic-C++): the versioned libc++/libc++abi/
	# libunwind .so files from the llvm-cxx prefix (built shared since the
	# dynamic-C++ milestone). Same staging rule as the X stack: the glob
	# carries the soname symlink + the versioned real file; the bare dev
	# symlink is link-time only and skipped. cpp_smoke NEEDs these sonames at runtime.
	@if [ ! -d "$(LLVM_CXX_PREFIX)/lib" ]; then \
		echo "llvm-cxx prefix missing - run make llvm-cxx first"; \
		exit 1; \
	fi
	@for l in libc++.so libc++abi.so libunwind.so; do \
		cp -a $(LLVM_CXX_PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# --- shared text stack (the X11/font port, tools/x11-shared-build.sh): fontconfig +
	# HarfBuzz + FreeType + the libpng/expat leaves, built SHARED into the
	# X prefix. Only what a consumer NEEDs today is staged (libharfbuzz-
	# subset/gobject are left out until something links them); the glob
	# carries soname + real file (libpng16.so.* covers libpng16.so.16,
	# not the bare libpng.so dev link).
	@if [ ! -d "$(X11PREFIX)/lib" ]; then \
		echo "X11 prefix missing - run tools/x11-shared-build.sh first"; \
		exit 1; \
	fi
	@for l in libpng16.so libexpat.so libfreetype.so libfontconfig.so \
		libharfbuzz.so; do \
		cp -a $(X11PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# hello_dl: the dynamic-linker smoke test. Staged under
	# System/Shared/tests - System/Tools is dynamic too since M1, but the
	# linter carve-out keeps this one out of the zero-allow scope.
	@mkdir -p "$(ROOTFS64)/System/Shared/tests"
	$(MUSL64_CC) userland/tests/hello_dl.c -o "$(ROOTFS64)/System/Shared/tests/hello_dl"
	# Overridable first-party defaults ship in Shared (plan §5.0); a
	# System copy overrides them (Xfb reads via resolved libconfig reads).
	@cp userland/configuration/system.xfb.conf "$(ROOTFS64)/Shared/Configuration/system.xfb.conf"
	# Argentum display physical size (S1.1, domain system.display): the
	# FNX-owned panel's real mm; 0 = unknown -> 96 dpi (4/3 px/pt)
	# fallback. The S1.1/S1.4 gates override via `config write -s
	# system.display display.width_mm <mm> display.height_mm <mm>`.
	@cp userland/configuration/system.display.conf \
		"$(ROOTFS64)/Shared/Configuration/system.display.conf"
	# --- the Admin home: the User Template, copied (Q9) ---
	rm -rf "$(ROOTFS64)/Users"
	@mkdir -p "$(ROOTFS64)/Users"
	cp -a "$(ROOTFS64)/System/User Template" "$(ROOTFS64)/Users/Admin"
	@echo "userland64: native x86_64 rootfs staged in $(ROOTFS64)"

# Build the native x86_64 ext2 root filesystem image (.build/root.img) from
# the ELF64 userland tree, so the kernel can boot /System/Tools/init straight
# off hdb (no initrd). Attached as a second IDE disk (hdb), it becomes the
# boot root via the kernel cmdline 'root=/dev/hdb rootfstype=ext2'.
